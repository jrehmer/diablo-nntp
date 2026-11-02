package e2e

import (
	"bufio"
	"fmt"
	"net"
	"strings"
	"testing"
	"time"
)

// RFC 3977 section 3.5: a server MUST allow pipelining (a client sending the next command before the
// response to the previous one), MUST NOT throw away text received after a command, and MUST process the
// commands in the order they are sent. There is no capability for it: it is mandatory.

// pipeReply is one response: the status line and, for a multi-line response, the lines up to the dot.
type pipeReply struct {
	status string
	lines  []string
}

func (r pipeReply) code() string {
	f := strings.Fields(r.status)
	if len(f) == 0 {
		return ""
	}
	return f[0]
}

// pipeline writes all commands in one write after the greeting and reads one response per entry of
// multi (true: a multi-line response), then returns them; extra is whatever else arrives within the
// grace period (data after QUIT must not be answered).
func pipeline(t *testing.T, conn net.Conn, br *bufio.Reader, cmds []string, multi []bool) (replies []pipeReply, extra []string) {
	t.Helper()
	conn.SetDeadline(time.Now().Add(60 * time.Second))
	if _, err := conn.Write([]byte(strings.Join(cmds, "\r\n") + "\r\n")); err != nil {
		t.Fatal(err)
	}
	for i := range multi {
		l, err := br.ReadString('\n')
		if err != nil {
			t.Fatalf("reply %d of %d (%q): %v", i+1, len(multi), cmds[i], err)
		}
		r := pipeReply{status: strings.TrimRight(l, "\r\n")}
		if multi[i] {
			for {
				l, err := br.ReadString('\n')
				if err != nil {
					t.Fatalf("reply %d (%q), multi-line: %v", i+1, cmds[i], err)
				}
				l = strings.TrimRight(l, "\r\n")
				if l == "." {
					break
				}
				r.lines = append(r.lines, l)
			}
		}
		replies = append(replies, r)
	}
	conn.SetReadDeadline(time.Now().Add(1500 * time.Millisecond))
	for {
		l, err := br.ReadString('\n')
		if err != nil {
			break
		}
		extra = append(extra, strings.TrimRight(l, "\r\n"))
	}
	return
}

func pipeDial(t *testing.T, addr, from string) (net.Conn, *bufio.Reader) {
	t.Helper()
	d := net.Dialer{Timeout: 10 * time.Second}
	if from != "" {
		d.LocalAddr = &net.TCPAddr{IP: net.ParseIP(from)}
	}
	c, err := d.Dial("tcp", addr)
	if err != nil {
		t.Fatalf("dial %s: %v", addr, err)
	}
	t.Cleanup(func() { c.Close() })
	br := bufio.NewReader(c)
	c.SetReadDeadline(time.Now().Add(10 * time.Second))
	if _, err := br.ReadString('\n'); err != nil {
		t.Fatalf("greeting: %v", err)
	}
	return c, br
}

// pipeReader returns a reader site with one article in e2e.test, and the article's message-id.
func pipeReader(t *testing.T) (*Site, string) {
	t.Helper()
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool})
	id := msgID(t)
	feedReader(t, s, st, "e2e.test", newArticle(id, "e2e.test", "pipelining", "hello pipeline", ".dot line"))
	c := s.Reader(t)
	waitFor(t, 15*time.Second, "the article in GROUP e2e.test", func() bool {
		_, line := c.Cmd("GROUP e2e.test")
		f := strings.Fields(line)
		return len(f) > 1 && f[0] == "211" && f[1] != "0"
	})
	c.Cmd("QUIT")
	return s, id
}

// TestPipeliningReaderMixed: a batch of different commands in one write is answered completely and in
// order, errors included, and nothing sent after QUIT is answered.
func TestPipeliningReaderMixed(t *testing.T) {
	s, id := pipeReader(t)
	conn, br := pipeDial(t, s.Addr, "")
	type step struct {
		cmd   string
		code  string
		multi bool
	}
	steps := []step{
		{"GROUP e2e.test", "211", false},
		{"STAT 1", "223", false},
		{"HEAD 1", "221", true},
		{"BODY 1", "222", true},
		{"ARTICLE 1", "220", true},
		{"OVER 1-1", "224", true},
		{"LISTGROUP e2e.test", "211", true},
		{"DATE", "111", false},
		{"CAPABILITIES", "101", true},
		{"FROBNICATE", "500", false},
		{"STAT 99", "423", false},
		{"STAT <" + strings.Trim(id, "<>") + ">", "223", false},
		{"HELP", "100", true},
		{"QUIT", "205", false},
	}
	var cmds []string
	var multi []bool
	for _, st := range steps {
		cmds = append(cmds, st.cmd)
		multi = append(multi, st.multi)
	}
	// data after QUIT in the same write: not answered
	cmds = append(cmds, "DATE", "DATE")
	replies, extra := pipeline(t, conn, br, cmds, multi)
	for i, st := range steps {
		if replies[i].code() != st.code {
			t.Errorf("%s: %q, want a %s response", st.cmd, replies[i].status, st.code)
		}
	}
	if len(extra) != 0 {
		t.Errorf("answers to commands sent after QUIT: %q", extra)
	}
	// the multi-line responses hold what they should
	art := replies[4].lines
	if len(art) < 3 || !slicesContains(art, "hello pipeline") || !slicesContains(art, "..dot line") && !slicesContains(art, ".dot line") {
		t.Errorf("ARTICLE 1 in the pipeline: %q", art)
	}
	if got := replies[6].lines; len(got) != 1 || got[0] != "1" {
		t.Errorf("LISTGROUP in the pipeline: %q, want [1]", got)
	}
}

func slicesContains(l []string, s string) bool {
	for _, x := range l {
		if x == s {
			return true
		}
	}
	return false
}

// TestPipeliningReaderAlternating: successes and failures alternate in one write and every response
// belongs to its command (the client matches them by position).
func TestPipeliningReaderAlternating(t *testing.T) {
	s, _ := pipeReader(t)
	conn, br := pipeDial(t, s.Addr, "")
	cmds := []string{"GROUP e2e.test"}
	multi := []bool{false}
	want := []string{"211"}
	for i := 0; i < 200; i++ {
		cmds = append(cmds, "STAT 1", "STAT 99", "NOSUCHCOMMAND")
		multi = append(multi, false, false, false)
		want = append(want, "223", "423", "500")
	}
	replies, _ := pipeline(t, conn, br, cmds, multi)
	for i := range want {
		if replies[i].code() != want[i] {
			t.Fatalf("response %d to %q: %q, want %s", i, cmds[i], replies[i].status, want[i])
		}
	}
}

// TestPipeliningReaderLarge: far more than the 64 KB input buffer of dreaderd, in one write (the writer
// does not wait for responses, so the output side must not deadlock): 500 whole articles, and 30,000
// DATE commands. Every command is answered, in order.
func TestPipeliningReaderLarge(t *testing.T) {
	s, id := pipeReader(t)
	t.Run("articles", func(t *testing.T) {
		conn, br := pipeDial(t, s.Addr, "")
		cmds := []string{"GROUP e2e.test"}
		multi := []bool{false}
		for i := 0; i < 500; i++ {
			cmds = append(cmds, "ARTICLE 1")
			multi = append(multi, true)
		}
		replies, _ := pipeline(t, conn, br, cmds, multi)
		for i, r := range replies[1:] {
			if !strings.HasPrefix(r.status, "220 1 "+id) {
				t.Fatalf("article %d: %q", i, r.status)
			}
			if !slicesContains(r.lines, "hello pipeline") {
				t.Fatalf("article %d: body %q", i, r.lines)
			}
		}
	})
	t.Run("commands", func(t *testing.T) {
		conn, br := pipeDial(t, s.Addr, "")
		var cmds []string
		var multi []bool
		for i := 0; i < 30000; i++ {
			cmds = append(cmds, "DATE")
			multi = append(multi, false)
		}
		cmds = append(cmds, "QUIT")
		multi = append(multi, false)
		replies, extra := pipeline(t, conn, br, cmds, multi)
		for i, r := range replies[:30000] {
			if r.code() != "111" {
				t.Fatalf("response %d: %q", i, r.status)
			}
		}
		if replies[30000].code() != "205" || len(extra) != 0 {
			t.Errorf("end: %q, extra %q", replies[30000].status, extra)
		}
	})
}

// TestPipeliningFeederStreaming (RFC 4644): MODE STREAM, hundreds of CHECK and interleaved TAKETHIS
// with their articles in one write; every response comes in order with its message-id, and the
// articles taken are known afterwards.
func TestPipeliningFeederStreaming(t *testing.T) {
	s := NewSite(t, SiteOptions{})
	conn, br := pipeDial(t, s.Addr, PeerIP)
	cmds := []string{"MODE STREAM"}
	multi := []bool{false}
	want := []string{"203"}
	var b strings.Builder
	for i := 0; i < 300; i++ {
		cmds = append(cmds, fmt.Sprintf("CHECK <chk%d@e2e.test>", i))
		multi = append(multi, false)
		want = append(want, fmt.Sprintf("238 <chk%d@e2e.test>", i))
	}
	for i := 0; i < 50; i++ {
		id := fmt.Sprintf("<take%d@e2e.test>", i)
		// the article follows its TAKETHIS in the same write; the dot ends it
		b.Reset()
		fmt.Fprintf(&b, "TAKETHIS %s\r\n%s\r\n.", id, strings.Join(newArticle(id, "e2e.test", "take", "body"), "\r\n"))
		cmds = append(cmds, b.String(), fmt.Sprintf("CHECK <after%d@e2e.test>", i))
		multi = append(multi, false, false)
		want = append(want, "239 "+id, fmt.Sprintf("238 <after%d@e2e.test>", i))
	}
	cmds = append(cmds, "QUIT")
	multi = append(multi, false)
	want = append(want, "205")
	replies, _ := pipeline(t, conn, br, cmds, multi)
	for i, r := range replies {
		if !strings.HasPrefix(r.status, want[i]) {
			t.Fatalf("response %d: %q, want %q...", i, r.status, want[i])
		}
	}
	// the articles are in the history now
	conn2, br2 := pipeDial(t, s.Addr, PeerIP)
	replies, _ = pipeline(t, conn2, br2, []string{"MODE STREAM", "CHECK <take0@e2e.test>", "CHECK <take49@e2e.test>", "QUIT"}, []bool{false, false, false, false})
	if !strings.HasPrefix(replies[1].status, "438") || !strings.HasPrefix(replies[2].status, "438") {
		t.Errorf("CHECK of taken articles: %q, %q; want 438", replies[1].status, replies[2].status)
	}
}
