package e2e

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
)

// holdOpendirSource makes the first opendir(".") of a process wait: it creates the file
// HOLD_MARK, then waits (polling every 5 ms, 60 s at most) until the file HOLD_GO exists.
// doverctl clean calls opendir(".") first when it starts on the group directories, which is
// after it has read the active file. Later calls, other directories and processes without
// HOLD_MARK are not touched. On NetBSD <dirent.h> renames opendir() to __opendir30 (see
// sleepGrowSource).
const holdOpendirSource = `#define _GNU_SOURCE
#define __EXTENSIONS__
#if defined(__NetBSD__)
#define REAL_OPENDIR "__opendir30"
#else
#define REAL_OPENDIR "opendir"
#endif
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int held;

DIR *
opendir(const char *path)
{
    static DIR *(*real)(const char *);
    const char *mark = getenv("HOLD_MARK");
    const char *go = getenv("HOLD_GO");
    struct stat st;
    int fd, i;

    if (real == NULL)
	real = (DIR *(*)(const char *))dlsym(RTLD_NEXT, REAL_OPENDIR);
    if (held || mark == NULL || go == NULL || strcmp(path, ".") != 0)
	return real(path);
    held = 1;
    if ((fd = open(mark, O_WRONLY | O_CREAT, 0644)) >= 0)
	close(fd);
    for (i = 0; i < 12000 && stat(go, &st) != 0; ++i)
	usleep(5000);
    return real(path);
}
`

// runCleanHeld runs `doverctl -v clean` with the preloaded opendir() of holdOpendirSource,
// waits until it is held at its first directory scan (the active file has been read), runs
// during, lets it go on and returns its output. A clean that fails fails the test.
func runCleanHeld(t *testing.T, s *Site, so string, during func()) string {
	t.Helper()
	work := t.TempDir()
	mark := filepath.Join(work, "mark")
	release := filepath.Join(work, "go")
	ctx, cancel := context.WithTimeout(context.Background(), testkit.Deadline())
	defer cancel()
	cmd := exec.CommandContext(ctx, filepath.Join(s.Bin, "doverctl"), "-v", "clean")
	cmd.Dir = s.Dir
	cmd.Env = append(append(os.Environ(), s.Env...), lockFaultEnv(so), "HOLD_MARK="+mark, "HOLD_GO="+release)
	var out strings.Builder
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	// clean is let go whatever happens below, so that it is not left waiting
	released := false
	releaseNow := func() {
		if !released {
			released = true
			os.WriteFile(release, nil, 0o644)
		}
	}
	defer releaseNow()

	for deadline := time.Now().Add(30 * time.Second); ; time.Sleep(10 * time.Millisecond) {
		if _, err := os.Stat(mark); err == nil {
			break
		}
		select {
		case err := <-done:
			t.Fatalf("test setup: doverctl ended before it reached its first directory scan (%v, or the shim does not load):\n%s", err, out.String())
		default:
		}
		if time.Now().After(deadline) {
			t.Fatalf("test setup: doverctl did not reach its first directory scan:\n%s", out.String())
		}
	}

	during()
	releaseNow()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("doverctl clean: %v\n%s", err, out.String())
		}
	case <-time.After(30 * time.Second):
		t.Fatalf("doverctl clean did not finish:\n%s", out.String())
	}
	return out.String()
}

// TestDoverctlCleanGroupCreatedDuringClean (D-285): doverctl clean read the active file once,
// at the start, and removed the over and data files of every group it did not find there. A
// group created while clean ran (a newgroup control, the admin) lost its overview files. Now a
// file of an unknown group is looked up again in the live active file and kept when the group
// exists.
//
// The over and data files of e2e.late are in place from the start; the group is not in the
// active file. A preloaded opendir() holds clean at its first directory scan, after the
// active file was read, until the test has created e2e.late with dgrpctl (NB 1, NE 300). The
// files of e2e.gone, a group that never exists, are the control: they are removed, with the
// message for a group that is not found, and the files of e2e.test, known from the start,
// stay.
func TestDoverctlCleanGroupCreatedDuringClean(t *testing.T) {
	so := preloadShim(t, "holdopendir", holdOpendirSource)
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	dir := filepath.Join(home, "e2e")
	var keep, gone []string
	for _, g := range []string{"e2e.test", "e2e.late", "e2e.gone"} {
		over := filepath.Join(dir, "o.0."+g)
		data := filepath.Join(dir, "d.256."+g)
		writeOver(t, over, overHead(g))
		writeOver(t, data, []byte("xxxxxxxxxx"))
		if g == "e2e.gone" {
			gone = append(gone, over, data)
		} else {
			keep = append(keep, over, data)
		}
	}

	out := runCleanHeld(t, s, so, func() {
		s.Run(t, "dgrpctl", "newgroup", "e2e.late", "y")
		s.Run(t, "dgrpctl", "adjust", "e2e.late", "NB", "=1")
		s.Run(t, "dgrpctl", "adjust", "e2e.late", "NE", "=300")
	})

	for _, f := range keep {
		if _, err := os.Stat(f); err != nil {
			t.Errorf("%s was removed: %v", filepath.Base(f), err)
		}
	}
	for _, f := range gone {
		if _, err := os.Stat(f); err == nil {
			t.Errorf("%s of a group that does not exist is still there", filepath.Base(f))
		}
	}
	if !strings.Contains(out, "Group for over file not found, removing") {
		t.Errorf("clean did not report the files of the group that does not exist:\n%s", out)
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out)
	}
}

// TestDoverctlCleanIterChangeDuringClean (D-285, found in the review of #506): the files of a
// group carry its iteration in their names with the default crc hash (over.<base>.<hash>.<iter>,
// no suffix for 0), and a group whose hash another group already uses gets the next one.
// dreaderd, dcancel and dgrpctl create the files of the new iteration first and write ITER
// into dactive.kp afterwards. The rescan did not enter a group it knew again, so the table kept
// iteration 0 and the files of iteration 1 of a known group were removed as "group not found".
//
// e2e.test is known at the start with ITER 0 and has over and data files of iteration 1.
// While clean is held at its first directory scan, ITER of e2e.test goes to 1 (case
// iter_written) or is left at 0 (case iter_not_yet_written: the window between creating the
// files and writing ITER): the files must stay. e2e.re is known with ITER 1 (removed and
// created again): its files of iteration 1 stay, and a data file of iteration 0 is not
// anybody's any more and is removed, with the message for a group that is not found.
func TestDoverctlCleanIterChangeDuringClean(t *testing.T) {
	so := preloadShim(t, "holdopendir", holdOpendirSource)
	for _, tc := range []struct {
		name string
		bump bool
	}{
		{"iter_written", true},
		{"iter_not_yet_written", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			db := filepath.Join(s.Dir, "dactive.kp")
			dkp := func(group, field string) {
				t.Helper()
				if o := dkpRun(t, "", "-w", db, group, field); strings.Contains(o, "rror") {
					t.Fatalf("dkp -w %s %s: %s", group, field, o)
				}
			}
			// files returns the directory under home and the hash part of the file names
			// ("<hash>" of over.0.<hash>) of a group, (listed before the group
			// has an ITER, in case the listing follows it)
			seen := map[string][2]string{}
			files := func(group string) (dir, hash string) {
				if v, ok := seen[group]; ok {
					return v[0], v[1]
				}
				f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", group))
				if len(f) != 2 || !strings.HasPrefix(filepath.Base(f[0]), "over.0.") {
					t.Fatalf("dgrpctl listgrouphashfile %s: %q", group, f)
				}
				dir, hash = filepath.Dir(f[0]), strings.TrimPrefix(filepath.Base(f[0]), "over.0.")
				seen[group] = [2]string{dir, hash}
				return dir, hash
			}
			files("e2e.re")
			for _, f := range []string{"NB=0000000001", "NE=0000000300", "S=y", "ITER=000001"} {
				dkp("e2e.re", f)
			}
			var keep []string
			put := func(group string, iter string, data string) {
				dir, hash := files(group)
				if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
					t.Fatal(err)
				}
				over := filepath.Join(home, dir, "over.0."+hash+iter)
				d256 := filepath.Join(home, dir, "data.256."+hash+iter)
				writeOver(t, over, overHead(group))
				writeOver(t, d256, []byte("xxxxxxxxxx"))
				keep = append(keep, over, d256)
				if data != "" {
					writeOver(t, filepath.Join(home, dir, data+hash), []byte("xxxxxxxxxx"))
				}
			}
			put("e2e.test", ".1", "")
			put("e2e.re", ".1", "data.512.")
			dir, hash := files("e2e.re")
			stale := filepath.Join(home, dir, "data.512."+hash)

			out := runCleanHeld(t, s, so, func() {
				if tc.bump {
					dkp("e2e.test", "ITER=000001")
				}
			})
			if tc.bump {
				if live, _, _ := kpRecords(t, db, "e2e.test"); len(live) != 1 || !strings.Contains(live[0], "ITER=000001") {
					t.Fatalf("test setup: ITER of e2e.test is not 1: %q", live)
				}
			}

			for _, f := range keep {
				if _, err := os.Stat(f); err != nil {
					t.Errorf("%s was removed: %v", filepath.Base(f), err)
				}
			}
			if _, err := os.Stat(stale); err == nil {
				t.Errorf("%s, a file of iteration 0 of a group that has iteration 1, is still there", filepath.Base(stale))
			}
			if !strings.Contains(out, "Group for over file not found, removing "+stale) {
				t.Errorf("clean did not report the file of iteration 0:\n%s", out)
			}
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}
		})
	}
}
