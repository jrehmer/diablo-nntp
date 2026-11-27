package e2e

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// cleanDirsOver returns the path of the over file of group below the group home, as
// `dgrpctl listgrouphashfile` gives it for the site's readergrouphash.
func cleanDirsOver(t *testing.T, s *Site, group string) string {
	t.Helper()
	f := strings.Fields(s.Run(t, "dgrpctl", "listgrouphashfile", group))
	if len(f) != 2 || f[1] != group {
		t.Fatalf("dgrpctl listgrouphashfile %s: %q", group, f)
	}
	return f[0]
}

// cleanDirsMD5Data turns the md5 over file name "<hash>.0.o.<bits>.0" into the name of the data
// file with the article base base: "<hash>.0.d.<bits>.<base>".
func cleanDirsMD5Data(t *testing.T, over string, base int) string {
	t.Helper()
	i := strings.LastIndex(over, ".o.")
	if i < 0 {
		t.Fatalf("%q is not an md5 over file name", over)
	}
	rest := over[i+3:]
	j := strings.Index(rest, ".")
	if j < 0 {
		t.Fatalf("%q is not an md5 over file name", over)
	}
	return fmt.Sprintf("%s.d.%s.%d", over[:i], rest[:j], base)
}

// TestDoverctlCleanHierarchyOneCharComponent (D-326): the hierarchy layout joins a one-character
// component to the next one, so the group a.bcd.efg lives in the directory a.bcd/ (its over file
// is a.bcd/o.0.a.bcd.efg). CleanSpool() entered only directories whose first two characters are
// alphanumeric: the second character of "a.bcd" is a dot, so stale files in it were never
// removed, while the same file in e2e/ was.
//
// Input: "readergrouphash hierarchy", the group a.bcd.efg (range 1..300) with its over file
// a.bcd/o.0.a.bcd.efg and a data file a.bcd/d.256.a.bcd.efg (the directory is taken from
// `dgrpctl listgrouphashfile` and checked to be a.bcd), and in the same directory a data file of
// no group (d.256.a.bcd.nogroup) and a data file of the group outside its range (d.1024); `doverctl
// -v clean`. Expected: the group's over and data files are there, unchanged; the two others are
// removed and reported.
func TestDoverctlCleanHierarchyOneCharComponent(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	hierGroupRange(t, s, "a.bcd.efg", "1", "300")
	dir := hierGroupDir(t, s, home, "a.bcd.efg")
	if dir != "a.bcd" {
		t.Fatalf("directory of a.bcd.efg: %q, want a.bcd", dir)
	}
	d := filepath.Join(home, dir)
	plain := bytes.Repeat([]byte("x"), 10)
	keep := []hierFile{
		{filepath.Join(d, "o.0.a.bcd.efg"), overHead("a.bcd.efg")},
		{filepath.Join(d, "d.256.a.bcd.efg"), plain},
	}
	noGroup := filepath.Join(d, "d.256.a.bcd.nogroup")
	outOfRange := filepath.Join(d, "d.1024.a.bcd.efg")
	hierWrite(t, keep)
	writeOver(t, noGroup, plain)
	writeOver(t, outOfRange, plain)

	out := s.Run(t, "doverctl", "-v", "clean")

	hierCheckKept(t, home, keep)
	for _, f := range []string{noGroup, outOfRange} {
		if _, err := os.Stat(f); err == nil {
			t.Errorf("%s, a stale file, is still there", filepath.Base(f))
		}
	}
	if !strings.Contains(out, "Group for over file not found, removing "+noGroup) {
		t.Errorf("clean did not report the file of no group")
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out)
	}
}

// TestDoverctlCleanMd5OneCharDirs (D-326, shown by go-while on the #511 head): with
// "readergrouphash md5-32\1" the layout directories have one character, and CleanSpool() scanned
// none of them.
//
// Input: the group e2e.test (range 1..300) with its over file and a data file in its one-character
// directory (from `dgrpctl listgrouphashfile`, checked to be one character), and in the same
// directory a data file of no group and one of the group outside its range; `doverctl -v clean`.
// Expected: the group's files are there, unchanged; the two others are removed and reported.
func TestDoverctlCleanMd5OneCharDirs(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": `md5-32\1`})
	over := cleanDirsOver(t, s, "e2e.test")
	dir, base := filepath.Split(over)
	dir = strings.TrimSuffix(dir, "/")
	if len(dir) != 1 {
		t.Fatalf("directory of e2e.test: %q, want one character (over file %s)", dir, over)
	}
	d := filepath.Join(home, dir)
	if err := os.MkdirAll(d, 0o755); err != nil {
		t.Fatal(err)
	}
	plain := bytes.Repeat([]byte("x"), 10)
	keep := []hierFile{
		{filepath.Join(d, base), overHead("e2e.test")},
		{filepath.Join(d, cleanDirsMD5Data(t, base, 256)), plain},
	}
	noGroup := filepath.Join(d, "zzzzzzzzzzzzzzzzzzzzzz.0.d.32.256")
	outOfRange := filepath.Join(d, cleanDirsMD5Data(t, base, 1024))
	hierWrite(t, keep)
	writeOver(t, noGroup, plain)
	writeOver(t, outOfRange, plain)

	out := s.Run(t, "doverctl", "-v", "clean")

	hierCheckKept(t, home, keep)
	for _, f := range []string{noGroup, outOfRange} {
		if _, err := os.Stat(f); err == nil {
			t.Errorf("%s, a stale file, is still there", filepath.Base(f))
		}
	}
	if !strings.Contains(out, "Group for over file not found, removing "+noGroup) {
		t.Errorf("clean did not report the file of no group")
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out)
	}
}

// TestDoverctlCleanLeavesForeignDirs (D-326, control): CleanSpool() enters the directories the
// layout can have made and no others, because scanDirectory() removes every entry of a directory
// that is not a group file. In the group home of each layout (crc, md5-32\1, hierarchy): lost+found
// and .hidden (a name starting with a dot is not a layout directory: dexpireover -R's junk files
// are such names), each with a file whose name no layout parses, and one with a subdirectory; and,
// where the name is not a directory of that layout, e2e (crc and md5), a three-character name
// (crc) and a two-character name (md5-32\1).
//
// Expected: every one of those files is still there after `doverctl -v clean`, and the stale file
// in the directory of e2e.test (a data file of no group) is removed, so the run did scan.
// lost+found fails on main (its first two characters are alphanumeric).
func TestDoverctlCleanLeavesForeignDirs(t *testing.T) {
	for _, tc := range []struct {
		name, hash string
		foreign    []string // top-level directories that are not layout directories
		stale      string   // name of a data file of no group
	}{
		{"crc", "", []string{"e2e", "abc"}, "data.256.deadbeef.cafebabe"},
		{"md5-32-1", `md5-32\1`, []string{"e2e", "ab"}, "zzzzzzzzzzzzzzzzzzzzzz.0.d.32.256"},
		{"hierarchy", "hierarchy", nil, "d.256.e2e.nogroup"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			cfg := map[string]string{}
			if tc.hash != "" {
				cfg["readergrouphash"] = tc.hash
			}
			s, home := overSite(t, cfg)
			dir := filepath.Dir(cleanDirsOver(t, s, "e2e.test"))
			if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
				t.Fatal(err)
			}
			stale := filepath.Join(home, dir, tc.stale)
			writeOver(t, stale, bytes.Repeat([]byte("x"), 10))
			var keep []string
			for _, d := range append([]string{"lost+found", ".hidden"}, tc.foreign...) {
				if err := os.MkdirAll(filepath.Join(home, d, "sub"), 0o755); err != nil {
					t.Fatal(err)
				}
				for _, f := range []string{filepath.Join(d, "junk.txt"), filepath.Join(d, "sub", "junk.txt")} {
					writeOver(t, filepath.Join(home, f), []byte("keep me"))
					keep = append(keep, filepath.Join(home, f))
				}
			}

			out := s.Run(t, "doverctl", "-v", "clean")

			for _, f := range keep {
				if _, err := os.Stat(f); err != nil {
					t.Errorf("%s was removed", strings.TrimPrefix(f, home))
				}
			}
			if _, err := os.Stat(stale); err == nil {
				t.Errorf("%s, a data file of no group in a layout directory, is still there", strings.TrimPrefix(stale, home))
			}
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}
		})
	}
}

// cleanDirsPlusMinusGroup returns a group name whose md5-64 hash starts with '+' or '-' (the
// base64 alphabet of the layout has both), so that with "md5-64\1" its directory is "+" or "-".
func cleanDirsPlusMinusGroup(t *testing.T, s *Site) (group, over string) {
	t.Helper()
	for i := 0; i < 2000; i++ {
		group = fmt.Sprintf("e2e.pm%d", i)
		over = cleanDirsOver(t, s, group)
		if strings.HasPrefix(over, "+/") || strings.HasPrefix(over, "-/") {
			return group, over
		}
	}
	t.Fatal("no group name with a '+' or '-' directory in 2000 tries")
	return "", ""
}

// TestOverToolsMd564PlusMinusDir (D-326): with "readergrouphash md5-64\1" a group whose hash starts
// with '+' or '-' has a directory of that name (the layout's alphabet is [a-zA-Z0-9+-], 2 of 64
// groups). doverctl clean and dexpireover -a entered only directories that start with an
// alphanumeric character, so they never touched the files of those groups.
//
// Input: a group of such a name (found with `dgrpctl listgrouphashfile`, range 1..300) with its
// over file and a data file, and in the same directory a data file of no group;
// `doverctl -v clean` and `dexpireover -a -v` in a case each. Expected: the group's files are
// there, unchanged; the file of no group is removed and reported.
func TestOverToolsMd564PlusMinusDir(t *testing.T) {
	for _, tool := range []struct {
		name string
		args []string
	}{
		{"doverctl", []string{"doverctl", "-v", "clean"}},
		{"dexpireover", []string{"dexpireover", "-a", "-v"}},
	} {
		t.Run(tool.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": `md5-64\1`})
			group, over := cleanDirsPlusMinusGroup(t, s)
			hierGroupRange(t, s, group, "1", "300")
			dir, base := filepath.Split(over)
			d := filepath.Join(home, dir)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			keep := []hierFile{
				{filepath.Join(d, base), hierOverFile(group, 1, 500)},
				{filepath.Join(d, cleanDirsMD5Data(t, base, 256)), bytes.Repeat([]byte("x"), 10)},
			}
			noGroup := filepath.Join(d, "zzzzzzzzzzzzzzzzzzzzzz.0.d.64.256")
			hierWrite(t, keep)
			writeOver(t, noGroup, bytes.Repeat([]byte("x"), 10))

			out := s.Run(t, tool.args[0], tool.args[1:]...)

			hierCheckKept(t, home, keep)
			hierCheckGone(t, home, out, []string{noGroup}, true)
			if t.Failed() {
				t.Logf("%s:\n%s", strings.Join(tool.args, " "), out)
			}
		})
	}
}
