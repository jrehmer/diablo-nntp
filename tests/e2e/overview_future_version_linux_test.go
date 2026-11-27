//go:build linux

package e2e

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"

	testkit "github.com/go-while/nntp-testkit"
)

// Install the future header only once the reader is waiting for byte 0.
// This exercises the second header read, after the initialization lock.
func TestReaderFutureOverviewVersionAfterLock(t *testing.T) {
	if !fileExists("/proc/locks") {
		t.Skip("requires /proc/locks")
	}
	s, home := overSite(t, map[string]string{"readergrouphash": "hierarchy"})
	path := filepath.Join(home, "e2e", "o.0.e2e.test")
	f, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR, 0o644)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	lock := syscall.Flock_t{Type: syscall.F_WRLCK, Whence: 0, Start: 0, Len: 4}
	if err := syscall.FcntlFlock(f.Fd(), syscall.F_SETLK, &lock); err != nil {
		t.Fatal(err)
	}
	restartReader(t, s)
	c := s.Reader(t)
	defer c.Close()
	c.MustSend("GROUP e2e.test\r\n")
	ownPID := strconv.Itoa(os.Getpid())
	testkit.WaitFor(t, testkit.Deadline(), "reader waiting for the overview initialization lock", func() bool {
		list, err := os.ReadFile("/proc/locks")
		if err != nil {
			t.Fatal(err)
		}
		var ownerID string
		for _, line := range strings.Split(string(list), "\n") {
			fs := strings.Fields(line)
			if len(fs) == 8 && fs[1] == "POSIX" && fs[3] == "WRITE" &&
				fs[4] == ownPID && fs[6] == "0" && fs[7] == "3" {
				ownerID = fs[0]
			}
			if len(fs) == 9 && fs[0] == ownerID && fs[1] == "->" &&
				fs[2] == "POSIX" && fs[4] == "WRITE" {
				return true
			}
		}
		return false
	})
	future := overHead("e2e.test")
	binary.NativeEndian.PutUint32(future, 5)
	if _, err := f.WriteAt(future, 0); err != nil {
		t.Fatal(err)
	}
	lock.Type = syscall.F_UNLCK
	if err := syscall.FcntlFlock(f.Fd(), syscall.F_SETLK, &lock); err != nil {
		t.Fatal(err)
	}
	c.ExpectStatus(211, "GROUP after lock release")
	c.ExpectCmd(224, "OVER 1")
	if rows := c.ReadDotLines(); len(rows) != 0 {
		t.Errorf("OVER 1 got %q", rows)
	}
	c.ExpectCmd(205, "QUIT")
	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, future) {
		t.Errorf("the exclusive-lock reread replaced the future header (%d bytes before, %d after)", len(future), len(got))
	}
}
