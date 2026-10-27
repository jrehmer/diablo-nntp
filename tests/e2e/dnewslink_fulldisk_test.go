//go:build linux

package e2e

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

// D-74 with a real full file system (root only: it mounts a 4 MB ext4 image on a loop device, or a 4 MB tmpfs).
// dnewslink rewrites the queue into <queue>.tmp and renames it over the queue. When the file
// system is full the rewrite is short; the old queue must be kept, not replaced by the short copy.
func TestDnewslinkFullDiskKeepsQueue(t *testing.T) {
	if os.Geteuid() != 0 {
		t.Skip("needs root: mounts a small file system")
	}
	need := func(name string, args ...string) {
		t.Helper()
		if out, err := exec.Command(name, args...).CombinedOutput(); err != nil {
			t.Skipf("%s %v: %v\n%s", name, args, err, out)
		}
	}
	img := filepath.Join(t.TempDir(), "fs.img")
	mnt := t.TempDir()
	f, err := os.Create(img)
	if err != nil {
		t.Fatal(err)
	}
	f.Truncate(4 << 20)
	f.Close()
	// ext4 on a loop device where there is one (not in containers); a size-limited tmpfs otherwise.
	fs := "ext4 (loop)"
	if exec.Command("mkfs.ext4", "-q", "-F", "-m", "0", img).Run() != nil ||
		exec.Command("mount", "-o", "loop", img, mnt).Run() != nil {
		fs = "tmpfs"
		need("mount", "-t", "tmpfs", "-o", "size=4m", "tmpfs", mnt)
	}
	t.Logf("file system: %s", fs)
	t.Cleanup(func() { exec.Command("umount", mnt).Run() })

	s := NewSite(t, SiteOptions{NoStart: true})
	qdir := filepath.Join(mnt, "dqueue")
	if err := os.MkdirAll(qdir, 0o755); err != nil {
		t.Fatal(err)
	}
	os.RemoveAll(filepath.Join(s.Dir, "dqueue"))
	if err := os.Symlink(qdir, filepath.Join(s.Dir, "dqueue")); err != nil {
		t.Fatal(err)
	}
	var b strings.Builder
	const n = 3000
	for i := 0; i < n; i++ {
		fmt.Fprintf(&b, "D.00000001/B.0001 <q%d@e2e.test> %d,100\n", i, i*100)
	}
	if err := os.WriteFile(filepath.Join(qdir, "out.S00000"), []byte(b.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	// Fill the file system, then free 16 KB: far less than the rewrite needs.
	fill, err := os.Create(filepath.Join(mnt, "fill"))
	if err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 64<<10)
	for {
		if _, err := fill.Write(buf); err != nil {
			break
		}
	}
	fill.Sync()
	st, _ := fill.Stat()
	fill.Truncate(st.Size() - 16<<10)
	fill.Close()
	exec.Command("sync").Run()

	port := dnlPeer(t, func(line string) (string, bool) {
		switch {
		case strings.HasPrefix(line, "mode stream"):
			return "203 stream ok\r\n", false
		case strings.HasPrefix(line, "check "):
			return "400 going away\r\n", true
		}
		return "", false
	})
	out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(port), "-t", "5")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	got := dnlQueue(t, s, "out.S00000")
	missing := 0
	for i := 0; i < n; i++ {
		if !strings.Contains(got, fmt.Sprintf("<q%d@e2e.test>", i)) {
			missing++
		}
	}
	if missing > 0 {
		t.Errorf("full file system: %d of %d queued entries are gone from the queue (queue now %d bytes)", missing, n, len(got))
	}
	if _, err := os.Stat(filepath.Join(qdir, "out.S00000.tmp")); err == nil {
		t.Errorf("out.S00000.tmp left behind")
	}
}
