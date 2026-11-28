package e2e

import (
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// The dexpireover directory scan with `readergrouphash hierarchy-full` (PR 2 of go-while's
// design notes for #516): a one-character component of a group name is a directory of its own,
// so the files of e2e.d.q and e2e.o.x are in e2e/d/ and e2e/o/ (with the names o.0.e2e.d.q and
// d.<n>.e2e.o.x, which the #513 parse must classify by the whole name), e2e.x.gz is in e2e/x/
// below the directory of e2e.x, and a.b.c.d.e is four directories deep. Each test feeds real
// articles to a running dreaderd, stops it, runs the tool and starts dreaderd again.

// hfxGroups are the groups of these tests and the directory hierarchy-full gives each of them
// below the group home.
var hfxGroups = []struct{ name, dir string }{
	{"e2e.test", "e2e"},
	{"e2e.d.q", "e2e/d"},
	{"e2e.o.x", "e2e/o"},
	{"e2e.x", "e2e"},
	{"e2e.x.gz", "e2e/x"},
	{"a.b.c", "a/b"},
	{"a.b.c.d.e", "a/b/c/d"},
	{"e2e.a.b.c.d.e.f", "e2e/a/b/c/d/e"},
	{"x.yz.w", "x/yz"},
}

// hfxArts articles are crossposted to every group; with dexpire.ctl e4 each group has the data
// files of the bases 0 (articles 1-3), 4, 8 and 12.
const hfxArts = 12

var hfxBases = []int{0, 4, 8, 12}

// hfxSite is a reader site with the hfxGroups, fed and stopped.
type hfxSite struct {
	s      *Site
	layout string
	home   string
	groups []string
	ids    []string            // the Message-IDs of articles 1..hfxArts
	dirs   map[string]string   // group -> the directory of its files
	before map[string][]string // group -> OVER 1-12 after the feed
}

// newHfxSite starts a reader with readergrouphash layout and dexpire.ctl ctl, feeds hfxArts
// articles crossposted to all hfxGroups, reads OVER 1-12 of each, stops dreaderd and checks
// that each group has its over file and the four data files in its directory (for hierarchy-full
// the one of hfxGroups).
func newHfxSite(t *testing.T, layout, ctl string) *hfxSite {
	t.Helper()
	var groups []string
	want := map[string]string{}
	for _, g := range hfxGroups {
		groups = append(groups, g.name)
		if layout == "hierarchy-full" {
			want[g.name] = g.dir + "/o.0." + g.name
		}
	}
	return newHfxSiteOf(t, layout, ctl, groups, want)
}

// newHfxSiteOf is newHfxSite for the given groups; want is the over file path below the group
// home that dgrpctl listgrouphashfile must give a group (no check for a group not in want).
func newHfxSiteOf(t *testing.T, layout, ctl string, groups []string, want map[string]string) *hfxSite {
	t.Helper()
	h := &hfxSite{layout: layout, groups: groups, dirs: map[string]string{}, before: map[string][]string{}}
	st := fakeserver.NewStore()
	for _, g := range h.groups {
		st.AddGroup(g, "hierarchy-full dexpireover test group")
	}
	spool := fakeserver.New(t, fakeserver.Options{Store: st})
	h.s = NewReaderSite(t, ReaderOptions{Spool: spool, Groups: h.groups,
		Config: map[string]string{"readergrouphash": layout},
		Files:  map[string]string{"dexpire.ctl": ctl}})
	for i := 1; i <= hfxArts; i++ {
		id := msgID(t)
		h.ids = append(h.ids, id)
		feedReader(t, h.s, st, "e2e.test", newArticle(id, strings.Join(h.groups, ","),
			fmt.Sprintf("hierarchy-full expire %02d", i), "body"))
	}
	for _, g := range h.groups {
		if h.before[g] = readOver(t, h.s, g, hfxArts); len(h.before[g]) != hfxArts {
			t.Fatalf("OVER 1-%d of %s: %q, want %d lines", hfxArts, g, h.before[g], hfxArts)
		}
	}
	h.s.Proc.Stop()

	h.home = strings.TrimSpace(h.s.Run(t, "dpath", "group"))
	for _, g := range h.groups {
		rel := hierListPath(t, h.s, g)
		if w, ok := want[g]; ok && rel != w {
			t.Fatalf("listgrouphashfile %s = %q, want %q", g, rel, w)
		}
		h.dirs[g] = filepath.Join(h.home, filepath.Dir(rel))
		for _, f := range append([]string{h.over(g)}, h.data(g)...) {
			if _, err := os.Stat(f); err != nil {
				t.Fatalf("after the feed: %v", err)
			}
		}
	}
	return h
}

// over is the over file of group g.
func (h *hfxSite) over(g string) string { return filepath.Join(h.dirs[g], "o.0."+g) }

// data are the data files of g, of the hfxBases.
func (h *hfxSite) data(g string) []string {
	var out []string
	for _, b := range hfxBases {
		out = append(out, h.dataOf(g, b))
	}
	return out
}

// dataOf is the plain data file of g with the article base b.
func (h *hfxSite) dataOf(g string, b int) string {
	return filepath.Join(h.dirs[g], fmt.Sprintf("d.%d.%s", b, g))
}

// rel is p below the group home.
func (h *hfxSite) rel(p string) string { return strings.TrimPrefix(p, h.home+"/") }

// run runs dexpireover with args; it must exit 0.
func (h *hfxSite) run(t *testing.T, args ...string) string {
	t.Helper()
	out, err := h.s.TryRun("dexpireover", args...)
	if err != nil {
		t.Errorf("dexpireover %s: %v", strings.Join(args, " "), err)
	}
	return out
}

// hfxExists: path is there (lstat: a symlink counts, its target is not looked at).
func hfxExists(path string) bool {
	_, err := os.Lstat(path)
	return err == nil
}

// hfxSnapshot reads the given files.
func hfxSnapshot(t *testing.T, files []string) map[string][]byte {
	t.Helper()
	m := map[string][]byte{}
	for _, f := range files {
		b, err := os.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		m[f] = b
	}
	return m
}

// checkKept reports each file of snap that is gone or changed.
func (h *hfxSite) checkKept(t *testing.T, when string, snap map[string][]byte) {
	t.Helper()
	for f, want := range snap {
		got, err := os.ReadFile(f)
		if err != nil {
			t.Errorf("%s: %s is gone: %v", when, h.rel(f), err)
		} else if !bytes.Equal(got, want) {
			t.Errorf("%s: %s was changed", when, h.rel(f))
		}
	}
}

// hfxBad are the parts of a dexpireover line that report a problem.
var hfxBad = []string{"bad file header", "stale overview data", "not found, removing", "nknown",
	"Unable", "failed", "lost", "Error", "entry removed", "unable to rewrite", "Removing old temp file",
	"left alone", "not compressed:", "Bad zlen", "inflate error"}

// checkReports fails for every line of out that reports a problem, except the lines that end
// in one of the paths of allowed (as said by prefix: the expected removals) and the lines that
// contain one of skip.
func (h *hfxSite) checkReports(t *testing.T, out string, allowed map[string]string, skip ...string) {
	t.Helper()
	for _, l := range strings.Split(out, "\n") {
		bad := false
		for _, b := range hfxBad {
			bad = bad || strings.Contains(l, b)
		}
		if !bad {
			continue
		}
		ok := false
		for p, prefix := range allowed {
			if strings.HasPrefix(l, prefix) && (strings.HasSuffix(l, p) || strings.Contains(l, " "+p+" (artBase=")) {
				ok = true
			}
		}
		for _, s := range skip {
			ok = ok || strings.Contains(l, s)
		}
		if !ok {
			t.Errorf("dexpireover reports: %s", l)
		}
	}
}

// checkGone reports each of files that is still there or whose removal out does not report
// with prefix ("Deleting stale overview data for " or "Group for over file not found, removing ").
func (h *hfxSite) checkGone(t *testing.T, out, prefix string, files []string) {
	t.Helper()
	for _, f := range files {
		if _, err := os.Lstat(f); err == nil {
			t.Errorf("%s is still there", h.rel(f))
		}
		said := false
		for _, l := range strings.Split(out, "\n") {
			if strings.HasPrefix(l, prefix) && (strings.HasSuffix(l, f) || strings.Contains(l, " "+f+" (artBase=")) {
				said = true
			}
		}
		if !said {
			t.Errorf("dexpireover did not report the removal of %s (%q)", h.rel(f), prefix)
		}
	}
}

// checkNoDotFiles reports a name starting with '.' in the directory of any group (a temporary
// file of -R or -z left behind).
func (h *hfxSite) checkNoDotFiles(t *testing.T, when string) {
	t.Helper()
	seen := map[string]bool{}
	for _, g := range h.groups {
		d := h.dirs[g]
		if seen[d] {
			continue
		}
		seen[d] = true
		ents, err := os.ReadDir(d)
		if err != nil {
			t.Errorf("%s: directory of %s: %v", when, g, err)
			continue
		}
		for _, e := range ents {
			if strings.HasPrefix(e.Name(), ".") {
				t.Errorf("%s: %s is left in %s", when, e.Name(), h.rel(d))
			}
		}
	}
}

// overAfter starts dreaderd again and returns the reply to GROUP g and OVER first-last of it
// (nil when GROUP or OVER does not succeed).
func (h *hfxSite) overAfter(t *testing.T, g string, first, last int) (string, []string) {
	t.Helper()
	c := h.s.Reader(t)
	defer c.Close()
	_, line := c.Cmd("GROUP %s", g)
	if !strings.HasPrefix(line, "211 ") {
		return line, nil
	}
	if code, _ := c.Cmd("OVER %d-%d", first, last); code != 224 {
		return line, nil
	}
	return line, c.ReadDotLines()
}

// checkOver checks GROUP g (211 with the range first-12) and that OVER first-12 gives the
// lines of articles first..12 from before.
func (h *hfxSite) checkOver(t *testing.T, when, g string, first int) {
	t.Helper()
	want := fmt.Sprintf("211 %d %d %d ", hfxArts-first+1, first, hfxArts)
	line, over := h.overAfter(t, g, first, hfxArts)
	if !strings.HasPrefix(line, want) {
		t.Errorf("%s: GROUP %s: %q, want %q", when, g, line, want)
		return
	}
	if strings.Join(over, "\n") != strings.Join(h.before[g][first-1:], "\n") {
		t.Errorf("%s: OVER %d-%d of %s differs:\nbefore %q\nafter  %q", when, first, hfxArts, g, h.before[g][first-1:], over)
	}
}

// hfxAge sets the receive time of the articles arts in the over file path to rcvd.
func hfxAge(t *testing.T, path string, arts []int, rcvd time.Time) {
	t.Helper()
	b, err := os.ReadFile(path)
	if err != nil || len(b) < 16 {
		t.Fatalf("over file %s: %v (%d bytes)", path, err, len(b))
	}
	o := binary.NativeEndian
	head := int(o.Uint32(b[8:]))
	slots := (len(b) - head) / 32 // sizeof(OverArt), dreaderd/defs.h
	if head <= 0 || slots <= 0 {
		t.Fatalf("over file %s: header %d, %d slots", path, head, slots)
	}
	for _, a := range arts {
		p := head + (a%slots)*32
		if n := int32(o.Uint32(b[p:])); n != int32(a) {
			t.Fatalf("over file %s: slot of article %d holds %d", path, a, n)
		}
		o.PutUint32(b[p+24:], uint32(rcvd.Unix())) // oa_TimeRcvd
	}
	writeOver(t, path, b)
}

// TestDexpireoverHierarchyFullExpire: `dexpireover -a -v` walks the nested directories of
// hierarchy-full, takes each over file for an over file and each data file for a data file
// (also o.0.e2e.d.q in e2e/d/ and d.<n>.e2e.o.x in e2e/o/, #513), and reaches the groups four
// and six directories deep (a/b/c/d/, e2e/a/b/c/d/e/; scanDirectory()'s level is never
// incremented, so the depth has no limit).
//
// Input: the hfxGroups (readergrouphash hierarchy-full, and plain hierarchy as the control), 12
// articles crossposted to all of them, dexpire.ctl "*:x30:e4"; dreaderd stopped, the receive time
// of the articles 1-4 set to 40 days ago in each over file; `dexpireover -a -v`. Expected: for
// every group "expired 4 NB += 4 ... left 8 ... grp=<group>"; its data file of the base 0
// (articles 1-3, now below the range) removed with "Deleting stale overview data for <group>";
// the over file and the data files of the bases 4, 8 and 12 there, the data files unchanged;
// nothing else reported (not found, bad, stale, unknown, failed). After a restart GROUP gives
// 211 8 5 12 and OVER 5-12 the lines of before. With hierarchy-full the file e2e/x/d.0.e2e.x.gz
// (the plain data of e2e.x.gz) also reads as the compressed data of e2e.x (D-318), but it is not
// in e2e.x's directory e2e/, so it is judged by e2e.x.gz alone (D-336) and removed by the first
// run, whatever order readdir() gives the entries of e2e/ (the subdirectory x and the over file
// o.0.e2e.x). Before the fix the directory was not compared and the file was kept while it was
// in the range of either group: kept by the first run when the scan reached e2e/x/ before
// e2e.x's over file was expired, removed when the over file came first. That order depends on the
// file system of the test's temporary directory (go-while's #522 review: xfs or ext4): kept on
// the Debian test VM (/tmp is tmpfs there), removed on AlmaLinux 9 and 10; this test accepted
// both. Now it must be removed by the first run.
func TestDexpireoverHierarchyFullExpire(t *testing.T) {
	for _, layout := range []string{"hierarchy-full", "hierarchy"} {
		t.Run(layout, func(t *testing.T) {
			h := newHfxSite(t, layout, "*:x30:e4\n")
			old := time.Now().Add(-40 * 24 * time.Hour)
			var gone []string
			var keep []string
			for _, g := range h.groups {
				hfxAge(t, h.over(g), []int{1, 2, 3, 4}, old)
				gone = append(gone, h.dataOf(g, 0))
				keep = append(keep, h.data(g)[1:]...)
			}
			snap := hfxSnapshot(t, keep)
			allowed := map[string]string{}
			for _, f := range gone {
				allowed[f] = "Deleting stale overview data for "
			}

			out := h.run(t, "-a", "-v")

			// with hierarchy-full also e2e/x/d.0.e2e.x.gz, by the first run (D-336)
			h.checkGone(t, out, "Deleting stale overview data for ", gone)
			h.checkKept(t, "after dexpireover -a", snap)
			h.checkReports(t, out, allowed)
			for _, g := range h.groups {
				re := regexp.MustCompile(`(?m)^expired 4 +NB \+= 4 +\( *0 can, +0 stale, +4 exprd\) left 8 .* grp=` + regexp.QuoteMeta(g) + `$`)
				if !re.MatchString(out) {
					t.Errorf("no \"expired 4 NB += 4 ... left 8\" line for %s", g)
				}
				if !hfxExists(h.over(g)) {
					t.Errorf("the over file %s is gone", h.rel(h.over(g)))
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

// TestDexpireoverHierarchyFullRewrite: `dexpireover -a -R` rewrites the files of every group in
// the nested directories. RewriteData() reads each old data file plain, else as "<name>.gz",
// writes the new plain one, and removes both "d.<n>.<group>" and "d.<n>.<group>.gz" in the
// group's directory before it renames the new one into place. With hierarchy-full the data
// files of e2e.x.gz are in e2e/x/, so the removals for e2e.x in e2e/ never reach them; with
// plain hierarchy both groups have their files in e2e/, and "d.<n>.e2e.x.gz" is e2e.x's
// compressed name and e2e.x.gz's plain data file at once (D-325), so -R rewrites neither.
//
// Input: the hfxGroups, 12 articles crossposted to all, dexpire.ctl "*:x30:e4" (and
// "*:x30:e4:F1:Z1" for compressed), dreaderd stopped. Cases:
//   - hierarchy-full: `dexpireover -a -R -v3` on plain data only (the .gz removal finds
//     nothing);
//   - hierarchy-full_compressed: the data files made two hours old and `dexpireover -a -z`
//     first ("Compressed 16 files": d.0 and d.4 of every group but e2e.x.gz, whose names end in
//     ".gz", become d.0.<group>.gz and d.4.<group>.gz; e2e.x's in e2e/), then `-a -R -v3`;
//   - hierarchy: the control, plain data, `-a -R -v3`.
//
// Expected with hierarchy-full: "End of rewrite: <group>" and "Rebuilt 4 data file(s) for
// <group>" for every group, nothing reported as failed, lost or removed, no '.' file left in any
// group's directory, the four plain data files of every group there; in compressed the old gzip
// files d.0.<group>.gz and d.4.<group>.gz are gone (rewritten to the plain d.0 and d.4: a -R that
// left them would fail here), among them e2e/d.0.e2e.x.gz and e2e/d.4.e2e.x.gz of e2e.x; the
// four data files of e2e.x.gz in e2e/x/ hold the Message-IDs of their articles; after a restart
// OVER 1-12 of every group as before. Control, plain hierarchy: the same for the groups other
// than e2e.x and e2e.x.gz; those two are not rewritten (D-325, gzPairShared()): "e2e.x: not
// rewritten: the group e2e.x.gz has its files in the same directory (D-325)" and the same for
// e2e.x.gz naming e2e.x, no "End of rewrite" for either, their data files byte-identical, and
// after a restart OVER 1-12 of both as before. Fails on main 2b0f1ecc, where this was logged
// only: e2e.x is rewritten and removes e2e/d.<n>.e2e.x.gz, so OVER of e2e.x.gz is empty after the
// restart.
func TestDexpireoverHierarchyFullRewrite(t *testing.T) {
	for _, tc := range []struct {
		name, layout string
		compressed   bool
	}{
		{"hierarchy-full", "hierarchy-full", false},
		{"hierarchy-full_compressed", "hierarchy-full", true},
		{"hierarchy", "hierarchy", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			layout := tc.layout
			ctl := "*:x30:e4\n"
			if tc.compressed {
				skipWithoutZlib(t)
				ctl = "*:x30:e4:F1:Z1\n"
			}
			h := newHfxSite(t, layout, ctl)
			// D-325: with plain hierarchy e2e.x and e2e.x.gz share e2e/, and -R rewrites neither
			pair := func(g string) bool { return layout == "hierarchy" && (g == "e2e.x" || g == "e2e.x.gz") }
			var pairData []string
			for _, g := range h.groups {
				if pair(g) {
					pairData = append(pairData, h.data(g)...)
				}
			}
			pairSnap := hfxSnapshot(t, pairData)
			var gzOld []string // the gzip files -z made, which -R must replace by plain ones
			if tc.compressed {
				old := time.Now().Add(-2 * time.Hour)
				for _, g := range h.groups {
					for _, f := range h.data(g) {
						if err := os.Chtimes(f, old, old); err != nil {
							t.Fatal(err)
						}
					}
				}
				zout := h.run(t, "-a", "-z", "-v")
				if !strings.Contains(zout, "Compressed 16 files\n") {
					t.Errorf("dexpireover -a -z: no \"Compressed 16 files\"")
				}
				for _, g := range h.groups {
					if g == "e2e.x.gz" {
						continue
					}
					for _, b := range []int{0, 4} {
						f := h.dataOf(g, b)
						if hfxExists(f) || !hfxExists(f+".gz") {
							t.Fatalf("after -z: %s there %v, %s.gz there %v, want only the .gz:\n%s",
								h.rel(f), hfxExists(f), h.rel(f), hfxExists(f+".gz"), zout)
						}
						gzOld = append(gzOld, f+".gz")
					}
				}
				h.checkReports(t, zout, nil)
			}

			out := h.run(t, "-a", "-R", "-v3")

			for _, g := range h.groups {
				other := map[string]string{"e2e.x": "e2e.x.gz", "e2e.x.gz": "e2e.x"}[g]
				d325 := g + ": not rewritten: the group " + other + " has its files in the same directory (D-325)\n"
				if pair(g) {
					if strings.Contains(out, "End of rewrite: "+g+"\n") || !strings.Contains(out, d325) {
						t.Errorf("hierarchy: %s rewritten, or no %q", g, strings.TrimSpace(d325))
					}
					continue
				}
				if other != "" && strings.Contains(out, d325) {
					t.Errorf("%s: %q", layout, strings.TrimSpace(d325))
				}
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
			for _, f := range gzOld {
				if hfxExists(f) {
					t.Errorf("after -R: the old compressed data file %s is still there", h.rel(f))
				}
			}
			h.checkKept(t, "after -R", pairSnap)
			for _, l := range strings.Split(out, "\n") {
				if strings.HasPrefix(l, "Rewrite ") || strings.HasPrefix(l, "copy failed") || strings.HasPrefix(l, "New data file") {
					t.Errorf("dexpireover -R reports: %s", l)
				}
			}
			h.checkReports(t, out, nil)
			h.checkNoDotFiles(t, "after -R")
			if layout == "hierarchy-full" {
				// the data of e2e.x.gz in e2e/x/ after the rewrite of e2e.x in e2e/
				for _, b := range hfxBases {
					d, err := os.ReadFile(h.dataOf("e2e.x.gz", b))
					if err != nil {
						t.Errorf("after -R: %v", err)
						continue
					}
					for a := max(b, 1); a < b+4 && a <= hfxArts; a++ {
						if !bytes.Contains(d, []byte(h.ids[a-1])) {
							t.Errorf("after -R: %s does not hold article %d", h.rel(h.dataOf("e2e.x.gz", b)), a)
						}
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

// TestDexpireoverHierarchyFullCompress: `dexpireover -a -z` compresses data files in the nested
// one-character directories of hierarchy-full, each in its own directory.
//
// Input: the hfxGroups (hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4:F1:Z1", dreaderd stopped, every data file made two hours old; `dexpireover -a -z -v`.
// Expected: "Compressed 16 files": of each group but e2e.x.gz, d.0 and d.4 are replaced by gzip
// files d.0.<group>.gz and d.4.<group>.gz in the group's directory with the same bytes inside (so
// e2e.x's go to e2e/, beside e2e/x/), d.8 and d.12 stay plain and unchanged ("proximity to end
// of data", F1). The data files of e2e.x.gz (in e2e/x/) stay plain and unchanged and no
// "d.<n>.e2e.x.gz.gz" is made: dexpireover never compresses a file whose name ends in ".gz"
// (ProcessOverviewFile(); with plain hierarchy that name could also be the plain data of a group
// e2e.x.gz.gz), so a group whose name ends in ".gz" keeps its data uncompressed (pinned here).
// No "not compressed", "Unable to compress" or failure line, no '.' file left, and after a
// restart OVER 1-12 of every group as before. Skipped without zlib.
func TestDexpireoverHierarchyFullCompress(t *testing.T) {
	skipWithoutZlib(t)
	h := newHfxSite(t, "hierarchy-full", "*:x30:e4:F1:Z1\n")
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

	out := h.run(t, "-a", "-z", "-v")

	if !strings.Contains(out, "Compressed 16 files\n") {
		t.Errorf("dexpireover -a -z: no \"Compressed 16 files\"")
	}
	plain := map[string][]byte{}
	for _, g := range h.groups {
		for _, b := range hfxBases {
			f := h.dataOf(g, b)
			if g == "e2e.x.gz" || b >= 8 {
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
	h.checkReports(t, out, nil)
	h.checkNoDotFiles(t, "after -z")
	if t.Failed() {
		t.Logf("dexpireover -a -z -v:\n%s", out)
	}

	restartReader(t, h.s)
	for _, g := range h.groups {
		h.checkOver(t, "after dexpireover -a -z", g, 1)
	}
}

// TestDexpireoverHierarchyFullGzPairCompressed: go-while's R2-F1 on ce927151 (#523 r2): with
// hierarchy-full, never switched, the compressed data of e2e.x is "e2e/d.<n>.e2e.x.gz", which
// is also the path of the plain data of e2e.x.gz in plain hierarchy (e2e/ is its directory
// there). With no over file of e2e.x.gz in e2e/ no dreaderd writes that file for e2e.x.gz (one
// makes the over file first), so it is judged by both readings (D-318) and not left alone; NB
// of e2e.x.gz is moved as usual (it is held only for a group with an over file in its directory
// of the other layout). The file is scanned in the run after the one that made it, so the order
// readdir() gives does not matter (illumos visited it in the same run, Linux does not).
//
// Input: the hfxGroups (hierarchy-full), 12 articles crossposted to all, dexpire.ctl
// "*:x30:e4:F1:Z1", dreaderd stopped, the data files two hours old; `dexpireover -a -z -v`
// ("Compressed 16 files": d.0 and d.4 of every group but e2e.x.gz, e2e.x's in e2e/), then
// `dexpireover -a -v` (it now scans e2e/d.0.e2e.x.gz and e2e/d.4.e2e.x.gz): nothing reported in
// either; then articles 1-8 made 40 days old in every over file and `dexpireover -a -v`.
// Expected: the base 0 and 4 data files of every group removed with "Deleting stale overview
// data for" (for e2e.x the gzip files in e2e/, for e2e.x.gz the plain ones in e2e/x/), the base
// 8 and 12 ones kept; after a restart GROUP of every group "211 4 9 12" and OVER 9-12 as before.
// Fails on ce927151 (r2): "left alone" for the e2e.x gzip files, which stay, and GROUP e2e.x.gz
// "211 12 1 12". On main c27f812d it fails on tmpfs: e2e/x/d.0.e2e.x.gz stays, judged also as
// e2e.x's before e2e.x is expired (the readdir order, as in TestDexpireoverHierarchyFullExpire).
// Skipped without zlib.
func TestDexpireoverHierarchyFullGzPairCompressed(t *testing.T) {
	skipWithoutZlib(t)
	h := newHfxSite(t, "hierarchy-full", "*:x30:e4:F1:Z1\n")
	two := time.Now().Add(-2 * time.Hour)
	for _, g := range h.groups {
		for _, f := range h.data(g) {
			if err := os.Chtimes(f, two, two); err != nil {
				t.Fatal(err)
			}
		}
	}
	out := h.run(t, "-a", "-z", "-v")
	if !strings.Contains(out, "Compressed 16 files\n") {
		t.Errorf("dexpireover -a -z: no \"Compressed 16 files\"")
	}
	h.checkReports(t, out, nil)
	for _, b := range []int{0, 4} {
		if f := h.dataOf("e2e.x", b) + ".gz"; !hfxExists(f) {
			t.Fatalf("%s was not made", h.rel(f))
		}
	}
	again := h.run(t, "-a", "-v")
	h.checkReports(t, again, nil)
	if t.Failed() {
		t.Logf("dexpireover -a -z -v:\n%s\ndexpireover -a -v:\n%s", out, again)
	}

	old := time.Now().Add(-40 * 24 * time.Hour)
	var gone, keep []string
	allowed := map[string]string{}
	for _, g := range h.groups {
		hfxAge(t, h.over(g), []int{1, 2, 3, 4, 5, 6, 7, 8}, old)
		for _, b := range hfxBases {
			f := h.dataOf(g, b)
			if b < 8 && g != "e2e.x.gz" {
				f += ".gz"
			}
			if b < 8 {
				gone = append(gone, f)
				allowed[f] = "Deleting stale overview data for "
			} else {
				keep = append(keep, f)
			}
		}
	}
	snap := hfxSnapshot(t, keep)
	exp := h.run(t, "-a", "-v")
	h.checkGone(t, exp, "Deleting stale overview data for ", gone)
	h.checkKept(t, "after the expiry", snap)
	h.checkReports(t, exp, allowed)
	if t.Failed() {
		t.Logf("dexpireover -a -v after the expiry:\n%s", exp)
	}

	restartReader(t, h.s)
	for _, g := range h.groups {
		h.checkOver(t, "after the expiry", g, 9)
	}
}

// TestDexpireoverHierarchyFullRemovedGroup: `dexpireover -a` removes the files of a group that
// is no longer in the active file from the nested directories of hierarchy-full, and leaves
// those of the other groups, also in the same, the parent and the child directories.
//
// Input: the hfxGroups (hierarchy-full), 12 articles crossposted to all, dexpire.ctl "*:x30:e4",
// dreaderd stopped, then `dgrpctl rmgroup`:
//   - one_char_dirs: a.b.c (a/b/, above a/b/c/d/ of a.b.c.d.e), e2e.x (e2e/, beside e2e.test,
//     above e2e/x/ of e2e.x.gz) and x.yz.w (the only group in x/yz/);
//   - gz_group: e2e.x.gz (e2e/x/), while e2e.x stays.
//
// `dexpireover -a -v`. Expected: the over file and the four data files of each removed group are
// removed, each with "Group for over file not found, removing <path>"; every file of the other
// groups is there and the data files are unchanged, nothing else is reported; after a restart
// GROUP of a removed group gives 411 and OVER 1-12 of every other group is as before. The
// directories are not removed (dexpireover has no rmdir: x/yz/ stays, empty; logged). In
// gz_group the data files e2e/x/d.<n>.e2e.x.gz also read as the compressed data of e2e.x, which
// exists and has the articles 1-12, but e2e/x/ is not e2e.x's directory (e2e/), so they are not
// e2e.x's and are removed with the over file (D-336; before the fix the directory was not
// compared and they were kept while e2e.x had articles in their bases); e2e.x's files in e2e/
// are kept unchanged.
func TestDexpireoverHierarchyFullRemovedGroup(t *testing.T) {
	for _, tc := range []struct {
		name    string
		removed []string
	}{
		{"one_char_dirs", []string{"a.b.c", "e2e.x", "x.yz.w"}},
		{"gz_group", []string{"e2e.x.gz"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			h := newHfxSite(t, "hierarchy-full", "*:x30:e4\n")
			removed := map[string]bool{}
			var gone, keepFiles []string
			for _, g := range tc.removed {
				removed[g] = true
				h.s.Run(t, "dgrpctl", "rmgroup", g)
			}
			for _, g := range h.groups {
				if removed[g] {
					gone = append(gone, h.over(g))
					gone = append(gone, h.data(g)...)
					continue
				}
				keepFiles = append(keepFiles, h.data(g)...)
			}
			snap := hfxSnapshot(t, keepFiles)
			allowed := map[string]string{}
			for _, f := range gone {
				allowed[f] = "Group for over file not found, removing "
			}

			out := h.run(t, "-a", "-v")

			h.checkGone(t, out, "Group for over file not found, removing ", gone)
			h.checkKept(t, "after dexpireover -a", snap)
			h.checkReports(t, out, allowed)
			for _, g := range h.groups {
				if !removed[g] && !hfxExists(h.over(g)) {
					t.Errorf("the over file %s is gone", h.rel(h.over(g)))
				}
			}
			for _, g := range tc.removed {
				d := h.dirs[g]
				ents, err := os.ReadDir(d)
				var names []string
				for _, e := range ents {
					names = append(names, e.Name())
				}
				t.Logf("the directory %s of %s after dexpireover -a: %v, entries %q", h.rel(d), g, err, names)
			}
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}

			restartReader(t, h.s)
			for _, g := range h.groups {
				if removed[g] {
					if line, _ := h.overAfter(t, g, 1, hfxArts); !strings.HasPrefix(line, "411 ") {
						t.Errorf("GROUP %s after rmgroup: %q, want 411", g, line)
					}
					continue
				}
				h.checkOver(t, "after dexpireover -a", g, 1)
			}
		})
	}
}
