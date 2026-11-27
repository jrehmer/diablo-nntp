package e2e

import (
	"slices"
	"strings"
	"testing"
)

func TestReaderOverviewPointAndMessageIDBound(t *testing.T) {
	const group = "e2e.test"
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Config: map[string]string{"readerxrefslavehost": PathHost},
		Files:  map[string]string{"dexpire.ctl": "*:x30:a150000\n"},
	})
	oldest, boundary := msgID(t), msgID(t)
	for _, article := range []struct{ number, id, subject string }{
		{"1", oldest, "outside scan"},
		{"2", boundary, "inside scan"},
		{"100001", msgID(t), "newest"},
	} {
		xref := "Xref: " + PathHost + " " + group + ":" + article.number
		data := append([]string{xref}, newArticle(article.id, group, article.subject, "body")...)
		feedReader(t, s, st, group, data)
	}
	waitGroup(t, s, "211 100001 1 100001")

	c := s.Reader(t)
	defer c.Close()
	c.ExpectCmd(412, "HEAD 1")
	c.ExpectCmd(211, "GROUP %s", group)
	spoolHeads := spool.Count("HEAD")
	if line := c.ExpectCmd(221, "HEAD 1"); !strings.HasPrefix(line, "221 1 "+oldest+" ") {
		t.Errorf("HEAD 1: %q", line)
	}
	if head := c.ReadDotLines(); !slices.Contains(head, "Subject: outside scan") {
		t.Errorf("HEAD 1 headers: %q", head)
	}
	if line := c.ExpectCmd(223, "STAT 2"); !strings.HasPrefix(line, "223 2 "+boundary+" ") {
		t.Errorf("STAT 2: %q", line)
	}

	// Article 2 is the 100,000th number scanned. Article 1 still has
	// readable overview, but it is only available by number or spool HEAD.
	c.MustSend("OVER " + boundary + "\r\nDATE\r\n")
	c.ExpectStatus(224, "OVER at the scan boundary")
	if rows := c.ReadDotLines(); len(rows) != 1 || !strings.HasPrefix(rows[0], "0\tinside scan\t") ||
		!strings.Contains(rows[0], "\t"+boundary+"\t") {
		t.Errorf("OVER at the scan boundary: %q", rows)
	}
	c.ExpectStatus(111, "DATE after OVER")
	c.ExpectCmd(225, "HDR Subject %s", boundary)
	if rows := c.ReadDotLines(); !slices.Equal(rows, []string{"0 inside scan"}) {
		t.Errorf("HDR at the scan boundary: %q", rows)
	}
	c.ExpectCmd(430, "OVER %s", oldest)
	if got := spool.Count("HEAD"); got != spoolHeads {
		t.Errorf("overview reads sent spool HEAD: count %d -> %d", spoolHeads, got)
	}
	c.ExpectCmd(225, "HDR Subject %s", oldest)
	if rows := c.ReadDotLines(); !slices.Equal(rows, []string{"0 outside scan"}) {
		t.Errorf("HDR beyond the scan boundary: %q", rows)
	}
	if got := spool.Count("HEAD"); got != spoolHeads+1 {
		t.Errorf("HDR beyond the scan boundary sent %d spool HEAD requests, want 1", got-spoolHeads)
	}
	if line := c.ExpectCmd(223, "STAT"); !strings.HasPrefix(line, "223 2 "+boundary+" ") {
		t.Errorf("current article after by-ID requests: %q", line)
	}
	c.ExpectCmd(423, "HEAD 3")
	c.ExpectCmd(423, "STAT 3")
	c.ExpectCmd(205, "QUIT")
}
