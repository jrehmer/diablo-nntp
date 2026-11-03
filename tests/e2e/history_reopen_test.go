package e2e

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"
	"time"
)

// histCmd starts the Diablo program name of site s with its stdin a pipe the
// test writes to. The returned channel gets the result of Wait.
func histCmd(t *testing.T, s *Site, env []string, name string, args ...string) (*exec.Cmd, io.WriteCloser, *bytes.Buffer, chan error) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 90*time.Second)
	t.Cleanup(cancel)
	cmd := exec.CommandContext(ctx, filepath.Join(s.Bin, name), args...)
	cmd.Dir = s.Dir
	cmd.Env = append(append(os.Environ(), s.Env...), env...)
	in, err := cmd.StdinPipe()
	if err != nil {
		t.Fatal(err)
	}
	var out bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	t.Cleanup(func() { in.Close() })
	return cmd, in, &out, done
}

var exRE = regexp.MustCompile(` ex=(\d+)`)

// histExp looks up id (a Message-ID or "h1.h2") in hist with dilookup and
// returns the record's exp field; found is false when there is none.
func histExp(t *testing.T, s *Site, hist, id string) (exp int, found bool) {
	t.Helper()
	out, err := s.TryRun("dilookup", "-h", hist, id)
	if m := exRE.FindStringSubmatch(out); err == nil && m != nil {
		n, _ := strconv.Atoi(m[1])
		return n, true
	}
	if !strings.Contains(out, "Not Found") {
		t.Fatalf("dilookup %s: %v\n%s", id, err, out)
	}
	return 0, false
}

// dumpLine is a didump line for diload: a record of hash h1.h2 of now.
func dumpLine(h1, h2 uint32) string {
	return fmt.Sprintf("DUMP %08x.%08x.0 gm=%d ex=100 boff=0 bsize=0\n", h1, h2, time.Now().Unix()/60)
}

// newHistory creates the site's dhistory (empty) and returns its path.
func newHistory(t *testing.T, s *Site) string {
	t.Helper()
	hist := filepath.Join(s.Dir, "dhistory")
	cmd := exec.Command(filepath.Join(s.Bin, "diload"), hist)
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("diload %s: %v\n%s", hist, err, out)
	}
	return hist
}

// TestHistoryReopenAfterSwap: a process that finds its history marked dead
// opens the history file again by name once dhisexpire -a has installed the
// new one. The reopen copied the name onto itself with snprintf, which left
// it empty: the process retried an empty path for six minutes and exited.
// Fails on main: diload does not finish its second add.
func TestHistoryReopenAfterSwap(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	hist := newHistory(t, s)
	_, in, out, done := histCmd(t, s, nil, "diload", hist)
	io.WriteString(in, dumpLine(0x3456789, 0xabcdef0))
	waitFor(t, 30*time.Second, "the first add", func() bool {
		_, found := histExp(t, s, hist, "03456789.0abcdef0")
		return found
	})
	s.Run(t, "dhisexpire", "-a", hist)
	io.WriteString(in, dumpLine(0x456789a, 0xbcdef01))
	in.Close()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("diload: %v\n%s", err, out.String())
		}
	case <-time.After(20 * time.Second):
		t.Fatalf("diload did not reopen the new history within 20s")
	}
	for _, hv := range []string{"03456789.0abcdef0", "0456789a.0bcdef01"} {
		if _, found := histExp(t, s, hist, hv); !found {
			t.Errorf("%s is not in the new history\n%s", hv, out.String())
		}
	}
}
