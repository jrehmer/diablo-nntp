package e2e

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	testkit "github.com/go-while/nntp-testkit"
)

// tfDataName is the name of the data file with the article base base that goes with the over
// file name over, in any layout: hierarchy "o.0.<group>" gives "d.<base>.<group>", crc and md5
// as rtsDataName().
func tfDataName(t *testing.T, over string, base int) string {
	t.Helper()
	if g, ok := strings.CutPrefix(over, "o.0."); ok {
		return fmt.Sprintf("d.%d.%s", base, g)
	}
	return rtsDataName(t, over, base)
}

// TestDoverctlConvertCopySymlinkDest (D-332, go-while's #515 review G1): `doverctl -m convert`
// copies each file of a group to its name in the new layout with MoveFile() (lib/subs.c), which
// opened the destination with O_CREAT|O_TRUNC. That follows a symlink already at the destination
// name: the link's target, wherever it is, was truncated and overwritten with the group's over or
// data file, and the link stayed in place of the group's file. MoveFile() now unlinks the
// destination name first and creates it with O_EXCL, as createTempFile() in dexpireover does.
//
// Input: dexpire.ctl "e2e.*:x30:e4" (four articles per data file), the crc layout, e2e.test fed
// six articles with overview data (data files of the bases 0 and 4), dreaderd stopped; at each
// name the group's files get in the new layout (the over file and the data files of the bases 0
// and 4) a symlink, each to its own file outside the group home with known content; `doverctl -v
// -m convert crc <new>`, then readergrouphash <new> in diablo.config and dreaderd restarted.
// Cases: crc to hierarchy and crc to md5-32\1. Expected: "Converted e2e.test"; the three outside
// files are byte-identical; the three files of the new layout are regular files; every index
// record is in its slot with a data offset; GROUP gives 211 6 1 6 and OVER 1-6 the same six lines
// as before. **Fails** on 45e090fc: the outside files hold the group's over and data, and the
// group's files in the new layout are the symlinks.
func TestDoverctlConvertCopySymlinkDest(t *testing.T) {
	for _, tc := range []struct{ name, hash string }{
		{"crc_to_hierarchy", "hierarchy"},
		{"crc_to_md5", `md5-32\1`},
	} {
		t.Run(tc.name, func(t *testing.T) {
			st, spool := spoolStore(t)
			s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: []string{"e2e.test"},
				Files: map[string]string{"dexpire.ctl": "e2e.*:x30:e4\n"}})
			const group = "e2e.test"
			for i := 1; i <= 6; i++ {
				feedReader(t, s, st, group, newArticle(msgID(t), group, fmt.Sprintf("convert copy %d", i), "body"))
			}
			groupReply := func() string {
				c := s.Reader(t)
				defer c.Close()
				_, line := c.Cmd("GROUP %s", group)
				return line
			}
			testkit.WaitFor(t, testkit.Deadline(), "GROUP "+group+" 211 6 1 6", func() bool {
				return strings.HasPrefix(groupReply(), "211 6 1 6 ")
			})
			overLines := func() []string {
				c := s.Reader(t)
				defer c.Close()
				c.ExpectCmd(211, "GROUP %s", group)
				c.ExpectCmd(224, "OVER 1-6")
				return c.ReadDotLines()
			}
			before := overLines()
			if len(before) != 6 {
				t.Fatalf("OVER 1-6 before the conversion: %d lines, want 6: %q", len(before), before)
			}
			s.Proc.Stop()

			// the names in the new layout, as dgrpctl gives them with the new readergrouphash
			home := strings.TrimSpace(s.Run(t, "dpath", "group"))
			s.WriteFile(t, "diablo.config", s.ReadFile(t, "diablo.config")+"\nreadergrouphash "+tc.hash+"\n")
			rel := cleanDirsOver(t, s, group)
			dir, overName := filepath.Join(home, filepath.Dir(rel)), filepath.Base(rel)
			if err := os.MkdirAll(dir, 0o755); err != nil {
				t.Fatal(err)
			}
			names := []string{overName, tfDataName(t, overName, 0), tfDataName(t, overName, 4)}
			outside := t.TempDir()
			var keep []hierFile
			for _, n := range names {
				f := hierFile{filepath.Join(outside, n), []byte("outside file, must stay as it is: " + n + "\n")}
				keep = append(keep, f)
				if err := os.Symlink(f.path, filepath.Join(dir, n)); err != nil {
					t.Fatal(err)
				}
			}
			hierWrite(t, keep)

			out, err := s.TryRun("doverctl", "-v", "-m", "convert", "crc", tc.hash)
			if err != nil {
				t.Errorf("doverctl -v -m convert crc %s: %v", tc.hash, err)
			}
			if !strings.Contains(out, "Converted "+group+"\n") {
				t.Errorf("doverctl -m did not convert %s", group)
			}
			hierCheckKept(t, outside, keep)
			for _, n := range names {
				if m, err := rtsMode(filepath.Join(dir, n)); err != nil || !m.IsRegular() {
					t.Errorf("%s of %s after convert -m: mode %v (%v), want a regular file", n, group, m, err)
				}
			}
			rtsCheckSlots(t, filepath.Join(dir, overName), "after doverctl -m convert", 6)
			restartReader(t, s)
			if g := groupReply(); !strings.HasPrefix(g, "211 6 1 6 ") {
				t.Errorf("GROUP %s after convert -m: %q, want 211 6 1 6", group, g)
			}
			if after := overLines(); strings.Join(after, "\n") != strings.Join(before, "\n") {
				t.Errorf("OVER 1-6 after convert -m differs:\nbefore %q\nafter  %q", before, after)
			}
			if t.Failed() {
				t.Logf("doverctl -v -m convert crc %s:\n%s", tc.hash, out)
			}
		})
	}
}

// TestDexpireoverRewriteTempLeftovers (D-333): an interrupted `dexpireover -R` leaves its
// temporary files ".<over name>" and ".<data name>" (createTempFile()), and an interrupted -z the
// compressed copy ".<data name>.gz" (diablo_minigzip()), in the group's directory; the next
// dexpireover removes them in the second pass of scanDirectory(). It matched them by ".data."
// (crc data), ".d." (hierarchy data) and `sscanf(name, ".%23[^.]s.%d.d.", ...) == 2` for md5
// data, which never returns 2 (the "s" after the scanset never matches the "." the scanset stops
// at), and had no pattern for the temporary over files of any layout. It now removes a name that
// is '.' followed by exactly the name GFName() gives an over or data file (plus ".gz" for data)
// in that directory (tempRewriteName()), and nothing else: the name parse alone ignores what
// follows its fields, so a first version (240ad29c, chatgpt-1's #519 r1 review) also removed an
// admin's ".over.0.<hash>.backup", and the second (07e6c74d) a hierarchy ".o.0.e2e.test~": the
// group must be in the active file.
//
// Input: the groups e2e.test and e2e.x (range 1..300), each with its over file (500 slots,
// article 1 received now) and the data file of the base 0; in e2e.test's directory the temporary
// names for it: ".<over name>", ".<data name of base 0>" and ".<data name of base 256>.gz"; as
// controls ".keep" and the lookalikes ".<over name>.backup", ".<over name>~", ".<over name>-old",
// ".<over name>bak", ".<data name of base 0>.gz.old", ".<data name of base 0>_1" and
// ".over.0.12345678.12345678.backup" (with hierarchy ".o.0.e2e.test~" is the over file name of a
// group "e2e.test~", which is not in the active file), in e2e.x's directory ".<over name>.orig"
// and ".<over name>.backup" (with hierarchy the names of the groups e2e.x.orig and e2e.x.backup,
// whose directory is e2e/ too), and an empty directory named ".<data name of base 512>";
// `dexpireover -a -v`. Cases: crc, md5-32\1, hierarchy and hierarchy-full. Expected: the three
// temporary files are removed, each reported with "Removing old temp file: <path>"; the controls,
// the directory and the groups' over and data files are there, unchanged, and nothing is reported
// about them. **Fails** on 45e090fc and 652d67b9 (main): the temporary over file stays in every
// layout, and so do the md5 temporary data files; with crc and the hierarchy layouts the
// directory and ".<data name>.gz.old" were removed. **Fails** on 240ad29c: the lookalikes are
// removed. **Fails** on 07e6c74d (hierarchy layouts): the lookalikes that read as the files of a
// group that does not exist, in this directory, are removed (go-while's #519 r2 review).
func TestDexpireoverRewriteTempLeftovers(t *testing.T) {
	for _, tc := range []struct{ name, hash string }{
		{"crc", ""},
		{"md5", `md5-32\1`},
		{"hierarchy", "hierarchy"},
		{"hierarchy_full", "hierarchy-full"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			var cfg map[string]string
			if tc.hash != "" {
				cfg = map[string]string{"readergrouphash": tc.hash}
			}
			s, home := overSite(t, cfg)
			const group = "e2e.test"
			rel := cleanDirsOver(t, s, group)
			dir, overName := filepath.Join(home, filepath.Dir(rel)), filepath.Base(rel)
			if err := os.MkdirAll(dir, 0o755); err != nil {
				t.Fatal(err)
			}
			dataName := tfDataName(t, overName, 0)
			// e2e.x: with hierarchy its directory is e2e/ (a one-character last component is
			// joined), where ".o.0.e2e.x.orig" is the over file name of a group e2e.x.orig
			hierGroupRange(t, s, "e2e.x", "1", "300")
			relX := cleanDirsOver(t, s, "e2e.x")
			dirX, overX := filepath.Join(home, filepath.Dir(relX)), filepath.Base(relX)
			if err := os.MkdirAll(dirX, 0o755); err != nil {
				t.Fatal(err)
			}
			files := []hierFile{
				{filepath.Join(dir, overName), hierOverFile(group, 1, 500)},
				{filepath.Join(dir, dataName), bytes.Repeat([]byte("x"), 10)},
				{filepath.Join(dirX, overX), hierOverFile("e2e.x", 1, 500)},
				{filepath.Join(dirX, tfDataName(t, overX, 0)), bytes.Repeat([]byte("y"), 10)},
			}
			// names the parse reads as group files that no tool creates for a group in the
			// active file (#519 r1, r2): they stay, like .keep
			controls := []hierFile{{filepath.Join(dir, ".keep"), []byte("not a temporary file\n")}}
			for _, n := range []string{"." + overName + ".backup", "." + dataName + ".gz.old",
				".over.0.12345678.12345678.backup", "." + overName + "~", "." + overName + "-old",
				"." + overName + "bak", "." + dataName + "_1"} {
				controls = append(controls, hierFile{filepath.Join(dir, n), []byte("an admin's file: " + n + "\n")})
			}
			for _, n := range []string{"." + overX + ".orig", "." + overX + ".backup"} {
				controls = append(controls, hierFile{filepath.Join(dirX, n), []byte("an admin's file: " + n + "\n")})
			}
			keep := append(append([]hierFile{}, files...), controls...)
			hierWrite(t, keep)
			temps := []string{"." + overName, "." + dataName, "." + tfDataName(t, overName, 256) + ".gz"}
			for _, n := range temps {
				writeOver(t, filepath.Join(dir, n), []byte("left by an interrupted run: "+n+"\n"))
			}
			tempDir := filepath.Join(dir, "."+tfDataName(t, overName, 512))
			if err := os.Mkdir(tempDir, 0o755); err != nil {
				t.Fatal(err)
			}

			out := s.Run(t, "dexpireover", "-a", "-v")

			for _, n := range temps {
				if m, err := rtsMode(filepath.Join(dir, n)); !os.IsNotExist(err) {
					t.Errorf("temporary file %s is still there: mode %v (%v)", n, m, err)
				}
				said := false
				for _, l := range strings.Split(out, "\n") {
					if strings.HasPrefix(l, "Removing old temp file: ") && strings.HasSuffix(l, "/"+filepath.Base(dir)+"/"+n) {
						said = true
					}
				}
				if !said {
					t.Errorf("dexpireover did not report the removal of %s", n)
				}
			}
			hierCheckKept(t, home, keep)
			if st, err := os.Stat(tempDir); err != nil || !st.IsDir() {
				t.Errorf("the directory %s was removed: %v", filepath.Base(tempDir), err)
			}
			for _, l := range strings.Split(out, "\n") {
				bad := strings.Contains(l, filepath.Base(tempDir))
				for _, f := range controls {
					bad = bad || strings.HasSuffix(l, "/"+filepath.Base(f.path))
				}
				if bad {
					t.Errorf("dexpireover reports: %s", l)
				}
			}
			hierNoBadReport(t, out, nil)
			if t.Failed() {
				t.Logf("dexpireover -a -v:\n%s", out)
			}
		})
	}
}

// TestDoverctlConvertSamePath (go-while's #519 r2 review): two methods can give a group's files
// the same path: md5-32:30\1 and md5-32:40\1 both keep the whole 26-character hash, so every
// path is the same, and md5-32/32 and md5-32/64 give the same directory to the groups whose
// hashNum() mod 64 is below 32. `doverctl -m convert` then copied each such file onto itself:
// MoveFile() (lib/subs.c) truncated it (O_TRUNC, main) or unlinked it (07e6c74d) and then
// removed the source, which is the same file, so the group's over and data files were gone while
// doverctl reported "Converted" and exited 0. MoveFile() now does nothing for a file that is
// already at its new name (one path, or the same inode), as rename() does.
//
// Input: sixteen groups e2e.test and e2e.sp01..e2e.sp15, two articles crossposted to all of
// them (each group gets an over file and the data file of the base 0), dreaderd stopped;
// `doverctl -v -m convert <old> <new>`, then readergrouphash <new> in diablo.config and dreaderd
// restarted. Cases: md5-32:30\1 to md5-32:40\1 (all sixteen paths the same), md5-32/32 to
// md5-32/64 (some the same, some not; the test checks that there is at least one of each).
// Expected: exit 0, "Converted <group>" for each group; each group's over and data files at the
// new names hold the bytes they had at the old names; GROUP gives 211 2 1 2 and OVER 1-2 the
// same two lines as before for every group. **Fails** on 652d67b9 (main) and 07e6c74d: the
// files of the groups with the same path are gone.
func TestDoverctlConvertSamePath(t *testing.T) {
	for _, tc := range []struct {
		name, from, to string
		allSame        bool
	}{
		{"md5_sigbytes", `md5-32:30\1`, `md5-32:40\1`, true},
		{"md5_buckets", "md5-32/32", "md5-32/64", false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			groups := []string{"e2e.test"}
			for i := 1; i <= 15; i++ {
				groups = append(groups, fmt.Sprintf("e2e.sp%02d", i))
			}
			st, spool := spoolStore(t)
			s := NewReaderSite(t, ReaderOptions{Spool: spool, Groups: groups,
				Config: map[string]string{"readergrouphash": tc.from}})
			for i := 1; i <= 2; i++ {
				feedReader(t, s, st, "e2e.test", newArticle(msgID(t), strings.Join(groups, ","), fmt.Sprintf("same path %d", i), "body"))
			}
			groupReply := func(g string) string {
				c := s.Reader(t)
				defer c.Close()
				_, line := c.Cmd("GROUP %s", g)
				return line
			}
			overLines := func(g string) []string {
				c := s.Reader(t)
				defer c.Close()
				c.ExpectCmd(211, "GROUP %s", g)
				c.ExpectCmd(224, "OVER 1-2")
				return c.ReadDotLines()
			}
			before := map[string][]string{}
			for _, g := range groups {
				testkit.WaitFor(t, testkit.Deadline(), "GROUP "+g+" 211 2 1 2", func() bool {
					return strings.HasPrefix(groupReply(g), "211 2 1 2 ")
				})
				if before[g] = overLines(g); len(before[g]) != 2 {
					t.Fatalf("OVER 1-2 of %s before the conversion: %q, want 2 lines", g, before[g])
				}
			}
			s.Proc.Stop()

			home := strings.TrimSpace(s.Run(t, "dpath", "group"))
			files := func(g string) []string {
				rel := cleanDirsOver(t, s, g)
				return []string{rel, filepath.Join(filepath.Dir(rel), tfDataName(t, filepath.Base(rel), 0))}
			}
			oldNames, content := map[string][]string{}, map[string][][]byte{}
			for _, g := range groups {
				oldNames[g] = files(g)
				for _, f := range oldNames[g] {
					b, err := os.ReadFile(filepath.Join(home, f))
					if err != nil {
						t.Fatalf("%s of %s before the conversion: %v", f, g, err)
					}
					content[g] = append(content[g], b)
				}
			}
			s.WriteFile(t, "diablo.config", s.ReadFile(t, "diablo.config")+"\nreadergrouphash "+tc.to+"\n")
			same, moved := 0, 0
			newNames := map[string][]string{}
			for _, g := range groups {
				newNames[g] = files(g)
				if newNames[g][0] == oldNames[g][0] {
					same++
				} else {
					moved++
				}
			}
			if same == 0 || (tc.allSame && moved != 0) || (!tc.allSame && moved == 0) {
				t.Fatalf("%s -> %s: %d groups keep their path, %d move; the case needs %s", tc.from, tc.to, same, moved,
					map[bool]string{true: "all to keep it", false: "some of each"}[tc.allSame])
			}

			out, err := s.TryRun("doverctl", "-v", "-m", "convert", tc.from, tc.to)
			if err != nil {
				t.Errorf("doverctl -v -m convert %s %s: %v", tc.from, tc.to, err)
			}
			for _, g := range groups {
				if !strings.Contains(out, "Converted "+g+"\n") {
					t.Errorf("doverctl -m did not convert %s", g)
				}
				for i, f := range newNames[g] {
					b, err := os.ReadFile(filepath.Join(home, f))
					if err != nil {
						t.Errorf("%s of %s (was %s) after convert -m: %v", f, g, oldNames[g][i], err)
					} else if !bytes.Equal(b, content[g][i]) {
						t.Errorf("%s of %s (was %s) after convert -m: %d bytes, not the %d it had", f, g, oldNames[g][i], len(b), len(content[g][i]))
					}
				}
			}
			restartReader(t, s)
			for _, g := range groups {
				if r := groupReply(g); !strings.HasPrefix(r, "211 2 1 2 ") {
					t.Errorf("GROUP %s after convert -m: %q, want 211 2 1 2", g, r)
				} else if after := overLines(g); strings.Join(after, "\n") != strings.Join(before[g], "\n") {
					t.Errorf("OVER 1-2 of %s after convert -m differs:\nbefore %q\nafter  %q", g, before[g], after)
				}
			}
			t.Logf("%s -> %s: %d groups keep their path, %d move", tc.from, tc.to, same, moved)
			if t.Failed() {
				t.Logf("doverctl -v -m convert %s %s:\n%s", tc.from, tc.to, out)
			}
		})
	}
}
