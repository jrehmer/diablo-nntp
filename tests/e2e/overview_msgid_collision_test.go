package e2e

// D-211 (overview): the reader's overview index keeps only the hash of each Message-ID, and
// OverStoreFindMessageId() (XHDR/XOVER/HDR/OVER by Message-ID) and FindCanceledMsg() (a cancel) took
// the first entry of the group whose hash matched. With msgidhash hash (crc, the default)
// "<1510900044@gn>" and "<1510900045@gn>" have one hash (D-210, TestMsgIdHashNeighbours), so
// with both in one group the lookup of the older one found the newer one's article, and the
// cancel of the older one removed the newer one. Both now compare the Message-ID: header kept
// in the overview data with the Message-ID asked for, and skip an entry that has another one;
// for an article with two Message-ID: headers only the last one counts, as in the index.

import (
	"strings"
	"testing"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/client"
	"github.com/go-while/nntp-testkit/fakeserver"
)

// collisionSite starts a reader with msgidhash hash (the default, set so the test keeps its
// meaning if the default changes), readercanlock all and a dcontrol.ctl that executes cancels,
// feeds the colliding pair to e2e.test (article 1 and 2) and returns a reader connection
// that sees both.
func collisionSite(t *testing.T, ids []string, subjects []string) (*Site, *fakeserver.Store, *client.Conn) {
	t.Helper()
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:   spool,
		Config:  map[string]string{"msgidhash": "hash", "readercanlock": "all"},
		Files:   map[string]string{"dcontrol.ctl": "cancel:*:*:doit,break\n"},
		NoStart: true,
	})
	_, port, _ := strings.Cut(s.Addr, ":")
	startReader(t, s, "[127.0.0.1]:"+port)
	feedReader(t, s, st, "e2e.test",
		newArticle(ids[0], "e2e.test", subjects[0], "body"),
		newArticle(ids[1], "e2e.test", subjects[1], "body"))
	c := s.Reader(t)
	testkit.WaitFor(t, testkit.Deadline(), "both articles in GROUP e2e.test", func() bool {
		_, line := c.Cmd("GROUP e2e.test")
		f := strings.Fields(line)
		return len(f) > 1 && f[0] == "211" && f[1] == "2"
	})
	return s, st, c
}

// TestOverviewMsgIdCollisionXhdr: the pair is fed to one group, the second (newest) last;
// XHDR Subject by each Message-ID gives that article's own Subject, and XOVER by the first
// gives an overview line with the first's own subject.
// Fails before the fix: XHDR of "<1510900044@gn>" answered the subject of the second article.
func TestOverviewMsgIdCollisionXhdr(t *testing.T) {
	ids := []string{"<1510900044@gn>", "<1510900045@gn>"}
	subjects := []string{"first of the pair", "second of the pair"}
	_, _, c := collisionSite(t, ids, subjects)
	defer c.ExpectCmd(205, "QUIT")
	for i := range ids {
		c.ExpectCmd(221, "XHDR Subject %s", ids[i])
		if got := c.ReadDotLines(); len(got) != 1 || got[0] != ids[i]+" "+subjects[i] {
			t.Errorf("XHDR Subject %s: %q, want [%q]", ids[i], got, ids[i]+" "+subjects[i])
		}
	}
	c.ExpectCmd(224, "XOVER %s", ids[0])
	if got := c.ReadDotLines(); len(got) != 1 || !strings.Contains(got[0], "\t"+subjects[0]+"\t") {
		t.Errorf("XOVER %s: %q, want one line with subject %q", ids[0], got, subjects[0])
	}
}

// TestOverviewMsgIdCollisionCancel: a cancel for the first of the pair removes the first
// (article 1) and not the second (article 2); then a cancel for the second removes it.
// Fails before the fix: the cancel for "<1510900044@gn>" removed article 2.
func TestOverviewMsgIdCollisionCancel(t *testing.T) {
	ids := []string{"<1510900044@gn>", "<1510900045@gn>"}
	s, st, c := collisionSite(t, ids, []string{"first of the pair", "second of the pair"})
	defer c.ExpectCmd(205, "QUIT")

	feedAndSettle(t, s, st, c, cancelArticle(t, ids[0], "e2e.test"))
	if present(t, c, 1) {
		t.Errorf("cancel for %s: article 1 (that Message-ID) is still there", ids[0])
	}
	if !present(t, c, 2) {
		t.Errorf("cancel for %s: article 2 (%s) was removed", ids[0], ids[1])
	}

	feedAndSettle(t, s, st, c, cancelArticle(t, ids[1], "e2e.test"))
	if present(t, c, 2) {
		t.Errorf("cancel for %s: article 2 is still there", ids[1])
	}
}

// TestOverviewMsgIdTwoHeaders: an article fed with two Message-ID: headers is indexed by the
// hash of the last one, so XHDR by either Message-ID must find it (a feed accepts such an
// article; POST refuses it). The overview compare reads only the last header, the one the
// index hash is made from, so this still passes: the lookup by the last one is answered from
// the overview, the one by the first one has another hash and is answered as before. Guards
// the overview check against reading only the first header.
func TestOverviewMsgIdTwoHeaders(t *testing.T) {
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool})
	first, last := msgID(t), strings.Replace(msgID(t), "@", "x@", 1)
	art := newArticle(first, "e2e.test", "two ids", "body")
	for i, l := range art {
		if strings.HasPrefix(l, "Message-ID: ") {
			art = append(art[:i+1], append([]string{"Message-ID: " + last}, art[i+1:]...)...)
			break
		}
	}
	feedReader(t, s, st, "e2e.test", art)
	c := s.Reader(t)
	defer c.ExpectCmd(205, "QUIT")
	testkit.WaitFor(t, testkit.Deadline(), "the article in GROUP e2e.test", func() bool {
		_, line := c.Cmd("GROUP e2e.test")
		f := strings.Fields(line)
		return len(f) > 1 && f[0] == "211" && f[1] == "1"
	})
	for _, id := range []string{first, last} {
		c.ExpectCmd(221, "XHDR Subject %s", id)
		if got := c.ReadDotLines(); len(got) != 1 || got[0] != id+" two ids" {
			t.Errorf("XHDR Subject %s: %q, want [%q]", id, got, id+" two ids")
		}
	}
}

// TestOverviewMsgIdTwoHeadersCollision: article 2 is fed with two Message-ID: headers, the
// first "<1510900044@gn>" (the Message-ID of article 1) and the last "<1510900045@gn>"; it is
// indexed by the hash of the last, which is also the hash of the first (D-210). A lookup or a
// cancel for the first must act on article 1, not on article 2 (newest first, so it is met
// first); a lookup by the last must still find article 2. Fails before the fix: the overview
// compare took a match against any Message-ID: header of the record, so XHDR of
// "<1510900044@gn>" answered the subject of article 2 and the cancel removed article 2.
func TestOverviewMsgIdTwoHeadersCollision(t *testing.T) {
	first, last := "<1510900044@gn>", "<1510900045@gn>"
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:   spool,
		Config:  map[string]string{"msgidhash": "hash", "readercanlock": "all"},
		Files:   map[string]string{"dcontrol.ctl": "cancel:*:*:doit,break\n"},
		NoStart: true,
	})
	_, port, _ := strings.Cut(s.Addr, ":")
	startReader(t, s, "[127.0.0.1]:"+port)
	feedReader(t, s, st, "e2e.test", newArticle(first, "e2e.test", "article one", "body"))
	art := newArticle(first, "e2e.test", "article two", "body")
	for i, l := range art {
		if strings.HasPrefix(l, "Message-ID: ") {
			art = append(art[:i+1], append([]string{"Message-ID: " + last}, art[i+1:]...)...)
			break
		}
	}
	// the feed's Message-ID is the last header's (the one the article is indexed by); the
	// spool server (fakeserver) cannot hold an article whose ID differs from its first header,
	// and the overview answers the commands below
	p := s.dialFrom(t, PeerIP)
	p.ExpectCmd(335, "IHAVE %s", last)
	p.MustSend(client.Stuff(art))
	p.ExpectStatus(235, "IHAVE "+last)
	p.ExpectCmd(205, "QUIT")

	c := s.Reader(t)
	defer c.ExpectCmd(205, "QUIT")
	testkit.WaitFor(t, testkit.Deadline(), "both articles in GROUP e2e.test", func() bool {
		_, line := c.Cmd("GROUP e2e.test")
		f := strings.Fields(line)
		return len(f) > 1 && f[0] == "211" && f[1] == "2"
	})
	check := func(id, subject string) {
		t.Helper()
		c.ExpectCmd(221, "XHDR Subject %s", id)
		if got := c.ReadDotLines(); len(got) != 1 || got[0] != id+" "+subject {
			t.Errorf("XHDR Subject %s: %q, want [%q]", id, got, id+" "+subject)
		}
	}
	check(first, "article one")
	check(last, "article two")
	c.ExpectCmd(224, "XOVER %s", first)
	if got := c.ReadDotLines(); len(got) != 1 || !strings.Contains(got[0], "\tarticle one\t") {
		t.Errorf("XOVER %s: %q, want one line with subject %q", first, got, "article one")
	}

	feedAndSettle(t, s, st, c, cancelArticle(t, first, "e2e.test"))
	if present(t, c, 1) {
		t.Errorf("cancel for %s: article 1 (that Message-ID) is still there", first)
	}
	if !present(t, c, 2) {
		t.Errorf("cancel for %s: article 2 (indexed by %s) was removed", first, last)
	}
	check(last, "article two")
}
