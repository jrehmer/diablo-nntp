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
	// the group is created whatever happens below, so that clean is not left waiting
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

	s.Run(t, "dgrpctl", "newgroup", "e2e.late", "y")
	s.Run(t, "dgrpctl", "adjust", "e2e.late", "NB", "=1")
	s.Run(t, "dgrpctl", "adjust", "e2e.late", "NE", "=300")
	releaseNow()

	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("doverctl clean: %v\n%s", err, out.String())
		}
	case <-time.After(30 * time.Second):
		t.Fatalf("doverctl clean did not finish:\n%s", out.String())
	}

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
	if !strings.Contains(out.String(), "Group for over file not found, removing") {
		t.Errorf("clean did not report the files of the group that does not exist:\n%s", out.String())
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out.String())
	}
}
