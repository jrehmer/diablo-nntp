package e2e

import (
	"os"
	"strconv"
	"strings"
	"testing"
)

// D-72: a line of 8 KB or more in a queue file (for example a hole of garbage after a crash) makes
// readline() fail for good; dnewslink treats that as the end of the file, sees nothing left to
// rewrite and removes the queue file with every entry behind the bad line.
func TestDnewslinkOverlongQueueLine(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	port := dnlPeer(t, func(line string) (string, bool) {
		if strings.HasPrefix(line, "mode stream") {
			return "203 stream ok\r\n", false
		}
		if strings.HasPrefix(line, "check ") {
			return "238 " + strings.Fields(line)[1] + "\r\n", false
		}
		return "", false
	})
	q := strings.Repeat("x", 9000) + "\n" +
		"D.00000001/B.0001 <b1@e2e.test> 0,100\nD.00000001/B.0001 <b2@e2e.test> 100,100\n"
	if err := os.WriteFile(s.Dir+"/dqueue/out.S00000", []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(port), "-t", "5")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	got := dnlQueue(t, s, "out.S00000")
	if !strings.Contains(got, "<b1@e2e.test>") || !strings.Contains(got, "<b2@e2e.test>") {
		t.Errorf("entries behind the long line were neither sent nor kept; queue file now: %.200q", got)
	}
}
