package e2e

// dnewslink and the replies of a streaming peer that do not fit the state of the article
// they answer (D-213), a 431 to TAKETHIS that must not lose the article (D-214) and the mark
// of a deferred article across a lost connection (D-215). The peers are fakeserver handlers.

import (
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// TestDnewslinkStreamReplyOutOfStep (D-213): a stream reply that matches no pending article in
// the state it answers is a peer out of step (RFC 4644 2.4.2, 2.5.2: 238 and 438 answer a
// CHECK, 239 and 439 a TAKETHIS, each with the Message-ID of the article). The reply that the
// pending article waits for may never come, so dnewslink fails the connection at once, as for an
// unknown reply code, instead of waiting for the read timeout (-t); the pending article goes
// back through the retry list and is offered again on the new connection.
//
// Input: two queued articles; the peer refuses the first (438, so that a failed connection is
// retried, not given up) and answers the second with one of the replies below and then stays
// silent; on later connections every CHECK is refused. Expected: dnewslink is done long before
// the 6 s read timeout and the article is offered again on the second connection. Fails on
// main: the reply is read and dropped, dnewslink waits the 6 s for the answer that never comes.
func TestDnewslinkStreamReplyOutOfStep(t *testing.T) {
	const (
		warm  = "<sr-warm@e2e.test>"
		id    = "<sr-id@e2e.test>"
		other = "<sr-other@e2e.test>"
	)
	for _, tc := range []struct {
		name  string
		check string // the answer to the CHECK of id on connection 1 ("": 238)
		take  string // the answer to its TAKETHIS on connection 1
	}{
		{"239 to CHECK", "239 " + id, ""},
		{"439 to CHECK", "439 " + id, ""},
		{"CHECK answered for another id", "438 " + other, ""},
		{"238 to TAKETHIS", "", "238 " + id},
		{"438 to TAKETHIS", "", "438 " + id},
		{"TAKETHIS answered for another id", "", "239 " + other},
	} {
		t.Run(tc.name, func(t *testing.T) {
			s := NewSite(t, SiteOptions{NoStart: true})
			dnlSpool(t, s)
			var (
				mu      sync.Mutex
				offered bool // CHECK for id on a later connection
			)
			srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(
				func(c *fakeserver.Conn, line string) ([]string, bool) {
					f := strings.Fields(line)
					if len(f) == 0 {
						return nil, false
					}
					switch strings.ToLower(f[0]) {
					case "mode":
						return []string{"203 stream ok"}, false
					case "quit":
						return []string{"205 bye"}, true
					}
					if len(f) < 2 {
						return nil, false
					}
					switch strings.ToLower(f[0]) {
					case "check":
						if c.ID() > 1 {
							mu.Lock()
							offered = offered || f[1] == id
							mu.Unlock()
							return []string{"438 " + f[1]}, false
						}
						if f[1] == id && tc.check != "" {
							return []string{tc.check}, false
						}
						if f[1] == id {
							return []string{"238 " + id}, false
						}
						return []string{"438 " + f[1]}, false
					case "takethis":
						return []string{tc.take}, false
					}
					return nil, false
				})})
			q := "D.00000001/B.0001 " + warm + " 0,100\nD.00000001/B.0001 " + id + " 100,100\n"
			if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q), 0o644); err != nil {
				t.Fatal(err)
			}
			start := time.Now()
			out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
				"-P", strconv.Itoa(srv.Port()), "-t", "6", "-w", "1")
			took := time.Since(start)
			t.Logf("dnewslink: err=%v after %v\n%s", err, took.Round(time.Millisecond), out)
			mu.Lock()
			defer mu.Unlock()
			if took >= 4*time.Second {
				t.Errorf("dnewslink took %v: it waited for the read timeout (6 s) for the reply that did not come", took)
			}
			if srv.Accepted() < 2 || !offered {
				t.Errorf("%d connections, offered again on a later one: %v; want 2 connections and the article offered again (lines %q)",
					srv.Accepted(), offered, srv.Lines())
			}
		})
	}
}

// TestDnewslinkTakethis431KeptDuringReplay (D-214): three articles; the peer answers the first
// CHECK of each with 431 (deferred), then 238 to the CHECKs sent while the deferred articles are
// sent again, and 431 to every TAKETHIS (Diablo's own feeder does while its history add fails
// for a time). A second defer is not taken for an article that was deferred already, but the
// article must not be lost: it is written back to the queue file, once, to be sent on a later
// run (RFC 4644 has no 431 for TAKETHIS; innfeed requeues a deferral). Expected: all three ids
// are in the queue file once, the feedlog says "kept", no article is logged as rejected and the
// connection ends with QUIT. Fails on main: the 431 was a reject, the queue file is empty.
func TestDnewslinkTakethis431KeptDuringReplay(t *testing.T) {
	ids := []string{"<sr-k1@e2e.test>", "<sr-k2@e2e.test>", "<sr-k3@e2e.test>"}
	s := NewSite(t, SiteOptions{NoStart: true})
	dnlSpool(t, s)
	var (
		mu    sync.Mutex
		seen  = map[string]bool{}
		takes int
	)
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(
		func(_ *fakeserver.Conn, line string) ([]string, bool) {
			f := strings.Fields(line)
			if len(f) == 0 {
				return nil, false
			}
			mu.Lock()
			defer mu.Unlock()
			switch strings.ToLower(f[0]) {
			case "mode":
				return []string{"203 stream ok"}, false
			case "quit":
				return []string{"205 bye"}, true
			case "check":
				if len(f) < 2 {
					return nil, false
				}
				if !seen[f[1]] {
					seen[f[1]] = true
					return []string{"431 " + f[1] + " later"}, false
				}
				return []string{"238 " + f[1]}, false
			case "takethis":
				if len(f) < 2 {
					return nil, false
				}
				takes++
				return []string{"431 " + f[1] + " later"}, false
			}
			return nil, false
		})})
	var q strings.Builder
	for i, id := range ids {
		q.WriteString("D.00000001/B.0001 " + id + " " + strconv.Itoa(i*100) + ",100\n")
	}
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-Aall", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(srv.Port()), "-t", "5", "-w", "1")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	mu.Lock()
	n := takes
	mu.Unlock()
	if n != len(ids) {
		t.Fatalf("the scenario did not develop: %d TAKETHIS (want %d, one per article, sent while the deferred ones are sent again; lines %q)",
			n, len(ids), srv.Lines())
	}
	log := rcuFeedlog(s)
	for _, id := range ids {
		rcuQueued(t, s, id)
		if !strings.Contains(log, id+" kept") {
			t.Errorf("feedlog has no %q:\n%s", id+" kept", log)
		}
	}
	if strings.Contains(log, " rejected") {
		t.Errorf("an article was logged as rejected, it must stay queued:\n%s", log)
	}
	if a, qn := srv.Accepted(), srv.Count("QUIT"); a != 1 || qn != 1 {
		t.Errorf("%d connections and %d QUIT, want 1 and 1 (lines %q)", a, qn, srv.Lines())
	}
}

// TestDnewslinkDeferMarkSurvivesReconnect (D-215): a deferred article is sent again; the
// connection fails while it is pending, after the replay is over. The article goes through the
// retry list, which kept only its queue line: after the reconnect it was offered as a new
// article and could be deferred once more. It keeps its mark: a 431 to its CHECK is then handled
// as for an article deferred once (TestDnewslinkCheck431Twice: taken as 238, the article goes with
// TAKETHIS).
//
// Input: queue W, A. Connection 1: W refused (438, so that dnewslink reconnects), the first
// CHECK of A answered 431 (deferred), the second (the replay, after the 5 s wait for defers)
// closes the connection unanswered. Connection 2: the first CHECK of A answered 431, the second
// 238, TAKETHIS 239. Expected: one CHECK of A on connection 2, then TAKETHIS (accepted). Fails on
// main: A is deferred again and its CHECK comes twice.
func TestDnewslinkDeferMarkSurvivesReconnect(t *testing.T) {
	const (
		warm = "<sr-mw@e2e.test>"
		id   = "<sr-ma@e2e.test>"
	)
	s := NewSite(t, SiteOptions{NoStart: true})
	dnlSpool(t, s)
	var (
		mu      sync.Mutex
		first   int // CHECK lines for id on connection 1
		checks2 int // CHECK lines for id on later connections
		takes   int
	)
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(
		func(c *fakeserver.Conn, line string) ([]string, bool) {
			f := strings.Fields(line)
			if len(f) == 0 {
				return nil, false
			}
			mu.Lock()
			defer mu.Unlock()
			switch strings.ToLower(f[0]) {
			case "mode":
				return []string{"203 stream ok"}, false
			case "quit":
				return []string{"205 bye"}, true
			case "check":
				if len(f) < 2 {
					return nil, false
				}
				if f[1] == warm {
					return []string{"438 " + warm}, false
				}
				if c.ID() == 1 {
					first++
					if first == 1 {
						return []string{"431 " + id + " later"}, false
					}
					return nil, true // the replay: go away with the article pending
				}
				checks2++
				if checks2 == 1 {
					return []string{"431 " + id + " later"}, false
				}
				return []string{"238 " + id}, false
			case "takethis":
				takes++
				return []string{"239 " + id}, false
			}
			return nil, false
		})})
	q := "D.00000001/B.0001 " + warm + " 0,100\nD.00000001/B.0001 " + id + " 100,100\n"
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-Aall", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(srv.Port()), "-t", "5", "-w", "1")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	mu.Lock()
	defer mu.Unlock()
	if first != 2 || srv.Accepted() < 2 {
		t.Fatalf("the scenario did not develop: %d CHECKs of the article on connection 1 (want 2: offered, deferred, sent again), %d connections (want 2 or more; lines %q)",
			first, srv.Accepted(), srv.Lines())
	}
	if checks2 != 1 || takes != 1 {
		t.Errorf("after the reconnect the article got %d CHECKs and %d TAKETHIS, want 1 and 1: the 431 to its CHECK is taken as 238 (it was deferred once), it must not be deferred again",
			checks2, takes)
	}
	if log := rcuFeedlog(s); !strings.Contains(log, id+" accepted") {
		t.Errorf("the article was not accepted:\n%s", log)
	}
}
