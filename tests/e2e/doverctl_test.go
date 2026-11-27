package e2e

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	testkit "github.com/go-while/nntp-testkit"
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// overSite is a reader site (not started) with one group e2e.test whose article range is
// 1..300, and the hierarchy layout of overview files: <group home>/e2e/o.0.e2e.test and
// d.<artBase>.e2e.test[.gz].
func overSite(t *testing.T, config map[string]string) (s *Site, home string) {
	t.Helper()
	s = NewReaderSite(t, ReaderOptions{
		SpoolAddr: "127.0.0.1:1",
		Groups:    []string{"e2e.test"},
		Config:    config,
		NoStart:   true,
	})
	s.Run(t, "dgrpctl", "adjust", "e2e.test", "NB", "=1")
	s.Run(t, "dgrpctl", "adjust", "e2e.test", "NE", "=300")
	home = strings.TrimSpace(s.Run(t, "dpath", "group"))
	if err := os.MkdirAll(filepath.Join(home, "e2e"), 0o755); err != nil {
		t.Fatal(err)
	}
	return s, home
}

// overHead returns an OverHead (dreaderd/defs.h: version 4, 256 entries per data file) for
// group, in the byte order of this host.
func overHead(group string) []byte { return overHeadMax(group, 1000) }

// overHeadMax is overHead with maxArts article slots promised in the header.
func overHeadMax(group string, maxArts uint32) []byte {
	b := make([]byte, 16+256+4)
	o := binary.NativeEndian
	o.PutUint32(b[0:], 4)
	o.PutUint32(b[4:], 0xF1E2D3C4)
	o.PutUint32(b[8:], uint32(len(b)))
	o.PutUint32(b[12:], maxArts)
	copy(b[16:16+256], group)
	o.PutUint32(b[16+256:], 256)
	return b
}

func writeOver(t *testing.T, path string, data []byte) {
	t.Helper()
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
}

// files lists the regular files and fifos below dir, as paths relative to it.
func filesBelow(t *testing.T, dir string) []string {
	t.Helper()
	var out []string
	err := filepath.WalkDir(dir, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() {
			rel, _ := filepath.Rel(dir, p)
			out = append(out, rel)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	return out
}

// TestDoverctlConvertMovesCompressedData (D-150): doverctl convert moves the compressed
// (.gz) data files together with the plain ones. It moved only the plain names, so after a
// hash switch the compressed files stayed behind in the old layout while the overview index
// had moved, and the data was unreachable.
func TestDoverctlConvertMovesCompressedData(t *testing.T) {
	s, home := overSite(t, nil)
	writeOver(t, filepath.Join(home, "e2e", "o.0.e2e.test"), overHead("e2e.test"))
	writeOver(t, filepath.Join(home, "e2e", "d.0.e2e.test"), []byte("plain"))
	writeOver(t, filepath.Join(home, "e2e", "d.256.e2e.test.gz"), []byte("gzipped"))

	out := s.Run(t, "doverctl", "-v", "convert", "hierarchy", "crc")
	if !strings.Contains(out, "Converted e2e.test") {
		t.Fatalf("group not converted:\n%s", out)
	}
	left, _ := filepath.Glob(filepath.Join(home, "e2e", "*"))
	if len(left) != 0 {
		t.Errorf("files left in the old layout: %q\n%s", left, out)
	}
	var gz, other int
	for _, f := range filesBelow(t, home) {
		if strings.HasSuffix(f, ".gz") {
			gz++
		} else {
			other++
		}
	}
	if gz != 1 || other != 2 {
		t.Errorf("after conversion: %d compressed and %d other files, want 1 and 2 (over file and one data file)", gz, other)
	}
}

// TestDoverctlConvertCopyFailure (D-151): with -m a data file that cannot be copied stops the
// conversion of the group, with a message and exit status 1, and the group is not reported
// as converted. The over file is moved last, so after the obstacle is gone a second run
// finishes the group: the data file that already moved is not found at its old place and
// skipped. The result of the copy was ignored.
func TestDoverctlConvertCopyFailure(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("root can read any file")
	}
	s, home := overSite(t, nil)
	over := filepath.Join(home, "e2e", "o.0.e2e.test")
	writeOver(t, over, overHead("e2e.test"))
	writeOver(t, filepath.Join(home, "e2e", "d.0.e2e.test"), []byte("first"))
	data := filepath.Join(home, "e2e", "d.256.e2e.test")
	writeOver(t, data, []byte("second"))
	if err := os.Chmod(data, 0); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("doverctl", "-v", "-m", "convert", "hierarchy", "crc")
	if err == nil {
		t.Errorf("doverctl exits 0 although a file could not be moved:\n%s", out)
	}
	if strings.Contains(out, "Converted e2e.test") {
		t.Errorf("incomplete conversion reported as converted:\n%s", out)
	}
	if !strings.Contains(out, "Cannot move") {
		t.Errorf("the failed move was not reported:\n%s", out)
	}
	if _, err := os.Stat(over); err != nil {
		t.Errorf("the over file moved before the data files were done: %v\n%s", err, out)
	}

	if err := os.Chmod(data, 0o644); err != nil {
		t.Fatal(err)
	}
	out = s.Run(t, "doverctl", "-v", "-m", "convert", "hierarchy", "crc")
	if !strings.Contains(out, "Converted e2e.test") {
		t.Errorf("the second run did not finish the group:\n%s", out)
	}
	if left, _ := filepath.Glob(filepath.Join(home, "e2e", "*")); len(left) != 0 {
		t.Errorf("files left in the old layout after the second run: %q", left)
	}
	if n := len(filesBelow(t, home)); n != 3 {
		t.Errorf("%d files after the conversion, want 3", n)
	}
}

// TestDoverctlCleanRemovesUnknownEntries (D-152): clean removes a file that is neither
// regular nor a directory by its own path. It looked for <dir>/<dir>/<name>, did not find it,
// and still counted the file as removed.
func TestDoverctlCleanRemovesUnknownEntries(t *testing.T) {
	s, home := overSite(t, nil)
	// the directory of e2e.test in the default (crc) layout: clean enters only directories the
	// layout can have made (D-326), and overSite's e2e/ is a hierarchy directory
	dir := filepath.Join(home, filepath.Dir(cleanDirsOver(t, s, "e2e.test")))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
	fifo := filepath.Join(dir, "junk")
	if err := exec.Command("mkfifo", fifo).Run(); err != nil {
		t.Skipf("no fifo: %v", err)
	}
	out := s.Run(t, "doverctl", "-v", "clean")
	if _, err := os.Lstat(fifo); err == nil {
		t.Errorf("the unknown entry is still there:\n%s", out)
	}
}

// TestDoverctlCleanMisalignedData (D-153): clean applies dexpireover's criteria, so a data
// file whose first article number is not a multiple of the entries per file (left over from
// an entry-size change) is removed, a properly aligned one in the group's range stays.
func TestDoverctlCleanMisalignedData(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	writeOver(t, filepath.Join(home, "e2e", "o.0.e2e.test"), overHead("e2e.test"))
	bad := filepath.Join(home, "e2e", "d.100.e2e.test")
	good := filepath.Join(home, "e2e", "d.256.e2e.test")
	writeOver(t, bad, []byte("misaligned"))
	writeOver(t, good, bytes.Repeat([]byte("x"), 10))
	out := s.Run(t, "doverctl", "-v", "clean")
	if _, err := os.Stat(bad); err == nil {
		t.Errorf("the misaligned data file is still there:\n%s", out)
	}
	if _, err := os.Stat(good); err != nil {
		t.Errorf("the aligned data file was removed:\n%s", out)
	}
}

// TestDexpireoverCompressionFailure (D-155): a compression that fails is counted and reported
// as a failure, not as "Compressed"; the plain file stays, and the exit status is 1. With -n
// the files that would be compressed are counted separately.
func TestDexpireoverCompressionFailure(t *testing.T) {
	skipWithoutZlib(t)
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Files: map[string]string{"dexpire.ctl": "e2e.*:x30:Z0:F0\n"}})
	feedReader(t, s, st, "e2e.test", newArticle(msgID(t), "e2e.test", "compress", "body"))
	c := s.Reader(t)
	testkit.WaitFor(t, 10*time.Second, "the article in GROUP e2e.test", func() bool {
		_, line := c.Cmd("GROUP e2e.test")
		return strings.HasPrefix(line, "211 1")
	})
	c.ExpectCmd(205, "QUIT")
	var data string
	testkit.WaitFor(t, 10*time.Second, "a data file", func() bool {
		m, _ := filepath.Glob(filepath.Join(s.Dir, "spool", "group", "*", "data.*"))
		if len(m) == 0 {
			m, _ = filepath.Glob(filepath.Join(s.Dir, "spool", "group", "data.*"))
		}
		if len(m) > 0 {
			data = m[0]
		}
		return data != ""
	})
	old := time.Now().Add(-48 * time.Hour)
	if err := os.Chtimes(data, old, old); err != nil {
		t.Fatal(err)
	}

	out := s.Run(t, "dexpireover", "-n", "-y", "-z", "-v")
	if !strings.Contains(out, "1 files would be compressed") {
		t.Errorf("dry run did not count the file that would be compressed:\n%s", out)
	}
	if strings.Contains(out, "Compressed 1 files") {
		t.Errorf("dry run says it compressed a file:\n%s", out)
	}

	// the name of the compressed file is taken by a directory: the compression cannot finish
	if err := os.Mkdir(data+".gz", 0o755); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dexpireover", "-y", "-z", "-v")
	if strings.Contains(out, "Compressed 1 files") || !strings.Contains(out, "Compression failed for 1 files") {
		t.Errorf("a failed compression is not reported as failed:\n%s", out)
	}
	if err == nil {
		t.Errorf("dexpireover exits 0 after a failed compression:\n%s", out)
	}
	if _, err := os.Stat(data); err != nil {
		t.Errorf("the plain data file is gone: %v", err)
	}
}

// TestDexpireoverHeaderOnlyOver: an over file that is only a header (no article slots) is
// skipped with a message; dexpireover divided by the number of slots and died with SIGFPE.
func TestDexpireoverHeaderOnlyOver(t *testing.T) {
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	s.WriteFile(t, "dexpire.ctl", "e2e.*:x30\n")
	writeOver(t, filepath.Join(home, "e2e", "o.0.e2e.test"), overHead("e2e.test"))
	out, err := s.TryRun("dexpireover", "-y", "-v")
	var ee *exec.ExitError
	if errors.As(err, &ee) && !ee.Exited() {
		t.Errorf("dexpireover on a header-only over file was killed: %v\n%s", err, out)
	}
	if !strings.Contains(out, "no article slots") {
		t.Errorf("the damaged over file was not reported:\n%s", out)
	}
}

// TestDexpireoverBadMaxArts: a header that promises no article slots (oh_MaxArts 0), or more
// than the file holds, is a damaged over file: skipped with a message, no modulo by zero and
// no read behind the mapped records.
func TestDexpireoverBadMaxArts(t *testing.T) {
	for _, maxArts := range []uint32{0, 1000000} {
		t.Run(fmt.Sprint(maxArts), func(t *testing.T) {
			s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
			s.WriteFile(t, "dexpire.ctl", "e2e.*:x30\n")
			writeOver(t, filepath.Join(home, "e2e", "o.0.e2e.test"),
				append(overHeadMax("e2e.test", maxArts), make([]byte, 64)...))
			out, err := s.TryRun("dexpireover", "-s", "-y", "-v")
			var ee *exec.ExitError
			if errors.As(err, &ee) && !ee.Exited() {
				t.Errorf("dexpireover with oh_MaxArts %d was killed: %v\n%s", maxArts, err, out)
			}
			if !strings.Contains(out, "no article slots") {
				t.Errorf("the damaged over file was not reported:\n%s", out)
			}
		})
	}
}
