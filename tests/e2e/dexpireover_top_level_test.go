package e2e

import (
	"bytes"
	"compress/gzip"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"slices"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/fakeserver"
)

// D-338: the hierarchy layouts put the files of some groups at the top of the group home, not in
// a subdirectory: FindGroupDir() (lib/hash.c) gives an empty directory to a group of one
// component (e2etop) in both layouts, and with plain `hierarchy` also to a group of two components
// whose first has one character (x.y, a.b, x.yz: a one-character component is joined to the next,
// and the last is no directory). dexpireover and doverctl clean
// entered only the directories of the group home, so these files were never expired, rewritten,
// compressed or cleaned: after articles 1-4 expired GROUP x.y stayed "211 12 1 12" (go-while's
// N2 in the #523 review). Each test feeds real articles to a running dreaderd, stops it, runs the
// tool and starts dreaderd again.

// tlGroups are the groups of these tests, with their directory below the group home in each
// layout ("" for the top).
var tlGroups = []struct{ name, hier, full string }{
	{"e2e.test", "e2e", "e2e"}, // control: a subdirectory in both layouts
	{"x.y", "", "x"},           // top in plain hierarchy (x.y.gz would be in x.y/)
	{"a.b", "", "a"},           // the same
	{"x.yz", "", "x"},          // top in plain hierarchy, with a last component of two characters
	{"e2etop", "", ""},         // one component: top in both layouts (e2etop.gz would be in e2etop/)
}

// tlDir is the directory of group g below the group home in layout ("" for the top).
func tlDir(g, layout string) string {
	for _, x := range tlGroups {
		if x.name == g {
			if layout == "hierarchy-full" {
				return x.full
			}
			return x.hier
		}
	}
	return ""
}

// newTopSite is a reader site with the tlGroups (readergrouphash layout, dexpire.ctl ctl), fed
// with hfxArts articles crossposted to all of them and stopped (newHfxSiteOf()); dgrpctl
// listgrouphashfile must give each group the directory of tlGroups.
func newTopSite(t *testing.T, layout, ctl string) *hfxSite {
	t.Helper()
	var groups []string
	want := map[string]string{}
	for _, g := range tlGroups {
		groups = append(groups, g.name)
		want[g.name] = "o.0." + g.name
		if d := tlDir(g.name, layout); d != "" {
			want[g.name] = d + "/o.0." + g.name
		}
	}
	return newHfxSiteOf(t, layout, ctl, groups, want)
}

// tlLook are an admin's files at the top of the group home, which no tool may touch: files whose
// names are no hierarchy names, or hierarchy names of no group whose directory is the top:
// "o.0.e2etop~" (an editor's copy of e2etop's over file) is the over file name of a group
// e2etop~, whose directory is the top too, but its header names e2etop; "o.0.x.y.bak" is the over
// file name of a group x.y.bak (directory x.y/ or x/y/), "o.0.e2e.test" and "d.0.e2e.test" are
// names of e2e.test, whose directory is e2e/; "over.0.0123abcd.89abcdef" has the form of a crc
// name; ".o.0.x.y.bak" and ".d.0.e2e.test" have the form of temporary files of no group at the
// top; the tlOrphans are data file names of groups whose directory is the top, with no over file
// (chatgpt-1's F3 on 120a76c8: a data file name alone is no evidence that the group existed;
// go-while's list in his #524 r1 review), and "d.0.junk.bak" is that of a group junk.bak whose
// directory is junk/ (a control). With dir also an empty directory "d.16.x.y" (a data file name
// of x.y).
type tlLook struct {
	files map[string][]byte
	dir   string
}

func tlLookalikes(t *testing.T, h *hfxSite, dir bool) *tlLook {
	t.Helper()
	read := func(p string) []byte {
		b, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		return b
	}
	l := &tlLook{files: map[string][]byte{
		"notes.txt":                []byte("an admin's notes\n"),
		"o.0.e2etop~":              read(h.over("e2etop")),
		"o.0.x.y.bak":              read(h.over("x.y")),
		"o.0.e2e.test":             read(h.over("e2e.test")),
		"d.0.e2e.test":             read(h.dataOf("e2e.test", 0)),
		"over.0.0123abcd.89abcdef": read(h.over("e2e.test")),
		".o.0.x.y.bak":             read(h.over("x.y")),
		".d.0.e2e.test":            read(h.dataOf("e2e.test", 0)),
		"d.0.junk.bak":             read(h.dataOf("e2etop", 0)),
	}}
	for _, n := range tlOrphans("hierarchy") { // also the plain-hierarchy ones with hierarchy-full
		l.files[n] = read(h.dataOf("e2etop", 0))
	}
	l.files["d.0.zero"] = []byte{} // a file of 0 bytes
	for n, b := range l.files {
		writeOver(t, filepath.Join(h.home, n), b)
	}
	if dir {
		l.dir = filepath.Join(h.home, "d.16.x.y")
		if err := os.Mkdir(l.dir, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	return l
}

// check reports each lookalike that is gone or changed.
func (l *tlLook) check(t *testing.T, h *hfxSite, when string) {
	t.Helper()
	snap := map[string][]byte{}
	for n, b := range l.files {
		snap[filepath.Join(h.home, n)] = b
	}
	h.checkKept(t, when, snap)
	if l.dir != "" {
		if st, err := os.Stat(l.dir); err != nil || !st.IsDir() {
			t.Errorf("%s: the directory %s is gone: %v", when, h.rel(l.dir), err)
		}
	}
}

// tlBakLine is what -v prints for o.0.e2etop~ at the top: the over file name of the group
// e2etop~, which does not exist, whose header names e2etop.
func tlBakLine(h *hfxSite) string {
	return "over file of no group at the top of the group home whose header does not name e2etop~, left alone: " +
		filepath.Join(h.home, "o.0.e2etop~")
}

// tlOrphanLine is what -v prints for a data file at the top of no group whose over file the run
// did not remove (F3).
func tlOrphanLine(h *hfxSite, name string) string {
	return "data file of no group at the top of the group home with no over file of that group, left alone: " +
		filepath.Join(h.home, name)
}

// tlPlainOrphans are data file names of groups whose directory is the top with plain hierarchy
// only (x.y-old, x.y~, a.bak); with hierarchy-full they are in x/ and a/, so the name is in no
// group's directory there.
var tlPlainOrphans = []string{"d.0.x.y-old", "d.4.x.y~", "d.0.a.bak"}

// tlOrphans are the lookalike data file names whose group's directory is the top in layout, with
// no over file of that group: they must stay with the tlOrphanLine. "d.0.junk~.gz" and
// "d.0.nogroup.gz" are read as junk~'s and nogroup's compressed data (junk~.gz and nogroup.gz
// would be in junk~/ and nogroup/); "d.0.zero" is empty.
func tlOrphans(layout string) []string {
	out := []string{"d.0.e2etop~", "d.4.gonegrp", "d.0.junk~.gz", "d.0.junk-old", "d.2024.notes",
		"d.0.nogroup", "d.0.nogroup.gz", "d.0.zero"}
	if layout == "hierarchy" {
		out = append(out, tlPlainOrphans...)
	}
	return out
}

// tlSkip are the lines checkReports() lets pass for the lookalikes; each must be there: the
// tlBakLine and the tlOrphanLine of each of the tlOrphans.
func tlSkip(t *testing.T, h *hfxSite, out string) []string {
	t.Helper()
	want := []string{tlBakLine(h)}
	for _, n := range tlOrphans(h.layout) {
		want = append(want, tlOrphanLine(h, n))
	}
	for _, l := range want {
		if !strings.Contains(out, l+"\n") {
			t.Errorf("no %q", l)
		}
	}
	return want
}

// TestDexpireoverTopLevelExpire: `dexpireover -a -v` expires the over files at the top of the
// group home and removes the data files there that fell below the range, as in a subdirectory,
// and leaves an admin's files there alone. With -p2 exactly one worker does the top.
//
// Input: the tlGroups, readergrouphash hierarchy or hierarchy-full, 12 articles crossposted to all
// of them, dexpire.ctl "*:x30:e4" (data bases 0, 4, 8, 12), dreaderd stopped, the receive time of
// the articles 1-4 set to 40 days ago in every over file, the tlLookalikes (with the directory);
// `dexpireover -a -v`, and `-a -v -p2`. Expected: for every group exactly one "expired 4 NB += 4
// ... left 8 ... grp=<group>" line; its data file of the base 0 removed with "Deleting stale
// overview data for <group>"; the other data files and the lookalikes there, unchanged; nothing
// else reported but the line for o.0.e2etop~ (tlBakLine); with -p2 two
// "Scanned" lines and "Parallelizing dexpireover (2 forks) finished."; after a restart GROUP
// "211 8 5 12" and OVER 5-12 as before for every group. Fails on main 2277c452 for the groups at
// the top (plain hierarchy: x.y, a.b, x.yz, e2etop; hierarchy-full: e2etop): no "expired" line,
// d.0 not removed, GROUP "211 12 1 12".
func TestDexpireoverTopLevelExpire(t *testing.T) {
	for _, tc := range []struct {
		name, layout string
		args         []string
	}{
		{"hierarchy", "hierarchy", nil},
		{"hierarchy-full", "hierarchy-full", nil},
		{"hierarchy_p2", "hierarchy", []string{"-p2"}},
		{"hierarchy-full_p2", "hierarchy-full", []string{"-p2"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			h := newTopSite(t, tc.layout, "*:x30:e4\n")
			old := time.Now().Add(-40 * 24 * time.Hour)
			var gone, keep []string
			allowed := map[string]string{}
			for _, g := range h.groups {
				hfxAge(t, h.over(g), []int{1, 2, 3, 4}, old)
				gone = append(gone, h.dataOf(g, 0))
				allowed[h.dataOf(g, 0)] = "Deleting stale overview data for "
				keep = append(keep, h.data(g)[1:]...)
			}
			snap := hfxSnapshot(t, keep)
			look := tlLookalikes(t, h, true)

			out := h.run(t, append([]string{"-a", "-v"}, tc.args...)...)

			h.checkGone(t, out, "Deleting stale overview data for ", gone)
			h.checkKept(t, "after dexpireover -a", snap)
			look.check(t, h, "after dexpireover -a")
			h.checkReports(t, out, allowed, tlSkip(t, h, out)...)
			for _, g := range h.groups {
				re := regexp.MustCompile(`(?m)^expired 4 +NB \+= 4 +\( *0 can, +0 stale, +4 exprd\) left 8 .* grp=` + regexp.QuoteMeta(g) + `$`)
				if n := len(re.FindAllString(out, -1)); n != 1 {
					t.Errorf("%d \"expired 4 NB += 4 ... left 8\" lines for %s, want 1", n, g)
				}
				// the over file is opened once: by one worker of -p
				seen := regexp.MustCompile(`(?m)^(expired |over file in use, skipped ).*grp=` + regexp.QuoteMeta(g) + `$`)
				if n := len(seen.FindAllString(out, -1)); n != 1 {
					t.Errorf("%d \"expired\" or \"over file in use\" lines for %s, want 1", n, g)
				}
			}
			if tc.args != nil {
				n := 0
				for _, l := range strings.Split(out, "\n") {
					if strings.HasPrefix(l, "Scanned ") {
						n++
					}
				}
				if n != 2 {
					t.Errorf("%d \"Scanned\" lines, want 2 (one per worker)", n)
				}
				if !strings.Contains(out, "Parallelizing dexpireover (2 forks) finished.\n") {
					t.Errorf("no \"Parallelizing dexpireover (2 forks) finished.\"")
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -v %s:\n%s", strings.Join(tc.args, " "), out)
			}

			restartReader(t, h.s)
			for _, g := range h.groups {
				h.checkOver(t, "after dexpireover -a", g, 5)
			}
		})
	}
}

// TestDexpireoverTopLevelRewrite: `dexpireover -a -R -v3` rewrites the files of the groups at the
// top of the group home, in the top itself, and removes the temporary files an interrupted -R or
// -z left there for those groups (tempRewriteName()).
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped; for each group at the top the temporary files ".o.0.<group>",
// ".d.0.<group>" and ".d.4.<group>.gz" at the top, and the tlLookalikes (with the directory);
// `dexpireover -a -v` (a -R run would make and rename the same temporary names itself), then
// `dexpireover -a -R -v3`. Expected: the first run removes the temporary files, each with
// "Removing old temp file: <path>"; the second gives "End of rewrite: <group>" and "Rebuilt 4
// data file(s) for <group>" for every group, its over file and four plain data files there; no
// '.' file in any group's directory but the lookalikes, which are there unchanged; nothing else
// reported in either run (but the tlBakLine); after a restart OVER 1-12 of every group as before.
// Fails on main 2277c452: the temporary files at the top stay, and no "End of rewrite" for the
// groups at the top.
func TestDexpireoverTopLevelRewrite(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		t.Run(layout, func(t *testing.T) {
			h := newTopSite(t, layout, "*:x30:e4\n")
			var temps []string
			allowed := map[string]string{}
			for _, g := range h.groups {
				if tlDir(g, layout) != "" {
					continue
				}
				for _, n := range []string{".o.0." + g, ".d.0." + g, ".d.4." + g + ".gz"} {
					p := filepath.Join(h.home, n)
					writeOver(t, p, []byte("left by an interrupted run: "+n+"\n"))
					temps = append(temps, p)
					allowed[p] = "Removing old temp file: "
				}
			}
			look := tlLookalikes(t, h, true)

			tout := h.run(t, "-a", "-v")
			h.checkGone(t, tout, "Removing old temp file: ", temps)
			h.checkReports(t, tout, allowed, tlSkip(t, h, tout)...)
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", tout)
			}

			out := h.run(t, "-a", "-R", "-v3")

			for _, g := range h.groups {
				if !strings.Contains(out, "End of rewrite: "+g+"\n") {
					t.Errorf("dexpireover -R did not rewrite %s (no \"End of rewrite: %s\")", g, g)
				}
				if !strings.Contains(out, "Rebuilt 4 data file(s) for "+g+"\n") {
					t.Errorf("dexpireover -R: no \"Rebuilt 4 data file(s) for %s\"", g)
				}
				for _, f := range append([]string{h.over(g)}, h.data(g)...) {
					if !hfxExists(f) {
						t.Errorf("after -R: %s is gone", h.rel(f))
					}
				}
			}
			look.check(t, h, "after -R")
			for _, l := range strings.Split(out, "\n") {
				if strings.HasPrefix(l, "Rewrite ") || strings.HasPrefix(l, "copy failed") || strings.HasPrefix(l, "New data file") {
					t.Errorf("dexpireover -R reports: %s", l)
				}
			}
			h.checkReports(t, out, nil, tlSkip(t, h, out)...)
			seen := map[string]bool{}
			for _, g := range h.groups {
				d := h.dirs[g]
				if seen[d] {
					continue
				}
				seen[d] = true
				ents, err := os.ReadDir(d)
				if err != nil {
					t.Errorf("directory of %s: %v", g, err)
				}
				for _, e := range ents {
					if _, ok := look.files[e.Name()]; strings.HasPrefix(e.Name(), ".") && !(ok && d == h.home) {
						t.Errorf("after -R: %s is left in %s", e.Name(), h.rel(d))
					}
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -R -v3:\n%s", out)
			}

			restartReader(t, h.s)
			for _, g := range h.groups {
				h.checkOver(t, "after dexpireover -a -R", g, 1)
			}
		})
	}
}

// TestDexpireoverTopLevelCompress: `dexpireover -a -z -v` compresses the data files at the top of
// the group home, at the top (diablo_minigzip() writes ".<name>.gz" in the file's directory and
// renames it).
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4:F1:Z1", dreaderd stopped, every data file two hours old, the tlLookalikes (without
// the directory). Expected: "Compressed 10 files": d.0 and d.4 of every group become
// d.0.<group>.gz and d.4.<group>.gz in its directory with the same bytes inside, d.8 and d.12
// stay plain and unchanged ("proximity to end of data", F1). None of these groups shares its
// directory with the group of its compressed name (x.y.gz is in x.y/, e2etop.gz in e2etop/;
// a one-character group x would share the top with x.gz, TestDexpireoverTopLevelGzPair), so
// gzNameTaken() (D-325) does not hold any back. No '.' file left but the
// lookalikes; nothing else reported (but the tlBakLine); after a restart OVER 1-12 of every group
// as before. Fails on main 2277c452: "Compressed 2 files" with hierarchy (only e2e.test's) and
// "Compressed 8 files" with hierarchy-full (not e2etop's); the data at the top stay plain.
// Skipped without zlib.
func TestDexpireoverTopLevelCompress(t *testing.T) {
	skipWithoutZlib(t)
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		t.Run(layout, func(t *testing.T) {
			h := newTopSite(t, layout, "*:x30:e4:F1:Z1\n")
			old := time.Now().Add(-2 * time.Hour)
			var all []string
			for _, g := range h.groups {
				for _, f := range h.data(g) {
					if err := os.Chtimes(f, old, old); err != nil {
						t.Fatal(err)
					}
					all = append(all, f)
				}
			}
			snap := hfxSnapshot(t, all)
			look := tlLookalikes(t, h, false)

			out := h.run(t, "-a", "-z", "-v")

			if !strings.Contains(out, "Compressed 10 files\n") {
				t.Errorf("dexpireover -a -z: no \"Compressed 10 files\"")
			}
			plain := map[string][]byte{}
			for _, g := range h.groups {
				for _, b := range hfxBases {
					f := h.dataOf(g, b)
					if b >= 8 {
						plain[f] = snap[f]
						if hfxExists(f + ".gz") {
							t.Errorf("%s.gz was made", h.rel(f))
						}
						continue
					}
					if hfxExists(f) {
						t.Errorf("%s is still there", h.rel(f))
					}
					zf, err := os.Open(f + ".gz")
					if err != nil {
						t.Errorf("no compressed file: %v", err)
						continue
					}
					zr, err := gzip.NewReader(zf)
					if err != nil {
						t.Errorf("%s.gz is not gzip: %v", h.rel(f), err)
					} else if got, err := io.ReadAll(zr); err != nil || !bytes.Equal(got, snap[f]) {
						t.Errorf("%s.gz does not hold the bytes of %s (%v)", h.rel(f), filepath.Base(f), err)
					}
					zf.Close()
				}
			}
			h.checkKept(t, "after dexpireover -a -z", plain)
			look.check(t, h, "after dexpireover -a -z")
			h.checkReports(t, out, nil, tlSkip(t, h, out)...)
			for _, g := range h.groups {
				ents, _ := os.ReadDir(h.dirs[g])
				for _, e := range ents {
					if _, ok := look.files[e.Name()]; strings.HasPrefix(e.Name(), ".") && !(ok && h.dirs[g] == h.home) {
						t.Errorf("after -z: %s is left in %s", e.Name(), h.rel(h.dirs[g]))
					}
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -z -v:\n%s", out)
			}

			restartReader(t, h.s)
			for _, g := range h.groups {
				h.checkOver(t, "after dexpireover -a -z", g, 1)
			}
		})
	}
}

// TestOverToolsTopLevelRemovedGroup: the files of a removed group whose directory is the top of
// the group home are removed by `dexpireover -a -v` and by `doverctl -v clean`, as in a
// subdirectory; an admin's files at the top stay, and an over file of no group there only goes
// when its header names the group of its name (o.0.e2etop~, a copy of e2etop's over file, stays,
// though it is the over file name of a group e2etop~ whose directory is the top).
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped, the tlLookalikes (the directory only for dexpireover: doverctl
// clean removes an empty directory at the top as before, and every file in one); for doverctl
// also a temporary file ".o.0.x.yz" of dexpireover (at the top with plain hierarchy, in x/ with
// hierarchy-full); the tool is run once (nothing may be reported or removed but that temporary
// file, "Deleting old temp file: <path>" at the top), then `dgrpctl rmgroup` x.y and e2etop, and
// the tool again. Subtests <layout>/dexpireover and <layout>/doverctl. Expected after the second
// run: the over file and the four data files of x.y and e2etop are removed, each with "Group for
// over file not found, removing <path>"; the files of the other groups and the lookalikes are
// there, the data files unchanged; nothing else reported (but the tlBakLine);
// after a restart GROUP x.y and e2etop give 411, OVER 1-12 of the others as before. Fails on main
// 2277c452 for the removed groups at the top (plain hierarchy x.y and e2etop, hierarchy-full
// e2etop): their files stay; with doverctl and plain hierarchy also ".o.0.x.yz" stays.
func TestOverToolsTopLevelRemovedGroup(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				h := newTopSite(t, layout, "*:x30:e4\n")
				look := tlLookalikes(t, h, tool == "dexpireover")
				run := func() string {
					if tool == "dexpireover" {
						return h.run(t, "-a", "-v")
					}
					out, err := h.s.TryRun("doverctl", "-v", "clean")
					if err != nil {
						t.Errorf("doverctl -v clean: %v", err)
					}
					return out
				}
				allowed := map[string]string{}
				if tool == "doverctl" {
					tmp := filepath.Join(h.dirs["x.yz"], ".o.0.x.yz")
					writeOver(t, tmp, []byte("left by an interrupted run\n"))
					first := run()
					if tlDir("x.yz", layout) == "" {
						h.checkGone(t, first, "Deleting old temp file: ", []string{tmp})
						allowed[tmp] = "Deleting old temp file: "
					} else {
						// below the top: removed as an unknown file, as on main
						allowed[tmp] = "Deleting unknown/stale file: "
					}
					tlCheckClean(t, h, "doverctl -v clean", first, allowed, tlSkip(t, h, first))
				} else {
					first := run()
					h.checkReports(t, first, nil, tlSkip(t, h, first)...)
				}
				var gone, keepFiles []string
				removed := map[string]bool{"x.y": true, "e2etop": true}
				for g := range removed {
					h.s.Run(t, "dgrpctl", "rmgroup", g)
				}
				allowed = map[string]string{}
				for _, g := range h.groups {
					if removed[g] {
						for _, f := range append([]string{h.over(g)}, h.data(g)...) {
							gone = append(gone, f)
							allowed[f] = "Group for over file not found, removing "
						}
						continue
					}
					keepFiles = append(keepFiles, h.data(g)...)
				}
				snap := hfxSnapshot(t, keepFiles)

				out := run()

				h.checkGone(t, out, "Group for over file not found, removing ", gone)
				h.checkKept(t, "after "+tool, snap)
				look.check(t, h, "after "+tool)
				if tool == "doverctl" {
					tlCheckClean(t, h, "doverctl -v clean", out, allowed, tlSkip(t, h, out))
				} else {
					h.checkReports(t, out, allowed, tlSkip(t, h, out)...)
				}
				for _, g := range h.groups {
					if !removed[g] && !hfxExists(h.over(g)) {
						t.Errorf("the over file %s is gone", h.rel(h.over(g)))
					}
				}
				if t.Failed() {
					t.Logf("%s after rmgroup:\n%s", tool, out)
				}

				restartReader(t, h.s)
				for _, g := range h.groups {
					if removed[g] {
						if line, _ := h.overAfter(t, g, 1, hfxArts); !strings.HasPrefix(line, "411 ") {
							t.Errorf("GROUP %s after rmgroup: %q, want 411", g, line)
						}
						continue
					}
					h.checkOver(t, "after "+tool, g, 1)
				}
			})
		}
	}
}

// tlCheckClean fails for each line of the doverctl clean output out that reports a removal or a
// problem ("Deleting", "Removing", "Removed", "not found", "bad file header", "Unable", "left
// alone"), except the lines that start with the prefix allowed gives a path they end in, and the
// lines in skip.
func tlCheckClean(t *testing.T, h *hfxSite, what, out string, allowed map[string]string, skip []string) {
	t.Helper()
	for _, l := range strings.Split(out, "\n") {
		bad := false
		for _, b := range []string{"Deleting", "Removing", "Removed", "not found", "bad file header", "Unable", "left alone", "failed"} {
			bad = bad || strings.Contains(l, b)
		}
		if !bad {
			continue
		}
		ok := false
		for p, prefix := range allowed {
			ok = ok || (strings.HasPrefix(l, prefix) && strings.HasSuffix(l, p))
		}
		for _, s := range skip {
			ok = ok || l == s
		}
		if !ok {
			t.Errorf("%s reports: %s", what, l)
		}
	}
}

// TestOverToolsTopLevelSwitch: after a switch between `hierarchy` and `hierarchy-full` made by
// editing readergrouphash (D-336), the old files of x.y and a.b in their directory of the other
// layout (the top for plain hierarchy, x/ and a/ for hierarchy-full) are left alone by both tools,
// as for the groups in subdirectories, and dexpireover does not move their first article number
// (NB) while those old over files are there (hierOtherOver()); the files of e2etop (the top in
// both layouts) are its own.
//
// Input: the groups e2e.test, x.y, a.b and e2etop; readergrouphash <from>, dexpire.ctl
// "*:x30:e4", 6 articles crossposted to all; dreaderd stopped, readergrouphash changed to <to>
// (dgrpctl listgrouphashfile gives the new directory), dreaderd started, 6 more articles to all;
// GROUP and OVER 1-12 of every group read, dreaderd stopped. Subtests hierarchy_to_full and
// full_to_hierarchy, each with `dexpireover -a -v` and `doverctl -v clean` on a site of its own.
// Expected: the old over file and data files d.0, d.4 of x.y and a.b are there with the same
// bytes, and -v says "file of <group> in the directory of the other hierarchy layout, left alone:
// <path>" for each; nothing else is removed or reported; dexpireover says "<group>: first article
// number not moved to 7: the group has an over file in the directory of the other hierarchy
// layout" for x.y and a.b (their over file of the configured layout starts at 7); after a restart
// GROUP gives "211 12 1 12" and OVER 1-12 as before the tool for every group. Fails on main
// 2277c452: hierarchy_to_full (both tools) without the "left alone" lines for the old files at
// the top, which the scan never reached; full_to_hierarchy/dexpireover without the "not moved"
// lines (the new files of x.y and a.b at the top were not expired); full_to_hierarchy/doverctl
// passes there too.
func TestOverToolsTopLevelSwitch(t *testing.T) {
	groups := []string{"e2e.test", "x.y", "a.b", "e2etop"}
	moved := []string{"x.y", "a.b"}
	for _, sw := range []struct{ name, from, to string }{
		{"hierarchy_to_full", "hierarchy", "hierarchy-full"},
		{"full_to_hierarchy", "hierarchy-full", "hierarchy"},
	} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(sw.name+"/"+tool, func(t *testing.T) {
				st := fakeserver.NewStore()
				for _, g := range groups {
					st.AddGroup(g, "top level switch test group")
				}
				spool := fakeserver.New(t, fakeserver.Options{Store: st})
				s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups,
					Config: map[string]string{"readergrouphash": sw.from},
					Files:  map[string]string{"dexpire.ctl": "*:x30:e4\n"}})
				home := strings.TrimSpace(s.Run(t, "dpath", "group"))
				rel := func(g, layout string) string {
					if d := tlDir(g, layout); d != "" {
						return d + "/o.0." + g
					}
					return "o.0." + g
				}
				swFeed(t, s, st, groups, swArts, "before")
				for _, g := range groups {
					readOver(t, s, g, swArts)
				}
				s.Proc.Stop()
				var alone []string
				for _, g := range groups {
					if got := hierListPath(t, s, g); got != rel(g, sw.from) {
						t.Fatalf("listgrouphashfile %s with %s = %q, want %q", g, sw.from, got, rel(g, sw.from))
					}
				}
				for _, g := range moved {
					dir := filepath.Join(home, tlDir(g, sw.from))
					for _, n := range []string{"o.0." + g, "d.0." + g, "d.4." + g} {
						f := filepath.Join(dir, n)
						if !hfxExists(f) {
							t.Fatalf("after the feed with %s: %s is missing", sw.from, f)
						}
						alone = append(alone, f)
					}
				}
				snap := hfxSnapshot(t, alone)

				conf := s.ReadFile(t, "diablo.config")
				s.WriteFile(t, "diablo.config", strings.Replace(conf, "readergrouphash\t"+sw.from+"\n", "readergrouphash\t"+sw.to+"\n", 1))
				for _, g := range groups {
					if got := hierListPath(t, s, g); got != rel(g, sw.to) {
						t.Fatalf("listgrouphashfile %s with %s = %q, want %q", g, sw.to, got, rel(g, sw.to))
					}
				}
				restartReader(t, s)
				swFeed(t, s, st, groups, swArts, "after")
				before := map[string][]string{}
				for _, g := range groups {
					want := 2 * swArts
					for _, m := range moved {
						if g == m {
							want = swArts // the new over file has the articles 7-12
						}
					}
					var line string
					testkit.WaitFor(t, 10*time.Second, fmt.Sprintf("OVER of %s: %d lines", g, want), func() bool {
						line, before[g] = swOver(t, s, g)
						return len(before[g]) == want
					})
					if !strings.HasPrefix(line, fmt.Sprintf("211 %d 1 %d ", 2*swArts, 2*swArts)) {
						t.Fatalf("before the tool: GROUP %s %q, want 211 %d 1 %d", g, line, 2*swArts, 2*swArts)
					}
				}
				s.Proc.Stop()

				var out string
				var err error
				if tool == "dexpireover" {
					out, err = s.TryRun("dexpireover", "-a", "-v")
				} else {
					out, err = s.TryRun("doverctl", "-v", "clean")
				}
				if err != nil {
					t.Errorf("%s: %v", tool, err)
				}
				for _, f := range alone {
					got, err := os.ReadFile(f)
					if err != nil {
						t.Errorf("%s: the old copy %s is gone: %v", tool, strings.TrimPrefix(f, home+"/"), err)
					} else if !bytes.Equal(got, snap[f]) {
						t.Errorf("%s: the old copy %s was changed", tool, strings.TrimPrefix(f, home+"/"))
					}
				}
				swCheckOutput(t, tool, out, alone)
				if tool == "dexpireover" {
					for _, g := range moved {
						l := g + ": first article number not moved to 7: the group has an over file in the directory of the other hierarchy layout\n"
						if !strings.Contains(out, l) {
							t.Errorf("dexpireover did not say %q", strings.TrimSpace(l))
						}
					}
				}
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}

				restartReader(t, s)
				for _, g := range groups {
					line, over := swOver(t, s, g)
					if !strings.HasPrefix(line, fmt.Sprintf("211 %d 1 %d ", 2*swArts, 2*swArts)) ||
						strings.Join(over, "\n") != strings.Join(before[g], "\n") {
						t.Errorf("after %s: GROUP %s %q, OVER 1-12 %q; before %q", tool, g, line, over, before[g])
					}
				}
			})
		}
	}
}

// tlRunLimit runs the program name of the site s with args as Site.TryRun does, but stops it
// after limit: hung reports that it did not finish (it was killed), so that a tool that blocks
// fails the test instead of the suite.
func tlRunLimit(s *Site, limit time.Duration, name string, args ...string) (out string, hung bool, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), limit)
	defer cancel()
	cmd := exec.CommandContext(ctx, filepath.Join(s.Bin, name), args...)
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	var b bytes.Buffer
	cmd.Stdout, cmd.Stderr = &b, &b
	cmd.WaitDelay = 3 * time.Second
	err = cmd.Run()
	if errors.Is(err, exec.ErrWaitDelay) {
		err = nil
	}
	return b.String(), ctx.Err() != nil, err
}

// tlTool runs dexpireover -a -v or doverctl -v clean on h with a limit of 30 seconds.
func tlTool(t *testing.T, h *hfxSite, tool string) string {
	t.Helper()
	args := []string{"-a", "-v"}
	if tool == "doverctl" {
		args = []string{"-v", "clean"}
	}
	out, hung, err := tlRunLimit(h.s, 30*time.Second, tool, args...)
	switch {
	case hung:
		t.Errorf("%s %s did not finish in 30s (killed); output:\n%s", tool, strings.Join(args, " "), out)
	case err != nil:
		t.Errorf("%s %s: %v", tool, strings.Join(args, " "), err)
	}
	return out
}

// TestOverToolsTopLevelSymlinks: chatgpt-1's F1 on 120a76c8 (#524 r1). Main never looked at the
// files at the top of the group home, so as for the trees #515 added, a symlink there is not
// followed, opened or removed by either tool, also when its name is that of a file of a group at
// the top.
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped, the articles 1-4 made 40 days old in every over file; then e2etop's
// over file o.0.e2etop and data file d.0.e2etop moved out of the group home (to <site>/outside/)
// and replaced by symlinks to them, a symlink .o.0.e2etop (a temporary file name of e2etop) to a
// file outside, and a dangling symlink d.0.gonelink. Subtests <layout>/dexpireover (`-a -v`) and
// <layout>/doverctl (`-v clean`). Expected: the four links are there, links to the same targets,
// the targets byte-identical; -v says "Skipping symlink <path>: not followed at the top of the
// group home" once for each link. Fails on 120a76c8: dexpireover expires and resizes e2etop's over
// file through the link (the target changed), removes the link d.0.e2etop as stale and
// .o.0.e2etop as a temporary file; doverctl removes .o.0.e2etop; no "Skipping symlink" line.
func TestOverToolsTopLevelSymlinks(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				h := newTopSite(t, layout, "*:x30:e4\n")
				old := time.Now().Add(-40 * 24 * time.Hour)
				for _, g := range h.groups {
					hfxAge(t, h.over(g), []int{1, 2, 3, 4}, old)
				}
				outside := filepath.Join(h.s.Dir, "outside")
				if err := os.MkdirAll(outside, 0o755); err != nil {
					t.Fatal(err)
				}
				links := map[string]string{} // link below the group home -> target
				for _, f := range []string{h.over("e2etop"), h.dataOf("e2etop", 0)} {
					to := filepath.Join(outside, filepath.Base(f))
					if err := os.Rename(f, to); err != nil {
						t.Fatal(err)
					}
					links[f] = to
				}
				tmp := filepath.Join(outside, "tempfile")
				writeOver(t, tmp, []byte("a file outside the group home\n"))
				links[filepath.Join(h.home, ".o.0.e2etop")] = tmp
				links[filepath.Join(h.home, "d.0.gonelink")] = filepath.Join(outside, "missing")
				var targets []string
				for l, to := range links {
					if err := os.Symlink(to, l); err != nil {
						t.Fatal(err)
					}
					if hfxExists(to) {
						targets = append(targets, to)
					}
				}
				snap := hfxSnapshot(t, targets)

				out := tlTool(t, h, tool)

				for l, to := range links {
					if got, err := os.Readlink(l); err != nil || got != to {
						t.Errorf("%s: the link %s is gone or changed: %q, %v", tool, h.rel(l), got, err)
					}
					line := "Skipping symlink " + l + ": not followed at the top of the group home"
					if n := strings.Count(out, line+"\n"); n != 1 {
						t.Errorf("%s: %d lines %q, want 1", tool, n, line)
					}
				}
				for f, b := range snap {
					if got, err := os.ReadFile(f); err != nil || !bytes.Equal(got, b) {
						t.Errorf("%s: the link target %s was changed (%v)", tool, filepath.Base(f), err)
					}
				}
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}
			})
		}
	}
}

// TestOverToolsTopLevelFifo: chatgpt-1's F2 on 120a76c8 (#524 r1): at the top of the group home
// only regular files are opened. A FIFO named like the over file of a group that does not exist
// made dexpireover open() it for the header check (overNamesGroup()), which blocks for ever with
// no writer.
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped, the FIFOs o.0.gone and d.0.gone (the over and data file names of a
// group gone, whose directory is the top) and .d.4.e2etop (a temporary file name of e2etop, which
// a run removes when it is a file) at the top. Subtests <layout>/dexpireover (`-a -v`) and
// <layout>/doverctl (`-v clean`), each stopped after 30 seconds. Expected: the tool finishes, both
// FIFOs are there. Fails on 120a76c8 with dexpireover: it does not finish (killed after 30s,
// blocked in the open() of o.0.gone in the first pass), and a copy that only drops the
// regular-file check of r2 removes .d.4.e2etop (the O_NONBLOCK open of the header check keeps
// it from blocking on o.0.gone); doverctl checked for a regular file there already (stat(),
// S_ISREG) and passes there too.
func TestOverToolsTopLevelFifo(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				h := newTopSite(t, layout, "*:x30:e4\n")
				var fifos []string
				for _, n := range []string{"o.0.gone", "d.0.gone", ".d.4.e2etop"} {
					f := filepath.Join(h.home, n)
					if err := exec.Command("mkfifo", f).Run(); err != nil {
						t.Skipf("no fifo: %v", err)
					}
					fifos = append(fifos, f)
				}

				out := tlTool(t, h, tool)

				for _, f := range fifos {
					if st, err := os.Lstat(f); err != nil || st.Mode()&os.ModeNamedPipe == 0 {
						t.Errorf("%s: the FIFO %s is gone or changed: %v", tool, h.rel(f), err)
					}
				}
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}
			})
		}
	}
}

// TestDexpireoverTopLevelGzPair: go-while's #524 r1 review: with plain hierarchy a group of one
// character x and the group x.gz both have their files at the top of the group home
// (FindGroupDir() skips the component "x" of x.gz), so "d.<n>.x.gz" is x's compressed name and
// x.gz's plain data file (D-325). `dexpireover -R` of x removes both names in its directory, so
// on 120a76c8, which scanned the top, it emptied OVER of x.gz; main never touched the top. Now
// -R rewrites neither group there and says why; -a judges "d.<n>.x.gz" by both groups (D-318),
// as in a subdirectory. With hierarchy-full x.gz is in x/ and both are rewritten as usual.
//
// Input: the groups e2e.test (control), x and x.gz; readergrouphash hierarchy or hierarchy-full;
// 12 articles crossposted to all three, dexpire.ctl "*:x30:e4", dreaderd stopped. Subtests:
//   - <layout>/rewrite: `dexpireover -a -R -v3`. Expected with hierarchy: "x: not rewritten: the
//     group x.gz has its files in the same directory, the top of the group home (D-325)" and the
//     same for x.gz (naming x), no "End of rewrite" for either, their over and data files there,
//     the data files byte-identical; e2e.test rewritten. With hierarchy-full: "End of rewrite" for
//     all three, no D-325 line. Nothing else reported; after a restart GROUP "211 12 1 12" and
//     OVER 1-12 as before for every group. Fails on 120a76c8 with hierarchy: OVER of x.gz empty
//     after the restart, its data files gone.
//   - <layout>/expire: the articles 1-4 made 40 days old in every over file, `dexpireover -a -v`.
//     Expected: one "expired 4 NB += 4 ... left 8" line per group; the base-0 data files of e2e.test
//     and x and "d.0.x.gz" (x.gz's plain data, and with hierarchy also x's compressed name: stale
//     for both groups) removed with "Deleting stale overview data for"; the bases 4, 8, 12 kept
//     unchanged; after a restart GROUP "211 8 5 12" and OVER 5-12 as before. Passes on
//     120a76c8 too (the D-318 rule judged the file by both groups there as well).
//   - hierarchy/switch (go-while's R2-F1 on 3956bf30): fed with hierarchy, dreaderd stopped,
//     readergrouphash changed to hierarchy-full (x stays at the top, x.gz moves to x/, its old
//     files stay at the top), `dexpireover -a -R -v3`, readergrouphash set back to hierarchy,
//     dreaderd started. Expected: the D-325 line for x (x.gz has its over file at the top, its
//     directory in the other layout), x not rewritten, the data files of x and x.gz there
//     byte-identical; nothing reported but the "left alone" lines of x.gz's old files; OVER
//     1-12 of every group as before. Fails on 3956bf30: x is rewritten and removes x.gz's
//     d.<n>.x.gz, OVER of x.gz is empty.
func TestDexpireoverTopLevelGzPair(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, mode := range []string{"rewrite", "expire", "switch"} {
			if mode == "switch" && layout != "hierarchy" {
				continue
			}
			t.Run(layout+"/"+mode, func(t *testing.T) {
				want := map[string]string{"e2e.test": "e2e/o.0.e2e.test", "x": "o.0.x", "x.gz": "o.0.x.gz"}
				if layout == "hierarchy-full" {
					want["x.gz"] = "x/o.0.x.gz"
				}
				h := newHfxSiteOf(t, layout, "*:x30:e4\n", []string{"e2e.test", "x", "x.gz"}, want)
				pair := layout == "hierarchy"
				if mode == "switch" {
					var data []string
					for _, g := range []string{"x", "x.gz"} {
						data = append(data, h.data(g)...)
					}
					snap := hfxSnapshot(t, data)
					setHash := func(from, to string) {
						conf := h.s.ReadFile(t, "diablo.config")
						old := "readergrouphash\t" + from + "\n"
						if !strings.Contains(conf, old) {
							t.Fatalf("diablo.config has no %q", old)
						}
						h.s.WriteFile(t, "diablo.config", strings.Replace(conf, old, "readergrouphash\t"+to+"\n", 1))
					}
					setHash("hierarchy", "hierarchy-full")
					if rel := hierListPath(t, h.s, "x.gz"); rel != "x/o.0.x.gz" {
						t.Fatalf("listgrouphashfile x.gz with hierarchy-full = %q", rel)
					}

					out := h.run(t, "-a", "-R", "-v3")

					setHash("hierarchy-full", "hierarchy")
					d325 := "x: not rewritten: the group x.gz has its files in the same directory, the top of the group home (D-325)\n"
					if !strings.Contains(out, d325) || strings.Contains(out, "End of rewrite: x\n") {
						t.Errorf("after the switch: x rewritten, or no %q", strings.TrimSpace(d325))
					}
					h.checkKept(t, "after -R with hierarchy-full", snap)
					h.checkReports(t, out, nil, swAloneLine)
					if t.Failed() {
						t.Logf("dexpireover -a -R -v3 with hierarchy-full:\n%s", out)
					}
					restartReader(t, h.s)
					for _, g := range h.groups {
						h.checkOver(t, "after the switch back", g, 1)
					}
					return
				}
				if mode == "rewrite" {
					var data []string
					for _, g := range []string{"x", "x.gz"} {
						data = append(data, h.data(g)...)
					}
					snap := hfxSnapshot(t, data)

					out := h.run(t, "-a", "-R", "-v3")

					for _, g := range h.groups {
						line := map[string]string{"x": "x.gz", "x.gz": "x"}[g]
						d325 := g + ": not rewritten: the group " + line + " has its files in the same directory, the top of the group home (D-325)\n"
						rewritten := strings.Contains(out, "End of rewrite: "+g+"\n")
						switch {
						case pair && g != "e2e.test":
							if rewritten || !strings.Contains(out, d325) {
								t.Errorf("hierarchy: %s rewritten %v, want not, with %q", g, rewritten, strings.TrimSpace(d325))
							}
						case !rewritten:
							t.Errorf("dexpireover -R did not rewrite %s (no \"End of rewrite: %s\")", g, g)
						}
						if line != "" && !pair && strings.Contains(out, d325) {
							t.Errorf("hierarchy-full: %q", strings.TrimSpace(d325))
						}
						for _, f := range append([]string{h.over(g)}, h.data(g)...) {
							if !hfxExists(f) {
								t.Errorf("after -R: %s is gone", h.rel(f))
							}
						}
					}
					if pair {
						h.checkKept(t, "after -R", snap)
					}
					for _, l := range strings.Split(out, "\n") {
						if strings.HasPrefix(l, "Rewrite ") || strings.HasPrefix(l, "copy failed") || strings.HasPrefix(l, "New data file") {
							t.Errorf("dexpireover -R reports: %s", l)
						}
					}
					h.checkReports(t, out, nil)
					if t.Failed() {
						t.Logf("dexpireover -a -R -v3:\n%s", out)
					}
					restartReader(t, h.s)
					for _, g := range h.groups {
						h.checkOver(t, "after dexpireover -a -R", g, 1)
					}
					return
				}

				old := time.Now().Add(-40 * 24 * time.Hour)
				var gone, keep []string
				allowed := map[string]string{}
				for _, g := range h.groups {
					hfxAge(t, h.over(g), []int{1, 2, 3, 4}, old)
					gone = append(gone, h.dataOf(g, 0))
					allowed[h.dataOf(g, 0)] = "Deleting stale overview data for "
					keep = append(keep, h.data(g)[1:]...)
				}
				snap := hfxSnapshot(t, keep)

				out := h.run(t, "-a", "-v")

				h.checkGone(t, out, "Deleting stale overview data for ", gone)
				h.checkKept(t, "after dexpireover -a", snap)
				h.checkReports(t, out, allowed)
				for _, g := range h.groups {
					re := regexp.MustCompile(`(?m)^expired 4 +NB \+= 4 +\( *0 can, +0 stale, +4 exprd\) left 8 .* grp=` + regexp.QuoteMeta(g) + `$`)
					if n := len(re.FindAllString(out, -1)); n != 1 {
						t.Errorf("%d \"expired 4 NB += 4 ... left 8\" lines for %s, want 1", n, g)
					}
				}
				if t.Failed() {
					t.Logf("dexpireover -a -v:\n%s", out)
				}
				restartReader(t, h.s)
				for _, g := range h.groups {
					h.checkOver(t, "after dexpireover -a", g, 5)
				}
			})
		}
	}
}

// tlMkfifo makes a FIFO at path (skips the test where mkfifo is missing).
func tlMkfifo(t *testing.T, path string) {
	t.Helper()
	if err := exec.Command("mkfifo", path).Run(); err != nil {
		t.Skipf("no fifo: %v", err)
	}
}

// tlIsFifo reports a path that is gone or no longer a FIFO.
func tlIsFifo(t *testing.T, h *hfxSite, what, path string) {
	t.Helper()
	if st, err := os.Lstat(path); err != nil || st.Mode()&os.ModeNamedPipe == 0 {
		t.Errorf("%s: the FIFO %s is gone or changed: %v", what, h.rel(path), err)
	}
}

// TestDexpireoverTopLevelRewriteFifo: chatgpt-1's M2 and go-while's R2-F2 on 3956bf30 (#524 r2):
// `dexpireover -R` opens a group's data files by their computed names (RewriteData()), not by the
// scan, so a FIFO at the name of a data file of a group, beside the group's valid over file,
// blocked it, at the top and, as on main, in a subdirectory. A group is now not rewritten when a
// data file name of its range is there but no regular file (at the top also a link), and nothing
// of -R is done for it (also not the forced resize).
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped; e2etop's d.0.e2etop and e2e.test's e2e/d.4.e2e.test replaced by
// FIFOs, with hierarchy also x.y's d.4.x.y; `dexpireover -a -R -v3` stopped after 30 seconds. Expected: it finishes; "<group>:
// not rewritten: <path> is not a regular file" for e2etop (and x.y), no "End of rewrite" for them;
// the FIFOs stay; their other data files byte-identical; every other group rewritten ("End of
// rewrite: <group>"); after a restart OVER 1-12 of the other groups as before (OVER of e2etop and
// x.y is not asked: dreaderd would open the FIFO). Fails on 3956bf30 and on main 2277c452 (for
// e2e.test): dexpireover does not finish (killed after 30s).
func TestDexpireoverTopLevelRewriteFifo(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		t.Run(layout, func(t *testing.T) {
			h := newTopSite(t, layout, "*:x30:e4\n")
			// at the top e2etop's (and x.y's), below it e2e.test's in e2e/ (R2-F2: on main too)
			fifoOf := map[string]string{"e2etop": h.dataOf("e2etop", 0), "e2e.test": h.dataOf("e2e.test", 4)}
			if layout == "hierarchy" {
				fifoOf["x.y"] = h.dataOf("x.y", 4)
			}
			var keep []string
			for g, f := range fifoOf {
				for _, d := range h.data(g) {
					if d != f {
						keep = append(keep, d)
					}
				}
			}
			snap := hfxSnapshot(t, keep)
			for _, f := range fifoOf {
				if err := os.Remove(f); err != nil {
					t.Fatal(err)
				}
				tlMkfifo(t, f)
			}

			out, hung, err := tlRunLimit(h.s, 30*time.Second, "dexpireover", "-a", "-R", "-v3")
			if hung {
				t.Fatalf("dexpireover -a -R -v3 did not finish in 30s (killed); output:\n%s", out)
			}
			if err != nil {
				t.Errorf("dexpireover -a -R -v3: %v", err)
			}

			for g, f := range fifoOf {
				tlIsFifo(t, h, "after -R", f)
				l := g + ": not rewritten: " + f + " is not a regular file\n"
				if !strings.Contains(out, l) || strings.Contains(out, "End of rewrite: "+g+"\n") {
					t.Errorf("no %q, or %s rewritten", strings.TrimSpace(l), g)
				}
			}
			h.checkKept(t, "after -R", snap)
			for _, g := range h.groups {
				if _, skip := fifoOf[g]; !skip && !strings.Contains(out, "End of rewrite: "+g+"\n") {
					t.Errorf("dexpireover -R did not rewrite %s", g)
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -R -v3:\n%s", out)
			}
			restartReader(t, h.s)
			for _, g := range h.groups {
				if _, skip := fifoOf[g]; !skip {
					h.checkOver(t, "after dexpireover -a -R", g, 1)
				}
			}
		})
	}
}

// TestOverToolsTopLevelOverFifo: the data files of a group at the top are judged by the header of
// the group's over file, which both tools open by its computed name (dataFileStale(),
// DataFileStale() -> getDataEntries()), not by the scan; a FIFO there blocked that open() for
// every data file of the group. At the top such data files are now left as they are.
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped, e2etop's over file o.0.e2etop replaced by a FIFO. Subtests
// <layout>/dexpireover (`-a -v`) and <layout>/doverctl (`-v clean`), stopped after 30 seconds.
// Expected: the tool finishes; the FIFO stays; the four data files of e2etop are there, unchanged,
// each with "data file left alone: the over file <path> of its group is not a regular file:
// <data path>". Fails on 3956bf30 with both tools: they do not finish (killed after 30s).
func TestOverToolsTopLevelOverFifo(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				h := newTopSite(t, layout, "*:x30:e4\n")
				snap := hfxSnapshot(t, h.data("e2etop"))
				over := h.over("e2etop")
				if err := os.Remove(over); err != nil {
					t.Fatal(err)
				}
				tlMkfifo(t, over)

				out := tlTool(t, h, tool)

				tlIsFifo(t, h, tool, over)
				h.checkKept(t, "after "+tool, snap)
				for _, d := range h.data("e2etop") {
					l := "data file left alone: the over file " + over + " of its group is not a regular file: " + d + "\n"
					if !strings.Contains(out, l) {
						t.Errorf("%s: no %q", tool, strings.TrimSpace(l))
					}
				}
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}
			})
		}
	}
}

// tlRemoveFailSource makes remove() and unlink() of a path whose last component is
// REMOVEFAIL_NAME fail with EPERM, as for a file an admin made immutable (chattr +i, chflags
// schg), which the e2e user cannot set up itself: on Linux the right to unlink is the
// directory's, not the file's.
const tlRemoveFailSource = `#define _GNU_SOURCE
#define __EXTENSIONS__
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/*FDPATH*/
static int
fail(const char *path)
{
    const char *name = getenv("REMOVEFAIL_NAME");
    const char *base = strrchr(path, '/');

    base = base ? base + 1 : path;
    if (name == NULL || strcmp(base, name) != 0)
	return 0;
    errno = EPERM;
    return 1;
}

int
remove(const char *path)
{
    static int (*real)(const char *);

    if (real == NULL)
	real = (int (*)(const char *))dlsym(RTLD_NEXT, "remove");
    if (fail(path))
	return -1;
    return real(path);
}

int
unlink(const char *path)
{
    static int (*real)(const char *);

    if (real == NULL)
	real = (int (*)(const char *))dlsym(RTLD_NEXT, "unlink");
    if (fail(path))
	return -1;
    return real(path);
}
`

// TestOverToolsTopLevelRemoveFails: chatgpt-1's M1 on 3956bf30 (#524 r2): the data files of a
// removed group at the top go only when its over file there was removed in the same run; the
// mark was set before the removal and its result not checked, so when the over file could not
// be removed its data files went all the same. Now the mark is set only after a successful
// removal (with -n as if it had worked, TestOverToolsTopLevelDryRun).
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped, `dgrpctl rmgroup` x.y and e2etop; a preloaded library makes
// remove() and unlink() of o.0.e2etop fail with EPERM. Subtests <layout>/dexpireover (`-a -v`)
// and <layout>/doverctl (`-v clean`, which exits 1 for the failed removal). Expected: o.0.e2etop
// is there ("Unable to remove <path>"), its four data files are there unchanged, each with "data
// file of no group at the top of the group home with no over file of that group, left alone";
// the over and data files of x.y are removed ("Group for over file not found, removing"). Fails
// on 3956bf30: e2etop's data files are removed. Skipped where LD_PRELOAD shims do not run.
func TestOverToolsTopLevelRemoveFails(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				so := preloadShim(t, "removefail", tlRemoveFailSource)
				h := newTopSite(t, layout, "*:x30:e4\n")
				for _, g := range []string{"x.y", "e2etop"} {
					h.s.Run(t, "dgrpctl", "rmgroup", g)
				}
				snap := hfxSnapshot(t, h.data("e2etop"))
				base := h.s.Env
				h.s.Env = append(slices.Clone(base), lockFaultEnv(so), "REMOVEFAIL_NAME=o.0.e2etop")
				args := []string{"-a", "-v"}
				if tool == "doverctl" {
					args = []string{"-v", "clean"}
				}
				out, hung, err := tlRunLimit(h.s, 30*time.Second, tool, args...)
				h.s.Env = base
				if hung {
					t.Fatalf("%s did not finish in 30s; output:\n%s", tool, out)
				}
				if err != nil && tool == "dexpireover" {
					t.Errorf("dexpireover: %v", err)
				}

				if !hfxExists(h.over("e2etop")) {
					t.Fatalf("%s: o.0.e2etop is gone: the shim did not work", tool)
				}
				if !strings.Contains(out, "Unable to remove "+h.over("e2etop")) {
					t.Errorf("%s: no \"Unable to remove %s\"", tool, h.over("e2etop"))
				}
				h.checkKept(t, "after "+tool, snap)
				for _, d := range h.data("e2etop") {
					if l := "data file of no group at the top of the group home with no over file of that group, left alone: " + d + "\n"; !strings.Contains(out, l) {
						t.Errorf("%s: no %q", tool, strings.TrimSpace(l))
					}
				}
				h.checkGone(t, out, "Group for over file not found, removing ", append([]string{h.over("x.y")}, h.data("x.y")...))
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}
			})
		}
	}
}

// TestOverToolsTopLevelDryRun: with -n the tools remove nothing but report what a run would do:
// after `dgrpctl rmgroup e2etop` "Group for over file not found, removing <path>" for its over
// file at the top and for each of its four data files (the removal of the over file counts as
// done for the data files, M1 above). Input: the tlGroups (hierarchy, hierarchy-full), 12 articles,
// dreaderd stopped, rmgroup e2etop; `dexpireover -a -n -v`, `doverctl -n -v clean`. Expected:
// the five lines, all five files still there unchanged. Passes on 3956bf30 too (it marked the
// group before the removal); a copy that marks only after a real removal fails it.
func TestOverToolsTopLevelDryRun(t *testing.T) {
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				h := newTopSite(t, layout, "*:x30:e4\n")
				h.s.Run(t, "dgrpctl", "rmgroup", "e2etop")
				files := append([]string{h.over("e2etop")}, h.data("e2etop")...)
				snap := hfxSnapshot(t, files)
				args := []string{"-a", "-n", "-v"}
				if tool == "doverctl" {
					args = []string{"-n", "-v", "clean"}
				}
				out, hung, err := tlRunLimit(h.s, 30*time.Second, tool, args...)
				if hung || err != nil {
					t.Errorf("%s %s: hung %v, %v", tool, strings.Join(args, " "), hung, err)
				}
				h.checkKept(t, "after "+tool+" -n", snap)
				for _, f := range files {
					if !strings.Contains(out, "Group for over file not found, removing "+f+"\n") {
						t.Errorf("%s -n: no \"Group for over file not found, removing %s\"", tool, h.rel(f))
					}
				}
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}
			})
		}
	}
}

// tlRunArgv runs argv (a full path first) with the environment and directory of the site s, as
// tlRunLimit does.
func tlRunArgv(s *Site, limit time.Duration, argv ...string) (out string, hung bool, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), limit)
	defer cancel()
	cmd := exec.CommandContext(ctx, argv[0], argv[1:]...)
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	var b bytes.Buffer
	cmd.Stdout, cmd.Stderr = &b, &b
	cmd.WaitDelay = 3 * time.Second
	err = cmd.Run()
	if errors.Is(err, exec.ErrWaitDelay) {
		err = nil
	}
	return b.String(), ctx.Err() != nil, err
}

// TestOverToolsTopLevelRemoveBusy: go-while's reproduction of M1 without root (#524 r2): in an
// unprivileged user and mount namespace (`unshare -rm`) a file bind-mounted onto the over file
// of a removed group at the top makes its unlink() fail with EBUSY. Its data files must stay.
//
// Input: the tlGroups (hierarchy, hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4", dreaderd stopped, `dgrpctl rmgroup` x.y and e2etop; the tool run as `unshare -rm sh
// -c 'mount --bind <copy> <home>/o.0.e2etop && exec <tool> ...'`. Subtests <layout>/dexpireover
// (`-a -v`), <layout>/doverctl (`-v clean`, exit 1). Expected: "Unable to remove <home>/o.0.e2etop:
// Device or resource busy" (dexpireover said nothing before), o.0.e2etop and e2etop's four data
// files there unchanged, each data file with the F3 "left alone" line; x.y's files removed. Fails
// on 3956bf30: e2etop's data files are removed. Skipped where `unshare -rm` with a bind mount does
// not work (no unshare, unprivileged user namespaces off).
func TestOverToolsTopLevelRemoveBusy(t *testing.T) {
	un, err := exec.LookPath("unshare")
	if err != nil {
		t.Skip("no unshare: the test needs `unshare -rm` (util-linux)")
	}
	probe := filepath.Join(t.TempDir(), "probe")
	writeOver(t, probe, []byte("x"))
	if out, err := exec.Command(un, "-rm", "sh", "-c", `mount --bind "$0" "$0"`, probe).CombinedOutput(); err != nil {
		t.Skipf("`unshare -rm` with a bind mount does not work here (unprivileged user namespaces off?): %v %s", err, out)
	}
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		for _, tool := range []string{"dexpireover", "doverctl"} {
			t.Run(layout+"/"+tool, func(t *testing.T) {
				h := newTopSite(t, layout, "*:x30:e4\n")
				for _, g := range []string{"x.y", "e2etop"} {
					h.s.Run(t, "dgrpctl", "rmgroup", g)
				}
				over := h.over("e2etop")
				files := append([]string{over}, h.data("e2etop")...)
				snap := hfxSnapshot(t, files)
				src := filepath.Join(h.s.Dir, "bind-source")
				writeOver(t, src, snap[over])
				args := []string{"-a", "-v"}
				if tool == "doverctl" {
					args = []string{"-v", "clean"}
				}
				argv := append([]string{un, "-rm", "sh", "-c", `mount --bind "$1" "$2" && shift 2 && exec "$@"`, "sh",
					src, over, filepath.Join(h.s.Bin, tool)}, args...)
				out, hung, err := tlRunArgv(h.s, 30*time.Second, argv...)
				if hung {
					t.Fatalf("%s did not finish in 30s; output:\n%s", tool, out)
				}
				if err != nil && tool == "dexpireover" {
					t.Errorf("dexpireover: %v\n%s", err, out)
				}
				if l := "Unable to remove " + over + ": Device or resource busy"; !strings.Contains(out, l) {
					t.Errorf("%s: no %q", tool, l)
				}
				h.checkKept(t, "after "+tool, snap)
				for _, d := range h.data("e2etop") {
					if l := "data file of no group at the top of the group home with no over file of that group, left alone: " + d + "\n"; !strings.Contains(out, l) {
						t.Errorf("%s: no %q", tool, strings.TrimSpace(l))
					}
				}
				h.checkGone(t, out, "Group for over file not found, removing ", append([]string{h.over("x.y")}, h.data("x.y")...))
				if t.Failed() {
					t.Logf("%s output:\n%s", tool, out)
				}
			})
		}
	}
}
