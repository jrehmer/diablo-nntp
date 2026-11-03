package e2e

// dreaderd under a login flood with slow DNS: new connections that find every DNS worker
// busy are queued briefly or refused at once with 400, and AUTHINFO logins of greeted
// clients go ahead of the queued new connections. The tests preload the resolver shim
// (testdata/dnsshim) with a lookup delay, so they run on Linux only.

import (
	"bufio"
	"fmt"
	"net"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/go-while/nntp-testkit/proc"
)

// floodAccess: fred must log in from 127.0.0.1 (as AuthReaderAccess); the flood clients
// from 127.1.0.0/16 are greeted and need no login.
const floodAccess = `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef fred
    user fred
    pass fredspass
end
readerdef userfred
    read yes
    post yes
    auth fred
end
readerdef rp
    read yes
end
access 127.0.0.1 userfred
access 127.1.0.0/16 rp
`

// floodResult is the outcome of one flood client: the first reply line (or an error
// text) and the time from the start of the dial to it.
type floodResult struct {
	line string
	wait time.Duration
}

// flood opens n connections from distinct 127.1.x.y addresses, spread over spread, and
// reads the first line of each. Every address is new to the DNS cache.
func flood(t *testing.T, addr string, n int, spread time.Duration) []floodResult {
	t.Helper()
	return floodAt(t, addr, 0, n, spread)
}

// floodAt is flood with the addresses of clients first .. first+n-1.
func floodAt(t *testing.T, addr string, first, n int, spread time.Duration) []floodResult {
	t.Helper()
	res := make([]floodResult, n)
	var wg sync.WaitGroup
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			time.Sleep(spread * time.Duration(i) / time.Duration(n))
			ip := fmt.Sprintf("127.1.%d.%d", 1+(first+i)/250, 1+(first+i)%250)
			d := net.Dialer{LocalAddr: &net.TCPAddr{IP: net.ParseIP(ip)}, Timeout: 60 * time.Second}
			t0 := time.Now()
			c, err := d.Dial("tcp", addr)
			if err != nil {
				res[i] = floodResult{"error: " + err.Error(), time.Since(t0)}
				return
			}
			defer c.Close()
			c.SetReadDeadline(time.Now().Add(60 * time.Second))
			l, err := bufio.NewReader(c).ReadString('\n')
			res[i].wait = time.Since(t0)
			if err != nil {
				res[i].line = "error: " + err.Error()
				return
			}
			res[i].line = strings.TrimSpace(l)
			fmt.Fprintf(c, "QUIT\r\n")
		}(i)
	}
	wg.Wait()
	return res
}

// countFlood checks that every flood client got a greeting or a 400 within bound and that
// at least 75 % of the 400 replies came within 0.5 s, and returns how many got each.
// Without the admission check none are quick; 75 % leaves room for a loaded CI runner
// (one Alpine container run had 88 %).
func countFlood(t *testing.T, res []floodResult, bound time.Duration) (greeted, busy int) {
	t.Helper()
	var slowGreet, slowBusy time.Duration
	quickBusy := 0
	for i, r := range res {
		switch {
		case strings.HasPrefix(r.line, "200"), strings.HasPrefix(r.line, "201"):
			greeted++
			slowGreet = max(slowGreet, r.wait)
		case strings.HasPrefix(r.line, "400"):
			busy++
			slowBusy = max(slowBusy, r.wait)
			if r.wait < 500*time.Millisecond {
				quickBusy++
			}
		default:
			t.Errorf("client %d: got %q after %v, want a greeting or 400", i, r.line, r.wait)
			continue
		}
		if r.wait > bound {
			t.Errorf("client %d: %q after %v, want it within %v", i, r.line, r.wait, bound)
		}
	}
	t.Logf("%d clients: %d greeted (slowest %v), %d got 400 (%d within 0.5 s, slowest %v)",
		len(res), greeted, slowGreet, busy, quickBusy, slowBusy)
	// a refusal is quick: at once, or as soon as a worker time shows it cannot make it
	if quickBusy*4 < busy*3 {
		t.Errorf("%d of %d 400 replies within 0.5 s, want at least 75 %%", quickBusy, busy)
	}
	return greeted, busy
}

// startFloodReader starts a reader with one DNS worker, the shim delaying every faked
// lookup by delayMs, and room for many sessions.
func startFloodReader(t *testing.T, delayMs int, conf map[string]string) *Site {
	t.Helper()
	sh := buildDNSShim(t)
	_, spool := spoolStore(t)
	c := map[string]string{"readerthreads": "64"}
	for k, v := range conf {
		c[k] = v
	}
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: floodAccess, Config: c, NoStart: true})
	s.Env = append(s.Env, "LD_PRELOAD="+sh.so, fmt.Sprintf("DSHIM_DELAY_MS=%d", delayMs))
	_, port, _ := net.SplitHostPort(s.Addr)
	startReader(t, s, "[127.0.0.1]:"+port)
	return s
}

// TestReaderDNSOverload400: a burst of 150 new clients against one DNS worker that needs
// 200 ms per login (30 s of work). Each client gets a greeting or a "400 Server too busy"
// within the default readerdnswait (5 s) plus slack, at least 75 % of the 400s within
// 0.5 s, and none is reset. Fails on main:
// dreaderd stopped accepting while its worker was busy, so the clients waited in the kernel
// backlog for up to 30 s.
func TestReaderDNSOverload400(t *testing.T) {
	s := startFloodReader(t, 100, nil)
	greeted, busy := countFlood(t, flood(t, s.Addr, 150, 500*time.Millisecond), 12*time.Second)
	if greeted == 0 || busy == 0 {
		t.Errorf("%d greeted and %d got 400, want some of each", greeted, busy)
	}
	c := s.Reader(t)
	expect(t, c, 381, "AUTHINFO USER fred")
	expect(t, c, 281, "AUTHINFO PASS fredspass")
	expect(t, c, 205, "QUIT")
}

// TestReaderDNSOverloadAuthFirst: 20 greeted clients send AUTHINFO PASS together while a
// flood of new clients keeps the only DNS worker busy. Every login gets 281 within 1.5 s:
// AUTHINFO logins wait ahead of the queued new connections and have no queue limit. Fails
// on main: the queue held 4 * readerdns + 4 requests, so most of the AUTHINFO PASS commands
// got "400 Server too busy".
func TestReaderDNSOverloadAuthFirst(t *testing.T) {
	s := startFloodReader(t, 100, nil)
	const n = 20
	conns := make([]net.Conn, n)
	rds := make([]*bufio.Reader, n)
	line := func(i int) string {
		conns[i].SetReadDeadline(time.Now().Add(60 * time.Second))
		l, err := rds[i].ReadString('\n')
		if err != nil {
			return "error: " + err.Error()
		}
		return strings.TrimSpace(l)
	}
	for i := 0; i < n; i++ {
		c, err := net.DialTimeout("tcp", s.Addr, 20*time.Second)
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		conns[i], rds[i] = c, bufio.NewReader(c)
		if g := line(i); !strings.HasPrefix(g, "200") && !strings.HasPrefix(g, "201") {
			t.Fatalf("client %d: greeting %q", i, g)
		}
		fmt.Fprintf(c, "AUTHINFO USER fred\r\n")
		if r := line(i); !strings.HasPrefix(r, "381") {
			t.Fatalf("client %d: AUTHINFO USER: %q", i, r)
		}
	}

	var fres []floodResult
	done := make(chan struct{})
	go func() {
		fres = flood(t, s.Addr, 100, 2*time.Second)
		close(done)
	}()
	time.Sleep(time.Second) // let new connections pile up behind the worker

	res := make([]string, n)
	waits := make([]time.Duration, n)
	var wg sync.WaitGroup
	for i := 0; i < n; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			t0 := time.Now()
			fmt.Fprintf(conns[i], "AUTHINFO PASS fredspass\r\n")
			res[i] = line(i)
			waits[i] = time.Since(t0)
			fmt.Fprintf(conns[i], "QUIT\r\n")
		}(i)
	}
	wg.Wait()
	// Behind the queued new connections a login would wait about half of readerdnswait
	// (2.5 s); ahead of them, one worker request (0.2 s) plus the logins before it.
	for i, r := range res {
		if !strings.HasPrefix(r, "281") {
			t.Errorf("greeted client %d: AUTHINFO PASS got %q, want 281", i, r)
		} else if waits[i] > 1500*time.Millisecond {
			t.Errorf("greeted client %d: 281 after %v, want it within 1.5 s (ahead of the new connections)",
				i, waits[i])
		}
	}
	t.Logf("slowest AUTHINFO PASS reply after %v", slices.Max(waits))
	<-done
	countFlood(t, fres, 12*time.Second)
}

// TestReaderDNSQueueOptions: readerdnsqueue 0 queues no new connection, so with the worker
// busy the burst gets 400 at once, and readerdnswait bounds the wait of the queued ones.
func TestReaderDNSQueueOptions(t *testing.T) {
	t.Run("queue0", func(t *testing.T) {
		s := startFloodReader(t, 250, map[string]string{"readerdnsqueue": "0"})
		greeted, busy := countFlood(t, flood(t, s.Addr, 30, 0), 5*time.Second)
		if greeted == 0 || busy < 20 {
			t.Errorf("%d greeted and %d got 400, want at least 1 and 20", greeted, busy)
		}
	})
	t.Run("wait1", func(t *testing.T) {
		s := startFloodReader(t, 100, map[string]string{"readerdnswait": "1"})
		greeted, busy := countFlood(t, flood(t, s.Addr, 60, 300*time.Millisecond), 4*time.Second)
		if greeted == 0 || busy == 0 {
			t.Errorf("%d greeted and %d got 400, want some of each", greeted, busy)
		}
	})
}

// TestReaderDNSOverloadBurst: 150 new clients at the same moment against one DNS worker that
// needs 200 ms per login, right after startup ("cold") and after 40 logins that were DNS
// cache hits ("after cache hits"). The worker time learned from fast requests must not let
// the whole burst into the queue: at least 75 % of the 400s come within 0.5 s, not after
// readerdnswait. Fails before the per-kind worker times, the 50 ms floor and the queue trim
// (0 of 125 quick after the cache hits).
func TestReaderDNSOverloadBurst(t *testing.T) {
	t.Run("cold", func(t *testing.T) {
		s := startFloodReader(t, 100, nil)
		countFlood(t, flood(t, s.Addr, 150, 0), 12*time.Second)
	})
	t.Run("after cache hits", func(t *testing.T) {
		s := startFloodReader(t, 100, nil)
		for i := 0; i < 40; i++ {
			c := s.Reader(t)
			c.TryCmd("QUIT")
			c.Close()
		}
		countFlood(t, flood(t, s.Addr, 150, 0), 12*time.Second)
	})
}

// proxyFloodAccess: 127.0.0.1 is a proxy whose clients name their real address with the
// "proxied" command (useproxied); the real addresses in 127.1.0.0/16 read. 127.2.0.1 must
// log in as fred.
const proxyFloodAccess = `vserverdef DEFAULT
    hostname ` + PathHost + `
end
authdef fred
    user fred
    pass fredspass
end
readerdef rpx
    read yes
    useproxied yes
    proxyfrom 127.0.0.1
end
readerdef rp
    read yes
end
readerdef userfred
    read yes
    auth fred
end
access 127.0.0.1 rpx
access 127.1.0.0/16 rp
access 127.2.0.1 userfred
`

// TestReaderDNSOverloadProxied: 150 clients behind a proxy (useproxied) name new real
// addresses at the same moment, against one DNS worker that needs 200 ms per lookup pair.
// The first lookup of a proxied client is admitted like a new connection: each gets 201 or
// 400 within 12 s, at least 75 % of the 400s within 0.5 s, and a real AUTHINFO login meanwhile
// gets 281 within 1.5 s. Fails before the fix: the proxied lookups went into the AUTHINFO
// queue without admission, the 400s came after readerdnswait (5 s), and the login waited
// behind them.
func TestReaderDNSOverloadProxied(t *testing.T) {
	sh := buildDNSShim(t)
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: proxyFloodAccess, NoStart: true,
		Config: map[string]string{"readerthreads": "64", "readerforks": "4"}})
	s.Env = append(s.Env, "LD_PRELOAD="+sh.so, "DSHIM_DELAY_MS=100")
	_, port, _ := net.SplitHostPort(s.Addr)
	startReader(t, s, "[127.0.0.1]:"+port)

	fred := s.dialFrom(t, "127.2.0.1")
	expect(t, fred, 381, "AUTHINFO USER fred")

	const n = 150
	type cl struct {
		c  net.Conn
		br *bufio.Reader
	}
	cs := make([]cl, n)
	for i := range cs {
		c, err := net.DialTimeout("tcp", s.Addr, 20*time.Second)
		if err != nil {
			t.Fatal(err)
		}
		defer c.Close()
		c.SetDeadline(time.Now().Add(60 * time.Second))
		br := bufio.NewReader(c)
		if l, _ := br.ReadString('\n'); !strings.HasPrefix(l, "20") {
			t.Fatalf("proxy client %d: greeting %q", i, l)
		}
		cs[i] = cl{c, br}
	}
	res := make([]floodResult, n)
	var wg sync.WaitGroup
	for i := range cs {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			t0 := time.Now()
			fmt.Fprintf(cs[i].c, "proxied 127.1.%d.%d:1234\r\n", 1+i/250, 1+i%250)
			l, err := cs[i].br.ReadString('\n')
			res[i].wait = time.Since(t0)
			if err != nil {
				res[i].line = "error: " + err.Error()
				return
			}
			res[i].line = strings.TrimSpace(l)
		}(i)
	}
	time.Sleep(300 * time.Millisecond)
	t0 := time.Now()
	code, line := fred.Cmd("AUTHINFO PASS fredspass")
	t.Logf("AUTHINFO PASS during the proxied flood: %q after %v", line, time.Since(t0))
	if code != 281 || time.Since(t0) > 1500*time.Millisecond {
		t.Errorf("AUTHINFO PASS during the proxied flood: %q after %v, want 281 within 1.5 s",
			line, time.Since(t0))
	}
	wg.Wait()
	greeted, busy := countFlood(t, res, 12*time.Second)
	if greeted == 0 || busy == 0 {
		t.Errorf("%d greeted and %d got 400, want some of each", greeted, busy)
	}
}

// TestReaderDNSAuthFdLimit: with a descriptor limit of 100, 150 greeted clients send
// AUTHINFO PASS at once to one DNS worker. Each gets 281 or 400 (none is dropped without a
// reply), and once all have left "drcmd status" shows no active connection. Fails before the
// fix: the AUTHINFO queue had no descriptor cap, descriptors handed back by the readers were
// dropped at the limit, and those connections were never released (Act stayed above 0).
func TestReaderDNSAuthFdLimit(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("needs prlimit (Linux)")
	}
	prl, err := exec.LookPath("prlimit")
	if err != nil {
		t.Skip("prlimit not found")
	}
	_, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: AuthReaderAccess, NoStart: true,
		Config: map[string]string{"readerforks": "4", "readerthreads": "64"}})
	_, port, _ := net.SplitHostPort(s.Addr)
	s.Proc = proc.StartTCP(t, proc.Options{
		Path: prl,
		Args: []string{"--nofile=100:100", filepath.Join(s.Bin, "dreaderd"),
			"-d", "-p", PathHost, "-h", PathHost, "-P", "[127.0.0.1]:" + port},
		Env: s.Env,
		Dir: s.Dir,
	}, s.Addr)
	time.Sleep(500 * time.Millisecond) // let the reader forks come up

	const n = 150
	type cl struct {
		c  net.Conn
		br *bufio.Reader
	}
	cs := make([]cl, n)
	for i := range cs {
		c, err := net.DialTimeout("tcp", s.Addr, 20*time.Second)
		if err != nil {
			t.Fatal(err)
		}
		c.SetDeadline(time.Now().Add(60 * time.Second))
		br := bufio.NewReader(c)
		if l, _ := br.ReadString('\n'); !strings.HasPrefix(l, "20") {
			t.Fatalf("client %d: greeting %q", i, l)
		}
		fmt.Fprintf(c, "AUTHINFO USER fred\r\n")
		if l, _ := br.ReadString('\n'); !strings.HasPrefix(l, "381") {
			t.Fatalf("client %d: AUTHINFO USER: %q", i, l)
		}
		cs[i] = cl{c, br}
	}
	codes := make([]string, n)
	var wg sync.WaitGroup
	for i := range cs {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			fmt.Fprintf(cs[i].c, "AUTHINFO PASS fredspass\r\n")
			l, err := cs[i].br.ReadString('\n')
			if err != nil {
				codes[i] = "error: " + err.Error()
				return
			}
			codes[i] = strings.TrimSpace(l)
		}(i)
	}
	wg.Wait()
	ok, busy := 0, 0
	for i, l := range codes {
		switch {
		case strings.HasPrefix(l, "281"):
			ok++
		case strings.HasPrefix(l, "400"):
			busy++
		default:
			t.Errorf("client %d: AUTHINFO PASS got %q, want 281 or 400", i, l)
		}
	}
	t.Logf("%d logins: %d got 281, %d got 400", n, ok, busy)
	for _, c := range cs {
		fmt.Fprintf(c.c, "QUIT\r\n")
		c.c.Close()
	}
	var st string
	limit := time.Now().Add(15 * time.Second)
	for {
		st = s.Run(t, "drcmd", "status")
		if strings.Contains(st, "Act=0/") || time.Now().After(limit) {
			break
		}
		time.Sleep(200 * time.Millisecond)
	}
	if !strings.Contains(st, "Act=0/") {
		t.Errorf("drcmd status after all clients left: %q, want Act=0", strings.TrimSpace(st))
	}
}

// TestReaderDNSOverloadWarm: once the worker time is known (a first, spread flood), a sharp
// burst of 150 new clients is refused by the admission estimate itself: at least half of
// the 400s come within 50 ms, well under one worker time (200 ms). The queue trim alone
// would answer them only after the first worker time.
func TestReaderDNSOverloadWarm(t *testing.T) {
	s := startFloodReader(t, 100, nil)
	countFlood(t, floodAt(t, s.Addr, 0, 40, 2*time.Second), 12*time.Second)
	time.Sleep(6 * time.Second) // the queue drains; the worker time stays
	res := floodAt(t, s.Addr, 1000, 150, 0)
	countFlood(t, res, 12*time.Second)
	busy, fast := 0, 0
	for _, r := range res {
		if strings.HasPrefix(r.line, "400") {
			busy++
			if r.wait < 50*time.Millisecond {
				fast++
			}
		}
	}
	t.Logf("%d of %d 400 replies within 50 ms", fast, busy)
	if busy == 0 || fast*2 < busy {
		t.Errorf("%d of %d 400 replies within 50 ms, want at least half (refused on admission)",
			fast, busy)
	}
}

// TestReaderTooManyRefsHostLimit: when passing a client to a reader fails with ETOOMANYREFS
// (shim: the first 3 fd passes of the main process), the client gets 400 and its entry in
// the per-host count is removed again. With maxconnperhost 2, two later clients from the
// same host are greeted and a third is refused. Fails before the fix: the entry was looked
// for under the DNS worker instead of the reader and stayed, so every later client got "Too
// many connections from your host".
func TestReaderTooManyRefsHostLimit(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("the ETOOMANYREFS shim needs Linux (LD_PRELOAD)")
	}
	cc := wmCC(t)
	so := filepath.Join(t.TempDir(), "tmrshim.so")
	src, err := filepath.Abs(filepath.Join("testdata", "tmrshim", "tmrshim.c"))
	if err != nil {
		t.Fatal(err)
	}
	if out, err := exec.Command(cc, "-O2", "-shared", "-fPIC", "-o", so, src, "-ldl").CombinedOutput(); err != nil {
		t.Skipf("cannot build the shim: %v\n%s", err, out)
	}
	_, spool := spoolStore(t)
	access := strings.Replace(DefaultReaderAccess, "readerdef rp\n", "readerdef rp\n    maxconnperhost 2\n", 1)
	s := NewReaderSite(t, ReaderOptions{Spool: spool, Access: access, NoStart: true})
	s.Env = append(s.Env, "LD_PRELOAD="+so, "TMR_N=3")
	_, port, _ := net.SplitHostPort(s.Addr)
	startReader(t, s, "[127.0.0.1]:"+port)

	greet := func() (net.Conn, string) {
		c, err := net.DialTimeout("tcp", s.Addr, 20*time.Second)
		if err != nil {
			t.Fatal(err)
		}
		c.SetReadDeadline(time.Now().Add(20 * time.Second))
		l, _ := bufio.NewReader(c).ReadString('\n')
		return c, strings.TrimSpace(l)
	}
	// use up the failing fd passes (the start-up probe may have taken some)
	for i := 0; i < 3; i++ {
		c, l := greet()
		c.Close()
		t.Logf("client %d: %q", i, l)
		if strings.HasPrefix(l, "20") {
			break
		}
	}
	time.Sleep(200 * time.Millisecond)
	for i := 0; i < 2; i++ {
		c, l := greet()
		defer c.Close()
		if !strings.HasPrefix(l, "200") {
			t.Errorf("client %d after the failed passes: %q, want 200 (maxconnperhost 2)", i, l)
		}
	}
	c, l := greet()
	c.Close()
	if !strings.Contains(l, "Too many connections from your host") {
		t.Errorf("third open client: %q, want the per-host limit", l)
	}
}
