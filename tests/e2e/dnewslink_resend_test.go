package e2e

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"syscall"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
	"github.com/go-while/nntp-testkit/fakeserver"
)

func TestDnewslinkFresh431DuringReplay(t *testing.T) {
	for _, size := range []int{1022, 1024} {
		t.Run(fmt.Sprint(size), func(t *testing.T) {
			testDnewslinkFresh431DuringReplay(t, size)
		})
	}
}

func testDnewslinkFresh431DuringReplay(t *testing.T, size int) {
	s := NewSite(t, SiteOptions{NoStart: true, Config: map[string]string{"feederrtstats": "label"}})
	dnlSpool(t, s)
	var mu sync.Mutex
	seen := make(map[string]int)
	const target = "<replay-1@e2e.test>"
	pending := false
	replay := false
	targetTakes := 0
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(_ *fakeserver.Conn, line string) ([]string, bool) {
		f := strings.Fields(strings.ToLower(line))
		if len(f) == 0 {
			return nil, false
		}
		mu.Lock()
		defer mu.Unlock()
		switch f[0] {
		case "mode":
			return []string{"203 stream ok"}, false
		case "check":
			seen[f[1]]++
			var n int
			fmt.Sscanf(f[1], "<replay-%d@e2e.test>", &n)
			if n >= 2 && n <= 81 && seen[f[1]] == 1 {
				return []string{"431 " + f[1] + " later"}, false
			}
			if n >= 2 && n <= 81 && seen[f[1]] == 2 && pending && !replay {
				replay = true
				return []string{"431 " + target + " later", "238 " + f[1]}, false
			}
			return []string{"238 " + f[1]}, false
		case "takethis":
			if f[1] == target {
				targetTakes++
			}
			if f[1] == target && !pending {
				pending = true
				return nil, false
			}
			return []string{"239 " + f[1]}, false
		case "quit":
			return []string{"205 bye"}, true
		}
		return nil, false
	})})
	targetLine := "D.00000001/B.0001 " + target + " 100,100"
	targetLine += strings.Repeat(" ", size-len(targetLine)-1) + "\n"
	var q strings.Builder
	for i := 1; i <= 120; i++ {
		if i == 1 {
			q.WriteString(targetLine)
		} else {
			fmt.Fprintf(&q, "D.00000001/B.0001 <replay-%d@e2e.test> %d,100\n", i, i*100)
		}
	}
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := s.TryRun("dnewslink", "-Aall", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", fmt.Sprint(srv.Port()), "-t", "5", "-w", "1", "-M", "20")
	t.Logf("dnewslink: err=%v\n%s", err, out)
	if err != nil {
		t.Fatalf("dnewslink failed: %v", err)
	}
	mu.Lock()
	sawReplay := replay
	nTarget := targetTakes
	mu.Unlock()
	if !sawReplay {
		t.Fatal("peer did not answer the pending fresh TAKETHIS during replay")
	}
	if nTarget != 1 {
		t.Errorf("target TAKETHIS sent %d times, want 1", nTarget)
	}
	if got := dnlQueue(t, s, "out.S00000"); got != targetLine {
		t.Errorf("fresh 431 queue line differs: got %q, want %q", got, targetLine)
	}
	if a, q := srv.Accepted(), srv.Count("QUIT"); a != 1 || q != 1 {
		t.Errorf("connections=%d QUIT=%d, want 1 each", a, q)
	}
	if log := rcuFeedlog(s); !strings.Contains(log, target+" kept") {
		t.Errorf("fresh 431 article was not logged as kept: %s", log)
	}
	stats := s.Run(t, "dfeedinfo", "-o", "-r", "out")
	if !hasDnewslinkStatsField(stats, "rej=0") {
		t.Errorf("fresh 431 counted as a rejection: %s", stats)
	}
}

func hasDnewslinkStatsField(out, want string) bool {
	for _, field := range strings.Fields(out) {
		if field == want {
			return true
		}
	}
	return false
}

// TestDnewslinkTakethis431RejectedBytes: a 431 to TAKETHIS that is a reject adds the size of the
// article it answers to the rejected bytes (the article was sent). A batch feed keeps such an
// article in its queue instead (D-214: TestDnewslinkTakethis431KeptDuringReplay), so the
// reject is left to a realtime feed (-r), which has no queue to write it to.
//
// Input: a realtime dnewslink and 85 articles of 100 bytes; the first 82 get 431 to their first
// CHECK (the replay of the deferred ones starts after 80 defers, MAXDEFER less 20), every
// TAKETHIS of those 82 gets 431. Expected: the replayed ones are rejects (rej > 0) and each adds
// its 100 bytes: rejbytes = 100 * rej, as the statistics read after dnewslink has ended
// (SIGTERM) say.
func TestDnewslinkTakethis431RejectedBytes(t *testing.T) {
	const (
		total    = 85
		deferred = 82
	)
	s := NewSite(t, SiteOptions{NoStart: true, Config: map[string]string{"feederrtstats": "label"}})
	dnlSpool(t, s)
	var (
		mu    sync.Mutex
		seen  = map[string]bool{}
		takes int // TAKETHIS answered 431
	)
	srv := fakeserver.New(t, fakeserver.Options{Greeting: "200 fake", Handler: fakeserver.LineHandler(func(c *fakeserver.Conn, line string) ([]string, bool) {
		f := strings.Fields(strings.ToLower(line))
		if len(f) == 0 {
			return nil, false
		}
		mu.Lock()
		defer mu.Unlock()
		var n int
		if len(f) > 1 {
			fmt.Sscanf(f[1], "<rb-%d@e2e.test>", &n)
		}
		switch f[0] {
		case "mode":
			return []string{"203 stream ok"}, false
		case "check":
			if n <= deferred && !seen[f[1]] {
				seen[f[1]] = true
				return []string{"431 " + f[1] + " later"}, false
			}
			return []string{"238 " + f[1]}, false
		case "takethis":
			if n <= deferred {
				takes++
				return []string{"431 " + f[1] + " later"}, false
			}
			return []string{"239 " + f[1]}, false
		case "quit":
			return []string{"205 bye"}, true
		}
		return nil, false
	})})
	var q strings.Builder
	for i := 1; i <= total; i++ {
		fmt.Fprintf(&q, "D.00000001/B.0001 <rb-%d@e2e.test> %d,100\n", i, i*100)
	}
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q.String()), 0o644); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(filepath.Join(s.Bin, "dnewslink"), "-Aall", "-r1", "-S", "0", "-b", "out.S%05d",
		"-h", "127.0.0.1", "-P", fmt.Sprint(srv.Port()), "-t", "5", "-w", "1", "-M", "20")
	cmd.Dir = s.Dir
	cmd.Env = append(os.Environ(), s.Env...)
	var out bytes.Buffer
	cmd.Stdout, cmd.Stderr = &out, &out
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() { _ = cmd.Wait(); close(done) }()
	t.Cleanup(func() {
		_ = cmd.Process.Kill()
		<-done
	})
	testkit.WaitFor(t, 20*time.Second, "TAKETHIS answered 431 for the replayed articles", func() bool {
		mu.Lock()
		defer mu.Unlock()
		return takes >= 40
	})
	_ = cmd.Process.Signal(syscall.SIGTERM)
	select {
	case <-done:
	case <-time.After(10 * time.Second):
		t.Fatalf("dnewslink still running 10s after SIGTERM\n%s", out.String())
	}
	stats := s.Run(t, "dfeedinfo", "-o", "-r", "out")
	t.Logf("statistics: %s", strings.TrimSpace(stats))
	var rej, rejbytes int
	for _, field := range strings.Fields(stats) {
		fmt.Sscanf(field, "rej=%d", &rej)
		fmt.Sscanf(field, "rejbytes=%d", &rejbytes)
	}
	if rej == 0 || rejbytes != 100*rej {
		t.Errorf("431 reject statistics: want rej > 0 and rejbytes = 100 * rej, got rej=%d rejbytes=%d: %s", rej, rejbytes, stats)
	}
}
