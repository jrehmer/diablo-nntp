package e2e

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/fakeserver"
)

// D-337: after a switch between `hierarchy` and `hierarchy-full` made by editing readergrouphash,
// dexpireover and doverctl clean leave the files of every moved group in its old directory alone
// (D-336: a dreaderd started before the change may still write them), and dexpireover keeps the
// group's first article number while its over file is there. `doverctl clean -o` removes them,
// only with dreaderd stopped.

// coRemoveLine is what doverctl clean -o prints for each file it removes (also with -n).
const coRemoveLine = " in the directory of the other hierarchy layout: "

// coGroups are the groups of these tests: the swGroups and x.y (the top of the group home in
// plain hierarchy, x/ in hierarchy-full).
func coGroups() []string {
	return append(swNames(""), "x.y")
}

// coLeftovers are the over file and the data files d.0, d.4 of every group of coGroups whose
// directory differs between from and to, in its directory of from, that clean -o removes after
// the switch to to (remove), and those it keeps (kept): with plain hierarchy e2e.x and e2e.x.gz
// share e2e/ (D-325), so e2e/d.<n>.e2e.x.gz is also the name of e2e.x's compressed data in the
// directory of e2e.x in both layouts; it is judged by both groups, not removed.
func coLeftovers(t *testing.T, home, from, to string) (remove, kept []string) {
	t.Helper()
	for _, g := range coGroups() {
		if swDir(g, from) == swDir(g, to) {
			continue
		}
		for _, f := range swOldFiles(home, g, from) {
			if !hfxExists(f) {
				t.Fatalf("after the feed with %s: %s is missing", from, f)
			}
			if from == "hierarchy" && g == "e2e.x.gz" && strings.HasPrefix(filepath.Base(f), "d.") {
				kept = append(kept, f)
				continue
			}
			remove = append(remove, f)
		}
	}
	return remove, kept
}

// coGroupOf is the group whose leftover f is (its name follows "o.0." or "d.<n>.").
func coGroupOf(f string) string {
	b := filepath.Base(f)
	return b[strings.Index(b[2:], ".")+3:]
}

// coCheckRemoved fails unless out has the coRemoveLine of each of the files in want exactly once
// and the "left alone" line of D-336 (swAloneLine) of each of the files in alone exactly once,
// and no line that reports another removal or a problem (hfxBad, "Deleting", "Removing");
// "Removed empty directory" lines pass only with dirs.
func coCheckRemoved(t *testing.T, what, out string, want, alone []string, dirs bool) {
	t.Helper()
	exp := map[string]bool{}
	for _, f := range want {
		exp["Removing file of "+coGroupOf(f)+coRemoveLine+f] = true
	}
	for _, f := range alone {
		exp["file of "+coGroupOf(f)+swAloneLine+f] = true
	}
	said := map[string]int{}
	for _, l := range strings.Split(out, "\n") {
		if exp[l] {
			said[l]++
			continue
		}
		if dirs && strings.HasPrefix(l, "Removed empty directory: ") {
			continue
		}
		for _, b := range append([]string{"Deleting", "Removing", "Removed", "removing"}, hfxBad...) {
			if strings.Contains(l, b) {
				t.Errorf("%s reports: %s", what, l)
				break
			}
		}
	}
	for l := range exp {
		if said[l] != 1 {
			t.Errorf("%s: %d times %q, want once", what, said[l], l)
		}
	}
}

// coLookalikes are an admin's files at the top of the group home, which clean -o must keep:
// notes.txt; o.0.x.y.bak (the over file name of a group x.y.bak, not in the active file: x.y.bak
// is no group, and its directory is x.y/ or x/y/), o.0.e2e.test and d.0.e2e.test (names of
// e2e.test, whose directory is e2e/ in both layouts: no group's files at the top), and a symlink
// d.64.x.y -> notes.txt (the name of x.y's data at the top: the top follows and removes no link,
// D-338). Returns the regular files with their contents.
func coLookalikes(t *testing.T, home string) map[string][]byte {
	t.Helper()
	read := func(rel string) []byte {
		b, err := os.ReadFile(filepath.Join(home, rel))
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	m := map[string][]byte{
		filepath.Join(home, "notes.txt"):    []byte("an admin's notes\n"),
		filepath.Join(home, "o.0.x.y.bak"):  read(swRel("e2e.test", "hierarchy")),
		filepath.Join(home, "o.0.e2e.test"): read(swRel("e2e.test", "hierarchy")),
		filepath.Join(home, "d.0.e2e.test"): read("e2e/d.0.e2e.test"),
	}
	for f, b := range m {
		writeOver(t, f, b)
	}
	if err := os.Symlink("notes.txt", filepath.Join(home, "d.64.x.y")); err != nil {
		t.Fatal(err)
	}
	return m
}

// coSite is a reader site with the coGroups, readergrouphash from and dexpire.ctl "*:x30:e4",
// fed with swArts articles crossposted to all of them (bases 0 and 4), switched to to by editing
// readergrouphash, restarted and fed with swArts more articles to all but e2e.q.quiet (OVER
// read until dreaderd has them), and left running. Returns the site, the group home and the
// GROUP line and OVER 1-12 of each group with to.
func coSite(t *testing.T, from, to string) (*Site, string, map[string]string, map[string][]string) {
	t.Helper()
	st := fakeserver.NewStore()
	all := coGroups()
	for _, g := range all {
		st.AddGroup(g, "clean -o test group")
	}
	spool := fakeserver.New(t, fakeserver.Options{Store: st})
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: all,
		Config: map[string]string{"readergrouphash": from},
		Files:  map[string]string{"dexpire.ctl": "*:x30:e4\n"}})
	home := strings.TrimSpace(s.Run(t, "dpath", "group"))
	swFeed(t, s, st, all, swArts, "before")
	for _, g := range all {
		readOver(t, s, g, swArts)
	}
	s.Proc.Stop()
	swSwitch(t, s, from, to, all)
	restartReader(t, s)
	var after []string
	for _, g := range all {
		if g != swQuiet {
			after = append(after, g)
		}
	}
	swFeed(t, s, st, after, swArts, "after")
	lines := map[string]string{}
	over := map[string][]string{}
	for _, g := range all {
		want := swArts // articles 7-12, in the new directory
		switch {
		case g == swQuiet:
			want = 0
		case swDir(g, from) == swDir(g, to):
			want = 2 * swArts
		}
		testkit.WaitFor(t, 10*time.Second, fmt.Sprintf("OVER of %s: %d lines", g, want), func() bool {
			lines[g], over[g] = swOver(t, s, g)
			return len(over[g]) == want
		})
	}
	return s, home, lines, over
}

// coCheckFiles fails for each file of snap that is gone or changed, unless it is in gone, which
// must be gone; and for a file below home that is not in snap.
func coCheckFiles(t *testing.T, what, home string, snap map[string][]byte, gone []string) {
	t.Helper()
	isGone := map[string]bool{}
	for _, f := range gone {
		isGone[f] = true
	}
	for f, want := range snap {
		got, err := os.ReadFile(f)
		switch {
		case isGone[f] && err == nil:
			t.Errorf("%s: %s is still there", what, strings.TrimPrefix(f, home+"/"))
		case isGone[f]:
		case err != nil:
			t.Errorf("%s: %s is gone: %v", what, strings.TrimPrefix(f, home+"/"), err)
		case !bytes.Equal(got, want):
			t.Errorf("%s: %s was changed", what, strings.TrimPrefix(f, home+"/"))
		}
	}
	for f := range swFiles(t, home) {
		if _, ok := snap[f]; !ok {
			t.Errorf("%s: new file %s", what, strings.TrimPrefix(f, home+"/"))
		}
	}
}

// coCheckLink fails unless the symlink d.64.x.y of the coLookalikes is there unchanged.
func coCheckLink(t *testing.T, what, home string) {
	t.Helper()
	if l, err := os.Readlink(filepath.Join(home, "d.64.x.y")); err != nil || l != "notes.txt" {
		t.Errorf("%s: the symlink d.64.x.y: %q, %v", what, l, err)
	}
}

// TestDoverctlCleanOtherLayout: `doverctl clean -o` removes the over and data files that the
// groups of the active file have in their directory of the other hierarchy layout after a switch
// between `hierarchy` and `hierarchy-full` made by editing readergrouphash, with dreaderd stopped;
// nothing else. Then dexpireover moves the first article number of the moved groups.
//
// Input: the coGroups (e2e.test and e2e.x: e2e/ in both layouts; e2e.x.gz: e2e/ or e2e/x/; a.b.c:
// a.b/ or a/b/; o.1.2: o.1/ or o/1/; e2e.q.quiet: e2e/ or e2e/q/; x.y: the top or x/), dexpire.ctl
// "*:x30:e4", 6 articles crossposted to all, readergrouphash switched (subtests
// hierarchy_to_full, full_to_hierarchy), dreaderd restarted, 6 more articles to all but
// e2e.q.quiet, dreaderd stopped (its pid file log/dreaderd.pid stays, naming the dead process);
// the coLookalikes at the top. Steps and expected results:
//   - `doverctl -v clean` (without -o): nothing removed, every file unchanged (as D-336);
//   - `doverctl -v -n clean -o`: one line "Removing file of <group> in the directory of the
//     other hierarchy layout: <path>" for each of the old over file, d.0 and d.4 of the moved
//     groups (coLeftovers), no other removal; nothing changed;
//   - `doverctl -v -w a.b.c clean -o`: only a.b.c's three old files removed (with the line);
//   - `doverctl -v clean -o`: the old files of the other groups removed (with the line), every
//     other file unchanged (the files of the configured layout, the lookalikes, the symlink, and
//     with hierarchy_to_full e2e/d.0.e2e.x.gz and e2e/d.4.e2e.x.gz, also e2e.x's compressed data
//     name in its directory, D-325); "Removed empty directory" lines allowed; a second run
//     removes nothing;
//   - `dexpireover -a -v`: no "first article number not moved" line; after a restart GROUP of
//     e2e.test and e2e.x "211 12 1 12" with OVER 1-12 as before, of e2e.x.gz, a.b.c, o.1.2 and
//     x.y "211 6 7 12" with OVER 1-12 the 6 articles 7-12 as before (before: "211 12 1 12").
//
// On main 297b8ed4 -o after clean is taken as the directory argument, which clean ignores: the
// -n and -o runs remove nothing and print no "Removing file of" line, and GROUP of the moved
// groups stays "211 12 1 12" (dexpireover: "first article number not moved").
func TestDoverctlCleanOtherLayout(t *testing.T) {
	for _, sw := range []struct{ name, from, to string }{
		{"hierarchy_to_full", "hierarchy", "hierarchy-full"},
		{"full_to_hierarchy", "hierarchy-full", "hierarchy"},
	} {
		t.Run(sw.name, func(t *testing.T) {
			s, home, lines, before := coSite(t, sw.from, sw.to)
			s.Proc.Stop()
			pidFile := filepath.Join(s.Dir, "log", "dreaderd.pid")
			if !hfxExists(pidFile) {
				t.Errorf("no stale %s after dreaderd stopped", pidFile)
			}
			for g, l := range lines {
				if !strings.HasPrefix(l, fmt.Sprintf("211 %d 1 %d ", 2*swArts, 2*swArts)) && g != swQuiet {
					t.Fatalf("before the tool: GROUP %s %q, want 211 %d 1 %d", g, l, 2*swArts, 2*swArts)
				}
			}
			remove, kept := coLeftovers(t, home, sw.from, sw.to)
			coLookalikes(t, home)
			snap := swFiles(t, home)
			t.Logf("leftovers to remove %q, kept %q", remove, kept)

			// without -o: as D-336
			out, err := s.TryRun("doverctl", "-v", "clean")
			if err != nil {
				t.Errorf("doverctl -v clean: %v", err)
			}
			coCheckRemoved(t, "doverctl -v clean", out, nil, append(append([]string{}, remove...), kept...), false)
			coCheckFiles(t, "doverctl -v clean", home, snap, nil)
			coCheckLink(t, "doverctl -v clean", home)
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}

			// -n: the list, nothing removed
			out, err = s.TryRun("doverctl", "-v", "-n", "clean", "-o")
			if err != nil {
				t.Errorf("doverctl -v -n clean -o: %v", err)
			}
			coCheckRemoved(t, "doverctl -v -n clean -o", out, remove, nil, false)
			coCheckFiles(t, "doverctl -v -n clean -o", home, snap, nil)
			coCheckLink(t, "doverctl -v -n clean -o", home)
			t.Logf("doverctl -v -n clean -o: %v\n%s", err, out)

			// -w: the files of the matching groups only
			var abc, rest []string
			for _, f := range remove {
				if coGroupOf(f) == "a.b.c" {
					abc = append(abc, f)
				} else {
					rest = append(rest, f)
				}
			}
			if len(abc) != 3 {
				t.Fatalf("a.b.c leftovers %q, want 3", abc)
			}
			out, err = s.TryRun("doverctl", "-v", "-w", "a.b.c", "clean", "-o")
			if err != nil {
				t.Errorf("doverctl -v -w a.b.c clean -o: %v", err)
			}
			coCheckRemoved(t, "doverctl -v -w a.b.c clean -o", out, abc, rest, true)
			coCheckFiles(t, "doverctl -v -w a.b.c clean -o", home, snap, abc)
			if t.Failed() {
				t.Logf("doverctl -v -w a.b.c clean -o:\n%s", out)
			}

			out, err = s.TryRun("doverctl", "-v", "clean", "-o")
			if err != nil {
				t.Errorf("doverctl -v clean -o: %v", err)
			}
			coCheckRemoved(t, "doverctl -v clean -o", out, rest, nil, true)
			coCheckFiles(t, "doverctl -v clean -o", home, snap, remove)
			coCheckLink(t, "doverctl -v clean -o", home)
			t.Logf("doverctl -v clean -o: %v\n%s", err, out)
			out, err = s.TryRun("doverctl", "-v", "clean", "-o")
			if err != nil {
				t.Errorf("second doverctl -v clean -o: %v", err)
			}
			coCheckRemoved(t, "second doverctl -v clean -o", out, nil, nil, true)
			var left []string
			for f := range swFiles(t, home) {
				left = append(left, strings.TrimPrefix(f, home+"/"))
			}
			sort.Strings(left)
			t.Logf("files left: %q", left)

			out = expireOver(t, s, "-a", "-v")
			if strings.Contains(out, "first article number not moved") {
				t.Errorf("dexpireover -a -v after clean -o still holds the first article number:\n%s", out)
			}

			restartReader(t, s)
			for _, g := range coGroups() {
				line, over := swOver(t, s, g)
				want := "211 12 1 12 "
				switch {
				case g == swQuiet:
					t.Logf("after clean -o and dexpireover: GROUP %s %q (before %q), OVER %q", g, line, lines[g], over)
					want = ""
				case swDir(g, sw.from) != swDir(g, sw.to):
					want = "211 6 7 12 "
				}
				if want != "" && !strings.HasPrefix(line, want) {
					t.Errorf("after clean -o and dexpireover: GROUP %s %q, want %q", g, line, want)
				}
				if strings.Join(over, "\n") != strings.Join(before[g], "\n") {
					t.Errorf("after clean -o and dexpireover: OVER 1-12 of %s %q; before %q", g, over, before[g])
				}
			}
		})
	}
}

// TestDoverctlCleanOtherLayoutRunning: `doverctl clean -o` refuses, exit status 1 and nothing
// removed, while dreaderd may run: by its pid file log/dreaderd.pid naming a live process, or by
// its control socket run/.dreadersock accepting a connection; also when the pid file holds no
// pid. Input: the coSite of hierarchy_to_full, dreaderd running; subtests in order:
//   - running: dreaderd as started ("dreaderd may be running (<pid file> names the live process
//     <pid>...");
//   - socket: dreaderd running, its pid file removed ("its control socket <path> accepts
//     connections");
//   - live_pid: dreaderd stopped, the pid file holding the pid of the test process;
//   - no_pid: dreaderd stopped, the pid file holding "garbage" ("holds no pid").
//
// Each: `doverctl -v clean -o` and `doverctl -v -n clean -o` exit 1 with "stop it first,
// nothing done" and the reason, and no "Removing" line; the old files are all there unchanged.
// At the end, the pid file removed and dreaderd stopped, `doverctl clean -o` exits 0 (control).
// On main 297b8ed4 -o is ignored and clean exits 0.
func TestDoverctlCleanOtherLayoutRunning(t *testing.T) {
	s, home, _, _ := coSite(t, "hierarchy", "hierarchy-full")
	remove, kept := coLeftovers(t, home, "hierarchy", "hierarchy-full")
	snap := hfxSnapshot(t, append(append([]string{}, remove...), kept...))
	pidFile := filepath.Join(s.Dir, "log", "dreaderd.pid")
	sock := filepath.Join(s.Dir, "run", ".dreadersock")
	refuse := func(t *testing.T, reason string) {
		t.Helper()
		for _, args := range [][]string{{"-v", "clean", "-o"}, {"-v", "-n", "clean", "-o"}} {
			out, err := s.TryRun("doverctl", args...)
			what := "doverctl " + strings.Join(args, " ")
			if err == nil {
				t.Errorf("%s: exit 0, want 1", what)
			}
			if !strings.Contains(out, "clean -o: dreaderd may be running (") ||
				!strings.Contains(out, "): stop it first, nothing done\n") || !strings.Contains(out, reason) {
				t.Errorf("%s: no refusal with %q", what, reason)
			}
			if strings.Contains(out, "Removing") || strings.Contains(out, "Deleting") {
				t.Errorf("%s removes files", what)
			}
			t.Logf("%s: %v\n%s", what, err, out)
		}
		for f, want := range snap {
			if got, err := os.ReadFile(f); err != nil || !bytes.Equal(got, want) {
				t.Errorf("%s of the old layout is gone or changed: %v", strings.TrimPrefix(f, home+"/"), err)
			}
		}
	}

	t.Run("running", func(t *testing.T) {
		pid := readMainPid(t, s)
		refuse(t, pidFile+" names the live process "+strconv.Itoa(pid)+";")
	})
	t.Run("socket", func(t *testing.T) {
		if err := os.Remove(pidFile); err != nil {
			t.Fatal(err)
		}
		refuse(t, "its control socket "+sock+" accepts connections")
	})
	s.Proc.Stop()
	t.Run("live_pid", func(t *testing.T) {
		writeOver(t, pidFile, []byte(strconv.Itoa(os.Getpid())+"\n"))
		refuse(t, pidFile+" names the live process "+strconv.Itoa(os.Getpid())+";")
	})
	t.Run("no_pid", func(t *testing.T) {
		writeOver(t, pidFile, []byte("garbage\n"))
		refuse(t, pidFile+" holds no pid;")
	})
	if err := os.Remove(pidFile); err != nil {
		t.Fatal(err)
	}
	if !hfxExists(sock) {
		t.Logf("no socket %s left behind by the stopped dreaderd", sock)
	}
	out, err := s.TryRun("doverctl", "-v", "clean", "-o")
	if err != nil {
		t.Errorf("doverctl -v clean -o with dreaderd stopped and no pid file: %v\n%s", err, out)
	}
	coCheckRemoved(t, "doverctl -v clean -o", out, remove, nil, true)
}

// TestDoverctlCleanOtherLayoutCrc: with readergrouphash crc there is no other hierarchy layout:
// `doverctl -v clean -o` and `doverctl -v -n clean -o` exit 1 with "clean -o: readergrouphash is
// crc:20\2, not hierarchy or hierarchy-full: there is no other hierarchy layout, nothing done" and
// change nothing; `doverctl -o convert crc md5-32/16` exits 1 with "-o is an option of clean
// only" (on main: "Unknown option: -o"). Input: groups e2e.test and a.b.c, 3 articles, dreaderd
// stopped, a copy of a.b.c's over file named a.b/o.0.a.b.c (the old hierarchy name of a.b.c in a
// directory crc never makes). On main 297b8ed4 -o after clean is ignored and clean exits 0.
func TestDoverctlCleanOtherLayoutCrc(t *testing.T) {
	st := fakeserver.NewStore()
	groups := []string{"e2e.test", "a.b.c"}
	for _, g := range groups {
		st.AddGroup(g, "clean -o crc test group")
	}
	spool := fakeserver.New(t, fakeserver.Options{Store: st})
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups,
		Config: map[string]string{"readergrouphash": "crc"}})
	home := strings.TrimSpace(s.Run(t, "dpath", "group"))
	swFeed(t, s, st, groups, 3, "crc")
	for _, g := range groups {
		readOver(t, s, g, 3)
	}
	s.Proc.Stop()
	rel := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", "a.b.c"))[0]
	b, err := os.ReadFile(filepath.Join(home, rel))
	if err != nil {
		t.Fatalf("over file of a.b.c %q: %v", rel, err)
	}
	if err := os.MkdirAll(filepath.Join(home, "a.b"), 0o755); err != nil {
		t.Fatal(err)
	}
	writeOver(t, filepath.Join(home, "a.b", "o.0.a.b.c"), b)
	snap := swFiles(t, home)

	for _, args := range [][]string{{"-v", "clean", "-o"}, {"-v", "-n", "clean", "-o"}} {
		what := "doverctl " + strings.Join(args, " ")
		out, err := s.TryRun("doverctl", args...)
		if err == nil {
			t.Errorf("%s: exit 0, want 1", what)
		}
		if !strings.Contains(out, "clean -o: readergrouphash is crc") ||
			!strings.Contains(out, ", not hierarchy or hierarchy-full: there is no other hierarchy layout, nothing done\n") {
			t.Errorf("%s: no refusal", what)
		}
		t.Logf("%s: %v\n%s", what, err, out)
		coCheckFiles(t, what, home, snap, nil)
	}
	out, err := s.TryRun("doverctl", "-o", "convert", "crc", "md5-32/16")
	if err == nil || !strings.Contains(out, "-o is an option of clean only\n") {
		t.Errorf("doverctl -o convert crc md5-32/16: %v, no refusal", err)
	}
	t.Logf("doverctl -o convert crc md5-32/16: %v\n%s", err, out)
	coCheckFiles(t, "doverctl -o convert", home, snap, nil)
}
