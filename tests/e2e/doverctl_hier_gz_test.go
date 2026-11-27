package e2e

import (
	"bytes"
	"compress/gzip"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// hierGzBytes is a valid gzip file of 10 bytes, as dexpireover -z makes of a data file.
func hierGzBytes(t *testing.T) []byte {
	t.Helper()
	var b bytes.Buffer
	zw := gzip.NewWriter(&b)
	if _, err := zw.Write(bytes.Repeat([]byte("x"), 10)); err != nil {
		t.Fatal(err)
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	return b.Bytes()
}

// hierNewGroup adds a group with the article range 1..300 to the site.
func hierNewGroup(t *testing.T, s *Site, group string) {
	t.Helper()
	s.Run(t, "dgrpctl", "newgroup", group, "y")
	s.Run(t, "dgrpctl", "adjust", group, "NB", "=1")
	s.Run(t, "dgrpctl", "adjust", group, "NE", "=300")
}

// TestDoverctlCleanHierarchyGz (D-318): the compressed data file of the hierarchy layout is
// d.<artBase>.<group>.gz. The file name pattern "d.%lld.%[^/].gz" of ExtractGroupHashInfo() let
// "%[^/]" take the ".gz" into the group name (sscanf() does not count the literal after it), so
// clean looked up the group "<group>.gz", did not find it and removed every compressed
// hierarchy data file as "Group for over file not found" while it kept the over file.
//
// Input: "readergrouphash hierarchy", e2e.test with the range 1..300, its over file o.0.e2e.test,
// a plain data file d.0.e2e.test and a valid gzip d.256.e2e.test.gz; `doverctl -v clean`.
// Expected: the three files are there, unchanged, and nothing is reported as "not found,
// removing". Controls: a compressed file of a group that does not exist (d.256.e2e.nogroup.gz)
// is removed ("not found, removing"), and a compressed file outside the range of its group
// (d.1024.e2e.test.gz) is removed as "stale overview data", as the plain one is.
func TestDoverctlCleanHierarchyGz(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	e2e := filepath.Join(home, "e2e")
	gz := hierGzBytes(t)
	plain := bytes.Repeat([]byte("x"), 10)
	over := overHead("e2e.test")
	keep := []struct {
		path string
		want []byte
	}{
		{filepath.Join(e2e, "o.0.e2e.test"), over},
		{filepath.Join(e2e, "d.0.e2e.test"), plain},
		{filepath.Join(e2e, "d.256.e2e.test.gz"), gz},
	}
	for _, f := range keep {
		writeOver(t, f.path, f.want)
	}
	noGroup := filepath.Join(e2e, "d.256.e2e.nogroup.gz")
	stale := filepath.Join(e2e, "d.1024.e2e.test.gz")
	stalePlain := filepath.Join(e2e, "d.1024.e2e.test")
	writeOver(t, noGroup, gz)
	writeOver(t, stale, gz)
	writeOver(t, stalePlain, plain)

	out := s.Run(t, "doverctl", "-v", "clean")

	for _, f := range keep {
		got, err := os.ReadFile(f.path)
		if err != nil {
			t.Errorf("%s was removed: %v", filepath.Base(f.path), err)
		} else if !bytes.Equal(got, f.want) {
			t.Errorf("%s was changed", filepath.Base(f.path))
		}
	}
	for _, f := range []string{noGroup, stale, stalePlain} {
		if _, err := os.Stat(f); err == nil {
			t.Errorf("%s, a file of no group or outside the range, is still there", filepath.Base(f))
		}
	}
	if strings.Contains(out, "Group for over file not found, removing "+keep[2].path) {
		t.Errorf("clean took the compressed data file of e2e.test for a file of no group")
	}
	staleReported := false
	for _, line := range strings.Split(out, "\n") {
		if strings.HasPrefix(line, "Deleting stale overview data e2e.test: ") && strings.HasSuffix(line, stale) {
			staleReported = true
		}
	}
	if !staleReported {
		t.Errorf("the compressed file outside the range was not reported as stale overview data of e2e.test")
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out)
	}
}

// TestDoverctlCleanHierarchyGzGroupName (D-318): a group name may end in ".gz". In the hierarchy
// layout d.256.e2e.foo.gz is then the plain data file of the group e2e.foo.gz (in e2e/foo/) or
// the compressed data file of the group e2e.foo (in e2e/); the name alone does not tell. clean
// must keep the file as long as either group exists. (With a one-character last component,
// e2e.x and e2e.x.gz would share the directory e2e/ and so the path: see D-325.)
//
// Input: the groups e2e.foo and e2e.foo.gz (range 1..300 each), in three combinations: both,
// only e2e.foo, only e2e.foo.gz; each existing group with its over file and its data file
// (e2e.foo: the gzip e2e/d.256.e2e.foo.gz; e2e.foo.gz: the plain e2e/foo/d.256.e2e.foo.gz). The
// directories are taken from `dgrpctl listgrouphashfile` and checked to be e2e and e2e/foo, so
// the test cannot drift from dreaderd's layout. `doverctl -v clean`. Expected: all files of the
// existing groups are there, unchanged, nothing is reported as "not found, removing". Control: a
// stale data file of no group in the same directory is removed.
func TestDoverctlCleanHierarchyGzGroupName(t *testing.T) {
	for _, tc := range []struct {
		name       string
		hasX, hasZ bool
	}{
		{"both_groups", true, true},
		{"only_e2e.foo", true, false},
		{"only_e2e.foo.gz", false, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			// groupDir is the directory of a group's files below the group home, as dreaderd
			// puts them (the over file of the group, without its name).
			groupDir := func(group string) string {
				f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", group))
				if len(f) != 2 || f[1] != group || filepath.Base(f[0]) != "o.0."+group {
					t.Fatalf("dgrpctl listgrouphashfile %s: %q", group, f)
				}
				return filepath.Dir(f[0])
			}
			dirX, dirZ := groupDir("e2e.foo"), groupDir("e2e.foo.gz")
			if dirX != "e2e" || dirZ != "e2e/foo" {
				t.Fatalf("directories of e2e.foo and e2e.foo.gz: %q and %q, want e2e and e2e/foo", dirX, dirZ)
			}
			for _, d := range []string{dirX, dirZ} {
				if err := os.MkdirAll(filepath.Join(home, d), 0o755); err != nil {
					t.Fatal(err)
				}
			}
			gz := hierGzBytes(t)
			plain := bytes.Repeat([]byte("x"), 10)
			type file struct {
				path string
				want []byte
			}
			var keep []file
			if tc.hasX {
				hierNewGroup(t, s, "e2e.foo")
				keep = append(keep,
					file{filepath.Join(home, dirX, "o.0.e2e.foo"), overHead("e2e.foo")},
					file{filepath.Join(home, dirX, "d.256.e2e.foo.gz"), gz})
			}
			if tc.hasZ {
				hierNewGroup(t, s, "e2e.foo.gz")
				keep = append(keep,
					file{filepath.Join(home, dirZ, "o.0.e2e.foo.gz"), overHead("e2e.foo.gz")},
					file{filepath.Join(home, dirZ, "d.256.e2e.foo.gz"), plain})
			}
			for _, f := range keep {
				writeOver(t, f.path, f.want)
			}
			noGroup := filepath.Join(home, dirX, "d.256.e2e.nogroup.gz")
			writeOver(t, noGroup, gz)

			out := s.Run(t, "doverctl", "-v", "clean")

			for _, f := range keep {
				got, err := os.ReadFile(f.path)
				if err != nil {
					t.Errorf("%s was removed: %v", strings.TrimPrefix(f.path, home), err)
				} else if !bytes.Equal(got, f.want) {
					t.Errorf("%s was changed", strings.TrimPrefix(f.path, home))
				}
			}
			if _, err := os.Stat(noGroup); err == nil {
				t.Errorf("%s, a file of no group, is still there", filepath.Base(noGroup))
			}
			for _, line := range strings.Split(out, "\n") {
				if strings.Contains(line, "not found, removing") && !strings.HasSuffix(line, noGroup) {
					t.Errorf("clean removed a file of an existing group: %s", line)
				}
			}
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}
		})
	}
}
