package e2e

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// dexpireoverOverFile returns an over file for group with room for 500 article slots and an
// article 1 received now in its slot: for the range 1..300 and the 30 days of the default expire
// `dexpireover -a` has no reason to resize the file, to expire a slot or to move the group's
// first article number (without a valid first article it would set NB to 301 and the group's
// data files would be stale for a good reason).
func dexpireoverOverFile(group string) []byte {
	const slot = 32 // OverArt, dreaderd/defs.h
	b := append(overHeadMax(group, 500), make([]byte, 500*slot)...)
	o := b[len(b)-500*slot+slot:]                                    // slot 1: article 1 % 500
	binary.NativeEndian.PutUint32(o[0:], 1)                          // oa_ArtNo
	binary.NativeEndian.PutUint32(o[24:], uint32(time.Now().Unix())) // oa_TimeRcvd
	return b
}

// TestDexpireoverCrcHashCollision (D-313, the dexpireover side of #508): the crc file name hash
// of two groups can be the same; the iteration keeps their files apart, but a group that has no
// over file yet has iteration 0 like the group that holds the name. dexpireover -a (the daily
// run of samples/adm/daily.reader) took the first group in the active file with the hash and
// the iteration of a file as the file's group: when that was the empty one, the populated
// group's data file was removed as "stale overview data" (the empty group's range is empty),
// and the over file got "bad file header" (the header names another group), so no expire.
//
// Input: crcCollideA with an over file (header of A) and a data file (articles 256 to 511, in
// A's range 1..300), crcCollideB in the active file with no files; the two orders of their
// records in dactive.kp. Expected: `dexpireover -a -v` leaves both files of A with their
// content, and reports no removal and no bad header. The order with the populated group first
// always worked. A stale data file of a group that is not in the active file is the control: it
// is removed.
func TestDexpireoverCrcHashCollision(t *testing.T) {
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
			overBytes := dexpireoverOverFile(crcCollideA)
			dataBytes := bytes.Repeat([]byte("x"), 10)
			writeOver(t, over, overBytes)
			writeOver(t, data, dataBytes)
			writeOver(t, stale, dataBytes)

			out := s.Run(t, "dexpireover", "-a", "-v")

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
			// every report about a file of A is wrong; only the control file's removal is expected
			for _, l := range strings.Split(out, "\n") {
				for _, bad := range []string{"bad file header", "stale overview data", "not found, removing"} {
					if strings.Contains(l, bad) && !strings.Contains(l, filepath.Base(stale)) {
						t.Errorf("dexpireover reports: %s", l)
					}
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
}

// TestDexpireoverHierarchyIter (D-314, the dexpireover side of #508): the file names of the
// hierarchy layout have no iteration, but the group's ITER stays in dactive.kp (crc and md5
// names carry it, and a conversion back needs it). dexpireover -a looked a hierarchy file up
// with the group's old ITER, reported "Group for over file not found" and removed the over file
// of every group with ITER above 0 (the data files stay: dexpireover does not scan the data
// names of the hierarchy layout).
//
// Input: e2e.test with ITER 1 and crc files over.0.<hash>.1 and data.256.<hash>.1 (articles 256
// to 511, in the range 1..300); `doverctl convert crc hierarchy`, the site's readergrouphash
// set to hierarchy, then `dexpireover -a -v`. Expected: o.0.e2e.test and d.256.e2e.test are
// there with their content, nothing is removed, and the conversion back to crc finds the
// files under their old names.
func TestDexpireoverHierarchyIter(t *testing.T) {
	s, home := overSite(t, nil)
	db := filepath.Join(s.Dir, "dactive.kp")
	if o := dkpRun(t, "", "-w", db, "e2e.test", "ITER=000001"); strings.Contains(o, "rror") {
		t.Fatalf("dkp -w ITER: %s", o)
	}
	dir, hash := groupFiles(t, s, "e2e.test")
	if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
		t.Fatal(err)
	}
	overBytes := dexpireoverOverFile("e2e.test")
	dataBytes := bytes.Repeat([]byte("x"), 10)
	crcOver := filepath.Join(home, dir, "over.0."+hash+".1")
	crcData := filepath.Join(home, dir, "data.256."+hash+".1")
	writeOver(t, crcOver, overBytes)
	writeOver(t, crcData, dataBytes)

	conv := s.Run(t, "doverctl", "-v", "convert", "crc", "hierarchy")
	s.WriteFile(t, "diablo.config", s.ReadFile(t, "diablo.config")+"\nreadergrouphash hierarchy\n")
	out := s.Run(t, "dexpireover", "-a", "-v")

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
	if strings.Contains(out, "not found, removing") {
		t.Errorf("dexpireover removed a file of the converted group:\n%s", out)
	}
	back := s.Run(t, "doverctl", "-v", "convert", "hierarchy", "crc")
	check(crcOver, crcData)
	if t.Failed() {
		t.Logf("doverctl convert crc hierarchy:\n%s\ndexpireover -a -v:\n%s\ndoverctl convert hierarchy crc:\n%s", conv, out, back)
	}
}
