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
// and "AB" (crc: GFName() prints "%02x", lowercase), and a two-character name and "6" and "Q"
// (md5-32\1: the base32 alphabet of the hash is [a-z0-5]).
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
		{"crc", "", []string{"e2e", "abc", "AB"}, "data.256.deadbeef.cafebabe"},
		{"md5-32-1", `md5-32\1`, []string{"e2e", "ab", "6", "Q"}, "zzzzzzzzzzzzzzzzzzzzzz.0.d.32.256"},
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

// TestDexpireoverMd564SignSymlink (D-326, the #515 r2 review): dexpireover now also scans a
// top-level directory of the group home whose name starts with '+' or '-' (md5-64), and it removes
// every over or data file of no group in a directory it scans. Such a name must be a real
// directory: a symlink of it is not followed. A symlink whose name starts with an alphanumeric
// character is followed, as on main (stat()).
//
// Input: readergrouphash md5-64\1; the symlink "+" (or "-" when "+" is e2e.test's directory)
// points to a directory outside the group home that holds an over file and a data file of
// e2e.absent, a group not in the active file; a one-character alphanumeric layout name (not
// e2e.test's directory) is a symlink to a second outside directory holding a data file of no
// group; e2e.test's directory holds a data file of no group; `dexpireover -a -v`. Expected: the
// files of e2e.absent are there, unchanged; the "+" link is still there and points to its
// directory; dexpireover reports "Skipping symlink <path>:"; the two data files of no group are
// removed and reported (the scan ran, and followed the alphanumeric link). **Fails** on 8e197418
// (#515 r2): the "+" link is followed and the files of e2e.absent are removed. On main the files
// and the link are kept too (main skipped every name starting with '+'); it fails there only for
// the missing "Skipping symlink" line.
func TestDexpireoverMd564SignSymlink(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": `md5-64\1`})
	dir := filepath.Dir(cleanDirsOver(t, s, "e2e.test"))
	link, alnum := "+", "x"
	if dir == link {
		link = "-"
	}
	if dir == alnum {
		alnum = "y"
	}
	if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
		t.Fatal(err)
	}
	stale := filepath.Join(home, dir, "zzzzzzzzzzzzzzzzzzzzzz.0.d.64.256")
	writeOver(t, stale, bytes.Repeat([]byte("x"), 10))

	outside := t.TempDir()
	absent := filepath.Base(cleanDirsOver(t, s, "e2e.absent"))
	keep := []hierFile{
		{filepath.Join(outside, absent), hierOverFile("e2e.absent", 1, 500)},
		{filepath.Join(outside, cleanDirsMD5Data(t, absent, 256)), bytes.Repeat([]byte("x"), 10)},
	}
	hierWrite(t, keep)
	linkPath := filepath.Join(home, link)
	if err := os.Symlink(outside, linkPath); err != nil {
		t.Fatal(err)
	}

	outside2 := t.TempDir()
	stale2 := filepath.Join(outside2, "yyyyyyyyyyyyyyyyyyyyyy.0.d.64.256")
	writeOver(t, stale2, bytes.Repeat([]byte("x"), 10))
	alnumPath := filepath.Join(home, alnum)
	if err := os.Symlink(outside2, alnumPath); err != nil {
		t.Fatal(err)
	}

	out := s.Run(t, "dexpireover", "-a", "-v")

	hierCheckKept(t, outside, keep)
	cleanDirsIsLink(t, home, linkPath, outside)
	if !strings.Contains(out, "Skipping symlink "+linkPath+":") {
		t.Errorf("dexpireover did not report that it skipped the symlink %s", link)
	}
	hierCheckGone(t, home, out, []string{stale}, true)
	// The alphanumeric link is followed: the path is the one through the link.
	hierCheckGone(t, home, out, []string{filepath.Join(alnumPath, filepath.Base(stale2))}, true)
	cleanDirsIsLink(t, home, alnumPath, outside2)
	if t.Failed() {
		t.Logf("dexpireover -a -v:\n%s", out)
	}
}

// cleanDirsIsLink reports an error unless path is still a symlink to target.
func cleanDirsIsLink(t *testing.T, home, path, target string) {
	t.Helper()
	got, err := os.Readlink(path)
	if err != nil {
		t.Errorf("%s is no longer a symlink: %v", strings.TrimPrefix(path, home), err)
	} else if got != target {
		t.Errorf("%s points to %s, want %s", strings.TrimPrefix(path, home), got, target)
	}
}

// cleanDirsRun runs `doverctl -v clean` and reports a non-zero exit as an error (not fatal, so the
// test still checks the files).
func cleanDirsRun(t *testing.T, s *Site) string {
	t.Helper()
	out, err := s.TryRun("doverctl", "-v", "clean")
	if err != nil {
		t.Errorf("doverctl -v clean: %v", err)
	}
	return out
}

// TestDoverctlCleanSkipsNewNameSymlink (D-326, the #515 r1 review): a top-level entry of the group
// home whose name only the layout rule accepts (not two alphanumerics, which main required: a
// one-character md5-32\1 directory, a hierarchy "a.bcd") is scanned only when it is a real
// directory. scanDirectory() removes every file it does not parse, so a symlink of such a name
// that points out of the group home must not be followed. A symlink of a two-alphanumeric name is
// still followed (TestDoverctlCleanFollowsOldNameSymlink).
//
// Input: md5-32\1 with the symlink "q" (or "r" when "q" is e2e.test's directory), and hierarchy
// with the symlink "a.bcd"; each points to a directory outside the group home that holds junk.txt
// (no layout parses that name); a data file of no group in the directory of e2e.test;
// `doverctl -v clean`. Expected: junk.txt is there, unchanged; the symlink is still there and
// still points to the directory; clean reports "Skipping symlink <path>"; the stale file is
// removed (so the run did scan) and doverctl exits 0. **Fails** on 0aa64066 (#515 r1): the link
// is followed and junk.txt removed.
func TestDoverctlCleanSkipsNewNameSymlink(t *testing.T) {
	for _, tc := range []struct {
		name, hash, link, stale string
	}{
		{"md5-32-1", `md5-32\1`, "q", "zzzzzzzzzzzzzzzzzzzzzz.0.d.32.256"},
		{"hierarchy", "hierarchy", "a.bcd", "d.256.e2e.nogroup"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": tc.hash})
			dir := filepath.Dir(cleanDirsOver(t, s, "e2e.test"))
			if dir == tc.link {
				tc.link = "r"
			}
			if err := os.MkdirAll(filepath.Join(home, dir), 0o755); err != nil {
				t.Fatal(err)
			}
			stale := filepath.Join(home, dir, tc.stale)
			writeOver(t, stale, bytes.Repeat([]byte("x"), 10))
			outside := t.TempDir()
			junk := filepath.Join(outside, "junk.txt")
			writeOver(t, junk, []byte("keep me"))
			link := filepath.Join(home, tc.link)
			if err := os.Symlink(outside, link); err != nil {
				t.Fatal(err)
			}

			out := cleanDirsRun(t, s)

			if got, err := os.ReadFile(junk); err != nil || string(got) != "keep me" {
				t.Errorf("junk.txt in the directory the symlink %s points to was removed or changed (%v)", tc.link, err)
			}
			cleanDirsIsLink(t, home, link, outside)
			if !strings.Contains(out, "Skipping symlink "+link+":") {
				t.Errorf("clean did not report that it skipped the symlink %s", tc.link)
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

// TestDoverctlCleanFollowsOldNameSymlink (D-326, the #515 r1 review, control): a top-level
// directory of a two-alphanumeric name that is a symlink is followed, as on main (an admin can
// put directories on other disks that way), and the scan comes back to the group home: it
// returns with chdir() to the directory it came from, not chdir("..") (the parent of the link's
// target), and it does not remove the link (remove() of a symlink unlinks it).
//
// Input: readergrouphash crc; the directory of e2e.test (from `dgrpctl listgrouphashfile`) is a
// symlink to outside/<dir>, which holds e2e.test's over file and a data file of no group; a second
// layout name is a symlink to outside/<name> holding only a data file of no group; a third layout
// name is a real directory of the group home holding a data file of no group; `doverctl -v clean`.
// Expected: the over file is there, unchanged; the three data files of no group are removed and
// reported; both symlinks are still there and point to their directories; outside/<name>, now
// empty, is still there; doverctl exits 0. **Fails** on main and on 0aa64066: after the first
// link the scan goes on in outside/, and remove(<name>) there removes outside/<name> (the link's
// target) once it is empty. Whether the real directory is scanned there depends on the order of
// readdir().
func TestDoverctlCleanFollowsOldNameSymlink(t *testing.T) {
	s, home := overSite(t, nil)
	dir, base := filepath.Split(cleanDirsOver(t, s, "e2e.test"))
	dir = strings.TrimSuffix(dir, "/")
	var names []string // two other crc layout names
	for _, n := range []string{"ab", "cd", "ef"} {
		if n != dir && len(names) < 2 {
			names = append(names, n)
		}
	}
	outside := t.TempDir()
	plain := bytes.Repeat([]byte("x"), 10)

	groupTarget := filepath.Join(outside, dir)
	emptyTarget := filepath.Join(outside, names[0])
	realDir := filepath.Join(home, names[1])
	for _, d := range []string{groupTarget, emptyTarget, realDir} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	groupLink := filepath.Join(home, dir)
	emptyLink := filepath.Join(home, names[0])
	for _, l := range [][2]string{{groupTarget, groupLink}, {emptyTarget, emptyLink}} {
		if err := os.Symlink(l[0], l[1]); err != nil {
			t.Fatal(err)
		}
	}
	keep := []hierFile{{filepath.Join(groupLink, base), overHead("e2e.test")}}
	stale := []string{
		filepath.Join(groupLink, "data.256.deadbeef.cafebabe"),
		filepath.Join(emptyLink, "data.256.deadbeef.cafebab0"),
		filepath.Join(realDir, "data.256.deadbeef.cafebab1"),
	}
	hierWrite(t, keep)
	for _, f := range stale {
		writeOver(t, f, plain)
	}

	out := cleanDirsRun(t, s)

	hierCheckKept(t, home, keep)
	hierCheckGone(t, home, out, stale, true)
	cleanDirsIsLink(t, home, groupLink, groupTarget)
	cleanDirsIsLink(t, home, emptyLink, emptyTarget)
	if st, err := os.Stat(emptyTarget); err != nil || !st.IsDir() {
		t.Errorf("%s, the target of the symlink %s, was removed (%v)", emptyTarget, names[0], err)
	}
	if t.Failed() {
		t.Logf("doverctl -v clean:\n%s", out)
	}
}

// TestDoverctlCleanMd5NumBuckets (D-326, the #515 r1 review): with "readergrouphash md5-32/N"
// GFName() names a first-level directory "%0Wx" of hashNum() % N, W the decimal digits of N-1
// (at least 1): md5-32/1 makes only "0", md5-32/16 makes "00" to "0f". A directory of the right
// width whose value is N or more, or with uppercase hex digits, is not the layout's and is not
// entered.
//
// Input: md5-32/1 and md5-32/16; the directory of e2e.test (from `dgrpctl listgrouphashfile`,
// checked to be "0" and of two characters) with its over file and a data file of no group; the
// directories "f" and "1" (md5-32/1), "10", "ff" and "0A" (md5-32/16), each with junk.txt;
// `doverctl -v clean`. Expected: the over file is there, unchanged; the data file of no group is
// removed and reported; every junk.txt is still there. **Fails** on 0aa64066 (#515 r1): only the
// width was checked, so "f", "1", "10" and "ff" are entered and junk.txt removed.
func TestDoverctlCleanMd5NumBuckets(t *testing.T) {
	for _, tc := range []struct {
		name, hash string
		width      int
		foreign    []string
	}{
		{"md5-32-1", "md5-32/1", 1, []string{"f", "1"}},
		{"md5-32-16", "md5-32/16", 2, []string{"10", "ff", "0A"}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": tc.hash})
			over := cleanDirsOver(t, s, "e2e.test")
			dir, base := filepath.Split(over)
			dir = strings.TrimSuffix(dir, "/")
			if len(dir) != tc.width || (tc.width == 1 && dir != "0") {
				t.Fatalf("directory of e2e.test: %q, want %d characters (over file %s)", dir, tc.width, over)
			}
			d := filepath.Join(home, dir)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			keep := []hierFile{{filepath.Join(d, base), overHead("e2e.test")}}
			noGroup := filepath.Join(d, "zzzzzzzzzzzzzzzzzzzzzz.0.d.32.256")
			hierWrite(t, keep)
			writeOver(t, noGroup, bytes.Repeat([]byte("x"), 10))
			var junk []string
			for _, f := range tc.foreign {
				if err := os.MkdirAll(filepath.Join(home, f), 0o755); err != nil {
					t.Fatal(err)
				}
				j := filepath.Join(home, f, "junk.txt")
				writeOver(t, j, []byte("keep me"))
				junk = append(junk, j)
			}

			out := cleanDirsRun(t, s)

			hierCheckKept(t, home, keep)
			hierCheckGone(t, home, out, []string{noGroup}, true)
			for _, j := range junk {
				if _, err := os.Stat(j); err != nil {
					t.Errorf("%s was removed", strings.TrimPrefix(j, home))
				}
			}
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}
		})
	}
}

// cleanDirsTools are the two runs that scan the group home: `doverctl -v clean` and
// `dexpireover -a -v`.
var cleanDirsTools = []struct {
	name string
	args []string
}{
	{"doverctl", []string{"doverctl", "-v", "clean"}},
	{"dexpireover", []string{"dexpireover", "-a", "-v"}},
}

// TestOverToolsMd564SignDirChildSymlink (D-326, the #515 r3 review): doverctl clean and
// dexpireover -a now enter a top-level directory "+" or "-" of the group home (md5-64\1), which
// main skipped, and since r2 such a name must be a real directory. Below it a child that is a
// symlink to a directory is not followed either: main never scanned that tree, and both tools
// remove every over or data file of no group in a directory they scan.
//
// Input: readergrouphash md5-64\1; the real directory "+" (or "-" when "+" is e2e.test's
// directory) holds a data file of no group and the symlink "sub" to a directory outside the
// group home that holds the over and data files of e2e.absent, a group not in the active file;
// `doverctl -v clean` and `dexpireover -a -v` in a case each. Expected: the files of e2e.absent
// are there, unchanged; "sub" is still a symlink to that directory; the tool reports
// "Skipping symlink <path>/sub:"; the data file of no group in "+" is removed and reported (the
// directory was scanned). **Fails** on d7ed0b9f (#515 r3) for both tools: the link is followed
// and the files of e2e.absent are removed. On main the files and the link are kept (it skipped
// "+"); it fails there for the data file of no group in "+" and the missing "Skipping symlink"
// line.
func TestOverToolsMd564SignDirChildSymlink(t *testing.T) {
	for _, tool := range cleanDirsTools {
		t.Run(tool.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": `md5-64\1`})
			sign := "+"
			if filepath.Dir(cleanDirsOver(t, s, "e2e.test")) == sign {
				sign = "-"
			}
			d := filepath.Join(home, sign)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			stale := filepath.Join(d, "zzzzzzzzzzzzzzzzzzzzzz.0.d.64.256")
			writeOver(t, stale, bytes.Repeat([]byte("x"), 10))

			outside := t.TempDir()
			absent := filepath.Base(cleanDirsOver(t, s, "e2e.absent"))
			keep := []hierFile{
				{filepath.Join(outside, absent), hierOverFile("e2e.absent", 1, 500)},
				{filepath.Join(outside, cleanDirsMD5Data(t, absent, 256)), bytes.Repeat([]byte("x"), 10)},
			}
			hierWrite(t, keep)
			link := filepath.Join(d, "sub")
			if err := os.Symlink(outside, link); err != nil {
				t.Fatal(err)
			}

			out, err := s.TryRun(tool.args[0], tool.args[1:]...)
			if err != nil {
				t.Errorf("%s: %v", strings.Join(tool.args, " "), err)
			}

			hierCheckKept(t, outside, keep)
			cleanDirsIsLink(t, home, link, outside)
			if !strings.Contains(out, "Skipping symlink "+link+":") {
				t.Errorf("%s did not report that it skipped the symlink %s/sub", tool.name, sign)
			}
			hierCheckGone(t, home, out, []string{stale}, true)
			if t.Failed() {
				t.Logf("%s:\n%s", strings.Join(tool.args, " "), out)
			}
		})
	}
}

// TestDoverctlCleanNewNameDirChildSymlink (D-326, the #515 r3 review): below a top-level directory
// that only the layout rule accepts (not two alphanumerics, which main required: a one-character
// md5-32\1 directory, a hierarchy "a.bcd"), doverctl clean does not follow a child symlink to a
// directory; main never entered such a tree.
//
// Input: md5-32\1 with the real directory "q" (or "r" when "q" is e2e.test's directory), and
// hierarchy with the real directory "a.bcd"; each holds a data file of no group and the symlink
// "sub" to a directory outside the group home that holds junk.txt and a data file of no group;
// `doverctl -v clean`. Expected: both outside files are there, unchanged; "sub" is still a
// symlink to that directory; clean reports "Skipping symlink <path>/sub:"; the data file of no
// group in the top-level directory is removed and reported, and doverctl exits 0. **Fails** on
// d7ed0b9f (#515 r3): the link is followed and both outside files are removed. On main the
// outside files and the link are kept (the directory is not scanned); it fails there for the
// data file of no group and the missing "Skipping symlink" line.
func TestDoverctlCleanNewNameDirChildSymlink(t *testing.T) {
	for _, tc := range []struct {
		name, hash, dir, stale string
	}{
		{"md5-32-1", `md5-32\1`, "q", "zzzzzzzzzzzzzzzzzzzzzz.0.d.32.256"},
		{"hierarchy", "hierarchy", "a.bcd", "d.256.a.bcd.nogroup"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": tc.hash})
			if filepath.Dir(cleanDirsOver(t, s, "e2e.test")) == tc.dir {
				tc.dir = "r"
			}
			d := filepath.Join(home, tc.dir)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			plain := bytes.Repeat([]byte("x"), 10)
			stale := filepath.Join(d, tc.stale)
			writeOver(t, stale, plain)
			outside := t.TempDir()
			keep := []hierFile{
				{filepath.Join(outside, "junk.txt"), []byte("keep me")},
				{filepath.Join(outside, tc.stale), plain},
			}
			hierWrite(t, keep)
			link := filepath.Join(d, "sub")
			if err := os.Symlink(outside, link); err != nil {
				t.Fatal(err)
			}

			out := cleanDirsRun(t, s)

			hierCheckKept(t, outside, keep)
			cleanDirsIsLink(t, home, link, outside)
			if !strings.Contains(out, "Skipping symlink "+link+":") {
				t.Errorf("clean did not report that it skipped the symlink %s/sub", tc.dir)
			}
			hierCheckGone(t, home, out, []string{stale}, true)
			if t.Failed() {
				t.Logf("doverctl -v clean:\n%s", out)
			}
		})
	}
}

// TestOverToolsOldNameDirChildSymlink (D-326, the #515 r3 review, control): below a top-level
// directory that main entered (crc: two lowercase hex digits), a child symlink to a directory is
// still followed by doverctl clean and dexpireover -a, as on main (an admin can put
// subdirectories on other disks that way).
//
// Input: readergrouphash crc; the real directory "ab" (or "cd" when "ab" is e2e.test's
// directory) holds the symlink "sub" to a directory outside the group home that holds a data
// file of no group; `doverctl -v clean` and `dexpireover -a -v` in a case each. Expected: the
// data file is removed and reported by its path through the link; "sub" is still a symlink to
// that directory, which is still there; no "Skipping symlink" line; the tool exits 0. Passes on
// d7ed0b9f. On main dexpireover passes; doverctl follows the link and removes the file as well,
// but exits 1: its scanDirectory() returned with chdir("..") to the parent of the link's target,
// so remove("sub") and remove("ab") failed there (#515 r1, TestDoverctlCleanFollowsOldNameSymlink).
func TestOverToolsOldNameDirChildSymlink(t *testing.T) {
	for _, tool := range cleanDirsTools {
		t.Run(tool.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			name := "ab"
			if filepath.Dir(cleanDirsOver(t, s, "e2e.test")) == name {
				name = "cd"
			}
			d := filepath.Join(home, name)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			outside := t.TempDir()
			writeOver(t, filepath.Join(outside, "data.256.deadbeef.cafebabe"), bytes.Repeat([]byte("x"), 10))
			link := filepath.Join(d, "sub")
			if err := os.Symlink(outside, link); err != nil {
				t.Fatal(err)
			}

			out, err := s.TryRun(tool.args[0], tool.args[1:]...)
			if err != nil {
				t.Errorf("%s: %v", strings.Join(tool.args, " "), err)
			}

			hierCheckGone(t, home, out, []string{filepath.Join(link, "data.256.deadbeef.cafebabe")}, true)
			cleanDirsIsLink(t, home, link, outside)
			if st, err := os.Stat(outside); err != nil || !st.IsDir() {
				t.Errorf("%s, the target of the symlink %s/sub, was removed (%v)", outside, name, err)
			}
			if strings.Contains(out, "Skipping symlink ") {
				t.Errorf("%s skipped a symlink", tool.name)
			}
			if t.Failed() {
				t.Logf("%s:\n%s", strings.Join(tool.args, " "), out)
			}
		})
	}
}

// cleanDirsOldOver is hierOverFile(group, 1, 500) with article 1 received 60 days ago, so that
// `dexpireover -a` with "e2e.*:x30" in dexpire.ctl expires it and so writes to the file.
func cleanDirsOldOver(group string) []byte {
	b := hierOverFile(group, 1, 500)
	const head, slot = 16 + 256 + 4, 32 // OverHead, OverArt (dreaderd/defs.h)
	binary.NativeEndian.PutUint32(b[head+1*slot+24:], uint32(time.Now().Add(-60*24*time.Hour).Unix()))
	return b
}

// cleanDirsCRCData turns the crc over file name "over.0.<hash>" into the name of the data file
// with the article base base: "data.<base>.<hash>".
func cleanDirsCRCData(t *testing.T, over string, base int) string {
	t.Helper()
	if !strings.HasPrefix(over, "over.0.") {
		t.Fatalf("%q is not a crc over file name", over)
	}
	return fmt.Sprintf("data.%d.%s", base, strings.TrimPrefix(over, "over.0."))
}

// cleanDirsFileLinks makes in d the three symlinks of the file-symlink tests, named as files of a
// live group (range 1..300): over (the group's over file name) to outside/over, which holds
// cleanDirsOldOver(group); data (a data file name of the group, base 1024: out of range) to
// outside/data, which holds 10 bytes; dangling (base 1280) to outside/missing, which does not
// exist. It returns the links and the two outside files.
func cleanDirsFileLinks(t *testing.T, d, outside, group, over, data, dangling string) (links [3][2]string, targets []hierFile) {
	t.Helper()
	targets = []hierFile{
		{filepath.Join(outside, "over"), cleanDirsOldOver(group)},
		{filepath.Join(outside, "data"), bytes.Repeat([]byte("x"), 10)},
	}
	hierWrite(t, targets)
	links = [3][2]string{
		{filepath.Join(d, over), targets[0].path},
		{filepath.Join(d, data), targets[1].path},
		{filepath.Join(d, dangling), filepath.Join(outside, "missing")},
	}
	for _, l := range links {
		if err := os.Symlink(l[1], l[0]); err != nil {
			t.Fatal(err)
		}
	}
	return links, targets
}

// TestOverToolsMd564SignDirFileSymlink (D-326, the #515 r4 review): below a top-level directory "+"
// or "-" of the group home (md5-64\1), which main skipped, doverctl clean and dexpireover -a skip
// every symlink, not only a symlink to a directory (r3): a link named as a live group's over file
// is otherwise opened read-write through the link, and dexpireover expires the over file it
// points to, wherever that is; a link named as a stale data file is removed.
//
// Input: readergrouphash md5-64\1, "e2e.*:x30" in dexpire.ctl; a group whose directory is "+" or
// "-" (found with `dgrpctl listgrouphashfile`, range 1..300); in that real directory a data file
// of no group and three symlinks named as files of the group: its over file name to an over file
// of the group outside the group home with article 1 received 60 days ago, its data file of base
// 1024 (out of range) to an outside file, and its data file of base 1280 to a file that does not
// exist; `doverctl -v clean` and `dexpireover -a -v` in a case each. Expected: both outside files
// are byte-identical; all three links are still there and point to their targets; the tool
// reports "Skipping symlink <path>:" for each; the data file of no group is removed and reported
// (the directory was scanned); the tool exits 0. **Fails** on 20427179 (#515 r4): dexpireover
// expires the outside over file (it changes) and removes the data link and the dangling link;
// doverctl removes the data link (stale) and prints no "Skipping symlink" line.
func TestOverToolsMd564SignDirFileSymlink(t *testing.T) {
	for _, tool := range cleanDirsTools {
		t.Run(tool.name, func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": `md5-64\1`})
			s.WriteFile(t, "dexpire.ctl", "e2e.*:x30\n")
			group, over := cleanDirsPlusMinusGroup(t, s)
			hierGroupRange(t, s, group, "1", "300")
			dir, base := filepath.Split(over)
			d := filepath.Join(home, dir)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			stale := filepath.Join(d, "zzzzzzzzzzzzzzzzzzzzzz.0.d.64.256")
			writeOver(t, stale, bytes.Repeat([]byte("x"), 10))
			links, targets := cleanDirsFileLinks(t, d, t.TempDir(), group, base,
				cleanDirsMD5Data(t, base, 1024), cleanDirsMD5Data(t, base, 1280))

			out, err := s.TryRun(tool.args[0], tool.args[1:]...)
			if err != nil {
				t.Errorf("%s: %v", strings.Join(tool.args, " "), err)
			}

			hierCheckKept(t, home, targets)
			for _, l := range links {
				cleanDirsIsLink(t, home, l[0], l[1])
				if !strings.Contains(out, "Skipping symlink "+l[0]+":") {
					t.Errorf("%s did not report that it skipped the symlink %s", tool.name, strings.TrimPrefix(l[0], home))
				}
			}
			hierCheckGone(t, home, out, []string{stale}, true)
			if t.Failed() {
				t.Logf("%s:\n%s", strings.Join(tool.args, " "), out)
			}
		})
	}
}

// TestOverToolsOldNameDirFileSymlink (D-326, the #515 r4 review, control): below a top-level
// directory that main entered (crc: two lowercase hex digits), symlinks to files are handled as on
// main and on 20427179; the r4 change (TestOverToolsMd564SignDirFileSymlink) is only for the trees
// main did not enter.
//
// Input: readergrouphash crc, "e2e.*:x30" in dexpire.ctl; in e2e.test's directory a data file of
// no group and the three symlinks of TestOverToolsMd564SignDirFileSymlink, named as e2e.test's over
// file, its data file of base 1024 (out of range) and of base 1280 (dangling); `doverctl -v clean`
// and `dexpireover -a -v` in a case each. Expected, as on 20427179: no "Skipping symlink" line;
// the over link is kept; dexpireover expires article 1 in the outside over file through the link
// (the file changes), doverctl only reads it (unchanged); the out-of-range data link is removed
// (the link, not the outside file, which is unchanged); the dangling link is removed by
// dexpireover (stale by its name) and kept by doverctl ("Unable to stat(<dir>/<name>)"); the data
// file of no group is removed; the tool exits 0.
func TestOverToolsOldNameDirFileSymlink(t *testing.T) {
	for _, tool := range cleanDirsTools {
		t.Run(tool.name, func(t *testing.T) {
			dexpire := tool.name == "dexpireover"
			s, home := overSite(t, nil)
			s.WriteFile(t, "dexpire.ctl", "e2e.*:x30\n")
			dir, base := filepath.Split(cleanDirsOver(t, s, "e2e.test"))
			dir = strings.TrimSuffix(dir, "/")
			d := filepath.Join(home, dir)
			if err := os.MkdirAll(d, 0o755); err != nil {
				t.Fatal(err)
			}
			stale := filepath.Join(d, "data.256.deadbeef.cafebabe")
			writeOver(t, stale, bytes.Repeat([]byte("x"), 10))
			links, targets := cleanDirsFileLinks(t, d, t.TempDir(), "e2e.test", base,
				cleanDirsCRCData(t, base, 1024), cleanDirsCRCData(t, base, 1280))

			out, err := s.TryRun(tool.args[0], tool.args[1:]...)
			if err != nil {
				t.Errorf("%s: %v", strings.Join(tool.args, " "), err)
			}

			if strings.Contains(out, "Skipping symlink ") {
				t.Errorf("%s skipped a symlink", tool.name)
			}
			cleanDirsIsLink(t, home, links[0][0], links[0][1])
			if got, err := os.ReadFile(targets[0].path); err != nil {
				t.Errorf("the outside over file was removed: %v", err)
			} else if changed := !bytes.Equal(got, targets[0].want); changed != dexpire {
				t.Errorf("the outside over file changed: %v, want %v (expired through the link only by dexpireover)", changed, dexpire)
			}
			hierCheckKept(t, home, targets[1:])
			if _, err := os.Lstat(links[1][0]); err == nil {
				t.Errorf("the out-of-range data link %s is still there", strings.TrimPrefix(links[1][0], home))
			}
			_, err = os.Lstat(links[2][0])
			if kept := err == nil; kept == dexpire {
				t.Errorf("the dangling link %s kept: %v, want %v", strings.TrimPrefix(links[2][0], home), kept, !dexpire)
			}
			if !dexpire && !strings.Contains(out, "Unable to stat("+dir+"/"+filepath.Base(links[2][0])+")") {
				t.Errorf("doverctl did not report the dangling link as not stat-able")
			}
			hierCheckGone(t, home, out, []string{stale}, true)
			if t.Failed() {
				t.Logf("%s:\n%s", strings.Join(tool.args, " "), out)
			}
		})
	}
}

// TestOverToolsDirLoopSymlink (D-326, go-while's #515 review, F3): a symlink to a directory that
// is already on the path from the group home (a loop: "cd -> ." in the group home, "loop -> ."
// or "up -> .." in a group directory) is skipped with a -v line "Skipping directory loop
// <path>:", not scanned again and not removed. scanDirectory()'s depth limit (*level) is never
// incremented, so before the check the recursion ended only when the path no longer fitted
// PATH_MAX, and since r1 doverctl no longer drifts with chdir(".."). Each scan through the loop
// also entered every directory of the group home with scanDirectory(), which does not apply
// CleanSpool()'s layout rule, so the files of lost+found were removed.
//
// Input: readergrouphash crc; in the group home the symlink "cd" (or "ab" when "cd" is e2e.test's
// directory) to "." and lost+found/junk.txt; in e2e.test's directory its over file, a data file of
// no group and the symlinks "loop" to "." and "up" to ".."; a data file of no group in the parent
// of the group home (outside it); `doverctl -v clean` and `dexpireover -a -v` in a case each.
// Expected: the tool exits 0; the over file is unchanged; the data file of no group in e2e.test's
// directory is removed and reported; the three links, junk.txt and the file outside the group
// home are still there; "Skipping directory loop <path>:" for each link. **Fails** on 20427179:
// both tools die of SIGSEGV (stack overflow); whether junk.txt goes before that depends on the
// readdir() order (go-while saw it removed). On main dexpireover dies of SIGSEGV too, and doverctl
// exits 1 with 1527 failed file operations: its chdir("..") after the links drifted out of the
// group home.
func TestOverToolsDirLoopSymlink(t *testing.T) {
	for _, tool := range cleanDirsTools {
		t.Run(tool.name, func(t *testing.T) {
			s, home := overSite(t, nil)
			dir, base := filepath.Split(cleanDirsOver(t, s, "e2e.test"))
			dir = strings.TrimSuffix(dir, "/")
			top := "cd"
			if dir == top {
				top = "ab"
			}
			d := filepath.Join(home, dir)
			for _, x := range []string{d, filepath.Join(home, "lost+found")} {
				if err := os.MkdirAll(x, 0o755); err != nil {
					t.Fatal(err)
				}
			}
			keep := []hierFile{
				{filepath.Join(d, base), hierOverFile("e2e.test", 1, 500)},
				{filepath.Join(home, "lost+found", "junk.txt"), []byte("keep me")},
				{filepath.Join(filepath.Dir(home), "data.256.deadbeef.cafebab9"), bytes.Repeat([]byte("x"), 10)},
			}
			hierWrite(t, keep)
			stale := filepath.Join(d, "data.256.deadbeef.cafebabe")
			writeOver(t, stale, bytes.Repeat([]byte("x"), 10))
			links := [][2]string{
				{filepath.Join(home, top), "."},
				{filepath.Join(d, "loop"), "."},
				{filepath.Join(d, "up"), ".."},
			}
			for _, l := range links {
				if err := os.Symlink(l[1], l[0]); err != nil {
					t.Fatal(err)
				}
			}

			out, err := s.TryRun(tool.args[0], tool.args[1:]...)
			if err != nil {
				t.Errorf("%s: %v", strings.Join(tool.args, " "), err)
			}

			hierCheckKept(t, home, keep)
			hierCheckGone(t, home, out, []string{stale}, true)
			for _, l := range links {
				cleanDirsIsLink(t, home, l[0], l[1])
				if !strings.Contains(out, "Skipping directory loop "+l[0]+":") {
					t.Errorf("%s did not report the directory loop %s", tool.name, strings.TrimPrefix(l[0], home))
				}
			}
			if t.Failed() {
				if len(out) > 8000 {
					out = out[:4000] + "\n...\n" + out[len(out)-4000:]
				}
				t.Logf("%s:\n%s", strings.Join(tool.args, " "), out)
			}
		})
	}
}
