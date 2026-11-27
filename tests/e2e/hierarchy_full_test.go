package e2e

import (
	"bytes"
	"fmt"
	"maps"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/fakeserver"
)

// D-325: `readergrouphash hierarchy-full` is the hierarchy layout with a directory for every
// component of the group name but the last (only empty components are skipped), so a group
// and the same name plus .gz never share a data file. `hierarchy` stays as it was.

// hierListPath is the over file path of group below the group home, as
// `dgrpctl listgrouphashfile` prints it.
func hierListPath(t *testing.T, s *Site, group string) string {
	t.Helper()
	f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", group))
	if len(f) != 2 || f[1] != group {
		t.Fatalf("dgrpctl listgrouphashfile %s: %q", group, f)
	}
	return f[0]
}

// TestHierarchyFullGroupPaths: `dgrpctl listgrouphashfile` with `readergrouphash hierarchy`
// and `hierarchy-full`. Input: group names with components of one character, an empty
// component and a single component. Expected: hierarchy gives the paths of a79ae323 (a component of
// one character is joined to the next one: e2e.x.gz in e2e/, a.b.c in a.b/); hierarchy-full
// makes a directory of every component but the last (e2e.x.gz in e2e/x/, a.b.c in a/b/), skips
// an empty one, and keeps the file names.
func TestHierarchyFullGroupPaths(t *testing.T) {
	cases := []struct{ group, hier, full string }{
		{"e2e.test", "e2e/o.0.e2e.test", "e2e/o.0.e2e.test"},
		{"e2e.x", "e2e/o.0.e2e.x", "e2e/o.0.e2e.x"},
		{"e2e.x.gz", "e2e/o.0.e2e.x.gz", "e2e/x/o.0.e2e.x.gz"},
		{"e2e.foo.gz", "e2e/foo/o.0.e2e.foo.gz", "e2e/foo/o.0.e2e.foo.gz"},
		{"a.b.c", "a.b/o.0.a.b.c", "a/b/o.0.a.b.c"},
		{"x.y", "o.0.x.y", "x/o.0.x.y"},
		{"e2e.a.b.c.d", "e2e/a.b/o.0.e2e.a.b.c.d", "e2e/a/b/c/o.0.e2e.a.b.c.d"},
		{"e2e..x", "e2e/o.0.e2e..x", "e2e/o.0.e2e..x"},
		{"x", "o.0.x", "o.0.x"},
	}
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		t.Run(layout, func(t *testing.T) {
			s, _ := overSite(t, map[string]string{"readergrouphash": layout})
			for _, c := range cases {
				want := c.hier
				if layout == "hierarchy-full" {
					want = c.full
				}
				if got := hierListPath(t, s, c.group); got != want {
					t.Errorf("%s: listgrouphashfile %s = %q, want %q", layout, c.group, got, want)
				}
			}
		})
	}
}

// TestHierarchyFullLongGroupName: the path length checks of GFName() and FindGroupDir() with
// both hierarchy layouts. Input: names e2e.x.<k times b>.c, with k found by bisection so that
// the path just fits (k) and just does not (k+1), and a name with a 9000-character component.
// Expected: the longest path that fits is printed in full (the directory of the layout, the
// file name with the first 1023 characters of the name); one character more and dgrpctl exits
// non-zero with "GFName: generated pathname too long"; a directory longer than the buffer gives
// "FindGroupDir: pathname too long". Nothing is cut short or printed wrong.
func TestHierarchyFullLongGroupName(t *testing.T) {
	name := func(k int) string { return "e2e.x." + strings.Repeat("b", k) + ".c" }
	for _, layout := range []string{"hierarchy", "hierarchy-full"} {
		t.Run(layout, func(t *testing.T) {
			s, _ := overSite(t, map[string]string{"readergrouphash": layout})
			want := func(k int) string {
				dir := "e2e/x." + strings.Repeat("b", k) + "/"
				if layout == "hierarchy-full" {
					dir = "e2e/x/" + strings.Repeat("b", k) + "/"
				}
				return dir + "o.0." + name(k)[:min(len(name(k)), 1023)]
			}
			fits := func(k int) (string, bool) {
				out, err := s.TryRun("dgrpctl", "listgrouphashfile", name(k))
				return out, err == nil
			}
			lo, hi := 1, 9000
			if out, ok := fits(lo); !ok {
				t.Fatalf("k=%d does not fit:\n%.300s", lo, out)
			}
			if out, ok := fits(hi); ok {
				t.Fatalf("k=%d fits:\n%.300s", hi, out)
			}
			for hi-lo > 1 {
				mid := (lo + hi) / 2
				if _, ok := fits(mid); ok {
					lo = mid
				} else {
					hi = mid
				}
			}
			out, _ := fits(lo)
			if got := strings.TrimRight(out, "\n"); got != want(lo)+" "+name(lo) {
				t.Errorf("k=%d: listgrouphashfile printed %d bytes %.120q..., want the path of %d bytes %.120q...",
					lo, len(got), got, len(want(lo)), want(lo))
			}
			t.Logf("%s: the longest path that fits has %d bytes (k=%d)", layout, len(want(lo)), lo)
			out, _ = fits(hi)
			if !strings.Contains(out, "GFName: generated pathname too long for newsgroup e2e.x.") {
				t.Errorf("k=%d: no \"GFName: generated pathname too long\":\n%.300s", hi, out)
			}
			out, err := s.TryRun("dgrpctl", "listgrouphashfile", "e2e."+strings.Repeat("b", 9000)+".c")
			if err == nil || !strings.Contains(out, "FindGroupDir: pathname too long for newsgroup e2e.") {
				t.Errorf("9000-character component: want a non-zero exit with \"FindGroupDir: pathname too long\" (%v):\n%.300s", err, out)
			}
		})
	}
}

// TestHierarchyFullConfig: diablo.config `readergrouphash`. Input: hierarchy-full in two
// spellings of case, hierarchy, and near misses. Expected: hierarchy-full is accepted as
// hierarchy is (strcasecmp, so HIERARCHY-FULL too) and puts e2e.x.gz in e2e/x/; hierarchy
// keeps e2e/; hierarchy-fullx, hierarchyfull, hierarchy- and hierarchy-ful are refused as
// any unknown value is (bogus as the control): exit 1, "Unknown diablo.config command:
// readergrouphash <value>" and "critical error parsing diablo.config, exiting.".
func TestHierarchyFullConfig(t *testing.T) {
	s, _ := overSite(t, nil)
	base := s.ReadFile(t, "diablo.config")
	for _, c := range []struct{ value, want string }{
		{"hierarchy-full", "e2e/x/o.0.e2e.x.gz"},
		{"HIERARCHY-FULL", "e2e/x/o.0.e2e.x.gz"},
		{"hierarchy", "e2e/o.0.e2e.x.gz"},
		{"hierarchy-fullx", ""},
		{"hierarchyfull", ""},
		{"hierarchy-", ""},
		{"hierarchy-ful", ""},
		{"bogus", ""},
	} {
		s.WriteFile(t, "diablo.config", base+"\nreadergrouphash "+c.value+"\n")
		out, err := s.TryRun("dgrpctl", "listgrouphashfile", "e2e.x.gz")
		if c.want != "" {
			if err != nil || strings.TrimSpace(out) != c.want+" e2e.x.gz" {
				t.Errorf("readergrouphash %s: %v, %q, want %q", c.value, err, out, c.want+" e2e.x.gz")
			}
			continue
		}
		if err == nil || !strings.Contains(out, "Unknown diablo.config command: readergrouphash "+c.value+"\n") ||
			!strings.Contains(out, "critical error parsing diablo.config, exiting.") {
			t.Errorf("readergrouphash %s: want it refused (exit 1, \"Unknown diablo.config command\"): %v\n%s",
				c.value, err, out)
		}
	}
}

// hierFullReader is a running reader site with readergrouphash hierarchy-full and groups,
// whose spool st has them too. It returns the site, the store and the group home.
func hierFullReader(t *testing.T, groups []string, files map[string]string) (*Site, *fakeserver.Store, string) {
	t.Helper()
	st := fakeserver.NewStore()
	for _, g := range groups {
		st.AddGroup(g, "hierarchy-full test group")
	}
	spool := fakeserver.New(t, fakeserver.Options{Store: st})
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups, Files: files,
		Config: map[string]string{"readergrouphash": "hierarchy-full"}})
	return s, st, strings.TrimSpace(s.Run(t, "dpath", "group"))
}

// readOver waits for n articles in group and returns its OVER 1-n.
func readOver(t *testing.T, s *Site, group string, n int) []string {
	t.Helper()
	c := s.Reader(t)
	testkit.WaitFor(t, 10*time.Second, fmt.Sprintf("%d articles in GROUP %s", n, group), func() bool {
		_, line := c.Cmd("GROUP %s", group)
		return strings.HasPrefix(line, fmt.Sprintf("211 %d 1 %d ", n, n))
	})
	c.ExpectCmd(224, "OVER 1-%d", n)
	lines := c.ReadDotLines()
	c.ExpectCmd(205, "QUIT")
	return lines
}

// TestHierarchyFullReaderFiles: dreaderd with readergrouphash hierarchy-full makes the nested
// directories and uses them. Input: one article fed to each of a.b.c, e2e.x and e2e.x.gz.
// Expected: the over and data files are a/b/o.0.a.b.c and a/b/d.0.a.b.c, e2e/o.0.e2e.x and
// e2e/d.0.e2e.x, e2e/x/o.0.e2e.x.gz and e2e/x/d.0.e2e.x.gz (each data file holds its own
// article's Message-ID and not the others'); nothing is in the hierarchy places (a.b/,
// e2e/o.0.e2e.x.gz, e2e/d.0.e2e.x.gz); OVER 1 of each group returns its own article, also after
// a restart of dreaderd; `drcmd config readergrouphash` reports hierarchy-full.
func TestHierarchyFullReaderFiles(t *testing.T) {
	groups := []string{"a.b.c", "e2e.x", "e2e.x.gz"}
	s, st, home := hierFullReader(t, groups, nil)
	ids := map[string]string{}
	for _, g := range groups {
		ids[g] = msgID(t)
		feedReader(t, s, st, g, newArticle(ids[g], g, "hierarchy-full "+g, "body of "+g))
	}
	check := func(when string) {
		for _, g := range groups {
			over := readOver(t, s, g, 1)
			if len(over) != 1 || !strings.Contains(over[0], ids[g]) || !strings.Contains(over[0], "hierarchy-full "+g) {
				t.Errorf("%s: OVER 1 of %s: %q, want its article %s", when, g, over, ids[g])
			}
		}
	}
	check("fed")

	files := map[string][2]string{
		"a.b.c":    {"a/b/o.0.a.b.c", "a/b/d.0.a.b.c"},
		"e2e.x":    {"e2e/o.0.e2e.x", "e2e/d.0.e2e.x"},
		"e2e.x.gz": {"e2e/x/o.0.e2e.x.gz", "e2e/x/d.0.e2e.x.gz"},
	}
	for _, g := range groups {
		if got := hierListPath(t, s, g); got != files[g][0] {
			t.Errorf("listgrouphashfile %s = %q, want %q", g, got, files[g][0])
		}
		if _, err := os.Stat(filepath.Join(home, files[g][0])); err != nil {
			t.Errorf("over file of %s: %v", g, err)
		}
		data, err := os.ReadFile(filepath.Join(home, files[g][1]))
		if err != nil {
			t.Errorf("data file of %s: %v", g, err)
			continue
		}
		for _, o := range groups {
			if has := bytes.Contains(data, []byte(ids[o])); has != (o == g) {
				t.Errorf("data file %s of %s: holds the article of %s: %v, want %v", files[g][1], g, o, has, o == g)
			}
		}
	}
	for _, p := range []string{"a.b", "e2e/o.0.e2e.x.gz", "e2e/d.0.e2e.x.gz"} {
		if _, err := os.Stat(filepath.Join(home, p)); err == nil {
			t.Errorf("%s exists (a hierarchy path)", p)
		}
	}
	if out := s.Run(t, "drcmd", "config", "readergrouphash"); !strings.Contains(out, "readergrouphash: hierarchy-full:") {
		t.Errorf("drcmd config readergrouphash: %q, want hierarchy-full", out)
	}

	s.Proc.Stop()
	_, port, _ := strings.Cut(s.Addr, ":")
	startReader(t, s, "[127.0.0.1]:"+port)
	check("after a restart")
}

// TestHierarchyFullReaderGzipPair: with hierarchy-full the compressed data of e2e.x and the
// plain data of e2e.x.gz are different files. Input: 12 articles fed to e2e.x and 2 to e2e.x.gz
// (dexpire.ctl e2e.*:x30:e4:F1:Z1), the four plain data files of e2e.x made two hours old,
// `dexpireover -y -z`, then a dreaderd restart. Expected: "Compressed 2 files": e2e/d.0.e2e.x.gz
// and e2e/d.4.e2e.x.gz are gzip files and the plain ones are gone, e2e/x/d.0.e2e.x.gz (the plain
// data of e2e.x.gz) is unchanged; OVER 1-12 of e2e.x and OVER 1-2 of e2e.x.gz are as before.
// (With hierarchy the two names are one path, and dexpireover does not compress them: D-318,
// TestDexpireoverHierarchyCompressGzName.) Skipped without zlib.
func TestHierarchyFullReaderGzipPair(t *testing.T) {
	skipWithoutZlib(t)
	s, st, home := hierFullReader(t, []string{"e2e.x", "e2e.x.gz"},
		map[string]string{"dexpire.ctl": "e2e.*:x30:e4:F1:Z1\n"})
	var arts [][]string
	for n := 1; n <= 12; n++ {
		arts = append(arts, newArticle(msgID(t), "e2e.x", fmt.Sprintf("group x %02d", n), "body"))
	}
	feedReader(t, s, st, "e2e.x", arts...)
	feedReader(t, s, st, "e2e.x.gz", newArticle(msgID(t), "e2e.x.gz", "group x.gz 01", "body"),
		newArticle(msgID(t), "e2e.x.gz", "group x.gz 02", "body"))

	read := func() map[string][]string {
		return map[string][]string{"e2e.x": readOver(t, s, "e2e.x", 12), "e2e.x.gz": readOver(t, s, "e2e.x.gz", 2)}
	}
	before := read()
	other := filepath.Join(home, "e2e", "x", "d.0.e2e.x.gz")
	otherData, err := os.ReadFile(other)
	if err != nil {
		t.Fatalf("plain data file of e2e.x.gz: %v", err)
	}
	plain, _ := filepath.Glob(filepath.Join(home, "e2e", "d.*.e2e.x"))
	if len(plain) != 4 {
		t.Fatalf("%d plain data files of e2e.x, want 4 (e4, 12 articles): %q", len(plain), plain)
	}
	old := time.Now().Add(-2 * time.Hour)
	for _, f := range plain {
		if err := os.Chtimes(f, old, old); err != nil {
			t.Fatal(err)
		}
	}

	out := s.Run(t, "dexpireover", "-y", "-z")
	if !strings.Contains(out, "Compressed 2 files") {
		t.Errorf("dexpireover -y -z: no \"Compressed 2 files\":\n%s", out)
	}
	for _, n := range []string{"d.0.e2e.x", "d.4.e2e.x"} {
		p := filepath.Join(home, "e2e", n)
		if b, err := os.ReadFile(p + ".gz"); err != nil || !bytes.HasPrefix(b, []byte{0x1f, 0x8b}) {
			t.Errorf("%s.gz: not a gzip file (%v):\n%s", p, err, out)
		}
		if _, err := os.Stat(p); err == nil {
			t.Errorf("%s is still there next to its .gz", p)
		}
	}
	if b, err := os.ReadFile(other); err != nil || !bytes.Equal(b, otherData) {
		t.Errorf("the plain data file of e2e.x.gz changed (%v):\n%s", err, out)
	}

	s.Proc.Stop()
	_, port, _ := strings.Cut(s.Addr, ":")
	startReader(t, s, "[127.0.0.1]:"+port)
	after := read()
	for _, g := range slices.Sorted(maps.Keys(before)) {
		if !slices.Equal(after[g], before[g]) {
			t.Errorf("OVER of %s after the compression differs:\nbefore %q\nafter  %q", g, before[g], after[g])
		}
	}
}

// treeSnapshot lists every directory and file below home, files with their content.
func treeSnapshot(t *testing.T, home string) map[string]string {
	t.Helper()
	m := map[string]string{}
	err := filepath.Walk(home, func(p string, fi os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel := strings.TrimPrefix(p, home)
		if fi.IsDir() {
			m[rel+"/"] = ""
			return nil
		}
		b, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		m[rel] = string(b)
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	return m
}

// hierConvertGroups are the groups of the convert tests: a one-character first component,
// and a one-character last component with the same name plus .gz (D-325).
var hierConvertGroups = []string{"a.b.c", "e2e.x", "e2e.x.gz"}

// TestHierarchyFullConvertRefused: `doverctl convert` between hierarchy and hierarchy-full.
// In hierarchy the plain data of e2e.x.gz and the compressed data of e2e.x are one file
// (e2e/d.0.e2e.x.gz), so there is no safe conversion (D-325). Input: a site of the source layout
// with a.b.c, e2e.x and e2e.x.gz (range 1..10), each with its over file and a plain data file
// d.0.<group> where the layout puts them; `doverctl -v convert <src> <dst>`, both directions.
// Expected: exit non-zero, "Cannot convert between hierarchy and hierarchy-full (not supported
// yet, D-325)", every file at its old path with the same bytes and no directory made or
// removed. Control: `doverctl -v convert crc hierarchy-full` on a crc site with the same groups
// is not refused and moves the files to a/b/, e2e/ and e2e/x/ unchanged.
func TestHierarchyFullConvertRefused(t *testing.T) {
	for _, c := range [][2]string{{"hierarchy", "hierarchy-full"}, {"hierarchy-full", "hierarchy"}} {
		t.Run(c[0]+"-to-"+c[1], func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": c[0]})
			for _, g := range hierConvertGroups {
				hierGroupRange(t, s, g, "1", "10")
				dir := hierGroupDir(t, s, home, g)
				writeOver(t, filepath.Join(home, dir, "o.0."+g), overHead(g))
				writeOver(t, filepath.Join(home, dir, "d.0."+g), []byte("data of "+g+"\x00"))
			}
			before := treeSnapshot(t, home)
			out, err := s.TryRun("doverctl", "-v", "convert", c[0], c[1])
			if err == nil {
				t.Errorf("doverctl convert %s %s exits 0", c[0], c[1])
			}
			if !strings.Contains(out, "Cannot convert between hierarchy and hierarchy-full (not supported yet, D-325)\n") {
				t.Errorf("doverctl convert %s %s: no refusal message", c[0], c[1])
			}
			after := treeSnapshot(t, home)
			for _, p := range slices.Sorted(maps.Keys(before)) {
				if a, ok := after[p]; !ok {
					t.Errorf("%s is gone", p)
				} else if a != before[p] {
					t.Errorf("%s was changed", p)
				}
			}
			for _, p := range slices.Sorted(maps.Keys(after)) {
				if _, ok := before[p]; !ok {
					t.Errorf("%s is new", p)
				}
			}
			if t.Failed() {
				t.Logf("doverctl -v convert %s %s:\n%s", c[0], c[1], out)
			}
		})
	}
	t.Run("crc-to-hierarchy-full", func(t *testing.T) {
		s, home := overSite(t, nil)
		for _, g := range hierConvertGroups {
			hierGroupRange(t, s, g, "1", "10")
			dir, hash := groupFiles(t, s, g)
			if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
				t.Fatal(err)
			}
			writeOver(t, filepath.Join(home, dir, "over.0."+hash), overHead(g))
			writeOver(t, filepath.Join(home, dir, "data.0."+hash), []byte("data of "+g+"\x00"))
		}
		out, err := s.TryRun("doverctl", "-v", "convert", "crc", "hierarchy-full")
		if err != nil || strings.Contains(out, "Cannot convert") {
			t.Fatalf("doverctl -v convert crc hierarchy-full: %v\n%s", err, out)
		}
		for g, dir := range map[string]string{"a.b.c": "a/b", "e2e.x": "e2e", "e2e.x.gz": "e2e/x"} {
			for name, want := range map[string]string{"o.0." + g: string(overHead(g)), "d.0." + g: "data of " + g + "\x00"} {
				if b, err := os.ReadFile(filepath.Join(home, dir, name)); err != nil || string(b) != want {
					t.Errorf("%s/%s after the conversion: %v, content as before: %v", dir, name, err, string(b) == want)
				}
			}
		}
		if t.Failed() {
			t.Logf("doverctl -v convert crc hierarchy-full:\n%s", out)
		}
	})
}

// TestHierarchyFullCleanOneCharDirs: `doverctl -v clean` with hierarchy-full enters the
// directories of one-character components (D-326, #515). Input: x.y (in x/), a.b.c (in a/b/)
// and e2e.x.gz (in e2e/x/), range 1..10, each with its over file and a plain data file d.0.<group>,
// and in each of those directories a data file of no group (d.0.x.nogroup, d.0.a.b.nogroup,
// d.0.e2e.x.nogroup). Expected: the groups' files are there, unchanged; the three others are
// removed with "Group for over file not found, removing <path>".
func TestHierarchyFullCleanOneCharDirs(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy-full"})
	var keep []hierFile
	var gone []string
	for g, want := range map[string]string{"x.y": "x", "a.b.c": "a/b", "e2e.x.gz": "e2e/x"} {
		hierGroupRange(t, s, g, "1", "10")
		dir := hierGroupDir(t, s, home, g)
		if dir != want {
			t.Fatalf("directory of %s: %q, want %q", g, dir, want)
		}
		d := filepath.Join(home, dir)
		keep = append(keep, hierFile{filepath.Join(d, "o.0."+g), overHead(g)},
			hierFile{filepath.Join(d, "d.0."+g), []byte("data of " + g + "\x00")})
		gone = append(gone, filepath.Join(d, "d.0."+strings.ReplaceAll(dir, "/", ".")+".nogroup"))
	}
	hierWrite(t, keep)
	for _, f := range gone {
		writeOver(t, f, []byte("data of no group\x00"))
	}

	out := s.Run(t, "doverctl", "-v", "clean")

	hierCheckKept(t, home, keep)
	for _, f := range gone {
		if _, err := os.Stat(f); err == nil {
			t.Errorf("%s, a file of no group, is still there", strings.TrimPrefix(f, home))
		}
		if !strings.Contains(out, "Group for over file not found, removing "+f+"\n") {
			t.Errorf("clean did not report %s", strings.TrimPrefix(f, home))
		}
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out)
	}
}
