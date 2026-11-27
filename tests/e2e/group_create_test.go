package e2e

// D-254: group creation is create-if-absent. A creator that found no record for a group and
// then writes NB and NE must not write them over a group another process made, and numbered,
// in between. Every creator of dactive.kp records is tested the same way: an LD_PRELOAD shim
// stops the process under test at its first append lock (byte 4 of dactive.kp, which every
// record creation takes), that is after its look at the record found nothing and before its
// write. While it waits, a second creator makes the group and sets NB=5 and NE=42 (the group
// has numbers); then the first one goes on. The group must still have NB=5 and NE=42, and
// the next article in it must get number 43.
//
// Nothing here sleeps for the race: the shim writes a mark file when it has the process
// and waits for a go file, which the test makes once the second creator is done.

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	testkit "github.com/go-while/nntp-testkit"
)

// groupCreatePauseSource: with GROUPCREATE_PAUSE=<prefix> and the file <prefix>.arm, the first
// process that asks for an exclusive blocking lock on bytes 4-7 of a file called dactive.kp
// (the append lock, which KPDBWrite() takes to create a record) writes <prefix>.held (its
// process id) and waits until <prefix>.go exists (60 s at most). The others, and that process
// after the wait, go on at once.
//
// KPDBReOpen() takes the same lock while it (re)maps the file: when the program opens it, and
// at the first look that finds nothing (KPDBLookup() maps again when the file has changed).
// The shim does not stop there: it follows the mappings of dactive.kp (mmap, munmap) and
// takes a request on a descriptor that has none for a reopen. A creation asks for the lock
// with the file mapped, after the look that found no record.
const groupCreatePauseSource = `#define _GNU_SOURCE
#define __EXTENSIONS__
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef int (*fcntl_fn)(int, int, ...);
/*FDPATH*/
#define MAXMAP 16
static struct { char *base; size_t len; int fd; } maps[MAXMAP];

static int
is_active(int fd)
{
    char path[4096];
    size_t l;

    fdpath(fd, path, sizeof(path));
    l = strlen(path);
    return l >= 10 && strcmp(path + l - 10, "dactive.kp") == 0;
}

static void
track_map(void *r, size_t len, int fd)
{
    int i;

    if (r == MAP_FAILED || fd < 0 || !is_active(fd))
	return;
    for (i = 0; i < MAXMAP; i++) {
	if (maps[i].base == NULL) {
	    maps[i].base = r;
	    maps[i].len = len;
	    maps[i].fd = fd;
	    return;
	}
    }
}

void *
mmap(void *a, size_t l, int prot, int flags, int fd, off_t off)
{
    static void *(*real)(void *, size_t, int, int, int, off_t);
    void *r;

    if (real == NULL)
	real = (void *(*)(void *, size_t, int, int, int, off_t))dlsym(RTLD_NEXT, "mmap");
    r = real(a, l, prot, flags, fd, off);
    track_map(r, l, fd);
    return r;
}

#ifdef __linux__
void *
mmap64(void *a, size_t l, int prot, int flags, int fd, off_t off)
{
    static void *(*real)(void *, size_t, int, int, int, off_t);
    void *r;

    if (real == NULL)
	real = (void *(*)(void *, size_t, int, int, int, off_t))dlsym(RTLD_NEXT, "mmap64");
    r = real(a, l, prot, flags, fd, off);
    track_map(r, l, fd);
    return r;
}
#endif

int
munmap(void *a, size_t l)
{
    static int (*real)(void *, size_t);
    int i;

    if (real == NULL)
	real = (int (*)(void *, size_t))dlsym(RTLD_NEXT, "munmap");
    for (i = 0; i < MAXMAP; i++) {
	if (maps[i].base != NULL && (char *)a >= maps[i].base && (char *)a < maps[i].base + maps[i].len)
	    maps[i].base = NULL;
    }
    return real(a, l);
}

static int
mapped(int fd)
{
    int i;

    for (i = 0; i < MAXMAP; i++) {
	if (maps[i].base != NULL && maps[i].fd == fd)
	    return 1;
    }
    return 0;
}

static void
pause_here(int fd, int cmd, struct flock *fl)
{
    const char *p = getenv("GROUPCREATE_PAUSE");
    char name[4200], pid[32];
    int h, i;

    if (p == NULL || cmd != F_SETLKW || fl->l_type != F_WRLCK || fl->l_start != 4)
	return;
    if (!mapped(fd))
	return;
    snprintf(name, sizeof(name), "%s.arm", p);
    if (access(name, F_OK) != 0)
	return;
    snprintf(name, sizeof(name), "%s.held", p);
    h = open(name, O_WRONLY|O_CREAT|O_EXCL, 0644);
    if (h < 0)
	return;
    snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
    (void)write(h, pid, strlen(pid));
    close(h);
    snprintf(name, sizeof(name), "%s.go", p);
    for (i = 0; i < 3000 && access(name, F_OK) != 0; i++)
	usleep(20000);
}

static int
call(const char *name, int fd, int cmd, void *arg)
{
    fcntl_fn real = (fcntl_fn)dlsym(RTLD_NEXT, name);

    if (real == NULL)
	real = (fcntl_fn)dlsym(RTLD_NEXT, "fcntl");
    pause_here(fd, cmd, arg);
    return real(fd, cmd, arg);
}

int
fcntl(int fd, int cmd, ...)
{
    va_list va;
    void *arg;

    va_start(va, cmd);
    arg = va_arg(va, void *);
    va_end(va);
    return call("fcntl", fd, cmd, arg);
}

int
fcntl64(int fd, int cmd, ...)
{
    va_list va;
    void *arg;

    va_start(va, cmd);
    arg = va_arg(va, void *);
    va_end(va);
    return call("fcntl64", fd, cmd, arg);
}
`

// groupCreateOutPauseSource: with GROUPCREATE_PAUSE=<prefix>, the file <prefix>.arm and
// GROUPCREATE_PAUSE_ON=<text>, the first printf() whose format contains the text writes
// <prefix>.held and waits for <prefix>.go (60 s at most), as the shim above does. A program
// that prints after each step of its work (dsyncgroups -v) stops between two steps.
const groupCreateOutPauseSource = `#define _GNU_SOURCE
#define __EXTENSIONS__
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
printf(const char *fmt, ...)
{
    const char *p = getenv("GROUPCREATE_PAUSE");
    const char *on = getenv("GROUPCREATE_PAUSE_ON");
    va_list ap;
    int r;

    if (p != NULL && on != NULL && strstr(fmt, on) != NULL) {
	char name[4200], pid[32];
	int h, i;

	snprintf(name, sizeof(name), "%s.arm", p);
	if (access(name, F_OK) == 0) {
	    snprintf(name, sizeof(name), "%s.held", p);
	    h = open(name, O_WRONLY|O_CREAT|O_EXCL, 0644);
	    if (h >= 0) {
		snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
		(void)write(h, pid, strlen(pid));
		close(h);
		snprintf(name, sizeof(name), "%s.go", p);
		for (i = 0; i < 3000 && access(name, F_OK) != 0; i++)
		    usleep(20000);
	    }
	}
    }
    va_start(ap, fmt);
    r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}
`

// Numbers the second creator gives the group, and the number the next article must get.
const (
	raceNB   = "0000000005"
	raceNE   = "0000000042"
	raceNext = "0000000043"
)

// createRace is one pause of one process at the creation of a record.
type createRace struct {
	prefix string
	env    []string // for the process to stop (and for a daemon, in its Env)
}

// newCreateRace builds the shim. It skips where LD_PRELOAD shims do not run.
func newCreateRace(t *testing.T) *createRace {
	t.Helper()
	return newCreateRaceFrom(t, "groupcreate", groupCreatePauseSource)
}

// newCreateRaceFrom is newCreateRace for another shim source.
func newCreateRaceFrom(t *testing.T, name, src string) *createRace {
	t.Helper()
	so := preloadShim(t, name, src)
	r := &createRace{prefix: filepath.Join(t.TempDir(), "gc")}
	r.env = []string{lockFaultEnv(so), "GROUPCREATE_PAUSE=" + r.prefix}
	t.Cleanup(r.release) // a shimmed process never waits on after the test
	return r
}

// arm makes the next process that reaches the creation wait there.
func (r *createRace) arm(t *testing.T) {
	t.Helper()
	if err := os.WriteFile(r.prefix+".arm", nil, 0o644); err != nil {
		t.Fatal(err)
	}
}

func (r *createRace) release() { _ = os.WriteFile(r.prefix+".go", nil, 0o644) }

func (r *createRace) waitHeld(d time.Duration) bool {
	for end := time.Now().Add(d); time.Now().Before(end); time.Sleep(10 * time.Millisecond) {
		if b, err := os.ReadFile(r.prefix + ".held"); err == nil && strings.HasSuffix(string(b), "\n") {
			return true
		}
	}
	return false
}

// between runs work (the second creator) once a process is held, then lets the process go. It
// lets it go whatever work returns. The result is work's error.
func (r *createRace) between(work func() error) <-chan error {
	errc := make(chan error, 1)
	go func() {
		defer r.release()
		if !r.waitHeld(60 * time.Second) {
			errc <- errors.New("no process reached the creation of a record (no .held file)")
			return
		}
		errc <- work()
	}()
	return errc
}

// runHeld arms the shim, runs the program name from the site's DIABLO_BIN with it, and between
// its two steps runs work. It returns the program's output.
func (r *createRace) runHeld(t *testing.T, s *Site, work func() error, name string, args ...string) string {
	t.Helper()
	r.arm(t)
	errc := r.between(work)
	cmd := exec.Command(filepath.Join(s.Bin, name), args...)
	cmd.Dir = s.Dir
	cmd.Env = append(append(os.Environ(), s.Env...), r.env...)
	cmd.WaitDelay = 3 * time.Second
	out, err := cmd.CombinedOutput()
	if werr := <-errc; werr != nil {
		t.Fatalf("%s: the second creator: %v\n%s", name, werr, out)
	}
	if err != nil {
		t.Fatalf("%s %s: %v\n%s", name, strings.Join(args, " "), err, out)
	}
	return string(out)
}

// secondCreator makes group with dgrpctl (the active file of the feeder with -s) and gives it
// the numbers raceNB and raceNE.
func secondCreator(s *Site, group string, feeder bool) func() error {
	return func() error {
		var pre []string
		if feeder {
			pre = []string{"-s"}
		}
		for _, a := range [][]string{
			{"newgroup", group, "y"},
			{"adjust", group, "NB", "=" + strings.TrimLeft(raceNB, "0")},
			{"adjust", group, "NE", "=" + strings.TrimLeft(raceNE, "0")},
		} {
			if out, err := s.run("dgrpctl", append(append([]string{}, pre...), a...)...); err != nil {
				return fmt.Errorf("dgrpctl %s: %v\n%s", strings.Join(a, " "), err, out)
			}
		}
		return nil
	}
}

// kpFields returns the fields of group's record in the site's dactive.kp (dkp -r), or nil when
// it has none.
func kpFields(t *testing.T, s *Site, group string) map[string]string {
	t.Helper()
	out := s.Run(t, "dkp", "-r", filepath.Join(s.Dir, "dactive.kp"), group)
	if !strings.HasPrefix(strings.TrimSpace(out), "key ") {
		return nil
	}
	f := map[string]string{}
	for _, w := range strings.Fields(out) {
		if k, v, ok := strings.Cut(w, "="); ok {
			f[k] = v
		}
	}
	return f
}

// wantNumbers fails the test unless group keeps the numbers the second creator gave it.
func wantNumbers(t *testing.T, s *Site, group, who string) {
	t.Helper()
	f := kpFields(t, s, group)
	if f == nil {
		t.Fatalf("%s: %s has no record", who, group)
	}
	if f["NB"] != raceNB || f["NE"] != raceNE {
		t.Errorf("%s: %s has NB=%s NE=%s, want NB=%s NE=%s (the group was numbered before the first creator wrote)",
			who, group, f["NB"], f["NE"], raceNB, raceNE)
	}
}

// wantFresh fails the test unless group is a new group with the numbers a creator gives it:
// NB=1 NE=0, status y and both times.
func wantFresh(t *testing.T, s *Site, group, who string) {
	t.Helper()
	f := kpFields(t, s, group)
	if f == nil {
		t.Fatalf("%s: the new group %s was not created", who, group)
	}
	if f["NB"] != "0000000001" || f["NE"] != "0000000000" || f["S"] != "y" || f["CTS"] == "" || f["LMTS"] == "" {
		t.Errorf("%s: new group %s has %v, want NB=0000000001 NE=0000000000 S=y and CTS, LMTS", who, group, f)
	}
}

// waitNE waits until group has NE=want and fails the test with what it has otherwise.
func waitNE(t *testing.T, s *Site, group, want, what string) {
	t.Helper()
	var f map[string]string
	ok := false
	for end := time.Now().Add(10 * time.Second); time.Now().Before(end); time.Sleep(100 * time.Millisecond) {
		if f = kpFields(t, s, group); f != nil && f["NE"] == want {
			ok = true
			break
		}
	}
	if !ok {
		t.Errorf("%s: %s has %v, want NE=%s", what, group, f, want)
	}
}

// TestGroupCreateDgrpctlNewgroup (D-254): dgrpctl newgroup read the group as absent, and a
// second creator made it and numbered it before newgroup wrote NB and NE, which it wrote over
// the numbers (NB=1, NE=0: the next articles got numbers that are in use). A group made and
// numbered in between keeps its numbers; newgroup still sets what it is meant to on the
// existing group (the description), and a new group is created as before. Fails on main: the
// group has NB=0000000001 NE=0000000000.
func TestGroupCreateDgrpctlNewgroup(t *testing.T) {
	r := newCreateRace(t)
	s := NewReaderSite(t, ReaderOptions{SpoolAddr: "127.0.0.1:1", Groups: []string{"e2e.test"}, NoStart: true})
	out := r.runHeld(t, s, secondCreator(s, "e2e.race", false),
		"dgrpctl", "newgroup", "e2e.race", "m", `""`, "The race group.")
	wantNumbers(t, s, "e2e.race", "dgrpctl newgroup")
	f := kpFields(t, s, "e2e.race")
	if f["GD"] == "" || f["S"] != "m" {
		t.Errorf("the existing group got no description and status from newgroup (GD=%q S=%q):\n%s", f["GD"], f["S"], out)
	}
	if !strings.Contains(out, "updated group") {
		t.Errorf("newgroup did not report the group as existing:\n%s", out)
	}

	s.Run(t, "dgrpctl", "newgroup", "e2e.fresh", "y", `""`, "A fresh group.")
	wantFresh(t, s, "e2e.fresh", "dgrpctl newgroup")
}

// TestGroupCreateDgrpctlCheckgroups (D-254): the same for dgrpctl checkgroups exec, which
// creates the groups of the list that do not exist. A group made and numbered after it looked
// is left as it is. Fails on main: NB=0000000001 NE=0000000000.
func TestGroupCreateDgrpctlCheckgroups(t *testing.T) {
	r := newCreateRace(t)
	s := NewReaderSite(t, ReaderOptions{SpoolAddr: "127.0.0.1:1", Groups: []string{"e2e.test"}, NoStart: true})
	s.WriteFile(t, "checkgroups", "e2e.test\tThe test group.\ne2e.race\tThe race group.\n")
	s.Run(t, "dgrpctl", "newgroup", "e2e.test", "y", `""`, "The test group.")
	cg := filepath.Join(s.Dir, "checkgroups")
	out := r.runHeld(t, s, secondCreator(s, "e2e.race", false), "dgrpctl", "checkgroups", "exec", cg)
	wantNumbers(t, s, "e2e.race", "dgrpctl checkgroups exec")
	if strings.Contains(out, "Added group e2e.race") {
		t.Errorf("checkgroups reports adding e2e.race, which existed by then:\n%s", out)
	}

	s.WriteFile(t, "checkgroups2", "e2e.test\tThe test group.\ne2e.race\tThe race group.\ne2e.fresh\tA fresh group.\n")
	out = s.Run(t, "dgrpctl", "checkgroups", "exec", filepath.Join(s.Dir, "checkgroups2"))
	if !strings.Contains(out, "Added group e2e.fresh") {
		t.Errorf("checkgroups did not add e2e.fresh:\n%s", out)
	}
	wantFresh(t, s, "e2e.fresh", "dgrpctl checkgroups exec")
	if f := kpFields(t, s, "e2e.fresh"); f["GD"] == "" {
		t.Errorf("e2e.fresh has no description: %v", f)
	}
}

// TestGroupCreateDsyncgroups (D-254): dsyncgroups -g creates the groups of the remote that
// its scan of dactive.kp did not find, and writes the record when the run is over. A group
// made and numbered after the scan is left as it is (it wrote NB=1, NE=0 and NX=0 over it).
// Fails on main: NB=0000000001 NE=0000000000.
func TestGroupCreateDsyncgroups(t *testing.T) {
	r := newCreateRace(t)
	s := NewReaderSite(t, ReaderOptions{SpoolAddr: "127.0.0.1:1", Groups: []string{"e2e.test"}, NoStart: true})
	db := filepath.Join(s.Dir, "dactive.kp")
	port := dsyncPeer{
		Active:     []string{"e2e.race 10 1 y"},
		Newsgroups: []string{"e2e.race\tThe race group."},
	}.listen(t)
	out := r.runHeld(t, s, secondCreator(s, "e2e.race", false),
		"dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-F", db, "-g", "-i")
	wantNumbers(t, s, "e2e.race", "dsyncgroups -g")
	if !strings.Contains(out, "left as it is") {
		t.Errorf("dsyncgroups did not say it left the group as it is:\n%s", out)
	}

	port = dsyncPeer{
		Active:     []string{"e2e.fresh 10 1 y"},
		Newsgroups: []string{"e2e.fresh\tA fresh group."},
	}.listen(t)
	s.Run(t, "dsyncgroups", "-h", "127.0.0.1", "-p", strconv.Itoa(port), "-F", db, "-g", "-i")
	f := kpFields(t, s, "e2e.fresh")
	if f == nil || f["NB"] != "0000000001" || f["NE"] != "0000000000" || f["NX"] != "0000000000" || f["GD"] == "" {
		t.Errorf("e2e.fresh made by dsyncgroups -g has %v, want NB=0000000001 NE=0000000000 NX=0000000000 and a description", f)
	}
}

// TestGroupCreateDsyncgroupsLock (D-254): dsyncgroups writes the fields of a new group with
// one write each. It used to take the record lock for none of them, so a process that read
// the record in between (diablo's Xref numbers, dreaderd's article numbers: they lock the
// record, read, and write NX or NE) saw it with NB and NE but no NX, and its number was
// written over by the next write. Now the record stays locked from the NB write to the last
// one, as for every other creator.
//
// A shim holds dsyncgroups -v when it prints " NE=" (after the write of NE, before that of
// NX): the group has NB and NE. dgrpctl adjust (it locks the record, reads NX, writes NX+5)
// starts then. With the lock it must wait for dsyncgroups and so end up behind it:
// NX=0000000005 at the end. Without it adjust is done at once (the test waits at most 2 s
// for that) and dsyncgroups then writes NX=0000000000 over its number. Fails on ed4f916e
// (#507 before this change): adjust ends while the group is half written, and
// NX=0000000000 at the end.
func TestGroupCreateDsyncgroupsLock(t *testing.T) {
	r := newCreateRaceFrom(t, "groupcreateout", groupCreateOutPauseSource)
	s := NewReaderSite(t, ReaderOptions{SpoolAddr: "127.0.0.1:1", Groups: []string{"e2e.test"}, NoStart: true})
	port := dsyncPeer{Active: []string{"e2e.lock 10 1 y"}}.listen(t)
	type result struct {
		out string
		err error
	}
	r.arm(t)
	aDone := make(chan result, 1)
	go func() {
		cmd := exec.Command(filepath.Join(s.Bin, "dsyncgroups"), "-h", "127.0.0.1", "-p", strconv.Itoa(port),
			"-F", filepath.Join(s.Dir, "dactive.kp"), "-g", "-v")
		cmd.Dir = s.Dir
		cmd.Env = append(append(os.Environ(), s.Env...), append(append([]string{}, r.env...), "GROUPCREATE_PAUSE_ON= NE=")...)
		cmd.WaitDelay = 3 * time.Second
		out, err := cmd.CombinedOutput()
		aDone <- result{string(out), err}
	}()
	if !r.waitHeld(60 * time.Second) {
		t.Fatal("dsyncgroups did not get to the write of NE of the new group (no .held file)")
	}
	if f := kpFields(t, s, "e2e.lock"); f == nil || f["NB"] != "0000000001" || f["NE"] != "0000000000" || f["NX"] != "" {
		t.Fatalf("the held group has %v, want NB and NE and no NX", f)
	}
	bDone := make(chan result, 1)
	go func() {
		out, err := s.run("dgrpctl", "adjust", "e2e.lock", "NX", "+5")
		bDone <- result{out, err}
	}()
	var b result
	early := false
	select {
	case b = <-bDone:
		early = true
	case <-time.After(2 * time.Second):
	}
	r.release()
	a := <-aDone
	if !early {
		b = <-bDone
	}
	if a.err != nil {
		t.Fatalf("dsyncgroups: %v\n%s", a.err, a.out)
	}
	if b.err != nil {
		t.Fatalf("dgrpctl adjust: %v\n%s", b.err, b.out)
	}
	if early {
		t.Errorf("dgrpctl adjust got through while dsyncgroups held the new group half written (NB and NE, no NX); it printed:\n%s", b.out)
	}
	if f := kpFields(t, s, "e2e.lock"); f["NB"] != "0000000001" || f["NE"] != "0000000000" || f["NX"] != "0000000005" || f["S"] != "y" {
		t.Errorf("e2e.lock has %v, want NB=0000000001 NE=0000000000 NX=0000000005 S=y (the adjust after dsyncgroups)", f)
	}
}

// controlArticle is a control message from e2e.test: command, one of "newgroup e2e.x" and
// "checkgroups", and the body lines.
func controlArticle(t *testing.T, command string, body ...string) []string {
	t.Helper()
	a := newArticle(msgID(t), "e2e.test", "cmsg "+command, body...)
	return append(a[:3:3], append([]string{"Control: " + command}, a[3:]...)...)
}

// TestGroupCreateControlNewgroup (D-254): a newgroup control message for a group that was made
// and numbered after dreaderd looked is applied to the existing group (description, as for any
// existing group) and leaves its numbers; the next article gets number 43. A new group is
// created as before. Fails on main: NB=0000000001 NE=0000000000, and the next article is
// number 1 (or 0000000001).
func TestGroupCreateControlNewgroup(t *testing.T) {
	r := newCreateRace(t)
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Groups: []string{"e2e.test"},
		Files:  map[string]string{"dcontrol.ctl": "newgroup:*:*:doit\n"},
		Env:    r.env,
	})
	r.arm(t)
	errc := r.between(secondCreator(s, "e2e.race", false))
	feedReader(t, s, st, "e2e.test", controlArticle(t, "newgroup e2e.race", "For your newsgroups file:", "e2e.race\tThe race group."))
	if err := <-errc; err != nil {
		t.Fatalf("the second creator: %v", err)
	}
	testkit.WaitFor(t, 10*time.Second, "the description of e2e.race", func() bool {
		f := kpFields(t, s, "e2e.race")
		return f != nil && f["GD"] != ""
	})
	wantNumbers(t, s, "e2e.race", "newgroup control message")

	st.AddGroup("e2e.race", "race")
	feedReader(t, s, st, "e2e.race", newArticle(msgID(t), "e2e.race", "next article", "body"))
	waitNE(t, s, "e2e.race", raceNext, "the next article")

	feedReader(t, s, st, "e2e.test", controlArticle(t, "newgroup e2e.fresh", "For your newsgroups file:", "e2e.fresh\tA fresh group."))
	testkit.WaitFor(t, 10*time.Second, "the new group e2e.fresh", func() bool {
		f := kpFields(t, s, "e2e.fresh")
		return f != nil && f["GD"] != ""
	})
	wantFresh(t, s, "e2e.fresh", "newgroup control message")
}

// TestGroupCreateControlCheckgroups (D-254): a checkgroups control message creates the groups
// of its list that do not exist; a group made and numbered after dreaderd looked is left as it
// is. Fails on main: NB=0000000001 NE=0000000000 (and "Added group e2e.race" in the log).
func TestGroupCreateControlCheckgroups(t *testing.T) {
	r := newCreateRace(t)
	st, spool := spoolStore(t)
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Groups: []string{"e2e.test"},
		Files:  map[string]string{"dcontrol.ctl": "checkgroups:*:*:doit=checkgroups.log\n" + cgGroupLines},
		Env:    r.env,
	})
	// the description of e2e.test is the list's: no write before the creation of e2e.race
	s.Run(t, "dgrpctl", "newgroup", "e2e.test", "y", `""`, "The test group.")
	r.arm(t)
	errc := r.between(secondCreator(s, "e2e.race", false))
	feedReader(t, s, st, "e2e.test", controlArticle(t, "checkgroups", "e2e.test\tThe test group.", "e2e.race\tThe race group."))
	if err := <-errc; err != nil {
		t.Fatalf("the second creator: %v", err)
	}
	logFile := filepath.Join(s.Dir, "log", "checkgroups.log")
	testkit.WaitFor(t, 10*time.Second, "e2e.race in the checkgroups log", func() bool {
		b, _ := os.ReadFile(logFile)
		return strings.Contains(string(b), "e2e.race")
	})
	wantNumbers(t, s, "e2e.race", "checkgroups control message")
	if b, _ := os.ReadFile(logFile); strings.Contains(string(b), "Added group e2e.race") {
		t.Errorf("the log says e2e.race was added, which existed by then:\n%s", b)
	}

	feedReader(t, s, st, "e2e.test", controlArticle(t, "checkgroups", "e2e.test\tThe test group.", "e2e.race\tThe race group.", "e2e.fresh\tA fresh group."))
	testkit.WaitFor(t, 10*time.Second, "the new group e2e.fresh", func() bool { return kpFields(t, s, "e2e.fresh") != nil })
	testkit.WaitFor(t, 10*time.Second, "its complete record", func() bool { f := kpFields(t, s, "e2e.fresh"); return f != nil && f["S"] != "" })
	wantFresh(t, s, "e2e.fresh", "checkgroups control message")
}

// TestGroupCreateReaderAutoAdd (D-254): with "readerautoaddtoactive on" dreaderd creates the
// group of an article it takes. A group made and numbered after it looked is left as it is,
// and the article gets number 43. Fails on main: NE=0000000001 after the article.
func TestGroupCreateReaderAutoAdd(t *testing.T) {
	r := newCreateRace(t)
	st, spool := spoolStore(t)
	st.AddGroup("e2e.race", "race")
	s := NewReaderSite(t, ReaderOptions{
		Spool:  spool,
		Groups: []string{"e2e.test"},
		Config: map[string]string{"readerautoaddtoactive": "on"},
		Env:    r.env,
	})
	r.arm(t)
	errc := r.between(secondCreator(s, "e2e.race", false))
	feedReader(t, s, st, "e2e.race", newArticle(msgID(t), "e2e.race", "first article", "body"))
	if err := <-errc; err != nil {
		t.Fatalf("the second creator: %v", err)
	}
	waitNE(t, s, "e2e.race", raceNext, "the article of the racing creator")
	if f := kpFields(t, s, "e2e.race"); f["NB"] != raceNB {
		t.Errorf("e2e.race has NB=%s, want %s", f["NB"], raceNB)
	}

	st.AddGroup("e2e.fresh", "fresh")
	feedReader(t, s, st, "e2e.fresh", newArticle(msgID(t), "e2e.fresh", "an article of a new group", "body"))
	waitNE(t, s, "e2e.fresh", "0000000001", "the article of a new group")
	if f := kpFields(t, s, "e2e.fresh"); f["NB"] != "0000000001" || f["S"] != "y" || f["CTS"] == "" || f["LMTS"] == "" {
		t.Errorf("new group e2e.fresh has %v, want NB=0000000001, S=y, CTS, LMTS", f)
	}
}

// TestGroupCreateFeederAutoAdd (D-254): with "feederautoaddtoactive on" diablo creates the
// groups of an article it takes (GenerateXRef()). A group made and numbered after it looked is
// left as it is: the article gets "e2e.race:43" in its Xref, as the next number of that group.
// Fails on main: e2e.race:1, a number the group has used.
func TestGroupCreateFeederAutoAdd(t *testing.T) {
	r := newCreateRace(t)
	c := map[string]string{"active": "on", "feederautoaddtoactive": "on"}
	s := NewSite(t, SiteOptions{Config: c, Files: map[string]string{"dactive.kp": ""}, Env: r.env})
	p := s.Peer(t)
	id := msgID(t)
	r.arm(t)
	errc := r.between(secondCreator(s, "e2e.race", true))
	x := deepRevXref(t, p, id, newArticle(id, "e2e.race", "the first article", "body"))
	if err := <-errc; err != nil {
		t.Fatalf("the second creator: %v", err)
	}
	if want := PathHost + " e2e.race:" + strings.TrimLeft(raceNext, "0"); x != want {
		t.Errorf("Xref of the article: %q, want %q", x, want)
	}
	// the feeder counts in NX, past NE; its numbers are the ones the second creator set
	if f := kpFields(t, s, "e2e.race"); f["NB"] != raceNB || f["NE"] != raceNE || f["NX"] != raceNext {
		t.Errorf("e2e.race has %v, want NB=%s NE=%s NX=%s", f, raceNB, raceNE, raceNext)
	}

	id2 := msgID(t)
	x2 := deepRevXref(t, p, id2, newArticle(id2, "e2e.fresh", "an article of a new group", "body"))
	if want := PathHost + " e2e.fresh:1"; x2 != want {
		t.Errorf("Xref of the article of a new group: %q, want %q", x2, want)
	}
	p.ExpectCmd(205, "QUIT")
	// the feeder writes NB=1 NE=0 as it always did; NX is the article just numbered
	if f := kpFields(t, s, "e2e.fresh"); f == nil || f["NB"] != "1" || f["NE"] != "0" || f["NX"] != "0000000001" || f["S"] != "y" || f["CTS"] == "" || f["LMTS"] == "" {
		t.Errorf("new group e2e.fresh has %v, want NB=1 NE=0 NX=0000000001 S=y, CTS, LMTS", f)
	}
}
