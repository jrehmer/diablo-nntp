package e2e

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"testing"

	"github.com/go-while/nntp-testkit/fakeserver"
)

// poolMapSource notes in the file SHIM_MARK every time a memory pool of lib/zalloc.c is
// taken from lib/alloc.c (pagealloc): there it is a malloc() of 65536 bytes on Linux and
// illumos (USE_ANON_MMAP is not set for them in lib/config.h), and an mmap() of 65536 bytes
// on the BSDs. The pools never shrink, so the number of notes is the number of pools
// dnewslink needed: a string that is not given back to its pool shows up as pools that
// keep coming.
const poolMapSource = `#define _GNU_SOURCE
#define __EXTENSIONS__
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
/*MARK*/
typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
typedef void *(*malloc_fn)(size_t);

void *
mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    static mmap_fn real;

    if (real == NULL)
	real = (mmap_fn)dlsym(RTLD_NEXT, "mmap");
    if (len == 65536 && prot == (PROT_READ|PROT_WRITE) && (flags & MAP_PRIVATE) != 0)
	shim_mark("pool");
    return real(addr, len, prot, flags, fd, off);
}

#if defined(__linux__)
/*
 * A program built with _FILE_OFFSET_BITS=64 calls mmap64().  On illumos the 64-bit mmap64
 * is mmap itself (<sys/mman.h> renames it), and the pools there are malloc()s anyway.
 */
void *
mmap64(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    static mmap_fn real;

    if (real == NULL)
	real = (mmap_fn)dlsym(RTLD_NEXT, "mmap64");
    if (len == 65536 && prot == (PROT_READ|PROT_WRITE) && (flags & MAP_PRIVATE) != 0)
	shim_mark("pool");
    return real(addr, len, prot, flags, fd, off);
}
#endif

#if defined(__linux__) || defined(__sun)
/*
 * Here the pools are malloc()s.  On the BSDs they are mmap()s (USE_ANON_MMAP in
 * lib/config.h), so malloc is left alone there: a dlsym wrapper with a static boot buffer
 * is not needed and risks a crash.
 */
void *
malloc(size_t n)
{
    static malloc_fn real;
    static int busy;
    static char boot[256];

    if (real == NULL) {
	if (busy)		/* dlsym() asking for memory itself */
	    return n <= sizeof(boot) ? boot : NULL;
	busy = 1;
	real = (malloc_fn)dlsym(RTLD_NEXT, "malloc");
	busy = 0;
    }
    if (n == 65536 && !busy) {
	busy = 1;
	shim_mark("pool");
	busy = 0;
    }
    return real(n);
}
#endif
`

// TestDnewslinkRetryEntriesFreed: when the connection to a streaming peer is lost with
// articles pending, dnewslink keeps them in their stream slots, marked for a retry, and
// offers them again on the new connection (readretry). Each entry holds three strings from
// the memory pool: the path, the Message-ID and the queue line. Reading the entry back for
// the new offer gave back only the queue line, so every retry cost the path and the
// Message-ID: the pool grew with the number of retries.
//
// Input: 6000 queued articles. On every connection the fake peer refuses (438) the first
// six CHECKs, answers nothing after that and closes the connection at the 22nd CHECK, with
// the window of 16 full again: 16 entries go to the retry list, are offered again, and so on
// for about 1000 connections, until the queue is used up (the last connection is
// answered). Expected: the queue is worked off after many retries, and dnewslink needed
// only a few memory pools of 64 KB (counted by an LD_PRELOAD shim; 2 with the fix),
// whatever the number of retries. Fails on main: the lost strings need 20 or more.
func TestDnewslinkRetryEntriesFreed(t *testing.T) {
	const total = 6000
	so := preloadShim(t, "poolmap", withMark(poolMapSource))
	mark := filepath.Join(t.TempDir(), "pools")
	s := NewSite(t, SiteOptions{NoStart: true})
	s.Env = append(s.Env, lockFaultEnv(so), "SHIM_MARK="+mark)
	dnlSpool(t, s)

	var (
		mu     sync.Mutex
		perCon = map[int]int{} // CHECK lines per connection
		checks int
		seen   = map[string]int{} // CHECK lines per id
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
			case "check":
				if len(f) < 2 {
					return nil, false
				}
				mu.Lock()
				defer mu.Unlock()
				perCon[c.ID()]++
				n := perCon[c.ID()]
				checks++
				seen[f[1]]++
				if n <= 6 {
					return []string{"438 " + f[1]}, false
				}
				if n >= 22 {
					return nil, true // the window is full again: go away
				}
				return nil, false
			}
			return nil, false
		})})

	var q strings.Builder
	for i := 0; i < total; i++ {
		fmt.Fprintf(&q, "D.00000001/B.0001 <q%05d@e2e.test> %d,100\n", i, i*100)
	}
	if err := os.WriteFile(filepath.Join(s.Dir, "dqueue", "out.S00000"), []byte(q.String()), 0o644); err != nil {
		t.Fatal(err)
	}

	out, err := s.TryRun("dnewslink", "-N", "1", "-S", "0", "-b", "out.S%05d", "-h", "127.0.0.1",
		"-P", strconv.Itoa(srv.Port()), "-t", "5")
	t.Logf("dnewslink: err=%v\n%.2000s", err, out)

	pools := len(shimMarked(mark, "pool"))
	mu.Lock()
	defer mu.Unlock()
	again := 0 // CHECKs beyond the first one of each id: the retries
	for _, n := range seen {
		again += n - 1
	}
	t.Logf("connections %d, CHECKs %d (%d ids, %d of them retries), 64 KB pools mapped %d",
		srv.Accepted(), checks, len(seen), again, pools)
	if again < 15000 || srv.Accepted() < 900 {
		t.Fatalf("the scenario did not develop: %d retried offers (want 15000 or more) on %d connections (want 900 or more)",
			again, srv.Accepted())
	}
	if pools == 0 {
		t.Fatalf("the pool shim saw no pool being mapped: the test cannot see the leak")
	}
	if pools > 5 {
		t.Errorf("dnewslink mapped %d memory pools of 64 KB for %d retried offers: the path and the Message-ID of an entry read back from the retry list are not given back (2 are enough)",
			pools, again)
	}
}
