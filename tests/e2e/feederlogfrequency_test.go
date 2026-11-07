package e2e

import (
	"regexp"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/client"
)

// feederlogfrequency: the number of articles between the status lines of an incoming
// connection ("<host> secs=... ihave=...") and between the "DIABLO uptime=..." total lines of
// the master in the general log. It was fixed at 1024, a lot of lines on a busy server. 0 logs
// a connection's status only when it ends.

var (
	sessionLine = regexp.MustCompile(`(?m) secs=\d+ ihave=\d+ `)
	totalLine   = regexp.MustCompile(`(?m)DIABLO uptime=`)
)

// feedArticles sends n new articles with IHAVE over one connection from the peer and quits.
func feedArticles(t *testing.T, s *Site, n int) {
	t.Helper()
	p := s.Peer(t)
	for i := 0; i < n; i++ {
		id := msgID(t)
		p.ExpectCmd(335, "IHAVE %s", id)
		p.MustSend(client.Stuff(newArticle(id, "e2e.test", "log frequency", "body")))
		p.ExpectStatus(235, "IHAVE article")
	}
	p.ExpectCmd(205, "QUIT")
}

// logCounts waits until the general log has want connection status lines, then a moment
// longer for any more, and returns the counts of status and total lines.
func logCounts(t *testing.T, s *Site, want int) (int, int) {
	t.Helper()
	testkit.WaitFor(t, 10*time.Second, "the status lines in general.log", func() bool {
		return len(sessionLine.FindAllString(generalLog(s), -1)) >= want
	})
	time.Sleep(time.Second)
	g := generalLog(s)
	return len(sessionLine.FindAllString(g, -1)), len(totalLine.FindAllString(g, -1))
}

// TestFeederLogFrequency: 5 articles over one connection give one status line every n
// articles plus one when the connection ends, and the master writes a total line every n
// accepted articles. Fails on main: diablo.config does not know feederlogfrequency, and
// diablo exits at startup.
func TestFeederLogFrequency(t *testing.T) {
	for _, c := range []struct {
		name     string
		value    string // "" = not set (1024)
		sessions int    // status lines of the connection
		totals   int    // DIABLO uptime= lines
	}{
		{"default", "", 1, 0},
		{"every 2", "2", 3, 2},
		{"only at the end", "0", 1, 0},
	} {
		t.Run(c.name, func(t *testing.T) {
			conf := map[string]string{"path_generallog": "%s/general.log"}
			if c.value != "" {
				conf["feederlogfrequency"] = c.value
			}
			s := NewSite(t, SiteOptions{Config: conf})
			feedArticles(t, s, 5)
			sessions, totals := logCounts(t, s, c.sessions)
			if sessions != c.sessions || totals != c.totals {
				t.Errorf("feederlogfrequency %q: %d status and %d total lines, want %d and %d:\n%s",
					c.value, sessions, totals, c.sessions, c.totals, generalLog(s))
			}
		})
	}
}

// TestFeederLogFrequencyDicmd: dicmd config sets feederlogfrequency for new connections and
// shows it; a value that is not a number of articles is refused and the old one kept.
func TestFeederLogFrequencyDicmd(t *testing.T) {
	s := NewSite(t, SiteOptions{Config: map[string]string{"path_generallog": "%s/general.log"}})
	if out := s.Dicmd(t, "config", "feederlogfrequency"); !strings.Contains(out, "feederlogfrequency: 1024") {
		t.Errorf("dicmd config feederlogfrequency: %q, want feederlogfrequency: 1024", out)
	}
	if out := s.Dicmd(t, "config", "feederlogfrequency", "3"); !strings.Contains(out, "Set 'feederlogfrequency' to '3'") {
		t.Fatalf("dicmd config feederlogfrequency 3: %q", out)
	}
	for _, bad := range []string{"abc", "-1", "3x"} {
		if out := s.Dicmd(t, "config", "feederlogfrequency", bad); strings.Contains(out, "Set '") {
			t.Errorf("dicmd config feederlogfrequency %s: %q, want it refused", bad, out)
		}
	}
	if out := s.Dicmd(t, "config", "feederlogfrequency"); !strings.Contains(out, "feederlogfrequency: 3") {
		t.Errorf("after refused values: %q, want feederlogfrequency: 3", out)
	}
	feedArticles(t, s, 3)
	if sessions, totals := logCounts(t, s, 2); sessions != 2 || totals != 1 {
		t.Errorf("after dicmd config feederlogfrequency 3, 3 articles: %d status and %d total lines, want 2 and 1:\n%s",
			sessions, totals, generalLog(s))
	}
}
