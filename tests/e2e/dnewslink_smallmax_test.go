package e2e

import (
	"os"
	"strconv"
	"strings"
	"sync"
	"testing"
)

// D-68: dnewslink -M 2 (dnewsfeeds "maxstream 2"): with so few stream slots the "drain by 5"
// water mark (STREAMDRAIN) was larger than the window, so after the window had been filled
// once no further queue line was read; dnewslink ended the run and handed the rest back to
// the queue file.
func TestDnewslinkSmallMaxstream(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	var mu sync.Mutex
	var checks []string
	port := dnlPeer(t, func(line string) (string, bool) {
		switch {
		case strings.HasPrefix(line, "mode stream"):
			return "203 stream ok\r\n", false
		case strings.HasPrefix(line, "check "):
			mu.Lock()
			checks = append(checks, strings.Fields(line)[1])
			mu.Unlock()
			return "238 " + strings.Fields(line)[1] + "\r\n", false
		case strings.HasPrefix(line, "takethis "):
			return "239 " + strings.Fields(line)[1] + "\r\n", false
		}
		return "", false
	})
	var q string
	for i := 1; i <= 6; i++ {
		q += "D.00000001/B.0001 <m" + strconv.Itoa(i) + "@e2e.test> " + strconv.Itoa(i*100) + ",100\n"
	}
	if err := os.WriteFile(s.Dir+"/dqueue/out.S00000", []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(port), "-t", "5", "-M", "2")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	mu.Lock()
	n := len(checks)
	mu.Unlock()
	left := dnlQueue(t, s, "out.S00000")
	t.Logf("peer saw %d CHECKs; queue file now:\n%s", n, left)
	if n != 6 {
		t.Errorf("peer was offered %d of the 6 queued articles", n)
	}
	if !strings.HasPrefix(left, "<missing") {
		t.Errorf("queue file should be gone after all 6 were offered; it holds: %.200q", left)
	}
}
