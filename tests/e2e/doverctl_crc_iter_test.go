package e2e

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Two group names whose crc file name hash is the same (found with the hhash() of lib/hash.c: it folds
// bit 31 of both halves into bit 0, and the 64-bit CRC is linear, so strings of the same length
// that differ in the right bits collide). The test checks with `dgrpctl listgrouphashfile` that
// they still do.
const (
	crcCollideA = "e2e.cc.aaaaaaaaaaaaaaaaaaaaaaaa"
	crcCollideB = "e2e.cc.imiaageiikgeiaikmkmcaaim"
)

// activeOrder returns the group names of the live records of an active file, in file order.
func activeOrder(t *testing.T, db string) []string {
	t.Helper()
	b, err := os.ReadFile(db)
	if err != nil {
		t.Fatal(err)
	}
	var out []string
	for _, l := range strings.Split(string(b), "\n") {
		if !strings.HasPrefix(l, "+") {
			continue
		}
		_, rest, ok := strings.Cut(l, ":")
		if !ok {
			continue
		}
		k, _, _ := strings.Cut(rest, " ")
		out = append(out, k)
	}
	return out
}

// groupFiles returns the directory (relative to the group home) and the hash part of the crc file
// names ("<hash>" of over.0.<hash>) of a group, as dgrpctl lists them.
func groupFiles(t *testing.T, s *Site, group string) (dir, hash string) {
	t.Helper()
	f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", group))
	if len(f) != 2 || !strings.HasPrefix(filepath.Base(f[0]), "over.0.") {
		t.Fatalf("dgrpctl listgrouphashfile %s: %q", group, f)
	}
	return filepath.Dir(f[0]), strings.TrimPrefix(filepath.Base(f[0]), "over.0.")
}

// TestDoverctlCleanCrcHashCollision (D-313): with the crc hash, two groups can have the same file
// name hash. The iteration (the suffix .<iter>, none for 0) keeps their files apart, but a group
// that has no over file yet has iteration 0, like the group that holds the name. clean took the
// first group in the active file with the hash and the iteration of a file as the file's group:
// when that was the empty one, the populated group's over file was removed as "bad file header"
// (the header names another group) and its data file as "stale overview data" (the empty group's
// range is empty).
//
// Input: crcCollideA with an over file (header of A) and a data file (articles 256 to 511, in A's
// range 1..300), crcCollideB in the active file with no files; the two orders of their records
// in dactive.kp. Expected: `doverctl -v clean` leaves both files of A with their content and
// reports no removal. The order with the populated group first always worked. A stale data file
// of a group that is not in the active file is the control: it is removed.
func TestDoverctlCleanCrcHashCollision(t *testing.T) {
	for _, tc := range []struct {
		name       string
		emptyFirst bool
	}{
		{"empty_group_first", true},
		{"populated_group_first", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			db := filepath.Join(s.Dir, "dactive.kp")
			dirA, hashA := groupFiles(t, s, crcCollideA)
			dirB, hashB := groupFiles(t, s, crcCollideB)
			if dirA != dirB || hashA != hashB {
				t.Fatalf("test setup: %s and %s do not share a file name hash: %s/%s and %s/%s",
					crcCollideA, crcCollideB, dirA, hashA, dirB, hashB)
			}
			newPopulated := func() {
				s.Run(t, "dgrpctl", "newgroup", crcCollideA, "y")
				s.Run(t, "dgrpctl", "adjust", crcCollideA, "NB", "=1")
				s.Run(t, "dgrpctl", "adjust", crcCollideA, "NE", "=300")
			}
			if tc.emptyFirst {
				s.Run(t, "dgrpctl", "newgroup", crcCollideB, "y")
				newPopulated()
			} else {
				newPopulated()
				s.Run(t, "dgrpctl", "newgroup", crcCollideB, "y")
			}
			var order []string
			for _, g := range activeOrder(t, db) {
				if g == crcCollideA || g == crcCollideB {
					order = append(order, g)
				}
			}
			first := crcCollideB
			if !tc.emptyFirst {
				first = crcCollideA
			}
			if len(order) != 2 || order[0] != first {
				t.Fatalf("test setup: records of the two groups in dactive.kp: %q, want %s first", order, first)
			}

			if err := os.MkdirAll(filepath.Join(home, dirA), 0o755); err != nil {
				t.Fatal(err)
			}
			over := filepath.Join(home, dirA, "over.0."+hashA)
			data := filepath.Join(home, dirA, "data.256."+hashA)
			stale := filepath.Join(home, dirA, "data.512.00000000.00000000")
			overBytes := overHead(crcCollideA)
			dataBytes := bytes.Repeat([]byte("x"), 10)
			writeOver(t, over, overBytes)
			writeOver(t, data, dataBytes)
			writeOver(t, stale, dataBytes)

			out := s.Run(t, "doverctl", "-v", "clean")

			for _, f := range []struct {
				path string
				want []byte
			}{{over, overBytes}, {data, dataBytes}} {
				got, err := os.ReadFile(f.path)
				if err != nil {
					t.Errorf("%s of %s was removed: %v", filepath.Base(f.path), crcCollideA, err)
				} else if !bytes.Equal(got, f.want) {
					t.Errorf("%s of %s was changed", filepath.Base(f.path), crcCollideA)
				}
			}
			if _, err := os.Stat(stale); err == nil {
				t.Errorf("%s, a file of no group, is still there", filepath.Base(stale))
			}
			for _, bad := range []string{"bad file header", "Deleting stale overview data"} {
				if strings.Contains(out, bad) {
					t.Errorf("clean reports %q:\n%s", bad, out)
				}
			}
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}
		})
	}
}

// TestDoverctlConvertHierarchyClean (D-314): the file names of the hierarchy layout have no
// iteration, but the group's ITER stays in dactive.kp (crc and md5 names carry it, and a
// conversion back needs it). clean looked a hierarchy file up with the group's old ITER,
// reported "Group for over file not found" and removed the over and data files of every group
// with ITER above 0.
//
// Input: e2e.test with ITER 1 and crc files over.0.<hash>.1 and data.256.<hash>.1 (articles 256
// to 511, in the range 1..300). Cases: `doverctl convert crc hierarchy clean` in one command; a
// `convert crc hierarchy`, the site's readergrouphash set to hierarchy, and a later `clean` run alone; and the first followed by `convert hierarchy crc clean`.
// Expected: the files are in the new layout with their content, nothing is removed, and after
// the conversion back the files have their old names again (the iteration is kept).
func TestDoverctlConvertHierarchyClean(t *testing.T) {
	for _, tc := range []struct {
		name string
		runs [][]string
		back bool
	}{
		{"same_command", [][]string{{"convert", "crc", "hierarchy", "clean"}}, false},
		// the site's readergrouphash is switched to hierarchy after the conversion, as the admin does
		{"later_clean", [][]string{{"convert", "crc", "hierarchy"}, {"@hierarchy"}, {"clean"}}, false},
		{"and_back", [][]string{{"convert", "crc", "hierarchy", "clean"}}, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			db := filepath.Join(s.Dir, "dactive.kp")
			if o := dkpRun(t, "", "-w", db, "e2e.test", "ITER=000001"); strings.Contains(o, "rror") {
				t.Fatalf("dkp -w ITER: %s", o)
			}
			dir, hash := groupFiles(t, s, "e2e.test")
			if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
				t.Fatal(err)
			}
			overBytes := overHead("e2e.test")
			dataBytes := bytes.Repeat([]byte("x"), 10)
			crcOver := filepath.Join(home, dir, "over.0."+hash+".1")
			crcData := filepath.Join(home, dir, "data.256."+hash+".1")
			writeOver(t, crcOver, overBytes)
			writeOver(t, crcData, dataBytes)

			var all strings.Builder
			for _, args := range tc.runs {
				if args[0] == "@hierarchy" {
					s.WriteFile(t, "diablo.config", s.ReadFile(t, "diablo.config")+"\nreadergrouphash hierarchy\n")
					continue
				}
				out := s.Run(t, "doverctl", append([]string{"-v"}, args...)...)
				all.WriteString(out)
			}
			check := func(over, data string) {
				t.Helper()
				for _, f := range []struct {
					path string
					want []byte
				}{{over, overBytes}, {data, dataBytes}} {
					got, err := os.ReadFile(f.path)
					if err != nil {
						t.Errorf("%s was removed or not moved: %v", strings.TrimPrefix(f.path, home), err)
					} else if !bytes.Equal(got, f.want) {
						t.Errorf("%s was changed", strings.TrimPrefix(f.path, home))
					}
				}
			}
			check(filepath.Join(home, "e2e", "o.0.e2e.test"), filepath.Join(home, "e2e", "d.256.e2e.test"))
			if strings.Contains(all.String(), "not found, removing") {
				t.Errorf("clean removed a file of the converted group:\n%s", all.String())
			}
			if tc.back {
				out := s.Run(t, "doverctl", "-v", "convert", "hierarchy", "crc", "clean")
				all.WriteString(out)
				check(crcOver, crcData)
				if strings.Contains(out, "not found, removing") {
					t.Errorf("clean removed a file after the conversion back:\n%s", out)
				}
			}
			if t.Failed() {
				t.Logf("doverctl output:\n%s", all.String())
			}
		})
	}
}
