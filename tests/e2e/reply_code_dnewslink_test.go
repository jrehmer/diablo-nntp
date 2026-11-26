package e2e

// Reply codes in dnewslink: the whole three-digit code, compared with the codes the command
// defines; an unknown code is an error, and a reply to MODE STREAM or MODE COMPRESS that is out
// of step does not leave the connection in use.

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"testing"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// rcuDnewslink writes the queue file out.S00000 with one entry for id (the spool file of
// dnlSpool), runs dnewslink once on it against srv with the article log (-A) and returns its
// output.
func rcuDnewslink(t *testing.T, s *Site, srv *fakeserver.Server, id string, args ...string) string {
	t.Helper()
	q := "D.00000001/B.0001 " + id + " 0,100\n"
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	a := append([]string{"-Aall", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(srv.Port()), "-t", "5", "-w", "1"}, args...)
	out, err := s.TryRun("dnewslink", a...)
	t.Logf("dnewslink: err=%v\n%s", err, out)
	return out
}

// rcuQueued fails the test unless the queue file out.S00000 holds id on exactly one line.
func rcuQueued(t *testing.T, s *Site, id string) {
	t.Helper()
	got := dnlQueue(t, s, "out.S00000")
	n := 0
	for _, l := range strings.Split(got, "\n") {
		if strings.Contains(l, id) {
			n++
		}
	}
	if n != 1 {
		t.Errorf("%s is on %d lines of the queue file, want 1 (the article must stay queued); queue file:\n%s", id, n, got)
	}
}

// rcuFeedlog returns dnewslink's article log for the peer 127.0.0.1 ("" when there is none).
func rcuFeedlog(s *Site) string {
	b, _ := os.ReadFile(filepath.Join(s.Dir, "log", "feedlog.127.0.0.1"))
	return string(b)
}

// rcuSent fails the test unless the article id is out of the queue file out.S00000 and the
// connection was ended with QUIT, not failed: one connection, one QUIT.
func rcuSent(t *testing.T, s *Site, srv *fakeserver.Server, id string) {
	t.Helper()
	if got := dnlQueue(t, s, "out.S00000"); strings.Contains(got, id) {
		t.Errorf("%s is still queued:\n%s", id, got)
	}
	if a, q := srv.Accepted(), srv.Count("QUIT"); a != 1 || q != 1 {
		t.Errorf("%d connections and %d QUIT, want 1 and 1: the connection was failed (lines %q)", a, q, srv.Lines())
	}
}

// TestDnewslinkIhave437Rejects: 437 belongs to the article, not to the IHAVE offer (RFC 3977
// 6.3.2), but some servers answer the offer with it. dnewslink takes it as a one-line reject:
// the article is logged as rejected and leaves the queue, and the connection goes on (it ends
// with QUIT). A failed connection would offer the same article again on every run.
func TestDnewslinkIhave437Rejects(t *testing.T) {
	const id = "<rcu-ihave@e2e.test>"
	s := NewSite(t, SiteOptions{NoStart: true})
	dnlSpool(t, s)
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
		l := strings.ToLower(strings.TrimSpace(line))
		switch {
		case strings.HasPrefix(l, "mode stream"):
			return []string{"501 no streaming"}, false
		case strings.HasPrefix(l, "ihave "):
			return []string{"437 " + id + " no"}, false
		case strings.HasPrefix(l, "quit"):
			return []string{"205 bye"}, true
		}
		return nil, false
	})})
	rcuDnewslink(t, s, srv, id)
	if log := rcuFeedlog(s); !strings.Contains(log, id+" rejected") {
		t.Errorf("437 to the IHAVE offer was not logged as a reject:\n%s", log)
	}
	rcuSent(t, s, srv, id)
}

// TestDnewslinkTakethis431Defers: 431 is not a TAKETHIS reply in RFC 4644, but Diablo's own
// feeder sends it (its history busy or failed). dnewslink defers the article once and offers it
// again at the end of the queue, on the same connection: taken then (239), or, when a second
// 431 comes while the deferred articles are sent, kept in the queue file for a later run (no
// second defer is taken, and the article must not be lost: D-214). The connection is not
// failed. Fails on main for "then 239": the first 431 was a reject at once, the article was not
// offered again; and for "431 again" (D-214): the second 431 was a reject, the article left
// the queue.
func TestDnewslinkTakethis431Defers(t *testing.T) {
	const id = "<rcu-431@e2e.test>"
	for _, tc := range []struct {
		name   string
		second string // the answer to the second TAKETHIS
		log    string // what the feedlog says of the article in the end
		kept   bool   // the article is in the queue file in the end
	}{
		{"then 239", "239 " + id, " accepted", false},
		{"431 again", "431 " + id + " later", " kept", true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s := NewSite(t, SiteOptions{NoStart: true})
			dnlSpool(t, s)
			var takes atomic.Int32
			srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
				l := strings.ToLower(strings.TrimSpace(line))
				switch {
				case strings.HasPrefix(l, "mode stream"):
					return []string{"203 stream ok"}, false
				case strings.HasPrefix(l, "check "):
					return []string{"238 " + id}, false
				case strings.HasPrefix(l, "takethis "):
					if takes.Add(1) == 1 {
						return []string{"431 " + id + " later"}, false
					}
					return []string{tc.second}, false
				case strings.HasPrefix(l, "quit"):
					return []string{"205 bye"}, true
				}
				return nil, false
			})})
			rcuDnewslink(t, s, srv, id)
			if n := srv.Count("TAKETHIS"); n != 2 {
				t.Errorf("TAKETHIS sent %d times, want 2 (the article deferred once after the 431)", n)
			}
			log := rcuFeedlog(s)
			for _, want := range []string{id + " deferred", id + tc.log} {
				if !strings.Contains(log, want) {
					t.Errorf("feedlog has no %q:\n%s", want, log)
				}
			}
			if tc.kept {
				rcuQueued(t, s, id)
				if a, q := srv.Accepted(), srv.Count("QUIT"); a != 1 || q != 1 {
					t.Errorf("%d connections and %d QUIT, want 1 and 1 (lines %q)", a, q, srv.Lines())
				}
			} else {
				rcuSent(t, s, srv, id)
			}
		})
	}
}

// TestDnewslinkCheck431Twice: a peer that answers every CHECK with 431 (Diablo's feeder does
// while another connection has the article in progress). dnewslink defers the article once;
// offered again, the second 431 counts as 238 and the article goes with TAKETHIS (taken, 239)
// on the same connection. Fails on main: the reply to the second CHECK comes when the defer
// replay is over, the article was deferred again and offered again without end.
func TestDnewslinkCheck431Twice(t *testing.T) {
	const id = "<rcu-check431@e2e.test>"
	s := NewSite(t, SiteOptions{NoStart: true})
	dnlSpool(t, s)
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
		l := strings.ToLower(strings.TrimSpace(line))
		switch {
		case strings.HasPrefix(l, "mode stream"):
			return []string{"203 stream ok"}, false
		case strings.HasPrefix(l, "check "):
			return []string{"431 " + id + " later"}, false
		case strings.HasPrefix(l, "takethis "):
			return []string{"239 " + id}, false
		case strings.HasPrefix(l, "quit"):
			return []string{"205 bye"}, true
		}
		return nil, false
	})})
	rcuDnewslink(t, s, srv, id)
	if c, tk := srv.Count("CHECK"), srv.Count("TAKETHIS"); c != 2 || tk != 1 {
		t.Errorf("CHECK sent %d times and TAKETHIS %d, want 2 and 1 (deferred once, then sent)", c, tk)
	}
	log := rcuFeedlog(s)
	for _, want := range []string{id + " deferred", id + " accepted"} {
		if !strings.Contains(log, want) {
			t.Errorf("feedlog has no %q:\n%s", want, log)
		}
	}
	rcuSent(t, s, srv, id)
}

// TestDnewslinkCheck431DuringReplay: the peer answers the first CHECK of every article with 431.
// After 80 defers (MAXDEFER 100 less 20) dnewslink starts sending the deferred ones while other
// articles still wait for their reply; a 431 to one of those comes while the defers are sent,
// when no second defer is taken. It counts as 238 then and the article goes with TAKETHIS:
// all 120 articles reach the peer and none is dropped as "error-doubledefer". Fails on 1d9180c4
// (an earlier head of this change), where such a 431 asked for a defer and the article was lost.
func TestDnewslinkCheck431DuringReplay(t *testing.T) {
	const n = 120
	s := NewSite(t, SiteOptions{NoStart: true})
	dnlSpool(t, s)
	var mu sync.Mutex
	seen := map[string]bool{}
	taken := map[string]bool{}
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
		l := strings.ToLower(strings.TrimSpace(line))
		f := strings.Fields(l)
		mu.Lock()
		defer mu.Unlock()
		switch {
		case strings.HasPrefix(l, "mode stream"):
			return []string{"203 stream ok"}, false
		case strings.HasPrefix(l, "check ") && len(f) > 1:
			if !seen[f[1]] {
				seen[f[1]] = true
				return []string{"431 " + f[1] + " later"}, false
			}
			return []string{"238 " + f[1]}, false
		case strings.HasPrefix(l, "takethis ") && len(f) > 1:
			taken[f[1]] = true
			return []string{"239 " + f[1]}, false
		case strings.HasPrefix(l, "quit"):
			return []string{"205 bye"}, true
		}
		return nil, false
	})})
	var q strings.Builder
	for i := 1; i <= n; i++ {
		q.WriteString("D.00000001/B.0001 <rcu-replay" + strconv.Itoa(i) + "@e2e.test> " + strconv.Itoa(i*100) + ",100\n")
	}
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-Aall", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(srv.Port()), "-t", "5", "-w", "1", "-M", "20")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	log := rcuFeedlog(s)
	if strings.Contains(log, "doubledefer") {
		t.Errorf("an article was dropped as a second defer during the replay:\n%s", log)
	}
	mu.Lock()
	got := len(taken)
	mu.Unlock()
	if got != n {
		t.Errorf("%d of the %d articles reached the peer with TAKETHIS, want all", got, n)
	}
	if left := dnlQueue(t, s, "out.S00000"); !strings.HasPrefix(left, "<missing") && strings.TrimSpace(left) != "" {
		t.Errorf("queue file should be empty or gone, it holds: %.300q", left)
	}
}

// TestDnewslinkModeReplyOutOfStep: a reply to MODE STREAM or MODE COMPRESS that the command does
// not have (here a code with lines after it) ends the connection; dnewslink does not go on
// without the mode on the same stream, where the left-over line would be read as the answer to
// the first offer. The article stays queued and is not offered. A one-line refusal still
// falls back (TestDnewslinkIHAVEFallback; "MODE COMPRESS 403" here). Fails on main: dnewslink fell back on the same
// connection and took the left-over "435"/"438" line for the peer's refusal of the article,
// which was dropped from the queue without being offered.
func TestDnewslinkModeReplyOutOfStep(t *testing.T) {
	const id = "<rcu-mode@e2e.test>"
	t.Run("MODE STREAM", func(t *testing.T) {
		s := NewSite(t, SiteOptions{NoStart: true})
		dnlSpool(t, s)
		srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
			l := strings.ToLower(strings.TrimSpace(line))
			switch {
			case strings.HasPrefix(l, "mode stream"):
				return []string{"215 list follows", "435 " + id + " left over", "."}, false
			case strings.HasPrefix(l, "quit"):
				return []string{"205 bye"}, true
			}
			return nil, false // no answer to IHAVE: only the left-over line could answer it
		})})
		out := rcuDnewslink(t, s, srv, id)
		if srv.Accepted() < 1 {
			t.Fatalf("dnewslink did not connect")
		}
		if n := srv.Count("IHAVE"); n != 0 {
			t.Errorf("IHAVE sent %d times after an out-of-step MODE STREAM reply, want 0 (lines %q)\n%s", n, srv.Lines(), out)
		}
		rcuQueued(t, s, id)
		if log := rcuFeedlog(s); strings.Contains(log, id+" refused") {
			t.Errorf("the left-over line was taken as the refusal of the article:\n%s", log)
		}
	})
	t.Run("MODE COMPRESS", func(t *testing.T) {
		skipWithoutZlib(t)
		s := NewSite(t, SiteOptions{NoStart: true})
		dnlSpool(t, s)
		srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
			l := strings.ToLower(strings.TrimSpace(line))
			switch {
			case strings.HasPrefix(l, "mode stream"):
				return []string{"203 stream ok"}, false
			case strings.HasPrefix(l, "mode compress"):
				return []string{"290 odd", "438 " + id + " left over"}, false
			case strings.HasPrefix(l, "quit"):
				return []string{"205 bye"}, true
			}
			return nil, false // no answer to CHECK
		})})
		out := rcuDnewslink(t, s, srv, id, "-Z", "6")
		if srv.Accepted() < 1 {
			t.Fatalf("dnewslink did not connect")
		}
		if n := srv.Count("CHECK"); n != 0 {
			t.Errorf("CHECK sent %d times after an out-of-step MODE COMPRESS reply, want 0 (lines %q)\n%s", n, srv.Lines(), out)
		}
		rcuQueued(t, s, id)
		if log := rcuFeedlog(s); strings.Contains(log, id+" refused") {
			t.Errorf("the left-over line was taken as the refusal of the article:\n%s", log)
		}
	})
	t.Run("MODE COMPRESS 403", func(t *testing.T) {
		// Diablo's feeder answers "403 Compression not available" when it cannot set up
		// compression: a one-line refusal, dnewslink goes on without compression on the same
		// connection (a compressed CHECK would not be read here).
		skipWithoutZlib(t)
		s := NewSite(t, SiteOptions{NoStart: true})
		dnlSpool(t, s)
		srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
			l := strings.ToLower(strings.TrimSpace(line))
			switch {
			case strings.HasPrefix(l, "mode stream"):
				return []string{"203 stream ok"}, false
			case strings.HasPrefix(l, "mode compress"):
				return []string{"403 Compression not available"}, false
			case strings.HasPrefix(l, "check "):
				return []string{"238 " + id}, false
			case strings.HasPrefix(l, "takethis "):
				return []string{"239 " + id}, false
			case strings.HasPrefix(l, "quit"):
				return []string{"205 bye"}, true
			}
			return nil, false
		})})
		rcuDnewslink(t, s, srv, id, "-Z", "6")
		if n := srv.Count("TAKETHIS"); n != 1 {
			t.Errorf("TAKETHIS sent %d times after 403 to MODE COMPRESS, want 1 (lines %q)", n, srv.Lines())
		}
		if log := rcuFeedlog(s); !strings.Contains(log, id+" accepted") {
			t.Errorf("the article was not accepted without compression:\n%s", log)
		}
		rcuSent(t, s, srv, id)
	})
}
