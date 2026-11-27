package e2e

import (
	"slices"
	"testing"
)

func TestReaderOverviewRangeContinuation(t *testing.T) {
	const group = "e2e.test"
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Config: map[string]string{"readerxrefslavehost": PathHost},
		Files:  map[string]string{"dexpire.ctl": "*:x30:a20000\n"},
	})
	for _, article := range []struct{ number, subject string }{
		{"1", "match first"},
		{"8200", "skip middle"},
		{"15001", "match last"},
	} {
		xref := "Xref: " + PathHost + " " + group + ":" + article.number
		data := append([]string{xref}, newArticle(msgID(t), group, article.subject, "body")...)
		feedReader(t, s, st, group, data)
	}
	waitGroup(t, s, "211 15001 1 15001")

	c := s.Reader(t)
	defer c.Close()
	c.ExpectCmd(211, "GROUP e2e.test")
	c.MustSend("XPAT Subject 1-15001 match*\r\nDATE\r\nQUIT\r\n")
	c.ExpectStatus(221, "XPAT Subject")
	if got, want := c.ReadDotLines(), []string{"1 match first", "15001 match last"}; !slices.Equal(got, want) {
		t.Errorf("XPAT across passes: got %q, want %q", got, want)
	}
	c.ExpectStatus(111, "DATE after XPAT")
	c.ExpectStatus(205, "QUIT after XPAT")
}
