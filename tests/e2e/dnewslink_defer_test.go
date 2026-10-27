package e2e

import (
	"net"
	"os"
	"strconv"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// dnlPeer starts a one-connection fake peer. h gets each lowercased command line and returns
// the reply text to write ("" for none) and whether to close the connection afterwards.
func dnlPeer(t *testing.T, h func(line string) (string, bool)) int {
	return dnlPeerN(t, func(_ int, line string) (string, bool) { return h(line) })
}

func dnlPeerN(t *testing.T, h func(conn int, line string) (string, bool)) int {
	var nconn int32
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { ln.Close() })
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			idx := int(atomic.AddInt32(&nconn, 1))
			go func() {
				defer c.Close()
				c.Write([]byte("200 fake\r\n"))
				buf := make([]byte, 4096)
				var got string
				for {
					c.SetReadDeadline(time.Now().Add(15 * time.Second))
					n, err := c.Read(buf)
					if err != nil {
						return
					}
					got += string(buf[:n])
					for {
						i := strings.Index(got, "\n")
						if i < 0 {
							break
						}
						line := strings.ToLower(strings.TrimSpace(got[:i]))
						got = got[i+1:]
						rep, cl := h(idx, line)
						if rep != "" {
							c.Write([]byte(rep))
						}
						if cl {
							return
						}
					}
				}
			}()
		}
	}()
	return ln.Addr().(*net.TCPAddr).Port
}

func dnlQueue(t *testing.T, s *Site, name string) string {
	b, err := os.ReadFile(s.Dir + "/dqueue/" + name)
	if err != nil {
		return "<missing: " + err.Error() + ">"
	}
	return string(b)
}

// D-66: articles that the peer defers (431 to CHECK) are kept only in dnewslink's memory. If the
// connection then ends without a reconnect (peer says 400 / goes away), they are not written
// back to the queue file and are lost.
func TestDnewslinkDeferredKeptOnExit(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	port := dnlPeer(t, func(line string) (string, bool) {
		switch {
		case strings.HasPrefix(line, "mode stream"):
			return "203 stream ok\r\n", false
		case strings.HasPrefix(line, "check <a1@") || strings.HasPrefix(line, "check <a2@"):
			return "431 " + strings.Fields(line)[1] + " try later\r\n", false
		case strings.HasPrefix(line, "check <a3@"):
			return "400 going away\r\n", true
		}
		return "", false
	})
	q := "D.00000001/B.0001 <a1@e2e.test> 0,100\nD.00000001/B.0001 <a2@e2e.test> 100,100\nD.00000001/B.0001 <a3@e2e.test> 200,100\n"
	if err := os.WriteFile(s.Dir+"/dqueue/out.S00000", []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(port), "-t", "5")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	got := dnlQueue(t, s, "out.S00000")
	for _, id := range []string{"<a1@e2e.test>", "<a2@e2e.test>", "<a3@e2e.test>"} {
		if !strings.Contains(got, id) {
			t.Errorf("%s was never delivered and is no longer queued; queue file now:\n%s", id, got)
		}
	}
}

