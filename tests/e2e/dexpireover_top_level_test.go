package e2e

import (
	"bytes"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
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
// top. With dir also an empty directory "d.16.x.y" (a data file name of x.y).
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
	}}
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

// tlSkip are the lines checkReports() lets pass for the lookalikes; the tlBakLine must be there.
func tlSkip(t *testing.T, h *hfxSite, out string) []string {
	t.Helper()
	if !strings.Contains(out, tlBakLine(h)+"\n") {
		t.Errorf("no %q", tlBakLine(h))
	}
	return []string{tlBakLine(h)}
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
// stay plain and unchanged ("proximity to end of data", F1). A group at the top never shares
// its directory with the group of its compressed name (x.y.gz is in x.y/, e2etop.gz in
// e2etop/), so gzNameTaken() (D-325) does not hold any back. No '.' file left but the
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
