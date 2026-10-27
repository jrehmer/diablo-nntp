package e2e

import (
	"os"
	"strconv"
	"strings"
	"testing"
)

// D-73: after a reconnect, dnewslink restarts the streams that were pending from readretry(), which
// rebuilds the queue line without the H and C<size> flags (the refile to the queue file was
// fixed for D-63, this path was not). Compressed-spool entries then go out as raw gzip bytes.
func TestDnewslinkQueueFieldsPerLine(t *testing.T) {
	s := NewSite(t, SiteOptions{NoStart: true})
	port := dnlPeerN(t, func(conn int, line string) (string, bool) {
		switch {
		case strings.HasPrefix(line, "mode stream"):
			return "203 stream ok\r\n", false
		case strings.HasPrefix(line, "check <z0@"):
			return "438 <z0@e2e.test>\r\n", false
		case strings.HasPrefix(line, "check <z1@"):
			return "", true // conn 1: go away after answering z0; conn 2: go away at once
		}
		return "", false
	})
	q := "D.00000001/B.0001 <z0@e2e.test> 0,100\n" +
		"D.00000001/B.0001 <z1@e2e.test> 100,100 C50\n" +
		"D.00000001/B.0001 <z2@e2e.test> 200,100\n"
	if err := os.WriteFile(s.Dir+"/dqueue/out.S00000", []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(port), "-t", "5")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	got := dnlQueue(t, s, "out.S00000")
	t.Logf("queue now:\n%s", got)
	if strings.HasPrefix(got, "<missing") {
		t.Fatalf("queue file is gone, dnewslink did not leave z1 and z2 queued: %s", got)
	}
	if strings.Contains(got, "<z0@") {
		t.Errorf("z0 was answered 438 but is still queued:\n%s", got)
	}
	seen := map[string]bool{}
	for _, l := range strings.Split(strings.TrimSpace(got), "\n") {
		hasC := strings.Contains(l, " C50")
		switch {
		case strings.Contains(l, "<z1@"):
			seen["z1"] = true
			if !hasC {
				t.Errorf("z1 lost its C50 flag: %q", l)
			}
		case strings.Contains(l, "<z2@"):
			seen["z2"] = true
			if strings.Contains(l, " C") {
				t.Errorf("z2 (not compressed) gained a C flag: %q", l)
			}
		}
	}
	for _, id := range []string{"z1", "z2"} {
		if !seen[id] {
			t.Errorf("%s is no longer queued; queue file now:\n%s", id, got)
		}
	}
}
