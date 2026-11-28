package e2e

import (
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"fmt"
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
	return hierOverFileDE(group, first, slots, 256)
}

// hierOverFileDE is hierOverFile with dataEntries articles per data file in the header (oh_DataEntries,
// the "e" value of dexpire.ctl when the file was made), which sets the base of the data files.
func hierOverFileDE(group string, first, slots, dataEntries int) []byte {
	const slot = 32 // OverArt, dreaderd/defs.h
	b := append(overHeadMax(group, uint32(slots)), make([]byte, slots*slot)...)
	binary.NativeEndian.PutUint32(b[16+256:], uint32(dataEntries))
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
	// a directory whose name reads as a data file name (one an administrator made: a real
	// hierarchy directory is a group name component and never parses as d.<n>.<group>) is
	// not a data file: it stays
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
// the name and looked them up as the group "e2e.d.foo.gz" only. Here X and X.gz have different
// directories, so the directory of the file tells (D-336): a reading counts only in the
// directory of its group.
//
// Input: "readergrouphash hierarchy", the groups X and X.gz (range 1..300 each) in three
// combinations: both, only X, only X.gz; each existing group with its over file and its data file
// (X: the gzip d.256.X.gz; X.gz: the plain d.256.X.gz in its own directory). The directories
// are taken from `dgrpctl listgrouphashfile`; the one of X.gz is below that of X (e2e/foo and
// e2e/d.foo below e2e). Files to go, in
// the directory of X: d.1024.X.gz (outside the range of both groups) and d.256.e2e.nogroup.gz
// (no such group). `dexpireover -a -v`. Expected: all files of existing groups are there,
// unchanged, nothing is reported about them; the two others are removed and reported: d.1024.X.gz
// as stale for X, and in only_X.gz, where X does not exist and the directory is not that of
// X.gz, as the file of no group (before the D-336 fix: as stale for X.gz).
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
				hierCheckGone(t, home, out, []string{stale}, !tc.hasX)
				hierCheckGone(t, home, out, []string{noGroup}, true)
				for _, l := range strings.Split(out, "\n") {
					if strings.Contains(l, "not found, removing") && !strings.HasSuffix(l, noGroup) &&
						!(!tc.hasX && strings.HasSuffix(l, stale)) {
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
// groups X and X.gz (outside its range) and in range of the other. When both groups have their
// files in the directory of the file (same_dir: e2e.x and e2e.x.gz, both in e2e/ with plain
// hierarchy, D-325), it is kept, as the other group's file may be this one. Before the D-318
// fix, dexpireover judged the file by the group X.gz alone (the name as it stands), so with the
// range of X.gz not covering 1024 the compressed data of X was removed. When the directories
// differ (other_dirs: e2e.d.foo in e2e/, e2e.d.foo.gz in e2e/d.foo/; the name e2e.d.foo is the one
// scanned before #511), the file in e2e/ is X's alone (D-336): kept when X covers 1024, removed as
// stale for X when only X.gz does (before the D-336 fix it was kept).
//
// Input: "readergrouphash hierarchy", the groups X and X.gz with their over files, one with the
// range 1..300 and the other 1..1100 (and an over file of 2048 slots), both ways round; the file
// d.1024.X.gz in the directory of X, and d.2048.X.gz, which is outside both ranges;
// `dexpireover -a -v`. Expected: d.1024 is there, unchanged, and not reported (other_dirs
// X.gz_covers: removed with "Deleting stale overview data for e2e.d.foo"); d.2048 is removed and
// reported.
func TestDexpireoverHierarchyGzOneReadingCovers(t *testing.T) {
	for _, pair := range []struct{ name, x string }{
		{"same_dir", "e2e.x"},
		{"other_dirs", "e2e.d.foo"},
	} {
		for _, tc := range []struct {
			name           string
			neX, neZ       string
			slotsX, slotsZ int
		}{
			{"X.gz_covers", "300", "1100", 500, 2048},
			{"X_covers", "1100", "300", 2048, 500},
		} {
			t.Run(pair.name+"/"+tc.name, func(t *testing.T) {
				x, z := pair.x, pair.x+".gz"
				s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
				dirX := hierGroupDir(t, s, home, x)
				dirZ := hierGroupDir(t, s, home, z)
				if (dirX == dirZ) != (pair.name == "same_dir") {
					t.Fatalf("directories of %s and %s: %q and %q", x, z, dirX, dirZ)
				}
				hierGroupRange(t, s, x, "1", tc.neX)
				hierGroupRange(t, s, z, "1", tc.neZ)
				plain := bytes.Repeat([]byte("x"), 10)
				keep := []hierFile{
					{filepath.Join(home, dirX, "o.0."+x), hierOverFile(x, 1, tc.slotsX)},
					{filepath.Join(home, dirZ, "o.0."+z), hierOverFile(z, 1, tc.slotsZ)},
				}
				covered := filepath.Join(home, dirX, "d.1024."+x+".gz")
				stale := []string{filepath.Join(home, dirX, "d.2048."+x+".gz")}
				if pair.name == "other_dirs" && tc.name == "X.gz_covers" {
					stale = append(stale, covered)
				} else {
					keep = append(keep, hierFile{covered, plain})
				}
				hierWrite(t, keep)
				for _, f := range stale {
					writeOver(t, f, plain)
				}

				out := s.Run(t, "dexpireover", "-a", "-v")

				hierCheckKept(t, home, keep)
				hierCheckGone(t, home, out, stale, false)
				hierNoBadReport(t, out, stale)
				if t.Failed() {
					t.Logf("dexpireover -a -v:\n%s", out)
				}
			})
		}
	}
}

// TestDexpireoverHierarchyGzWild (D-318): with -w only the groups that match are loaded, so of
// the two groups a ".gz" data file may belong to, one may exist without being loaded. The file
// is then not judged by the loaded one alone, when both groups have their files in the
// directory of the file (same_dir: e2e.x and e2e.x.gz in e2e/ with plain hierarchy). The
// directory of a group follows from its name, so it is known without loading the group: when the
// other group's directory is another one and its over file is there (other_dirs: e2e.foo in
// e2e/, e2e.foo.gz in e2e/foo/), the file is X's alone and judged by X also with -w (D-336).
//
// Input: as TestDexpireoverHierarchyGzOneReadingCovers, X with the range 1..300 and X.gz with
// 1..1100; d.1024.X.gz (outside the range of X only) and d.2048.X.gz (outside both), in the
// directory of X. `dexpireover -a -v -w X` (X.gz is not loaded): same_dir: both files are there,
// and -v says d.1024 is left alone; then `-w 'X*'` (both loaded): d.1024 is kept, d.2048 is
// removed and reported. other_dirs: the first run already removes both, as stale for X (before
// the D-336 fix: both left, as in same_dir).
func TestDexpireoverHierarchyGzWild(t *testing.T) {
	for _, pair := range []struct{ name, x string }{
		{"same_dir", "e2e.x"},
		{"other_dirs", "e2e.foo"},
	} {
		t.Run(pair.name, func(t *testing.T) {
			x, z := pair.x, pair.x+".gz"
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			dirX := hierGroupDir(t, s, home, x)
			dirZ := hierGroupDir(t, s, home, z)
			if (dirX == dirZ) != (pair.name == "same_dir") {
				t.Fatalf("directories of %s and %s: %q and %q", x, z, dirX, dirZ)
			}
			hierGroupRange(t, s, x, "1", "300")
			hierGroupRange(t, s, z, "1", "1100")
			plain := bytes.Repeat([]byte("x"), 10)
			keep := []hierFile{
				{filepath.Join(home, dirX, "o.0."+x), hierOverFile(x, 1, 500)},
				{filepath.Join(home, dirZ, "o.0."+z), hierOverFile(z, 1, 2048)},
			}
			covered := filepath.Join(home, dirX, "d.1024."+x+".gz")
			stale := filepath.Join(home, dirX, "d.2048."+x+".gz")
			hierWrite(t, keep)
			writeOver(t, covered, plain)
			writeOver(t, stale, plain)

			one := s.Run(t, "dexpireover", "-a", "-v", "-w", x)

			if pair.name == "other_dirs" {
				hierCheckKept(t, home, keep)
				hierCheckGone(t, home, one, []string{covered, stale}, false)
				hierNoBadReport(t, one, []string{covered, stale})
				if t.Failed() {
					t.Logf("dexpireover -a -v -w %s:\n%s", x, one)
				}
				return
			}
			hierCheckKept(t, home, append(keep, hierFile{covered, plain}, hierFile{stale, plain}))
			hierNoBadReport(t, one, nil)
			if !strings.Contains(one, "left alone: "+covered) {
				t.Errorf("dexpireover -w %s does not say that it left %s alone", x, covered)
			}

			both := s.Run(t, "dexpireover", "-a", "-v", "-w", x+"*")

			hierCheckKept(t, home, append(keep, hierFile{covered, plain}))
			hierCheckGone(t, home, both, []string{stale}, false)
			hierNoBadReport(t, both, []string{stale})
			if t.Failed() {
				t.Logf("dexpireover -a -v -w %s:\n%s\ndexpireover -a -v -w %s*:\n%s", x, one, x, both)
			}
		})
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

// TestDexpireoverHierarchyCompressGzName (D-325): the hierarchy layout leaves a last component of
// one character out of the directory, so the groups e2e.x and e2e.x.gz have their files in one
// directory, and "d.256.e2e.x.gz" is the compressed data of e2e.x and the plain data of e2e.x.gz.
// `dexpireover -z` of the plain d.256.e2e.x of e2e.x would put the gzip file on that name:
// diablo_minigzip() does not replace an existing file (the run reports "Unable to compress" and
// exits 1 instead, shown on 2b1bf382), and where the name is free, e2e.x.gz takes the gzip file
// for its own plain data file, also when that group is made later. So a file whose compressed
// name is the plain data file name of the group "<G>.gz" is never compressed, whether that group
// exists, is loaded or not (-w), or the compressed file exists; -v says so. e2e.foo and
// e2e.foo.gz have different directories (e2e/ and e2e/foo/): no collision, the file is compressed.
//
// Input: "readergrouphash hierarchy", `dexpire.ctl` "e2e.*:x30:Z0:F0", the group X (range 1..300)
// with its over file and an old (two days) plain d.256.X of content A, and:
//   - both_live: e2e.x and e2e.x.gz (range 1..300) with its over file and a live plain
//     d.256.e2e.x.gz of content B (the file the compression would have replaced);
//   - group_no_data: e2e.x.gz exists, with no over file and no data file;
//   - wild_unloaded: as group_no_data, with `-w e2e.x` (e2e.x.gz is not loaded);
//   - wild_no_group: e2e.x only, with `-w e2e.x`;
//   - gz_exists: no group e2e.x.gz, but a file d.256.e2e.x.gz (content B);
//   - no_group: e2e.x only, no file on the compressed name;
//   - control_other_dir: e2e.foo and e2e.foo.gz (over file and plain d.256.e2e.foo.gz of content B in
//     e2e/foo/): not the same path.
//
// `dexpireover -a -z -v`. Expected (all but the control): exit 0, d.256.X is still A and plain,
// d.256.e2e.x.gz is still B or absent, "not compressed:" is said for d.256.X, "Unable to compress"
// and "Compressed" are not printed. Control: "Compressed 1 files", d.256.e2e.foo is gone and
// d.256.e2e.foo.gz in e2e/ is gzip with content A, e2e.foo.gz's file in e2e/foo/ is B.
// Fails on 2b1bf382 (both_live and gz_exists: "Unable to compress", exit 1; the others: the file
// is compressed). Skipped without zlib.
func TestDexpireoverHierarchyCompressGzName(t *testing.T) {
	skipWithoutZlib(t)
	contentA := bytes.Repeat([]byte("A"), 100)
	contentB := bytes.Repeat([]byte("B"), 70)
	for _, tc := range []struct {
		name         string
		x, z         string
		zGroup       bool   // the group z exists
		zOver, zData bool   // z has an over file / its plain data file
		gzOfX        bool   // d.256.x.gz exists in the directory of x without a group z
		wild         string // -w pattern
		compress     bool   // d.256.x is expected to be compressed
	}{
		{name: "both_live", x: "e2e.x", z: "e2e.x.gz", zGroup: true, zOver: true, zData: true},
		{name: "group_no_data", x: "e2e.x", z: "e2e.x.gz", zGroup: true},
		{name: "wild_unloaded", x: "e2e.x", z: "e2e.x.gz", zGroup: true, wild: "e2e.x"},
		{name: "wild_no_group", x: "e2e.x", z: "e2e.x.gz", wild: "e2e.x"},
		{name: "gz_exists", x: "e2e.x", z: "e2e.x.gz", gzOfX: true},
		{name: "no_group", x: "e2e.x", z: "e2e.x.gz"},
		{name: "control_other_dir", x: "e2e.foo", z: "e2e.foo.gz", zGroup: true, zOver: true, zData: true, compress: true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			s.WriteFile(t, "dexpire.ctl", "e2e.*:x30:Z0:F0\n")
			dirX := hierGroupDir(t, s, home, tc.x)
			hierGroupRange(t, s, tc.x, "1", "300")
			plain := filepath.Join(home, dirX, "d.256."+tc.x)
			keep := []hierFile{{filepath.Join(home, dirX, "o.0."+tc.x), hierOverFile(tc.x, 1, 500)}}
			writeOver(t, plain, contentA)
			old := time.Now().Add(-48 * time.Hour)
			if err := os.Chtimes(plain, old, old); err != nil {
				t.Fatal(err)
			}
			var zData string // the file of content B
			if tc.zGroup {
				hierGroupRange(t, s, tc.z, "1", "300")
				dirZ := hierGroupDir(t, s, home, tc.z)
				if (dirZ == dirX) != (tc.x == "e2e.x") {
					t.Fatalf("directories of %s and %s: %q and %q", tc.x, tc.z, dirX, dirZ)
				}
				if tc.zOver {
					keep = append(keep, hierFile{filepath.Join(home, dirZ, "o.0."+tc.z), hierOverFile(tc.z, 1, 500)})
				}
				if tc.zData {
					zData = filepath.Join(home, dirZ, "d.256."+tc.z)
				}
			}
			if tc.gzOfX {
				zData = plain + ".gz"
			}
			if zData != "" {
				keep = append(keep, hierFile{zData, contentB})
			}
			hierWrite(t, keep)

			args := []string{"-a", "-z", "-v"}
			if tc.wild != "" {
				args = append(args, "-w", tc.wild)
			}
			out, err := s.TryRun("dexpireover", args...)
			if err != nil {
				t.Errorf("dexpireover %s: %v", strings.Join(args, " "), err)
			}

			hierCheckKept(t, home, keep)
			if strings.Contains(out, "Unable to compress") {
				t.Errorf("dexpireover tried to compress %s onto an existing file", plain)
			}
			if tc.compress {
				if !strings.Contains(out, "Compressed 1 files") {
					t.Errorf("dexpireover did not compress %s", plain)
				}
				if _, err := os.Stat(plain); err == nil {
					t.Errorf("the plain file %s is still there", plain)
				}
				zr, err := os.Open(plain + ".gz")
				if err != nil {
					t.Errorf("no compressed file: %v", err)
				} else {
					defer zr.Close()
					gr, err := gzip.NewReader(zr)
					if err != nil {
						t.Errorf("%s.gz is not gzip: %v", plain, err)
					} else if got, _ := io.ReadAll(gr); !bytes.Equal(got, contentA) {
						t.Errorf("%s.gz holds %q", plain, got)
					}
				}
			} else {
				if got, err := os.ReadFile(plain); err != nil || !bytes.Equal(got, contentA) {
					t.Errorf("%s was removed or changed: %v", plain, err)
				}
				if strings.Contains(out, "Compressed ") {
					t.Errorf("dexpireover compressed a file: %s", out)
				}
				if zData == "" {
					if _, err := os.Stat(plain + ".gz"); err == nil {
						t.Errorf("%s.gz was made", plain)
					}
				}
				said := false
				for _, l := range strings.Split(out, "\n") {
					if strings.HasPrefix(l, "Overview "+plain+" not compressed: ") {
						said = true
					}
				}
				if !said {
					t.Errorf("dexpireover -v does not say that it did not compress %s", plain)
				}
			}
			if t.Failed() {
				t.Logf("dexpireover %s:\n%s", strings.Join(args, " "), out)
			}
		})
	}
}

// TestDexpireoverHierarchyDataEntries (D-318, review of #513): the data files of a group hold
// "e" articles each (dexpire.ctl, kept in the header of its over file), and dexpireover judges a
// data file stale by that number: outside the range, or a base that is not a multiple of it. It
// read the number from the over file by a path that is relative to the group home for the
// hierarchy layout, but the scan has changed into the directory of the file by then, so the open
// failed and the number was taken as 256. Before #513 only group names with ".d." were scanned,
// now every hierarchy data file is, so with another "e" the live data files of every group were
// judged by 256: with e4 or e64 the files on no multiple of 256 were removed, with e1024 the one
// with the oldest live articles, and `-z` could take the file still written to.
//
// Input: "readergrouphash hierarchy", `dexpire.ctl` "e2e.*:x30:e<N>", e2e.test with an over file
// whose header has N entries per data file and the data files below, `dexpireover -a -v`.
//   - e4: range 1..300; d.0, d.4 and d.8 stay; d.6 (no multiple of 4) and d.1024 (outside the
//     range) go (the control);
//   - e64: range 1..300; d.0, d.64 and d.128 stay; d.100 and d.1024 go;
//   - e1024: range 1500..1800; d.1024 (the oldest live articles) and d.2048 stay; d.0 and d.3072
//     go;
//   - gz_same_dir: e2e.x (256 entries) and e2e.x.gz (4 entries), both in e2e/ with plain
//     hierarchy (D-325): d.4.e2e.x.gz is the compressed data of e2e.x (stale there, 4 is no
//     multiple of 256) and the plain data of e2e.x.gz (in range), so it stays, which needs the
//     over file of e2e.x.gz; d.1024.e2e.x.gz goes;
//   - gz_other_dir: e2e.foo (256 entries) and e2e.foo.gz (4 entries, its files in e2e/foo/):
//     d.4.e2e.foo.gz in e2e/ is the compressed data of e2e.foo only (the plain data of
//     e2e.foo.gz would be in e2e/foo/, D-336), stale there, so it goes with d.1024.e2e.foo.gz
//     (before the D-336 fix it was kept as e2e.foo.gz's, whose over file was read from e2e/foo/).
//
// Fails on 8a146aa8 for every case but the control files.
func TestDexpireoverHierarchyDataEntries(t *testing.T) {
	plain := bytes.Repeat([]byte("x"), 10)
	for _, tc := range []struct {
		name         string
		e, nb, ne    int
		keep, remove []int
	}{
		{"e4", 4, 1, 300, []int{0, 4, 8}, []int{6, 1024}},
		{"e64", 64, 1, 300, []int{0, 64, 128}, []int{100, 1024}},
		{"e1024", 1024, 1500, 1800, []int{1024, 2048}, []int{0, 3072}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			s.WriteFile(t, "dexpire.ctl", fmt.Sprintf("e2e.*:x30:e%d\n", tc.e))
			s.Run(t, "dgrpctl", "adjust", "e2e.test", "NB", fmt.Sprintf("=%d", tc.nb))
			s.Run(t, "dgrpctl", "adjust", "e2e.test", "NE", fmt.Sprintf("=%d", tc.ne))
			dir := filepath.Join(home, "e2e")
			keep := []hierFile{{filepath.Join(dir, "o.0.e2e.test"), hierOverFileDE("e2e.test", tc.nb, 500, tc.e)}}
			for _, b := range tc.keep {
				keep = append(keep, hierFile{filepath.Join(dir, fmt.Sprintf("d.%d.e2e.test", b)), plain})
			}
			hierWrite(t, keep)
			var gone []string
			for _, b := range tc.remove {
				f := filepath.Join(dir, fmt.Sprintf("d.%d.e2e.test", b))
				writeOver(t, f, plain)
				gone = append(gone, f)
			}

			out := s.Run(t, "dexpireover", "-a", "-v")

			hierCheckKept(t, home, keep)
			hierCheckGone(t, home, out, gone, false)
			hierNoBadReport(t, out, gone)
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
	for _, pair := range []struct{ name, x string }{
		{"gz_same_dir", "e2e.x"},
		{"gz_other_dir", "e2e.foo"},
	} {
		t.Run(pair.name, func(t *testing.T) {
			x, z := pair.x, pair.x+".gz"
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			s.WriteFile(t, "dexpire.ctl", "e2e.*:x30\n")
			dirX := hierGroupDir(t, s, home, x)
			dirZ := hierGroupDir(t, s, home, z)
			if (dirX == dirZ) != (pair.name == "gz_same_dir") {
				t.Fatalf("directories of %s and %s: %q and %q", x, z, dirX, dirZ)
			}
			hierGroupRange(t, s, x, "1", "300")
			hierGroupRange(t, s, z, "1", "300")
			keep := []hierFile{
				{filepath.Join(home, dirX, "o.0."+x), hierOverFileDE(x, 1, 500, 256)},
				{filepath.Join(home, dirZ, "o.0."+z), hierOverFileDE(z, 1, 500, 4)},
			}
			four := filepath.Join(home, dirX, "d.4."+x+".gz")
			stale := []string{filepath.Join(home, dirX, "d.1024."+x+".gz")}
			if pair.name == "gz_same_dir" {
				keep = append(keep, hierFile{four, plain})
			} else {
				stale = append(stale, four)
			}
			hierWrite(t, keep)
			for _, f := range stale {
				writeOver(t, f, plain)
			}

			out := s.Run(t, "dexpireover", "-a", "-v")

			hierCheckKept(t, home, keep)
			hierCheckGone(t, home, out, stale, false)
			hierNoBadReport(t, out, stale)
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
}

// TestDexpireoverCrcDataEntries: the crc layout reads the data entries of the over file by
// GFName() with the group home in front, which opens from any directory; it must go on doing
// so (the hierarchy fix of TestDexpireoverHierarchyDataEntries changes the same call).
//
// Input: the crc layout, `dexpire.ctl` "e2e.*:x30:e4", e2e.test (range 1..300) with an over file
// of 4 entries per data file and data.0, data.4 and data.8; data.6 (no multiple of 4) and
// data.1024 (outside the range); `dexpireover -a -v`. Expected: the first three stay, the last
// two are removed and reported. Passes on 8a146aa8 as well.
func TestDexpireoverCrcDataEntries(t *testing.T) {
	s, home := overSite(t, nil)
	s.WriteFile(t, "dexpire.ctl", "e2e.*:x30:e4\n")
	dir, hash := groupFiles(t, s, "e2e.test")
	if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
		t.Fatal(err)
	}
	plain := bytes.Repeat([]byte("x"), 10)
	keep := []hierFile{{filepath.Join(home, dir, "over.0."+hash), hierOverFileDE("e2e.test", 1, 500, 4)}}
	for _, b := range []int{0, 4, 8} {
		keep = append(keep, hierFile{filepath.Join(home, dir, fmt.Sprintf("data.%d.%s", b, hash)), plain})
	}
	hierWrite(t, keep)
	var gone []string
	for _, b := range []int{6, 1024} {
		f := filepath.Join(home, dir, fmt.Sprintf("data.%d.%s", b, hash))
		writeOver(t, f, plain)
		gone = append(gone, f)
	}

	out := s.Run(t, "dexpireover", "-a", "-v")

	hierCheckKept(t, home, keep)
	hierCheckGone(t, home, out, gone, false)
	hierNoBadReport(t, out, gone)
	if t.Failed() {
		t.Logf("dexpireover -a -v:\n%s", out)
	}
}
