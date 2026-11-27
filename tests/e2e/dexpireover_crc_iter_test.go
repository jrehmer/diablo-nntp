package e2e

import (
	"bytes"
	"encoding/binary"
	"fmt"
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

// TestDexpireoverWildEmptyGroup (D-313, the -w case of #508): with -w only the groups that match
// the wildcard are loaded, so for the files of a populated group A whose name does not match,
// the empty group B with the same crc hash and iteration 0 was the only candidate, and the
// single candidate was taken without a look at the over file: A's data file was removed as
// "stale overview data" (B's range is empty) and A's over file got "bad file header". The files
// of a group that does not match are not for this run to touch.
//
// Input: the colliding pair of TestDexpireoverCrcHashCollision (A with over and data file, B
// with none; the order in dactive.kp does not matter, only the group that matches is loaded), then `dexpireover -a -v -w <B>`. Expected: A's two files
// are unchanged, and nothing is reported about them. Control: `-w <A>` keeps them too, and a
// stale data file of a group that is not in the active file is not removed under -w.
func TestDexpireoverWildEmptyGroup(t *testing.T) {
	for _, tc := range []struct {
		name string
		wild string
	}{
		{"wild_empty_group", crcCollideB},
		{"wild_populated_group", crcCollideA},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			dirA, hashA := groupFiles(t, s, crcCollideA)
			dirB, hashB := groupFiles(t, s, crcCollideB)
			if dirA != dirB || hashA != hashB {
				t.Fatalf("test setup: %s and %s do not share a file name hash: %s/%s and %s/%s",
					crcCollideA, crcCollideB, dirA, hashA, dirB, hashB)
			}
			s.Run(t, "dgrpctl", "newgroup", crcCollideB, "y")
			s.Run(t, "dgrpctl", "newgroup", crcCollideA, "y")
			s.Run(t, "dgrpctl", "adjust", crcCollideA, "NB", "=1")
			s.Run(t, "dgrpctl", "adjust", crcCollideA, "NE", "=300")

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

			out := s.Run(t, "dexpireover", "-a", "-v", "-w", tc.wild)

			for _, f := range []struct {
				path string
				want []byte
			}{{over, overBytes}, {data, dataBytes}, {stale, dataBytes}} {
				got, err := os.ReadFile(f.path)
				if err != nil {
					t.Errorf("%s was removed with -w %s: %v", filepath.Base(f.path), tc.wild, err)
				} else if !bytes.Equal(got, f.want) {
					t.Errorf("%s was changed with -w %s", filepath.Base(f.path), tc.wild)
				}
			}
			for _, l := range strings.Split(out, "\n") {
				for _, bad := range []string{"bad file header", "stale overview data", "not found, removing"} {
					if strings.Contains(l, bad) {
						t.Errorf("dexpireover reports: %s", l)
					}
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -v -w %s:\n%s", tc.wild, out)
			}
		})
	}
}

// Three group names whose file name hash is the same with `readergrouphash md5-32:2/256` (the
// two bytes are one character of the base32 md5 and the terminating NUL: 32 hashes, so
// collisions are easy). e2e.m0015 has files, the other two are empty. The wildcard '????????1'
// selects the two empty ones (nine characters, the last one 1) and not e2e.m0015. The test
// checks with `dgrpctl listgrouphashfile` that they still share directory and hash.
const (
	md5CollidePopulated = "e2e.m0015"
	md5CollideEmpty1    = "e2e.m0021"
	md5CollideEmpty2    = "e2e.m0031"
)

// TestDexpireoverWildTwoEmptyColliders (D-322, the -w case of D-313 with two selected groups):
// with -w only the groups that match are loaded. With two or more loaded groups that fit a
// file's hash and iteration and none named in the over file's header, FindGroupByHash() fell
// back to the first of them, as without -w; but under -w the owner may simply not be loaded.
// The range of an empty selected group was then applied to the files of the unselected
// populated group: its over file was reported "bad file header" and its data file was removed as
// "stale overview data" (a single selected candidate was already left alone, #508).
//
// Input: `readergrouphash md5-32:2/256`; e2e.m0015 (NB=1, NE=300) with a valid over file naming
// it and a data file (articles 256 to 511); e2e.m0021 and e2e.m0031 empty (NB=1, NE=0), all
// three with the same hash and directory; `dexpireover -a -v -w '????????1'` (the two empty
// groups). Expected: the over file and the data file of e2e.m0015 are unchanged, nothing is
// reported about them as bad or stale, and with -v the skip is said. Controls: `-w e2e.m0015`
// and no -w keep the files too (the owner is named in the header; the same as before).
func TestDexpireoverWildTwoEmptyColliders(t *testing.T) {
	for _, tc := range []struct {
		name     string
		wild     string // "" = no -w
		wantSkip bool   // the -v line that says the files are left alone
	}{
		{"wild_two_empty_groups", "????????1", true},
		{"wild_populated_group", md5CollidePopulated, false},
		{"no_wild", "", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "md5-32:2/256"})
			// md5 names: <hash>.<iteration>.o.32.<first article> and ...d.32...; the hash
			// is one character of the md5 here (two bytes with the terminating NUL)
			overRel := func(g string) string {
				f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", g))
				if len(f) != 2 || !strings.HasSuffix(f[0], ".0.o.32.0") {
					t.Fatalf("dgrpctl listgrouphashfile %s: %q", g, f)
				}
				return f[0]
			}
			rel := overRel(md5CollidePopulated)
			for _, g := range []string{md5CollideEmpty1, md5CollideEmpty2} {
				if r := overRel(g); r != rel {
					t.Fatalf("test setup: %s and %s do not share a file name hash: %s and %s",
						md5CollidePopulated, g, rel, r)
				}
			}
			s.Run(t, "dgrpctl", "newgroup", md5CollidePopulated, "y")
			s.Run(t, "dgrpctl", "adjust", md5CollidePopulated, "NB", "=1")
			s.Run(t, "dgrpctl", "adjust", md5CollidePopulated, "NE", "=300")
			s.Run(t, "dgrpctl", "newgroup", md5CollideEmpty1, "y")
			s.Run(t, "dgrpctl", "newgroup", md5CollideEmpty2, "y")

			if err := os.MkdirAll(filepath.Join(home, filepath.Dir(rel)), 0o755); err != nil {
				t.Fatal(err)
			}
			over := filepath.Join(home, rel)
			data := filepath.Join(home, strings.TrimSuffix(rel, ".o.32.0")+".d.32.256")
			overBytes := dexpireoverOverFile(md5CollidePopulated)
			dataBytes := bytes.Repeat([]byte("x"), 10)
			writeOver(t, over, overBytes)
			writeOver(t, data, dataBytes)

			args := []string{"-a", "-v"}
			if tc.wild != "" {
				args = append(args, "-w", tc.wild)
			}
			out := s.Run(t, "dexpireover", args...)

			for _, f := range []struct {
				path string
				want []byte
			}{{over, overBytes}, {data, dataBytes}} {
				got, err := os.ReadFile(f.path)
				if err != nil {
					t.Errorf("%s of %s was removed: %v", filepath.Base(f.path), md5CollidePopulated, err)
				} else if !bytes.Equal(got, f.want) {
					t.Errorf("%s of %s was changed", filepath.Base(f.path), md5CollidePopulated)
				}
			}
			for _, l := range strings.Split(out, "\n") {
				for _, bad := range []string{"bad file header", "stale overview data", "not found, removing"} {
					if strings.Contains(l, bad) {
						t.Errorf("dexpireover reports: %s", l)
					}
				}
			}
			if tc.wantSkip && !strings.Contains(out, "left alone") {
				t.Errorf("dexpireover -v does not say that the files are left alone")
			}
			if t.Failed() {
				t.Logf("dexpireover %s:\n%s", strings.Join(args, " "), out)
			}
		})
	}
}

// TestDexpireoverWildOverFileUnusable (D-313 docs, go-while #508 r3): under -w a selected group
// whose own over file is missing, short, of the wrong byte order or of a bad version is skipped
// without a message about its data files ("bad file header", "stale overview data"), and its
// out-of-range data files stay until the next `-a` (without -w the group is read and the file
// is its own). With -v the skip is said.
//
// Input: e2e.test (NB=1, NE=300) with a data file for articles 4096 to 4351 (out of range) and
// each of the four over files; `dexpireover -a -v -w e2e.test`. Expected: the data file is
// still there, the output has no "bad file header" or "stale overview data" and says the files
// are left alone. Control: with a good over file of the group the same data file is removed as
// stale.
func TestDexpireoverWildOverFileUnusable(t *testing.T) {
	good := dexpireoverOverFile("e2e.test")
	short := good[:10]
	badOrder := append([]byte(nil), good...)
	binary.NativeEndian.PutUint32(badOrder[4:], 0xC4D3E2F1) // the byte order of the other kind
	badVersion := append([]byte(nil), good...)
	binary.NativeEndian.PutUint32(badVersion[0:], 99)
	for _, tc := range []struct {
		name string
		over []byte // nil: no over file
		kept bool
	}{
		{"missing", nil, true},
		{"short", short, true},
		{"wrong_byte_order", badOrder, true},
		{"bad_version", badVersion, true},
		{"good_over_file_control", good, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			dir, hash := groupFiles(t, s, "e2e.test")
			if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
				t.Fatal(err)
			}
			if tc.over != nil {
				writeOver(t, filepath.Join(home, dir, "over.0."+hash), tc.over)
			}
			data := filepath.Join(home, dir, "data.4096."+hash)
			writeOver(t, data, bytes.Repeat([]byte("x"), 10))

			out := s.Run(t, "dexpireover", "-a", "-v", "-w", "e2e.test")

			_, err := os.Stat(data)
			if tc.kept && err != nil {
				t.Errorf("out-of-range data file of a group with an unusable over file was removed: %v", err)
			}
			if !tc.kept && err == nil {
				t.Errorf("control: the out-of-range data file of a group with a good over file is still there")
			}
			if tc.kept {
				for _, bad := range []string{"bad file header", "stale overview data"} {
					if strings.Contains(out, bad) {
						t.Errorf("dexpireover reports %q:\n%s", bad, out)
					}
				}
				if !strings.Contains(out, "left alone") {
					t.Errorf("dexpireover -v does not say that the files are left alone")
				}
			}
			if t.Failed() {
				t.Logf("dexpireover -a -v -w e2e.test:\n%s", out)
			}
		})
	}
}

// TestDexpireoverNewIterBeforeIterWrite (D-319): a group that is about to change its iteration
// (its hash is used by another group) gets files of the next iteration first, and dreaderd
// writes ITER into dactive.kp only afterwards; the write can also be lost. dexpireover -a keeps
// a file whose group is not in its table only when the live ITER equals the file's iteration
// (the group removed and created again during the run); with ITER still below it, the new
// iteration's files were removed as "Group for over file not found". doverctl's
// IterInProgress() keeps a file when the live ITER is at most its iteration.
//
// Input: e2e.test (NB=1, NE=300) with ITER=L in dactive.kp and, for the iteration F, the over
// file (header names e2e.test) and a data file of F in the crc layout (`over.0.<hash>.F`,
// `data.256.<hash>.F`); `dexpireover -a -v`. Expected for F above L (L=0, F=1; L=1, F=2): both
// files stay with their content, nothing is removed. Control, F below L (L=2, F=1): files of an
// iteration the group has already left are removed ("Group for over file not found").
func TestDexpireoverNewIterBeforeIterWrite(t *testing.T) {
	for _, tc := range []struct {
		name string
		live int // ITER in dactive.kp
		file int // iteration of the files
		kept bool
	}{
		{"new_iteration_before_iter_write", 0, 1, true},
		{"new_iteration_ahead_of_iter_1", 1, 2, true},
		{"older_iteration_control", 2, 1, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			db := filepath.Join(s.Dir, "dactive.kp")
			if tc.live > 0 {
				if o := dkpRun(t, "", "-w", db, "e2e.test", fmt.Sprintf("ITER=%06d", tc.live)); strings.Contains(o, "rror") {
					t.Fatalf("dkp -w ITER: %s", o)
				}
			}
			dir, hash := groupFiles(t, s, "e2e.test")
			if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
				t.Fatal(err)
			}
			over := filepath.Join(home, dir, fmt.Sprintf("over.0.%s.%d", hash, tc.file))
			data := filepath.Join(home, dir, fmt.Sprintf("data.256.%s.%d", hash, tc.file))
			overBytes := dexpireoverOverFile("e2e.test")
			dataBytes := bytes.Repeat([]byte("x"), 10)
			writeOver(t, over, overBytes)
			writeOver(t, data, dataBytes)

			out := s.Run(t, "dexpireover", "-a", "-v")

			for _, f := range []struct {
				path string
				want []byte
			}{{over, overBytes}, {data, dataBytes}} {
				got, err := os.ReadFile(f.path)
				switch {
				case tc.kept && err != nil:
					t.Errorf("%s of iteration %d (ITER %d) was removed: %v", filepath.Base(f.path), tc.file, tc.live, err)
				case tc.kept && !bytes.Equal(got, f.want):
					t.Errorf("%s of iteration %d (ITER %d) was changed", filepath.Base(f.path), tc.file, tc.live)
				case !tc.kept && err == nil:
					t.Errorf("control: %s of iteration %d (ITER %d) is still there", filepath.Base(f.path), tc.file, tc.live)
				}
			}
			if tc.kept && strings.Contains(out, "not found, removing") {
				t.Errorf("dexpireover removes files of the new iteration:\n%s", out)
			}
			if !tc.kept && !strings.Contains(out, "Group for over file not found, removing") {
				t.Errorf("control: no \"Group for over file not found\" for the old iteration's files")
			}
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
}
