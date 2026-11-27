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
	h := &hfxSite{layout: layout, dirs: map[string]string{}, before: map[string][]string{}}
	for _, g := range hfxGroups {
		h.groups = append(h.groups, g.name)
	}
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
	for _, g := range hfxGroups {
		rel := hierListPath(t, h.s, g.name)
		if layout == "hierarchy-full" && rel != g.dir+"/o.0."+g.name {
			t.Fatalf("listgrouphashfile %s = %q, want %q", g.name, rel, g.dir+"/o.0."+g.name)
		}
		h.dirs[g.name] = filepath.Join(h.home, filepath.Dir(rel))
		for _, f := range append([]string{h.over(g.name)}, h.data(g.name)...) {
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
// (the plain data of e2e.x.gz) also reads as the compressed data of e2e.x, whose directory is
// e2e/ (D-318 keeps it while it is in the range of either; the directory is not compared,
// D-336): when the scan reaches e2e/x/ before e2e.x's over file in e2e/ is expired, the file is
// kept for that run and a second `dexpireover -a -v` removes it; the test allows both and logs
// which happened.
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

			late := ""
			if layout == "hierarchy-full" {
				// see the comment of the test: kept by the reading of e2e.x in this run
				if f := h.dataOf("e2e.x.gz", 0); hfxExists(f) {
					late = f
					t.Logf("%s was kept by the first run (the scan reached e2e/x/ before e2e.x was expired)", h.rel(f))
				}
			}
			var goneNow []string
			for _, f := range gone {
				if f != late {
					goneNow = append(goneNow, f)
				}
			}
			h.checkGone(t, out, "Deleting stale overview data for ", goneNow)
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
			if late != "" {
				out2 := h.run(t, "-a", "-v")
				h.checkGone(t, out2, "Deleting stale overview data for ", []string{late})
				h.checkKept(t, "after the second dexpireover -a", snap)
				h.checkReports(t, out2, map[string]string{late: "Deleting stale overview data for "})
				if t.Failed() {
					t.Logf("second dexpireover -a -v:\n%s", out2)
				}
			}

			restartReader(t, h.s)
			for _, g := range h.groups {
				h.checkOver(t, "after dexpireover -a", g, 5)
			}
		})
	}
}

// TestDexpireoverHierarchyFullRewrite: `dexpireover -a -R` rewrites the files of every group in
// the nested directories, and with hierarchy-full the pair e2e.x / e2e.x.gz keeps its data: the
// data files of e2e.x.gz are in e2e/x/, so the rewrite of e2e.x, which removes "d.<n>.e2e.x" and
// "d.<n>.e2e.x.gz" (its compressed name) in e2e/, does not touch them. With plain hierarchy both
// groups have their files in e2e/, and "d.<n>.e2e.x.gz" is the plain data of e2e.x.gz (D-325).
//
// Input: the hfxGroups, 12 articles crossposted to all, dexpire.ctl "*:x30:e4", dreaderd
// stopped; `dexpireover -a -R -v3`. Expected with hierarchy-full: "End of rewrite: <group>" and
// "Rebuilt 4 data file(s) for <group>" for every group, nothing reported as failed, lost or
// removed, no '.' file left in any group's directory, the four data files of every group there
// (those of e2e.x.gz in e2e/x/ hold the Message-IDs of their articles), and after a restart OVER
// 1-12 of every group as before. Control, plain hierarchy: the same for the groups other than
// e2e.x and e2e.x.gz; for those two the result is logged, not checked (D-325: one of them loses
// its overview data).
func TestDexpireoverHierarchyFullRewrite(t *testing.T) {
	for _, layout := range []string{"hierarchy-full", "hierarchy"} {
		t.Run(layout, func(t *testing.T) {
			h := newHfxSite(t, layout, "*:x30:e4\n")
			pair := func(g string) bool { return layout == "hierarchy" && (g == "e2e.x" || g == "e2e.x.gz") }
			var skip []string
			if layout == "hierarchy" {
				skip = []string{" e2e.x", ":e2e.x", "/d.0.e2e.x", "/d.4.e2e.x", "/d.8.e2e.x", "/d.12.e2e.x"}
			}

			out := h.run(t, "-a", "-R", "-v3")

			for _, g := range h.groups {
				if pair(g) {
					continue
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
			for _, l := range strings.Split(out, "\n") {
				if strings.HasPrefix(l, "Rewrite ") || strings.HasPrefix(l, "copy failed") || strings.HasPrefix(l, "New data file") {
					ok := false
					for _, s := range skip {
						ok = ok || strings.Contains(l, s)
					}
					if !ok {
						t.Errorf("dexpireover -R reports: %s", l)
					}
				}
			}
			h.checkReports(t, out, nil, skip...)
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
				if !pair(g) {
					h.checkOver(t, "after dexpireover -a -R", g, 1)
					continue
				}
				line, over := h.overAfter(t, g, 1, hfxArts)
				var files []string
				for _, f := range h.data(g) {
					if hfxExists(f) {
						files = append(files, filepath.Base(f))
					}
				}
				t.Logf("hierarchy (D-325): after -R GROUP %s: %q, OVER 1-12: %d of 12 lines as before (%d lines); its data file names in e2e/: %q",
					g, line, hfxSameLines(over, h.before[g]), len(over), files)
			}
			if layout == "hierarchy" {
				for _, l := range strings.Split(out, "\n") {
					if strings.Contains(l, "e2e.x") && !strings.HasPrefix(l, "Rewriting: ") && !strings.HasPrefix(l, "DT ") &&
						!strings.HasPrefix(l, "resize ") && !strings.HasPrefix(l, "test ") {
						t.Logf("hierarchy (D-325): %s", l)
					}
				}
			}
		})
	}
}

// hfxSameLines is the number of lines of got that are in want.
func hfxSameLines(got, want []string) int {
	n := 0
	for _, l := range got {
		for _, w := range want {
			if l == w {
				n++
				break
			}
		}
	}
	return n
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
// directories are not removed (dexpireover has no rmdir: x/yz/ stays, empty; logged). Not
// checked, logged: in gz_group the data files e2e/x/d.<n>.e2e.x.gz also read as the compressed
// data of e2e.x, which exists and has the articles 1-12, so they are kept (the directory e2e/x/
// is not that of e2e.x, but it is not compared: D-336); only the over file goes.
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
			var gzData []string // gz_group: the data files of e2e.x.gz, see the comment of the test
			for _, g := range h.groups {
				if removed[g] {
					gone = append(gone, h.over(g))
					if g == "e2e.x.gz" {
						gzData = h.data(g)
					} else {
						gone = append(gone, h.data(g)...)
					}
					continue
				}
				keepFiles = append(keepFiles, h.data(g)...)
			}
			snap := hfxSnapshot(t, keepFiles)
			allowed := map[string]string{}
			for _, f := range append(append([]string{}, gone...), gzData...) {
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
			for _, f := range gzData {
				t.Logf("D-336: %s after dexpireover -a: there %v", h.rel(f), hfxExists(f))
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
