package e2e

import (
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// hierFile is a file of a test with the content it must still have afterwards.
type hierFile struct {
	path string
	want []byte
}

// hierOverFile returns an over file for group with slots article slots and the article first,
// received now, in its slot, so that `dexpireover -a` neither moves the group's first article
// number (a group whose range starts at first) nor resizes the file.
func hierOverFile(group string, first, slots int) []byte {
	const slot = 32 // OverArt, dreaderd/defs.h
	b := append(overHeadMax(group, uint32(slots)), make([]byte, slots*slot)...)
	o := b[len(b)-slots*slot+(first%slots)*slot:]
	binary.NativeEndian.PutUint32(o[0:], uint32(first))              // oa_ArtNo
	binary.NativeEndian.PutUint32(o[24:], uint32(time.Now().Unix())) // oa_TimeRcvd
	return b
}

// hierGroupRange adds a group with the article range nb..ne to the site.
func hierGroupRange(t *testing.T, s *Site, group, nb, ne string) {
	t.Helper()
	s.Run(t, "dgrpctl", "newgroup", group, "y")
	s.Run(t, "dgrpctl", "adjust", group, "NB", "="+nb)
	s.Run(t, "dgrpctl", "adjust", group, "NE", "="+ne)
}

// hierGroupDir returns the directory below the group home that holds the files of group, as
// dgrpctl listgrouphashfile gives it (the directory of its over file), and makes it.
func hierGroupDir(t *testing.T, s *Site, home, group string) string {
	t.Helper()
	f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", group))
	if len(f) != 2 || f[1] != group || filepath.Base(f[0]) != "o.0."+group {
		t.Fatalf("dgrpctl listgrouphashfile %s: %q", group, f)
	}
	dir := filepath.Dir(f[0])
	if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
		t.Fatal(err)
	}
	return dir
}

// hierWrite writes the files.
func hierWrite(t *testing.T, files []hierFile) {
	t.Helper()
	for _, f := range files {
		writeOver(t, f.path, f.want)
	}
}

// hierCheckKept reports each file that is gone or changed.
func hierCheckKept(t *testing.T, home string, files []hierFile) {
	t.Helper()
	for _, f := range files {
		got, err := os.ReadFile(f.path)
		if err != nil {
			t.Errorf("%s was removed: %v", strings.TrimPrefix(f.path, home), err)
		} else if !bytes.Equal(got, f.want) {
			t.Errorf("%s was changed", strings.TrimPrefix(f.path, home))
		}
	}
}

// hierCheckGone reports each file that is still there, and expects out to say why it went: for
// a file of an existing group "Deleting stale overview data for <group>: <path>" (-v), for a
// file of no group "Group for over file not found, removing <path>".
func hierCheckGone(t *testing.T, home, out string, files []string, noGroup bool) {
	t.Helper()
	for _, f := range files {
		if _, err := os.Stat(f); err == nil {
			t.Errorf("%s, a stale file, is still there", strings.TrimPrefix(f, home))
		}
		said := false
		for _, l := range strings.Split(out, "\n") {
			if strings.HasSuffix(l, f) && noGroup && strings.HasPrefix(l, "Group for over file not found, removing ") {
				said = true
			}
			if strings.Contains(l, " "+f+" (artBase=") && !noGroup && strings.HasPrefix(l, "Deleting stale overview data for ") {
				said = true
			}
		}
		if !said {
			t.Errorf("dexpireover did not report the removal of %s", strings.TrimPrefix(f, home))
		}
	}
}

// hierNoBadReport fails for every line of out that reports a problem with a file, except for the
// lines that name one of the files in allowed.
func hierNoBadReport(t *testing.T, out string, allowed []string) {
	t.Helper()
	for _, l := range strings.Split(out, "\n") {
		for _, bad := range []string{"bad file header", "stale overview data", "not found, removing"} {
			if !strings.Contains(l, bad) {
				continue
			}
			ok := false
			for _, a := range allowed {
				if strings.Contains(l, a) {
					ok = true
				}
			}
			if !ok {
				t.Errorf("dexpireover reports: %s", l)
			}
		}
	}
}

// TestDexpireoverHierarchyGzDotD (D-318, the dexpireover side of #511): dexpireover -a took a
// file for an over or a data file by a substring of its name, and a hierarchy name holds the
// group name: "d.256.e2e.d.q.gz" was scanned only because the group name contains ".d.", and
// then looked up as the group "e2e.d.q.gz" (the ".gz" is part of the name as far as the parse
// goes), which does not exist: the compressed data of the live group e2e.d.q was removed as
// "Group for over file not found". And the over file "o.0.e2e.d.q" is taken for a data file
// too, because of the ".d." in it: it is a file with article base 0, which is outside the range
// of a group whose first article is 256 or more, so it was removed as "stale overview data".
//
// Input: "readergrouphash hierarchy", the group e2e.d.q with its over file, a plain data file
// and a valid gzip d.256.e2e.d.q.gz, all in range: with the articles 1..300 (d.0 and d.256), and
// with 300..400 (d.256 plain and gzip; the first article is above 255); `dexpireover -a -v`.
// Expected: the three files are there, unchanged, and nothing is reported about them.
func TestDexpireoverHierarchyGzDotD(t *testing.T) {
	for _, tc := range []struct {
		name, nb, ne string
		first        int
		plain        string // base of the plain data file
	}{
		{"range_1_300", "1", "300", 1, "0"},
		{"range_300_580", "300", "580", 300, "256"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			hierGroupRange(t, s, "e2e.d.q", tc.nb, tc.ne)
			dir := filepath.Join(home, hierGroupDir(t, s, home, "e2e.d.q"))
			keep := []hierFile{
				{filepath.Join(dir, "o.0.e2e.d.q"), hierOverFile("e2e.d.q", tc.first, 500)},
				{filepath.Join(dir, "d."+tc.plain+".e2e.d.q"), bytes.Repeat([]byte("x"), 10)},
				{filepath.Join(dir, "d.256.e2e.d.q.gz"), hierGzBytes(t)},
			}
			hierWrite(t, keep)

			out := s.Run(t, "dexpireover", "-a", "-v")

			hierCheckKept(t, home, keep)
			hierNoBadReport(t, out, nil)
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
}

// TestDexpireoverHierarchyDataStale (D-318): dexpireover -a looked at the hierarchy data names
// "d.<n>.<group>" only when the group name contains ".d.", unlike the crc names ("data.") and
// the md5 names. A stale data file of every other group stayed, and so did a file of no group.
//
// Input: "readergrouphash hierarchy", e2e.test (range 1..300) with its over file, the plain data
// files d.0 and d.256 (in range), and as files to go: d.1024.e2e.test and d.1024.e2e.test.gz
// (outside the range), d.100.e2e.test (not a multiple of the 256 entries per file), and
// d.256.e2e.nogroup and d.256.e2e.nogroup.gz (no such group), and an empty directory
// d.5.e2e.test (its name reads as a stale data file); `dexpireover -a -v`. Expected: the
// files in range and the directory are there, unchanged; the stale ones are removed and reported with a
// "Deleting stale overview data for e2e.test" line, the ones of no group with "Group for over
// file not found, removing".
func TestDexpireoverHierarchyDataStale(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	dir := filepath.Join(home, "e2e")
	plain := bytes.Repeat([]byte("x"), 10)
	keep := []hierFile{
		{filepath.Join(dir, "o.0.e2e.test"), hierOverFile("e2e.test", 1, 500)},
		{filepath.Join(dir, "d.0.e2e.test"), plain},
		{filepath.Join(dir, "d.256.e2e.test"), plain},
	}
	hierWrite(t, keep)
	// a directory whose name reads as a data file name (hierarchy directories can hold
	// dots, as e2e/d.foo does) is not a data file: it stays
	dirName := filepath.Join(dir, "d.5.e2e.test")
	if err := os.Mkdir(dirName, 0o755); err != nil {
		t.Fatal(err)
	}
	var stale, noGroup []string
	for _, n := range []string{"d.1024.e2e.test", "d.1024.e2e.test.gz", "d.100.e2e.test"} {
		stale = append(stale, filepath.Join(dir, n))
	}
	for _, n := range []string{"d.256.e2e.nogroup", "d.256.e2e.nogroup.gz"} {
		noGroup = append(noGroup, filepath.Join(dir, n))
	}
	for _, f := range append(append([]string{}, stale...), noGroup...) {
		writeOver(t, f, plain)
	}

	out := s.Run(t, "dexpireover", "-a", "-v")

	hierCheckKept(t, home, keep)
	hierCheckGone(t, home, out, stale, false)
	hierCheckGone(t, home, out, noGroup, true)
	if st, err := os.Stat(dirName); err != nil || !st.IsDir() {
		t.Errorf("the directory %s was removed: %v", dirName, err)
	}
	if t.Failed() {
		t.Logf("dexpireover -a -v:\n%s", out)
	}
}

// TestDexpireoverHierarchyGzGroupName (D-318): a group name may end in ".gz", so in the
// hierarchy layout d.256.<X>.gz is the compressed data of the group X (in its directory) or the
// plain data of the group X.gz (in the directory of that one), and the name alone does not tell.
// As doverctl clean does (TestDoverctlCleanHierarchyGzGroupName), dexpireover looks up both and
// keeps the file unless it is stale for every group that exists. The names e2e.foo and e2e.d.foo
// are tried: with the second one dexpireover (before the fix) scanned the files by the ".d." in
// the name and looked them up as the group "e2e.d.foo.gz" only.
//
// Input: "readergrouphash hierarchy", the groups X and X.gz (range 1..300 each) in three
// combinations: both, only X, only X.gz; each existing group with its over file and its data file
// (X: the gzip d.256.X.gz; X.gz: the plain d.256.X.gz in its own directory). The directories
// are taken from `dgrpctl listgrouphashfile`; the one of X.gz is below that of X (e2e/foo and
// e2e/d.foo below e2e). Files to go, in
// the directory of X: d.1024.X.gz (outside the range of both groups) and d.256.e2e.nogroup.gz
// (no such group). `dexpireover -a -v`. Expected: all files of existing groups are there,
// unchanged, nothing is reported about them; the two others are removed and reported.
func TestDexpireoverHierarchyGzGroupName(t *testing.T) {
	for _, base := range []string{"e2e.foo", "e2e.d.foo"} {
		for _, tc := range []struct {
			name       string
			hasX, hasZ bool
		}{
			{"both_groups", true, true},
			{"only_X", true, false},
			{"only_X.gz", false, true},
		} {
			t.Run(base+"/"+tc.name, func(t *testing.T) {
				s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
				x, z := base, base+".gz"
				dirX := hierGroupDir(t, s, home, x)
				dirZ := hierGroupDir(t, s, home, z)
				if dirZ != path.Join(dirX, strings.TrimPrefix(x, "e2e.")) {
					t.Fatalf("directories of %s and %s: %q and %q, want the second below the first", x, z, dirX, dirZ)
				}
				plain := bytes.Repeat([]byte("x"), 10)
				var keep []hierFile
				if tc.hasX {
					hierGroupRange(t, s, x, "1", "300")
					keep = append(keep,
						hierFile{filepath.Join(home, dirX, "o.0."+x), hierOverFile(x, 1, 500)},
						hierFile{filepath.Join(home, dirX, "d.256."+x+".gz"), hierGzBytes(t)})
				}
				if tc.hasZ {
					hierGroupRange(t, s, z, "1", "300")
					keep = append(keep,
						hierFile{filepath.Join(home, dirZ, "o.0."+z), hierOverFile(z, 1, 500)},
						hierFile{filepath.Join(home, dirZ, "d.256."+x+".gz"), plain})
				}
				hierWrite(t, keep)
				stale := filepath.Join(home, dirX, "d.1024."+x+".gz")
				noGroup := filepath.Join(home, dirX, "d.256.e2e.nogroup.gz")
				writeOver(t, stale, plain)
				writeOver(t, noGroup, plain)

				out := s.Run(t, "dexpireover", "-a", "-v")

				hierCheckKept(t, home, keep)
				hierCheckGone(t, home, out, []string{stale}, false)
				hierCheckGone(t, home, out, []string{noGroup}, true)
				for _, l := range strings.Split(out, "\n") {
					if strings.Contains(l, "not found, removing") && !strings.HasSuffix(l, noGroup) {
						t.Errorf("dexpireover removed a file of an existing group: %s", l)
					}
				}
				if t.Failed() {
					t.Logf("dexpireover -a -v:\n%s", out)
				}
			})
		}
	}
}

// TestDexpireoverHierarchyGzOneReadingCovers (D-318): d.1024.<X>.gz is stale for one of the two
// groups X and X.gz (outside its range) and in range of the other: it is kept, as the other
// group's file may be this one. Before the fix, dexpireover judged the file by the group X.gz
// alone (the name as it stands), so with the range of X.gz not covering 1024 the compressed data
// of X was removed. The name e2e.d.foo is the one scanned before the fix.
//
// Input: "readergrouphash hierarchy", the groups e2e.d.foo and e2e.d.foo.gz with their over files,
// one with the range 1..300 and the other 1..1100 (and an over file of 2048 slots), both ways
// round; the file d.1024.e2e.d.foo.gz in the directory of e2e.d.foo, and d.2048.e2e.d.foo.gz,
// which is outside both ranges; `dexpireover -a -v`. Expected: d.1024 is there, unchanged, and
// not reported; d.2048 is removed and reported.
func TestDexpireoverHierarchyGzOneReadingCovers(t *testing.T) {
	const x, z = "e2e.d.foo", "e2e.d.foo.gz"
	for _, tc := range []struct {
		name           string
		neX, neZ       string
		slotsX, slotsZ int
	}{
		{"X.gz_covers", "300", "1100", 500, 2048},
		{"X_covers", "1100", "300", 2048, 500},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			dirX := hierGroupDir(t, s, home, x)
			dirZ := hierGroupDir(t, s, home, z)
			hierGroupRange(t, s, x, "1", tc.neX)
			hierGroupRange(t, s, z, "1", tc.neZ)
			plain := bytes.Repeat([]byte("x"), 10)
			keep := []hierFile{
				{filepath.Join(home, dirX, "o.0."+x), hierOverFile(x, 1, tc.slotsX)},
				{filepath.Join(home, dirZ, "o.0."+z), hierOverFile(z, 1, tc.slotsZ)},
				{filepath.Join(home, dirX, "d.1024."+x+".gz"), plain},
			}
			hierWrite(t, keep)
			stale := filepath.Join(home, dirX, "d.2048."+x+".gz")
			writeOver(t, stale, plain)

			out := s.Run(t, "dexpireover", "-a", "-v")

			hierCheckKept(t, home, keep)
			hierCheckGone(t, home, out, []string{stale}, false)
			hierNoBadReport(t, out, []string{stale})
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
}

// TestDexpireoverHierarchyGzWild (D-318): with -w only the groups that match are loaded, so of
// the two groups a ".gz" data file may belong to, one may exist without being loaded. The file
// is then not judged by the loaded one alone.
//
// Input: as TestDexpireoverHierarchyGzOneReadingCovers, e2e.foo with the range 1..300 and
// e2e.foo.gz with 1..1100; d.1024.e2e.foo.gz (outside the range of e2e.foo only) and
// d.2048.e2e.foo.gz (outside both), in the directory of e2e.foo. `dexpireover -a -v -w e2e.foo`
// (e2e.foo.gz is not loaded): both files are there, and -v says d.1024 is left alone; then
// `-w 'e2e.foo*'` (both loaded): d.1024 is kept, d.2048 is removed and reported.
func TestDexpireoverHierarchyGzWild(t *testing.T) {
	const x, z = "e2e.foo", "e2e.foo.gz"
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	dirX := hierGroupDir(t, s, home, x)
	dirZ := hierGroupDir(t, s, home, z)
	hierGroupRange(t, s, x, "1", "300")
	hierGroupRange(t, s, z, "1", "1100")
	plain := bytes.Repeat([]byte("x"), 10)
	keep := []hierFile{
		{filepath.Join(home, dirX, "o.0."+x), hierOverFile(x, 1, 500)},
		{filepath.Join(home, dirZ, "o.0."+z), hierOverFile(z, 1, 2048)},
		{filepath.Join(home, dirX, "d.1024."+x+".gz"), plain},
	}
	hierWrite(t, keep)
	stale := filepath.Join(home, dirX, "d.2048."+x+".gz")
	writeOver(t, stale, plain)

	one := s.Run(t, "dexpireover", "-a", "-v", "-w", x)

	hierCheckKept(t, home, append(keep, hierFile{stale, plain}))
	hierNoBadReport(t, one, nil)
	if !strings.Contains(one, "left alone: "+keep[2].path) {
		t.Errorf("dexpireover -w %s does not say that it left %s alone", x, keep[2].path)
	}

	both := s.Run(t, "dexpireover", "-a", "-v", "-w", x+"*")

	hierCheckKept(t, home, keep)
	hierCheckGone(t, home, both, []string{stale}, false)
	hierNoBadReport(t, both, []string{stale})
	if t.Failed() {
		t.Logf("dexpireover -a -v -w %s:\n%s\ndexpireover -a -v -w %s*:\n%s", x, one, x, both)
	}
}

// TestDexpireoverHierarchyGroupNameO (D-318): a data file "d.256.e2e.o.x" of the group e2e.o.x
// was taken for an over file, because its name contains ".o.": opened read-write, locked, read
// as an over header ("bad file header" for every data file, which never got its range check).
//
// Input: "readergrouphash hierarchy", the group e2e.o.x (range 1..300) with its over file, the
// data files d.0 and d.256 (in range), and d.1024.e2e.o.x (outside); `dexpireover -a -v`.
// Expected: the over file and the two data files are there, unchanged, and no "bad file header"
// is reported; d.1024 is removed and reported as stale overview data of e2e.o.x.
func TestDexpireoverHierarchyGroupNameO(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	hierGroupRange(t, s, "e2e.o.x", "1", "300")
	dir := filepath.Join(home, hierGroupDir(t, s, home, "e2e.o.x"))
	plain := bytes.Repeat([]byte("x"), 10)
	keep := []hierFile{
		{filepath.Join(dir, "o.0.e2e.o.x"), hierOverFile("e2e.o.x", 1, 500)},
		{filepath.Join(dir, "d.0.e2e.o.x"), plain},
		{filepath.Join(dir, "d.256.e2e.o.x"), plain},
	}
	hierWrite(t, keep)
	stale := filepath.Join(dir, "d.1024.e2e.o.x")
	writeOver(t, stale, plain)

	out := s.Run(t, "dexpireover", "-a", "-v")

	hierCheckKept(t, home, keep)
	hierCheckGone(t, home, out, []string{stale}, false)
	hierNoBadReport(t, out, []string{stale})
	if t.Failed() {
		t.Logf("dexpireover -a -v:\n%s", out)
	}
}

// TestDexpireoverHierarchyCompress (D-318): the hierarchy data names are now scanned like the
// crc ones, so `dexpireover -z` also compresses a plain hierarchy data file that is old enough
// and not near the end of the group (before, only a group whose name contains ".d." was).
//
// Input: "readergrouphash hierarchy", `dexpire.ctl` "e2e.*:x30:Z0:F0", e2e.test (range 1..300)
// with its over file and a plain, non-empty d.0.e2e.test with a modification time two days ago;
// `dexpireover -a -z -v`. Expected: d.0.e2e.test.gz is a gzip file with the 10 bytes, the plain
// file is gone, and "Compressed 1 files" is printed. Skipped without zlib.
func TestDexpireoverHierarchyCompress(t *testing.T) {
	skipWithoutZlib(t)
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	s.WriteFile(t, "dexpire.ctl", "e2e.*:x30:Z0:F0\n")
	dir := filepath.Join(home, "e2e")
	writeOver(t, filepath.Join(dir, "o.0.e2e.test"), hierOverFile("e2e.test", 1, 500))
	plain := filepath.Join(dir, "d.0.e2e.test")
	writeOver(t, plain, bytes.Repeat([]byte("x"), 10))
	old := time.Now().Add(-48 * time.Hour)
	if err := os.Chtimes(plain, old, old); err != nil {
		t.Fatal(err)
	}

	out := s.Run(t, "dexpireover", "-a", "-z", "-v")

	if !strings.Contains(out, "Compressed 1 files") {
		t.Errorf("dexpireover -z did not compress the plain hierarchy data file")
	}
	if _, err := os.Stat(plain); err == nil {
		t.Errorf("the plain file d.0.e2e.test is still there")
	}
	zr, err := os.Open(plain + ".gz")
	if err != nil {
		t.Errorf("no compressed file: %v", err)
	} else {
		defer zr.Close()
		gr, err := gzip.NewReader(zr)
		if err != nil {
			t.Errorf("d.0.e2e.test.gz is not gzip: %v", err)
		} else if got, _ := io.ReadAll(gr); !bytes.Equal(got, bytes.Repeat([]byte("x"), 10)) {
			t.Errorf("d.0.e2e.test.gz holds %q", got)
		}
	}
	if t.Failed() {
		t.Logf("dexpireover -a -z -v:\n%s", out)
	}
}
