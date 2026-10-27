package e2e

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

// D-69: a compression level zlib rejects (dnewsfeeds "compress 12" becomes dnewslink -Z12): the peer
// accepts MODE COMPRESS, deflateInit() fails, but compression stays switched on. The first
// write then spins in flushCompressBuffer() forever (deflate keeps failing, avail_in never
// drops), logging an error on every pass.
func TestDnewslinkBadCompressLevel(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	port := dnlPeer(t, func(line string) (string, bool) {
		switch {
		case strings.HasPrefix(line, "mode stream"):
			return "203 stream ok\r\n", false
		case strings.HasPrefix(line, "mode compress"):
			return "207 compress ok\r\n", false
		case strings.HasPrefix(line, "check "):
			return "438 " + strings.Fields(line)[1] + "\r\n", false
		}
		return "", false
	})
	q := "D.00000001/B.0001 <z1@e2e.test> 0,100\n"
	if err := os.WriteFile(s.Dir+"/dqueue/out.S00000", []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(filepath.Join(s.Bin, "dnewslink"), "-N", "1", "-S", "0", "-b", "out.S%05d",
		"-h", "127.0.0.1", "-P", strconv.Itoa(port), "-t", "30", "-Z", "12")
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	var out bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() { _ = cmd.Wait(); close(done) }()
	defer func() { _ = cmd.Process.Kill(); <-done }()
	select {
	case <-done:
		t.Logf("dnewslink ended by itself:\n%s", out.String())
	case <-time.After(6 * time.Second):
		ps, _ := exec.Command("ps", "-o", "pid,time,pcpu,args", "-p", strconv.Itoa(cmd.Process.Pid)).CombinedOutput()
		t.Errorf("dnewslink -Z12 still busy after 6 s against a peer that accepted MODE COMPRESS:\n%s", ps)
	}
}
