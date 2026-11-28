package e2e

import (
	"regexp"
	"strings"
	"testing"
	"time"
)

// D-325 in a subdirectory: with plain `hierarchy` FindGroupDir() (lib/hash.c) skips a component
// of one character, so a group X whose last component has one character and the group X.gz have
// one directory (e2e.x and e2e.x.gz in e2e/, o.1.2 and o.1.2.gz in o.1/), and "d.<n>.X.gz" is X's
// compressed data name and X.gz's plain data file at once. RewriteData() of X removes both names
// "d.<n>.X" and "d.<n>.X.gz" in its directory, so `dexpireover -R` emptied OVER of X.gz; D-338
// skipped such a pair only at the top of the group home (TestDexpireoverTopLevelGzPair). With
// hierarchy-full the two groups are in different directories (e2e/ and e2e/x/, o/1/ and o/1/2/).

// gpGroups are the groups of these tests with their directories below the group home in plain
// hierarchy and in hierarchy-full.
var gpGroups = []struct{ name, hier, full string }{
	{"e2e.test", "e2e", "e2e"}, // control: no pair
	{"e2e.x", "e2e", "e2e"},    // X
	{"e2e.x.gz", "e2e", "e2e/x"},
	{"o.1.2", "o.1", "o/1"}, // X, two one-character components (also md5-like names, D-335)
	{"o.1.2.gz", "o.1", "o/1/2"},
	{"e2e.y", "e2e", "e2e"}, // a one-character last component, but no group e2e.y.gz
}

// gpPair is the other group of g's pair, "" for none.
var gpPair = map[string]string{"e2e.x": "e2e.x.gz", "e2e.x.gz": "e2e.x", "o.1.2": "o.1.2.gz", "o.1.2.gz": "o.1.2"}

// gpLine is what `dexpireover -R -v` prints for a group g of a pair that it does not rewrite.
func gpLine(g string) string {
	return g + ": not rewritten: the group " + gpPair[g] + " has its files in the same directory (D-325)\n"
}

// newGpSite is a reader site with the gpGroups (readergrouphash layout, dexpire.ctl "*:x30:e4"),
// fed with hfxArts articles crossposted to all of them and stopped; dgrpctl listgrouphashfile
// must give each group its directory of gpGroups.
func newGpSite(t *testing.T, layout string) *hfxSite {
	t.Helper()
	var groups []string
	want := map[string]string{}
	for _, g := range gpGroups {
		groups = append(groups, g.name)
		d := g.hier
		if layout == "hierarchy-full" {
			d = g.full
		}
		want[g.name] = d + "/o.0." + g.name
	}
	return newHfxSiteOf(t, layout, "*:x30:e4\n", groups, want)
}

// gpSetHash changes readergrouphash of the stopped site from one layout to the other.
func gpSetHash(t *testing.T, h *hfxSite, from, to string) {
	t.Helper()
	conf := h.s.ReadFile(t, "diablo.config")
	old := "readergrouphash\t" + from + "\n"
	if !strings.Contains(conf, old) {
		t.Fatalf("diablo.config has no %q", old)
	}
	h.s.WriteFile(t, "diablo.config", strings.Replace(conf, old, "readergrouphash\t"+to+"\n", 1))
}

// gpCheckRewrite checks the -R output out for the groups: those in skipped have the D-325 line
// and no "End of rewrite", every other one has "End of rewrite" and "Rebuilt 4 data file(s)" and
// no D-325 line; no "Rewrite ", "copy failed" or "New data file" line.
func gpCheckRewrite(t *testing.T, out string, groups []string, skipped map[string]bool) {
	t.Helper()
	for _, g := range groups {
		rewritten := strings.Contains(out, "End of rewrite: "+g+"\n")
		said := gpPair[g] != "" && strings.Contains(out, gpLine(g))
		switch {
		case skipped[g] && (rewritten || !said):
			t.Errorf("%s rewritten %v, want not, with %q", g, rewritten, strings.TrimSpace(gpLine(g)))
		case !skipped[g] && (!rewritten || said):
			t.Errorf("%s: rewritten %v, D-325 line %v; want rewritten, no D-325 line", g, rewritten, said)
		case !skipped[g] && !strings.Contains(out, "Rebuilt 4 data file(s) for "+g+"\n"):
			t.Errorf("no \"Rebuilt 4 data file(s) for %s\"", g)
		}
	}
	for _, l := range strings.Split(out, "\n") {
		if strings.HasPrefix(l, "Rewrite ") || strings.HasPrefix(l, "copy failed") || strings.HasPrefix(l, "New data file") {
			t.Errorf("dexpireover -R reports: %s", l)
		}
	}
}

// TestDexpireoverGzPairDirectory: `dexpireover -R` rewrites neither group of a pair X and X.gz
// that shares one directory (D-325, gzPairShared() in util/dexpireover.c), in a subdirectory as
// D-338 did at the top, and also after a manual switch to hierarchy-full, where X.gz's old files
// stay in X's directory (go-while's R2-F1 on #524 in a subdirectory, his jobs 5300/5301/5305/5306).
//
// Input: the gpGroups, 12 articles crossposted to all, dexpire.ctl "*:x30:e4" (data files of the
// bases 0, 4, 8, 12), dreaderd stopped. Subtests:
//   - hierarchy/rewrite: `dexpireover -a -R -v3`. Expected: "<group>: not rewritten: the group
//     <other> has its files in the same directory (D-325)" for e2e.x, e2e.x.gz, o.1.2 and
//     o.1.2.gz, no "End of rewrite" for them, their data files byte-identical; e2e.test and e2e.y
//     rewritten ("End of rewrite", "Rebuilt 4 data file(s)"); nothing reported; after a restart
//     GROUP "211 12 1 12" and OVER 1-12 as before for every group. Fails on main 2b0f1ecc: the
//     pair groups are rewritten, OVER of e2e.x.gz and o.1.2.gz is empty after the restart.
//   - hierarchy-full/rewrite (control, never switched: e2e.x.gz in e2e/x/, o.1.2.gz in o/1/2/):
//     every group rewritten, no D-325 line, OVER 1-12 as before. Passes on main too.
//   - hierarchy/wild: `-a -R -v3 -w <group>` for e2e.x, o.1.2.gz and e2e.y, one run each: e2e.x
//     and o.1.2.gz are not rewritten (the other group of the pair is in the active file, though -w
//     did not load it), e2e.y is (no group e2e.y.gz: not a pair); the pair data byte-identical, OVER
//     1-12 of all as before. Fails on main: e2e.x and o.1.2.gz are rewritten; with the pair check
//     asking hierGroupKnown() instead of the active file, e2e.y is not rewritten (every name is
//     known under -w).
//   - hierarchy/switch: fed with hierarchy, readergrouphash changed to hierarchy-full (e2e.x stays
//     in e2e/, e2e.x.gz moves to e2e/x/, o.1.2 and o.1.2.gz to o/1/ and o/1/2/; their old files stay),
//     `dexpireover -a -R -v3`, readergrouphash set back, dreaderd started. Expected: the D-325 line
//     for e2e.x (e2e.x.gz has its over file in e2e/, its directory of the other layout), e2e.x not
//     rewritten, e2e.test and e2e.y rewritten, the data files of the four pair groups and the old
//     over files of the three that moved byte-identical, nothing reported but the "left alone"
//     lines of the old files; OVER 1-12 of every group as before. Fails on main: e2e.x is
//     rewritten and removes e2e/d.<n>.e2e.x.gz,
//     OVER of e2e.x.gz is empty after the switch back.
//   - hierarchy/expire: articles 1-4 made 40 days old in every over file, `dexpireover -a -v`
//     (no -R). Expected: one "expired 4 NB += 4 ... left 8" line per group; the base-0 data file
//     of every group removed with "Deleting stale overview data for", among them
//     e2e/d.0.e2e.x.gz and o.1/d.0.o.1.2.gz (the plain data of the X.gz groups and the compressed
//     names of e2e.x and o.1.2: stale for both groups, D-318); the bases 4, 8, 12 kept unchanged;
//     after a restart GROUP "211 8 5 12" and OVER 5-12 as before. Passes on main too: expiry is
//     unchanged.
func TestDexpireoverGzPairDirectory(t *testing.T) {
	for _, tc := range []struct{ layout, mode string }{
		{"hierarchy", "rewrite"},
		{"hierarchy-full", "rewrite"},
		{"hierarchy", "wild"},
		{"hierarchy", "switch"},
		{"hierarchy", "expire"},
	} {
		t.Run(tc.layout+"/"+tc.mode, func(t *testing.T) {
			h := newGpSite(t, tc.layout)
			var pairFiles []string
			for _, g := range h.groups {
				if gpPair[g] != "" {
					pairFiles = append(pairFiles, h.data(g)...)
					if tc.mode == "switch" && g != "e2e.x" {
						// the old over files of the groups that move (e2e.x stays in e2e/,
						// and its over file is its own, resized by -a as usual)
						pairFiles = append(pairFiles, h.over(g))
					}
				}
			}
			snap := hfxSnapshot(t, pairFiles)

			switch tc.mode {
			case "rewrite":
				out := h.run(t, "-a", "-R", "-v3")
				skipped := map[string]bool{}
				if tc.layout == "hierarchy" {
					for g := range gpPair {
						skipped[g] = true
					}
					h.checkKept(t, "after -R", snap)
				}
				gpCheckRewrite(t, out, h.groups, skipped)
				for _, g := range h.groups {
					for _, f := range append([]string{h.over(g)}, h.data(g)...) {
						if !hfxExists(f) {
							t.Errorf("after -R: %s is gone", h.rel(f))
						}
					}
				}
				h.checkReports(t, out, nil)
				h.checkNoDotFiles(t, "after -R")
				if t.Failed() {
					t.Logf("dexpireover -a -R -v3:\n%s", out)
				}
				restartReader(t, h.s)
				for _, g := range h.groups {
					h.checkOver(t, "after dexpireover -a -R", g, 1)
				}

			case "wild":
				for _, w := range []string{"e2e.x", "o.1.2.gz", "e2e.y"} {
					out := h.run(t, "-a", "-R", "-v3", "-w", w)
					gpCheckRewrite(t, out, []string{w}, map[string]bool{w: gpPair[w] != ""})
					h.checkReports(t, out, nil, "file of a group that -w did not select, left alone: ")
					if t.Failed() {
						t.Logf("dexpireover -a -R -v3 -w %s:\n%s", w, out)
					}
				}
				h.checkKept(t, "after -R -w", snap)
				restartReader(t, h.s)
				for _, g := range h.groups {
					h.checkOver(t, "after dexpireover -a -R -w", g, 1)
				}

			case "switch":
				gpSetHash(t, h, "hierarchy", "hierarchy-full")
				for _, g := range gpGroups {
					if rel := hierListPath(t, h.s, g.name); rel != g.full+"/o.0."+g.name {
						t.Fatalf("listgrouphashfile %s with hierarchy-full = %q", g.name, rel)
					}
				}

				out := h.run(t, "-a", "-R", "-v3")

				gpSetHash(t, h, "hierarchy-full", "hierarchy")
				if !strings.Contains(out, gpLine("e2e.x")) || strings.Contains(out, "End of rewrite: e2e.x\n") {
					t.Errorf("after the switch: e2e.x rewritten, or no %q", strings.TrimSpace(gpLine("e2e.x")))
				}
				for _, g := range []string{"e2e.test", "e2e.y"} {
					if !strings.Contains(out, "End of rewrite: "+g+"\n") {
						t.Errorf("after the switch: %s not rewritten", g)
					}
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

			case "expire":
				old := time.Now().Add(-40 * 24 * time.Hour)
				var gone, keep []string
				allowed := map[string]string{}
				for _, g := range h.groups {
					hfxAge(t, h.over(g), []int{1, 2, 3, 4}, old)
					gone = append(gone, h.dataOf(g, 0))
					allowed[h.dataOf(g, 0)] = "Deleting stale overview data for "
					keep = append(keep, h.data(g)[1:]...)
				}
				ksnap := hfxSnapshot(t, keep)

				out := h.run(t, "-a", "-v")

				h.checkGone(t, out, "Deleting stale overview data for ", gone)
				h.checkKept(t, "after dexpireover -a", ksnap)
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
			}
		})
	}
}
