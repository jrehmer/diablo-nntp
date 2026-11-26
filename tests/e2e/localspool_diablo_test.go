package e2e

// D-233: localspool= with a real diablo as the spool server.  dreaderd asks "whereis <id> REL",
// and the feeder names the spool file with the offset and length of the article text; dreaderd
// then reads the article from the file itself (dreaderd/spool.c NNGetLocal1, dreaderd/dfa.c).
// What it sends its client from the file must be what it sends when it fetches the same article
// from the feeder over NNTP.  A record it cannot read in place (a gzip store, wireformat) is
// refused by the feeder (503), and the reader fetches the article with ARTICLE instead.

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"
	"testing"

	"github.com/go-while/nntp-testkit/client"
)

// lsFeederNewsfeeds lets PeerIP feed and 127.0.0.1 (the reader) ask for articles and WHEREIS.
const lsFeederNewsfeeds = GlobalLabel + "label peer\n    inhost " + PeerIP + "\n    addgroup *\nend\n" +
	"label rdr\n    inhost 127.0.0.1\n    readonly yes\n    whereis yes\n    addgroup *\nend\n"

// lsAccessOther gives the reader a cluster name that is not the feeder's pathhost, so that its
// Path and Xref rewriting changes what the feeder stored.
const lsAccessOther = `vserverdef DEFAULT
    hostname reader.example.test
end
readerdef rp
    read yes
    post yes
end
readerdef feed
    feed yes
end
access 127.0.0.1 rp
access ` + PeerIP + ` feed
`

// lsFeeder starts a feeder site (so.Newsfeeds is set here) and returns it with its spool directory.
func lsFeeder(t *testing.T, so SiteOptions) (*Site, string) {
	t.Helper()
	so.Newsfeeds = lsFeederNewsfeeds
	f := NewSite(t, so)
	return f, strings.TrimSpace(f.Run(t, "dpath", "spool"))
}

// lsStore sends the articles (lines, as newArticle returns them) to the feeder with IHAVE.
func lsStore(t *testing.T, f *Site, arts ...[]string) []string {
	t.Helper()
	p := f.dialFrom(t, PeerIP)
	ids := make([]string, len(arts))
	for i, a := range arts {
		ids[i] = headerValue(a, "Message-ID")
		p.ExpectCmd(335, "IHAVE %s", ids[i])
		p.MustSend(client.Stuff(a))
		p.ExpectStatus(235, "IHAVE "+ids[i])
	}
	p.ExpectCmd(205, "QUIT")
	return ids
}

// lsProxy sits between a reader and the feeder and records the commands the reader sends.
type lsProxy struct {
	Addr string
	mu   sync.Mutex
	cmds []string
}

func newLsProxy(t *testing.T, target string) *lsProxy {
	t.Helper()
	l, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { l.Close() })
	p := &lsProxy{Addr: l.Addr().String()}
	go func() {
		for {
			c, err := l.Accept()
			if err != nil {
				return
			}
			s, err := net.Dial("tcp", target)
			if err != nil {
				c.Close()
				continue
			}
			t.Cleanup(func() { c.Close(); s.Close() })
			go io.Copy(c, s)
			go func() {
				r := bufio.NewReader(c)
				for {
					line, err := r.ReadString('\n')
					if err != nil {
						return
					}
					if f := strings.Fields(line); len(f) > 0 {
						p.mu.Lock()
						p.cmds = append(p.cmds, strings.ToLower(f[0]))
						p.mu.Unlock()
					}
					if _, err := io.WriteString(s, line); err != nil {
						return
					}
				}
			}()
		}
	}()
	return p
}

// count returns how many times the reader sent the command.
func (p *lsProxy) count(cmd string) int {
	p.mu.Lock()
	defer p.mu.Unlock()
	n := 0
	for _, c := range p.cmds {
		if c == cmd {
			n++
		}
	}
	return n
}

// lsReader starts a reader whose only spool server is the feeder (through the proxy), with
// localspool=dir when dir is not empty.
func lsReader(t *testing.T, f *Site, dir string, cfg map[string]string, access string) (*Site, *lsProxy) {
	t.Helper()
	px := newLsProxy(t, f.Addr)
	port := px.Addr[strings.LastIndex(px.Addr, ":")+1:]
	hosts := "127.0.0.1\ts1p" + port + "\n"
	if dir != "" {
		hosts = "127.0.0.1\ts1p" + port + " localspool=" + dir + "\n"
	}
	return NewReaderSite(t, ReaderOptions{SpoolAddr: px.Addr, Config: cfg, Access: access,
		Files: map[string]string{"dserver.hosts": hosts}}), px
}

// lsFetch sends cmd (ARTICLE or BODY with the message-id) on c and returns the status line and
// the raw reply block, dot-stuffing and CRLF as sent.
func lsFetch(t *testing.T, c *client.Conn, cmd, id string) (string, []byte) {
	t.Helper()
	code, line := c.Cmd("%s %s", cmd, id)
	var raw []byte
	if code == 220 || code == 222 {
		raw = c.RawUntilDot()
	}
	return line, raw
}

// lsCompare fetches every article with ARTICLE and BODY through a reader that asks the feeder
// over NNTP (the reference) and one with localspool=, and reports every difference.  It returns
// the local reader's ARTICLE replies and how often it sent whereis and article to the feeder.
func lsCompare(t *testing.T, f *Site, dir string, cfg map[string]string, access string, ids []string) (arts map[string][]byte, whereis, article int) {
	t.Helper()
	wire, _ := lsReader(t, f, "", cfg, access)
	local, px := lsReader(t, f, dir, cfg, access)
	cw, cl := wire.Reader(t), local.Reader(t)
	arts = map[string][]byte{}
	for _, id := range ids {
		for _, cmd := range []string{"ARTICLE", "BODY"} {
			stW, rawW := lsFetch(t, cw, cmd, id)
			stL, rawL := lsFetch(t, cl, cmd, id)
			if !strings.HasPrefix(stW, "22") {
				t.Fatalf("%s %s over NNTP: %q", cmd, id, stW)
			}
			if stW != stL || !bytes.Equal(rawW, rawL) {
				t.Errorf("%s %s: localspool differs from the fetch over NNTP\nNNTP:       %q %q\nlocalspool: %q %q", cmd, id, stW, rawW, stL, rawL)
			}
			if cmd == "ARTICLE" {
				arts[id] = rawL
			}
		}
	}
	cw.ExpectCmd(205, "QUIT")
	cl.ExpectCmd(205, "QUIT")
	return arts, px.count("whereis"), px.count("article")
}

// lsArticles returns articles that go through the local read in different ways: a plain one, a
// body with dot lines, and one of 100 KB (over the 64 KiB the reader reads at a time) with a
// dot line now and then.
func lsArticles(t *testing.T) [][]string {
	t.Helper()
	plain := newArticle(msgID(t), "e2e.test", "localspool plain", "line one", "line two")
	dots := newArticle(msgID(t), "e2e.test", "localspool dots", "first", ".", "..two dots", "last")
	big := make([]string, 1400)
	for i := range big {
		big[i] = fmt.Sprintf("%05d %s", i, strings.Repeat("x", 66))
		if i%97 == 0 {
			big[i] = "." + big[i]
		}
	}
	return [][]string{plain, dots, newArticle(msgID(t), "e2e.test", "localspool big", big...)}
}

// TestLocalspoolDiabloPlain: a plain text spool.  The feeder answers whereis with the article
// text (not the SpoolArtHdr in front of it), so the reader serves ARTICLE and BODY from the file
// (no ARTICLE sent to the feeder) and the reply is the one it gives from an NNTP fetch: starts
// with the headers, the Path header with the reader's cluster name in front (not a second time
// when the Path starts with it already, as it does with the feeder's own name), no Xref header
// made up (the feeder keeps none), the body byte for byte. Fails on main: the reply starts with
// the 24 binary bytes of the SpoolArtHdr, and "Path: ...!not-for-mail" and "XRef: ... no-xref"
// lines are added.
func TestLocalspoolDiabloPlain(t *testing.T) {
	for _, tc := range []struct{ name, access, path string }{
		{"cluster-name-is-the-feeders", "", "Path: " + PathHost + "!127.0.0.2.MISMATCH!e2e.peer!not-for-mail\r\n"},
		{"other-cluster-name", lsAccessOther, "Path: reader.example.test!" + PathHost + "!127.0.0.2.MISMATCH!e2e.peer!not-for-mail\r\n"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			f, dir := lsFeeder(t, SiteOptions{})
			ids := lsStore(t, f, lsArticles(t)...)
			arts, whereis, article := lsCompare(t, f, dir, nil, tc.access, ids)
			if whereis != len(ids)*2 || article != 0 {
				t.Errorf("the reader sent %d whereis and %d article to the feeder, want %d and 0 (served from the file)",
					whereis, article, len(ids)*2)
			}
			for _, id := range ids {
				a := string(arts[id])
				if !strings.HasPrefix(a, tc.path) || strings.IndexByte(a, 0xff) >= 0 || strings.IndexByte(a, 0) >= 0 || strings.Contains(strings.ToLower(a), "xref:") {
					t.Errorf("ARTICLE %s starts %q, want %q", id, a[:min(len(a), 100)], tc.path)
				}
			}
		})
	}
}

// TestLocalspoolDiabloFallback: a gzip store (dspool.ctl compresslvl) and a wireformat one are
// not article text in the file.  The feeder refuses whereis for them with 503 and the reader
// fetches the article over NNTP, so what the client gets is what an NNTP fetch gives.  Fails on
// main: whereis names the compressed record, which the reader sends as it is (binary), and the
// wire record is dot-stuffed a second time.
func TestLocalspoolDiabloFallback(t *testing.T) {
	for _, tc := range []struct {
		name string
		so   SiteOptions
		zlib bool
	}{
		{"gzip", SiteOptions{SpoolCtl: compressedSpoolCtl}, true},
		{"wireformat", SiteOptions{Config: map[string]string{"wireformat": "on"}}, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if tc.zlib {
				skipWithoutZlib(t)
			}
			f, dir := lsFeeder(t, tc.so)
			ids := lsStore(t, f, lsArticles(t)...)
			arts, whereis, article := lsCompare(t, f, dir, nil, "", ids)
			if whereis != len(ids)*2 || article != len(ids)*2 {
				t.Errorf("the reader sent %d whereis and %d article to the feeder, want %d of each (whereis refused, then article)",
					whereis, article, len(ids)*2)
			}
			for _, id := range ids {
				if a := arts[id]; !bytes.HasPrefix(a, []byte("Path: ")) || !bytes.Contains(a, []byte("\r\n\r\n")) {
					t.Errorf("ARTICLE %s starts %q", id, a[:min(len(a), 60)])
				}
			}
			// the feeder's own answer to whereis
			p := f.dialFrom(t, "127.0.0.1")
			code, line := p.Cmd("WHEREIS %s REL", ids[0])
			if code != 503 || !strings.Contains(line, "use ARTICLE") {
				t.Errorf("WHEREIS on a %s store: %q, want 503 ... use ARTICLE", tc.name, line)
			}
			p.ExpectCmd(205, "QUIT")
		})
	}
}

// TestLocalspoolDiabloWhereis: the answer to whereis on a plain store names the article text:
// offset and length are those of the text inside the record, and the bytes there are the stored
// article (they start with "Path:" and end with the body's last line).  Fails on main: offset
// of the SpoolArtHdr, 24 bytes more.
func TestLocalspoolDiabloWhereis(t *testing.T) {
	f, dir := lsFeeder(t, SiteOptions{})
	ids := lsStore(t, f, newArticle(msgID(t), "e2e.test", "whereis", "line one", "last line"))
	p := f.dialFrom(t, "127.0.0.1")
	code, line := p.Cmd("WHEREIS %s REL", ids[0])
	if code != 223 {
		t.Fatalf("WHEREIS: %q", line)
	}
	fl := strings.Fields(line)
	var name, off, length string
	for i := range fl {
		switch fl[i] {
		case "in":
			name = fl[i+1]
		case "offset":
			off = fl[i+1]
		case "length":
			length = fl[i+1]
		}
	}
	data, err := os.ReadFile(filepath.Join(dir, name))
	if err != nil {
		t.Fatalf("%q: %v", line, err)
	}
	o, err := strconv.Atoi(off)
	if err != nil {
		t.Fatal(err)
	}
	n, err := strconv.Atoi(length)
	if err != nil {
		t.Fatal(err)
	}
	if o+n > len(data) {
		t.Fatalf("%q: past the end of the %d-byte file", line, len(data))
	}
	got := string(data[o : o+n])
	if !strings.HasPrefix(got, "Path: ") || !strings.HasSuffix(got, "\n\nline one\nlast line\n") {
		t.Errorf("WHEREIS %q names %q", line, got)
	}
	p.ExpectCmd(205, "QUIT")
}

// lsSpoolFile runs a reader whose whereis server (localSpoolServer) names the file <id>.art
// in a temporary directory, writes text there and returns an ARTICLE connection.
func lsSpoolFile(t *testing.T, id, text string) *client.Conn {
	t.Helper()
	dir := t.TempDir()
	l := localSpoolServer(t, dir, 0)
	_, port, _ := net.SplitHostPort(l.Addr().String())
	s := NewReaderSite(t, ReaderOptions{SpoolAddr: l.Addr().String(),
		Files: map[string]string{"dserver.hosts": "127.0.0.1\ts1p" + port + " localspool=" + dir + "\n"}})
	if err := os.WriteFile(filepath.Join(dir, strings.Trim(id, "<>")+".art"), []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
	return s.Reader(t)
}

// lsRewritten is the article lines as the reader sends them with the default cluster name: this
// site's name in front of Path (unless it is first there already), its name in the Xref.
func lsRewritten(lines []string) []string {
	out := slices.Clone(lines)
	for i, l := range out {
		if strings.HasPrefix(l, "Path: ") && !strings.HasPrefix(l, "Path: "+PathHost+"!") {
			out[i] = "Path: " + PathHost + "!" + l[len("Path: "):]
		}
		if strings.HasPrefix(l, "Xref: ") {
			out[i] = "Xref: " + PathHost + l[strings.Index(l[6:], " ")+6:]
		}
	}
	return out
}

// lsPad returns X-Pad header lines of need bytes in all (0, or at least 8).
func lsPad(need int) []string {
	const overhead = len("X-Pad: \n")
	var out []string
	for need > 0 {
		k := need - overhead
		if k > 900 {
			k = 900
			if rest := need - (k + overhead); rest > 0 && rest < overhead {
				k -= overhead
			}
		}
		out = append(out, "X-Pad: "+strings.Repeat("p", k))
		need -= k + overhead
	}
	return out
}

// TestLocalspoolDfaFiveCharHeader: the last header line is 5 characters long and has no space
// in its first four ("X-A:b").  dfa.c state 5 took the end of such a line for a character of
// the line, so the blank line after it was read as the line that continued it and the body was
// parsed as headers: the "." line after "first" was not doubled and ended the reply there.
// Last headers with a space early ("Xy: z"), 6 characters ("X-Abc:") and 6 without a space
// ("X-A:bc") are the controls.  Fails on main for "X-A:b": the reply ends after "first".
func TestLocalspoolDfaFiveCharHeader(t *testing.T) {
	for _, last := range []string{"X-A:b", "Xy: z", "X-Abc:", "X-A:bc"} {
		t.Run(strings.ReplaceAll(last, ":", "_"), func(t *testing.T) {
			id := msgID(t)
			head := []string{
				"Path: e2e.peer!not-for-mail",
				"From: E2E Test <e2e@example.invalid>",
				"Newsgroups: e2e.test",
				"Subject: dfa five",
				"Message-ID: " + id,
				"Date: Thu, 26 Nov 2026 20:00:00 +0000",
				"Xref: spool.example.test e2e.test:1",
				last,
			}
			body := []string{"first", ".", "..two", "last"}
			art := append(append(slices.Clone(head), ""), body...)
			c := lsSpoolFile(t, id, strings.Join(art, "\n")+"\n")
			c.ExpectCmd(220, "ARTICLE %s", id)
			if got, want := c.ReadDotLines(), lsRewritten(art); !slices.Equal(got, want) {
				t.Errorf("ARTICLE with the last header %q:\n got %q\nwant %q", last, got, want)
			}
			c.ExpectCmd(222, "BODY %s", id)
			if got := c.ReadDotLines(); !slices.Equal(got, body) {
				t.Errorf("BODY with the last header %q:\n got %q\nwant %q", last, got, body)
			}
			c.ExpectCmd(205, "QUIT")
		})
	}
}

// TestLocalspoolDfaBodyFirstDot: the first line of the body starts with a dot, or is one dot.  BODY
// skips the headers (JMPBDY) and went on with the line start unknown, so that line was not
// doubled: "." ended the reply at once, and ".x" went out for "..x".  ARTICLE is the control.
// Fails on main for BODY: 222, then the reply ends after the status line.
func TestLocalspoolDfaBodyFirstDot(t *testing.T) {
	for _, first := range []string{".", ".x", "..", "x"} {
		t.Run(strings.NewReplacer(".", "dot", "x", "x").Replace(first), func(t *testing.T) {
			id := msgID(t)
			head := newArticle(id, "e2e.test", "dfa first dot")
			head = head[:len(head)-1] // without the blank line
			head = append(head, "Xref: spool.example.test e2e.test:1")
			body := []string{first, "second", ".", "last"}
			art := append(append(slices.Clone(head), ""), body...)
			c := lsSpoolFile(t, id, strings.Join(art, "\n")+"\n")
			c.ExpectCmd(222, "BODY %s", id)
			if got := c.ReadDotLines(); !slices.Equal(got, body) {
				t.Errorf("BODY with the first body line %q:\n got %q\nwant %q", first, got, body)
			}
			c.ExpectCmd(220, "ARTICLE %s", id)
			if got, want := c.ReadDotLines(), lsRewritten(art); !slices.Equal(got, want) {
				t.Errorf("ARTICLE with the first body line %q:\n got %q\nwant %q", first, got, want)
			}
			c.ExpectCmd(205, "QUIT")
		})
	}
}

// TestLocalspoolDfaBufferEnd: the Xref host and the start of a Path value fall on the end of the
// 64 KiB read buffer.  The Xref host word ends exactly at the end of the buffer: dfa.c's
// WRDDEL state left its position one past the data and sent a stale byte of the buffer in front
// of the next line.  A Path value starts at the end of the buffer, or 3 characters before it:
// its first characters are held back across the two reads to decide whether this site's name is
// there already (a Path that starts with it is not prefixed again, any other is).  Fails on main:
// the stale byte in the Xref case, a Path prefixed twice.
func TestLocalspoolDfaBufferEnd(t *testing.T) {
	const bufsize = 65536
	for _, tc := range []struct {
		name, line string
		cut        int // the buffer ends this many bytes into the line
	}{
		{"xref-host-word-ends-the-buffer", "Xref: spool.example.test e2e.test:1 e2e.other:2", len("Xref: spool.example.test")},
		{"xref-host-word-cut", "Xref: spool.example.test e2e.test:1 e2e.other:2", len("Xref: spool.ex")},
		{"path-with-the-name", "Path: " + PathHost + "!e2e.peer!not-for-mail", len("Path: ") + 3},
		{"path-without-the-name", "Path: other.peer!e2e.peer!not-for-mail", len("Path: ") + 3},
		{"path-value-starts-the-buffer", "Path: " + PathHost + "!e2e.peer!not-for-mail", len("Path: ")},
		{"path-name-ends-the-buffer", "Path: " + PathHost + "!e2e.peer!not-for-mail", len("Path: " + PathHost)},
	} {
		t.Run(tc.name, func(t *testing.T) {
			id := msgID(t)
			head := []string{
				"From: E2E Test <e2e@example.invalid>",
				"Newsgroups: e2e.test",
				"Subject: dfa buffer",
				"Message-ID: " + id,
				"Date: Thu, 26 Nov 2026 20:00:00 +0000",
			}
			// the other one of Path and Xref is there too, so that nothing hangs on one being
			// missing
			if !strings.HasPrefix(tc.line, "Path:") {
				head = append([]string{"Path: e2e.peer!not-for-mail"}, head...)
			}
			if !strings.HasPrefix(tc.line, "Xref:") {
				head = append(head, "Xref: spool.example.test e2e.test:1")
			}
			n := 0
			for _, l := range head {
				n += len(l) + 1
			}
			head = append(head, lsPad(bufsize-tc.cut-n)...)
			head = append(head, tc.line)
			art := append(append(slices.Clone(head), ""), "body", ".", "end")
			text := strings.Join(art, "\n") + "\n"
			if at := strings.Index(text, tc.line); at+tc.cut != bufsize {
				t.Fatalf("the line starts at %d and the buffer ends %d into it: want it to end at %d", at, tc.cut, bufsize)
			}
			c := lsSpoolFile(t, id, text)
			c.ExpectCmd(220, "ARTICLE %s", id)
			got, want := c.ReadDotLines(), lsRewritten(art)
			if !slices.Equal(got, want) {
				for i := range min(len(got), len(want)) {
					if got[i] != want[i] {
						t.Errorf("line %d:\n got %q\nwant %q (%d lines, want %d)", i, got[i], want[i], len(got), len(want))
						break
					}
				}
				if len(got) != len(want) {
					t.Errorf("%d lines, want %d", len(got), len(want))
				}
			}
			c.ExpectCmd(205, "QUIT")
		})
	}
}

// TestLocalspoolHeaderedRecordFallsBack: a whereis answer whose range starts with a SpoolArtHdr
// (magic 0xff 0x99; what an older diablo names for every record, also gzip and wireformat ones)
// is not article text: the reader does not send it but asks for the article (the fake spool
// server has none: 430).  Fails on main: 220 and the binary bytes.
func TestLocalspoolHeaderedRecordFallsBack(t *testing.T) {
	id := msgID(t)
	text := "\xff\x99\x01\x18\x01\x00\x00\x00" + strings.Repeat("\x00", 16) +
		"Path: e2e.peer!not-for-mail\nMessage-ID: " + id + "\n\nbody\n"
	c := lsSpoolFile(t, id, text)
	if code, line := c.Cmd("ARTICLE %s", id); code != 430 {
		t.Fatalf("ARTICLE of a record that starts with a SpoolArtHdr: %q, want 430 (fetched over NNTP, which has none)", line)
	}
	c.ExpectCmd(205, "QUIT")
}
