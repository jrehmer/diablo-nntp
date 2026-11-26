package e2e

// D-242: the feeder matches IP, CIDR and wildcard-IP entries, and name entries by the
// addresses the host cache has for them, before any DNS lookup. Only when none matches and an
// entry can match by name alone (a wildcard name, or a name with no cached address) does a
// session look up the PTR name (and confirm it forward), in a child, at most 10 s.

import (
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/go-while/nntp-testkit/client"
)

// dnsOrderSource is preloaded into diablo. getnameinfo() with NI_NAMEREQD (the reverse
// lookup) writes the mark "PTR <address>" and, while the file named by DNSORDER_MODE exists,
// does what it says: "slow" sleeps 30 s and fails (a resolver that does not answer),
// "name:<host>" answers host. getaddrinfo() of a name ending in ".dnsorder.test" answers
// 127.0.0.3, so the forward lookup confirms such a name for 127.0.0.3.
const dnsOrderSource = `#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
/*MARK*/
int
getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
	    char *serv, socklen_t servlen, int flags)
{
    static int (*real)(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t, int);
    const char *mode = getenv("DNSORDER_MODE");
    char buf[256];
    int fd, n;

    if (real == NULL)
	real = (int (*)(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t, int))
	    dlsym(RTLD_NEXT, "getnameinfo");
    if (!(flags & NI_NAMEREQD))
	return real(sa, salen, host, hostlen, serv, servlen, flags);
    {
	char num[64];

	if (real(sa, salen, num, sizeof(num), NULL, 0, NI_NUMERICHOST) == 0)
	    shim_mark("PTR %s", num);
    }
    if (mode == NULL || (fd = open(mode, O_RDONLY)) < 0)
	return real(sa, salen, host, hostlen, serv, servlen, flags);
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    buf[n > 0 ? n : 0] = 0;
    if (strncmp(buf, "slow", 4) == 0) {
	sleep(30);
	return EAI_AGAIN;
    }
    if (strncmp(buf, "name:", 5) == 0 && strlen(buf + 5) < hostlen) {
	strcpy(host, buf + 5);
	return 0;
    }
    return real(sa, salen, host, hostlen, serv, servlen, flags);
}

int
getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    static int (*real)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
    size_t l = node ? strlen(node) : 0;

    if (real == NULL)
	real = (int (*)(const char *, const char *, const struct addrinfo *, struct addrinfo **))
	    dlsym(RTLD_NEXT, "getaddrinfo");
    if (l > 14 && strcmp(node + l - 14, ".dnsorder.test") == 0)
	return real("127.0.0.3", service, hints, res);
    return real(node, service, hints, res);
}
`

// dnsOrderSite is a feeder with the shim above; mode sets what the reverse lookup does.
type dnsOrderSite struct {
	*Site
	mark, mode string
}

func newDNSOrderSite(t *testing.T, feeds string) *dnsOrderSite {
	t.Helper()
	if !preloadHost(false) {
		t.Skip("the DNS shim uses LD_PRELOAD")
	}
	so := preloadShim(t, "dnsorder", withMark(dnsOrderSource))
	d := &dnsOrderSite{mark: filepath.Join(t.TempDir(), "mark"), mode: filepath.Join(t.TempDir(), "mode")}
	d.Site = NewSite(t, SiteOptions{
		Newsfeeds: feeds,
		Env:       []string{lockFaultEnv(so), "SHIM_MARK=" + d.mark, "DNSORDER_MODE=" + d.mode},
	})
	t.Cleanup(func() {
		if t.Failed() {
			t.Logf("marks:\n%s", strings.Join(shimMarks(d.mark), "\n"))
		}
	})
	return d
}

func (d *dnsOrderSite) setMode(t *testing.T, m string) {
	t.Helper()
	if err := os.WriteFile(d.mode, []byte(m), 0o600); err != nil {
		t.Fatal(err)
	}
}

// greet connects from ip and returns the greeting and how long it took; within is the
// longest it may take (a slower greeting is an error, as is none).
func (d *dnsOrderSite) greet(t *testing.T, ip string, within time.Duration) (string, time.Duration) {
	t.Helper()
	start := time.Now()
	dl := net.Dialer{LocalAddr: &net.TCPAddr{IP: net.ParseIP(ip)}, Timeout: within}
	nc, err := dl.Dial("tcp", d.Addr)
	if err != nil {
		t.Fatalf("dial from %s: %v", ip, err)
	}
	defer nc.Close()
	// the client sets read deadlines of its own: the time is checked here
	c := client.New(t, nc)
	g, err := c.ReadGreeting()
	took := time.Since(start)
	if err != nil {
		t.Fatalf("greeting from %s: none after %v (%v)", ip, took, err)
	}
	t.Logf("from %s: %q after %v", ip, g, took)
	if took > within {
		t.Errorf("greeting from %s took %v, want at most %v", ip, took, within)
	}
	return g, took
}

// TestFeederDNSAfterIPMatch: with a reverse lookup that does not answer (30 s), a peer listed
// by IP is greeted (200) at once and no reverse lookup is done for it; a stranger is refused
// (502) at once without one when no entry can match by name. Fails on main: every session
// does the reverse lookup before the access check, so both wait for it (30 s).
func TestFeederDNSAfterIPMatch(t *testing.T) {
	d := newDNSOrderSite(t, DefaultNewsfeeds)
	d.setMode(t, "slow")

	g, took := d.greet(t, PeerIP, 5*time.Second)
	if !strings.HasPrefix(g, "200 ") {
		t.Errorf("the peer listed by IP (%s): %q after %v, want 200", PeerIP, g, took)
	}
	g, took = d.greet(t, "127.0.0.4", 5*time.Second)
	if !strings.HasPrefix(g, "502 ") {
		t.Errorf("a stranger (127.0.0.4) with only IP entries: %q after %v, want 502", g, took)
	}
	if m := shimMarked(d.mark, "PTR "); len(m) != 0 {
		t.Errorf("reverse lookups done: %v, want none (no entry matches by name alone)", m)
	}
}

// TestFeederDNSByNameTimed: with a wildcard name entry, a stranger whose reverse lookup does
// not answer is refused (502) after the 10 s limit, with a log line, instead of after the
// lookup (30 s); a peer listed by IP still gets no lookup. A name the lookup confirms still
// matches the wildcard entry (200). Fails on main: the refusal takes 30 s, and the IP peer
// waits for its lookup too.
func TestFeederDNSByNameTimed(t *testing.T) {
	feeds := DefaultNewsfeeds + "label wild\n    inhost *.dnsorder.test\n    addgroup *\nend\n"
	d := newDNSOrderSite(t, feeds)
	d.setMode(t, "slow")

	g, took := d.greet(t, PeerIP, 5*time.Second)
	if !strings.HasPrefix(g, "200 ") {
		t.Errorf("the peer listed by IP (%s): %q after %v, want 200", PeerIP, g, took)
	}
	if m := shimMarked(d.mark, "PTR "+PeerIP); len(m) != 0 {
		t.Errorf("a reverse lookup for the peer listed by IP: %v, want none", m)
	}

	g, took = d.greet(t, "127.0.0.4", 20*time.Second)
	if !strings.HasPrefix(g, "502 ") || took < 9*time.Second || took > 15*time.Second {
		t.Errorf("a stranger (127.0.0.4) whose reverse lookup does not answer: %q after %v, want 502 after about 10 s", g, took)
	}
	if m := shimMarked(d.mark, "PTR 127.0.0.4"); len(m) == 0 {
		t.Errorf("no reverse lookup for the stranger, want one (a wildcard name entry exists)")
	}
	if !strings.Contains(generalLog(d.Site), "DNS lookup of 127.0.0.4 took longer than 10 s") {
		t.Errorf("no log line about the lookup that took too long")
	}

	// a name the forward lookup confirms (peer.dnsorder.test is 127.0.0.3)
	d.setMode(t, "name:peer.dnsorder.test")
	g, took = d.greet(t, "127.0.0.3", 5*time.Second)
	if !strings.HasPrefix(g, "200 ") {
		t.Errorf("127.0.0.3, reverse name peer.dnsorder.test (confirmed forward): %q after %v, want 200 (label wild)", g, took)
	}
}
