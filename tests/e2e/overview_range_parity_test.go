package e2e

import (
	"slices"
	"strings"
	"testing"

	testkit "github.com/go-while/nntp-testkit"
)

func TestReaderSparseOverviewRange(t *testing.T) {
	const group = "e2e.test"
	const last = "15001"
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Config: map[string]string{"readerxrefslavehost": PathHost},
		Files:  map[string]string{"dexpire.ctl": "*:x30:a20000\n"},
	})
	firstID, lastID := msgID(t), msgID(t)
	first := append([]string{"Xref: " + PathHost + " " + group + ":1"},
		newArticle(firstID, group, "first article", "body")...)
	second := append([]string{"Xref: " + PathHost + " " + group + ":" + last},
		newArticle(lastID, group, "last article", "body")...)
	feedReader(t, s, st, group, first, second)

	c := s.Reader(t)
	testkit.WaitFor(t, testkit.Deadline(), "both endpoints of the sparse group", func() bool {
		_, line := c.Cmd("GROUP %s", group)
		fields := strings.Fields(line)
		return len(fields) >= 4 && fields[0] == "211" && fields[2] == "1" && fields[3] == last
	})

	var over []string
	for _, command := range []string{"OVER", "XOVER"} {
		if err := c.Send(command + " 1-" + last + "\r\nDATE\r\n"); err != nil {
			t.Fatal(err)
		}
		c.ExpectStatus(224, command)
		rows := c.ReadDotLines()
		c.ExpectStatus(111, "DATE after "+command)
		if len(rows) != 2 || !strings.HasPrefix(rows[0], "1\tfirst article\t") ||
			!strings.Contains(rows[0], "\t"+firstID+"\t") ||
			!strings.HasPrefix(rows[1], last+"\tlast article\t") ||
			!strings.Contains(rows[1], "\t"+lastID+"\t") {
			t.Fatalf("%s: got %q, want only the two endpoint articles in order", command, rows)
		}
		if over == nil {
			over = rows
		} else if !slices.Equal(rows, over) {
			t.Errorf("%s: got %q, want the same rows as OVER %q", command, rows, over)
		}
	}

	if err := c.Send("HDR Subject 1-" + last + "\r\nQUIT\r\n"); err != nil {
		t.Fatal(err)
	}
	c.ExpectStatus(225, "HDR Subject")
	if rows, want := c.ReadDotLines(), []string{"1 first article", last + " last article"}; !slices.Equal(rows, want) {
		t.Errorf("HDR Subject: got %q, want %q", rows, want)
	}
	c.ExpectStatus(205, "QUIT after HDR")
}
