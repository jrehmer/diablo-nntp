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
// is '.' followed by an over or data file name of any layout (tempRewriteName()), and nothing
// else.
//
// Input: the group e2e.test (range 1..300) with its over file (500 slots, article 1 received now)
// and the data file of the base 0; in the group's directory the temporary names for this group:
// ".<over name>", ".<data name of base 0>" and ".<data name of base 256>.gz", a control dotfile
// ".keep", and an empty directory named ".<data name of base 512>"; `dexpireover -a -v`. Cases:
// crc, md5-32\1 and hierarchy. Expected: the three temporary files are removed, each reported
// with "Removing old temp file: <path>"; ".keep", the directory and the group's over and data
// files are there, unchanged, and nothing else is reported about them. **Fails** on 45e090fc: the
// temporary over file stays in every layout, and so do the md5 temporary data files; with crc and
// hierarchy the directory was removed (remove() of an empty directory).
func TestDexpireoverRewriteTempLeftovers(t *testing.T) {
	for _, tc := range []struct{ name, hash string }{
		{"crc", ""},
		{"md5", `md5-32\1`},
		{"hierarchy", "hierarchy"},
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
			keep := []hierFile{
				{filepath.Join(dir, overName), hierOverFile(group, 1, 500)},
				{filepath.Join(dir, dataName), bytes.Repeat([]byte("x"), 10)},
				{filepath.Join(dir, ".keep"), []byte("not a temporary file\n")},
			}
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
				if strings.Contains(l, "/.keep") || strings.Contains(l, filepath.Base(tempDir)) {
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
