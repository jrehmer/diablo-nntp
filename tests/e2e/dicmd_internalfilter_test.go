package e2e

import (
	"strings"
	"testing"
)

// feedSameBody offers n articles with different Message-IDs and the identical body over one
// connection and returns how many of them diablo stored (STAT finds them afterwards).
func feedSameBody(t *testing.T, s *Site, n int, body string) int {
	t.Helper()
	p := s.Peer(t)
	var ids []string
	for i := 0; i < n; i++ {
		id := msgID(t)
		ids = append(ids, id)
		expect(t, p, 335, "IHAVE %s", id)
		send(t, p, wire(newArticle(id, "e2e.test", "same body", body)))
		if _, _, err := p.ReadStatus(); err != nil {
			t.Fatalf("IHAVE %s: %v", id, err)
		}
	}
	stored := 0
	for _, id := range ids {
		if code, _ := p.Cmd("STAT %s", id); code == 223 {
			stored++
		}
	}
	expect(t, p, 205, "QUIT")
	return stored
}

// TestDicmdConfigInternalFilter (D-78): "dicmd config internalfilter" tears the spam filter
// down (termSpamData() sets Fd = -1 but leaves BodyFilterFd) and the following
// InitSpamFilter() then skips opening the filter file, so the internal spam filter is
// silently off from then on, even when the value is unchanged. "dicmd info" still says it
// is enabled.
func TestDicmdConfigInternalFilter(t *testing.T) {
	const n = 8
	s := NewSite(t, SiteOptions{
		Config: map[string]string{"internalfilter": "B3"},
		// the label ISPAM names the articles the internal filter looks at
		Newsfeeds: DefaultNewsfeeds + "label ISPAM\n    addgroup *\nend\n",
	})

	before := feedSameBody(t, s, n, "identical spam body one")
	if before >= n {
		t.Fatalf("internalfilter B3: all %d articles with the same body were stored; the filter is not working in the test setup", n)
	}

	out := s.Dicmd(t, "config", "internalfilter", "B3")
	if !strings.Contains(out, "Set 'internalfilter' to 'B3'") {
		t.Fatalf("dicmd config internalfilter B3: %q", out)
	}

	after := feedSameBody(t, s, n, "identical spam body two")
	if after >= n {
		t.Errorf("after dicmd config internalfilter B3 the internal spam filter lets everything through: %d of %d articles with the same body stored (%d of %d before the dicmd)", after, n, before, n)
	}
}
