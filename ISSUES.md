# Known Issues

Findings about Diablo 6 since the 6-CUR-20261024-BWH release (2026-10-24). They come from
the end-to-end tests in `tests/e2e` (PR #11), builds on 21 systems (FreeBSD, SmartOS, 19
Linux distributions, Debian GNU/Hurd), and code review. [REVIEW.md](REVIEW.md) tracks how
much of the code has been reviewed.

Line numbers refer to `main` at `ed3ce67` unless a PR is named. "Verified" means checked
against the code by reading it; "from review" means reported by a review pass and not
re-checked yet.

Status values: **open**, **PR** (fix waiting for review, with its number), **fixed** (with
the PR that fixed it), **decision** (needs the maintainer to decide), **won't fix**.

Last updated 2026-10-26, after batch 1 (#30, #31, #37 to #44, #47 to #49) and the first part
of batch 2 (#46, #50, #52, #62) were merged. Open: #35 (GnuPG pgpverify, held for a private
advisory), #45 with its tests in #56, #53 to #55, #57 to #61, #63 to #65.

| ID | Severity | Summary | Status |
|---|---|---|---|
| [D-01](#d-01-host-cache-never-rebuilt-after-a-start-without-dhostscache) | Medium | Host cache never rebuilt after a start without `dhosts.cache` | fixed (#10) |
| [D-02](#d-02-privilege-drop-results-not-checked) | Medium | `setgroups`/`setgid`/`setuid` results not checked | fixed (#6) |
| [D-03](#d-03-non-root-start-refused-after-d-02) | High | A non-root start refused after the first D-02 fix | fixed (#6, 5a6e877) |
| [D-04](#d-04-write-through-a-null-pointer-in-feedwrite) | High | Write through a NULL pointer in `FeedWrite()` | fixed (#8) |
| [D-05](#d-05-const-strchr-warnings-with-glibc-243) | Low | `const strchr()` warnings with glibc 2.43 or later | fixed (#8) |
| [D-06](#d-06-dreaderd-writes-400-too-busy-after-a-205) | Medium | dreaderd writes "400 too busy" after a session's 205 | fixed (#12) |
| [D-07](#d-07-dreaderd-cleanup-can-close-another-clients-descriptor) | Medium | dreaderd cleanup can close another client's descriptor | fixed (#12) |
| [D-08](#d-08-dreaderd--p-hostport-does-not-bind) | Low | `dreaderd -P host:port` (and `-B`) does not bind | fixed (#13) |
| [D-09](#d-09-dreaderd-does-not-build-with-musl) | Medium | dreaderd does not build with musl (Alpine): `loff_t` | fixed (#14) |
| [D-10](#d-10-diablo--b-falls-through-into--a) | Low | `diablo -b fd` falls through into `-A` | fixed (#15) |
| [D-11](#d-11-kpdb-deletes-the-wrong-record-on-a-race) | High | KPDB deletes the wrong record on a create/update race | fixed (#33, test #36) |
| [D-12](#d-12-a-dead-dns-worker-can-be-picked-for-a-new-client) | High | A dead DNS worker can be picked for a new client | fixed (#34) |
| [D-13](#d-13-interrupted-kpdb-sort-marker-is-negative-as-int) | Medium | Interrupted KPDB sort marker `FFFFFFFF` is negative as `int` | fixed (#33) |
| [D-14](#d-14-a-damaged-cachehits-is-refused-forever) | Medium | A damaged `cache.hits` is refused forever | fixed (#40) |
| [D-15](#d-15-dreaderd-has-no-capabilities) | Medium | dreaderd has no `CAPABILITIES` (RFC 3977) | fixed (#41) |
| [D-16](#d-16-linux-build-fails-with-the-default-libvendorh) | Medium | Linux build fails with the default `lib/vendor.h` (RADIUS) | decision |
| [D-17](#d-17-unchecked-results-under-_fortify_source) | Low | 80 `-Wunused-result` warnings under `_FORTIFY_SOURCE` | fixed (#71) |
| [D-18](#d-18-debian-gnuhurd-build) | Low | Debian GNU/Hurd build: `PATH_MAX`, `setpgrp`, `db_185.h` | open |
| [D-19](#d-19-changelog-links-a-missing-document) | Low | CHANGELOG links the missing `BUILD-PORTABLE-MAKE.md` | fixed (#61) |
| [D-20](#d-20-strncpy-truncation-warning-in-dreaderddnsc) | Low | `strncpy` truncation warning in `dreaderd/dns.c` (GCC 16) | fixed (#65) |
| [D-21](#d-21-dactivekp-write-errors-now-exit-the-process) | — | dactive.kp write errors now exit the process (#7) | decision |
| [D-22](#d-22-gnu-make-install-ignores-prefix-and-destdir) | High | GNU Make `install`/`uninstall` ignore `PREFIX` and `DESTDIR` (live `/news` used) | fixed (#16) |
| [D-23](#d-23-feederreader-sample-config-is-ignored) | Medium | `samples/feeder+reader/diablo.config` has no `version 2.0`: every directive ignored | fixed (#26) |
| [D-24](#d-24-sample-dreaderaccess-allows-1008-not-localhost) | Medium | Sample `dreader.access` allows 1.0.0.0/8, not localhost | fixed (#55) |
| [D-25](#d-25-installed-config-files-are-read-only-symlinks-into-samples) | Medium | Installed config files are read-only symlinks into `samples/`; edits are lost on reinstall | fixed (#21) |
| [D-26](#d-26-iter-written-as-decimal-read-as-hex) | Medium | Overview `ITER` written as decimal, read as hex by `dexpireover` | fixed (#54) |
| [D-27](#d-27-documentation-is-stale-or-wrong) | Medium | INSTALL, README.md, README.READER: xmake, missing Linux steps, news user, PGP | open |
| [D-28](#d-28-man-pages) | Low | Man pages: none fully accurate, 17 programs without one, broken SEE ALSO | open |
| [D-29](#d-29-dreaderd--sdigits-sets-a-mangled-receive-buffer) | Low | `dreaderd -S<digits>` also sets a mangled receive buffer | fixed (#58) |
| [D-30](#d-30-diablo--x-sets-the-xref-host-not-xrefsync) | Low | `diablo -x` sets the Xref host, not xrefsync | fixed (#59) |
| [D-31](#d-31-option-parsing-bugs-in-utilities) | Low | Option parsing bugs in utilities (`-C file`, `dilookup -f`, `dhisexpire -o`, ...) | fixed (#64, #70) |
| [D-32](#d-32-drcmd-exits-1-after-a-good-status) | Low | `drcmd` exits 1 after a good `status` reply | fixed (#57) |
| [D-33](#d-33-dreaderd--b-host-without-port-binds-a-random-port) | Low | `dreaderd -B host` without a port binds a random port; `-P`/`-B` drop the config bind settings | fixed (#60) |
| [D-34](#d-34-installed-adm-scripts-are-not-executable) | Low | Installed adm scripts are mode 444; no `/news/adm`; csh needed | fixed (#21, #22) |
| [D-35](#d-35-samples-with-wrong-directives-or-options) | Low | Samples with wrong directives or options (dexpire.ctl, diablo.config, crontab, dcontrol.ctl) | fixed (#22, #74) |
| [D-36](#d-36-dgrpctl-checkgroups-exec) | Low | `dgrpctl checkgroups exec`: new groups without S/GD, lock order | fixed (#73) |
| [D-37](#d-37-articles-lost-when-the-spool-directory-is-removed) | High | Articles accepted (235) but lost when their spool directory is removed while diablo writes into it | fixed (#24) |
| [D-38](#d-38-dexpire-removes-the-directory-diablo-writes-into) | High | `dexpire` removes a spool's newest directory, the one diablo writes into | fixed (#25) |
| [D-39](#d-39-dreaderd-spool-server-address-may-be-udp) | Medium | dreaderd connects its spool servers with the first `getaddrinfo()` result, which is UDP on FreeBSD | fixed (#27) |
| [D-40](#d-40-feeder-children-hang-on-sighup) | High | Feeder children hang forever on the SIGHUP of `dicmd exit`/`pause` | fixed (#32) |
| [D-41](#d-41-diablo-ctl-stops-other-sites-daemons) | High | diablo-ctl stops the daemons of another site of the same user | fixed (#31) |
| [D-42](#d-42-dnewslink-ignores-sigterm-on-a-stalled-peer) | Medium | dnewslink ignores SIGTERM for up to `-T` (600 s) per wait while a peer is stalled | fixed (#37) |
| [D-43](#d-43-connlife-retires-one-rotation-late) | Low | A connlife worker retires one rotation late (connlife 60 gives about 120 s) | fixed (#30, test #38) |
| [D-44](#d-44-listgroup-reply-and-range) | Medium | dreaderd LISTGROUP: wrong 211 line, range ignored, 481 without a group | open |
| [D-45](#d-45-hdr-reply-code-and-list-headers) | Medium | dreaderd HDR answers 221 (XHDR's code); LIST HEADERS missing | open |
| [D-46](#d-46-authinfo-reply-codes) | Medium | dreaderd AUTHINFO: a wrong password gets 502 (no retry); PASS before USER, and a second login | open |
| [D-47](#d-47-other-reader-reply-codes) | Low | dreaderd reply codes: NEXT, ARTICLE in an empty group, bad article numbers, OVER past the high mark, LIST ACTIVE.TIMES | open |
| [D-48](#d-48-feeder-reply-codes) | Low | diablo: MODE READER 500, IHAVE/CHECK without an argument | open |
| [D-49](#d-49-moderation-handling) | Medium | Moderation: Approved: from any reader, posts to "n" groups, crosspost mails every moderator, failures not reported | fixed (#42, #43, #45 via #56, #46, #50) |
| [D-50](#d-50-codeql-robustness-findings) | Low | CodeQL: a `%` in a feed label or in the diablo.config path is used as a format; two log lines with wrong format types | fixed (#72) |

Feature requests are at the end: [F-01 Cancel-Lock](#f-01-cancel-lock-rfc-8315).

## Details

### D-01 Host cache never rebuilt after a start without dhosts.cache
`util/diablo.c` waited for the startup rebuild with `waitpid()` but left `HostCachePid` set,
and the main loop only rebuilds while it is 0. After a first start (or a deleted cache),
peers added to or removed from `dnewsfeeds` were ignored until a restart.
Test: `TestHostCacheRebuildAfterFreshStart`.

### D-02 Privilege drop results not checked
`diablo`, `dreaderd` and `dstart` ignored the results of `setgroups`, `setgid` and `setuid`.
A failure while root left the daemon running as root.

### D-03 Non-root start refused after D-02
The first D-02 fix exited on any failure, but `setgroups()` is root-only, so a start as an
ordinary user (or as `news`) now exited: "Unable to set supplementary groups to news". Fixed
by changing user only when `geteuid() == 0`. Found by every e2e test (they run as `news`).

### D-04 Write through a NULL pointer in FeedWrite()
`lib/newsfeed.c`. Found and fixed by the maintainer.

### D-05 const strchr() warnings with glibc 2.43 or later
11 `-Wdiscarded-qualifiers` warnings on Fedora 46 and Arch (glibc returns `const char *` for
a `const` argument).

### D-06 dreaderd writes 400 too busy after a 205
At session end the reader child hands the descriptor back to the master for accounting
(`DR_SESSEXIT_RPT`). With no free DNS worker, `SendDns()` wrote `400 Server too busy` to the
client that had already received `205` for QUIT, and closed the descriptor the caller closes
again. Reproduced on every run with `readerdns 1`, and on Rocky 10 with plain sequential
connects. Test: `TestReaderQuitWhileConnecting`.

### D-07 dreaderd cleanup can close another client's descriptor
`SendDns()` recorded the accounting descriptor in `d_FdPend`; the caller closes it and the
accounting reply never clears it. When that DNS worker died later, the `wait3()` cleanup
closed whatever descriptor had reused the number. No test (needs a worker to die).

### D-08 dreaderd -P host:port does not bind
For an unbracketed `host:port`, `-P` and `-B` passed the whole string as the host to
`getaddrinfo()` and dreaderd exited; only `[host]:port` worked. In the 2017 source too.
Test: `TestReaderBindHostPort`.

### D-09 dreaderd does not build with musl
`dreaderd/filealloc.c` casts to `loff_t`, a glibc type. Alpine 3.22 to 3.24 built 38 of 39
programs, without dreaderd.

### D-10 diablo -b falls through into -A
`case 'b'` had no `break`. dstart passes `-b<fd>`, so the news admin became the descriptor
number ("contact 5"); `-b <fd>` swallowed the next argument.

### D-11 KPDB deletes the wrong record on a race
In #7 (`lib/kpdb.c:857` on that branch), the `-` delete marker is written at `lockoff` from
the first, unlocked lookup instead of the record the locked second lookup found. A create
race writes `-` at offset 0 (the header) and leaves two live records; two writers on one
record also leave duplicates. Needs no I/O error. Details in the #7 review.
#7 was merged with it; fixed in #33 (the `-` goes to the record the merge read). Test:
`TestKPDBConcurrentUpdate` (#36), which holds the record lock between two writers' lookups.

### D-12 A dead DNS worker can be picked for a new client
In #7 (`dreaderd/main.c:1615` on that branch), the new EOF path sets `d_Count = 0` before
`wait3()` reaps the worker, so `SendDns()` can choose it: the pending client's descriptor
leaks and the new client hangs. Details in the #7 review.
#7 was merged with it; fixed in #34 (a `d_Dead` flag; `wait3()` keeps the cleanup). No test:
the window is the microseconds while the worker exits.

### D-13 Interrupted KPDB sort marker is negative as int
In #7, an interrupted sort leaves `FFFFFFFF` in the append counter; `int cval` makes it -1,
so the sort runs without the forced remap. Use `0FFFFFFF`, as the creation header does.
Fixed in #33.

### D-14 A damaged cache.hits is refused forever
In #7, `OpenCacheHits()` re-initializes only for a bad magic; a structurally bad file (the
state the old growth bug left) disables scoreboard caching until an admin deletes it.
On main since #7 was merged; fix in #40.

### D-15 dreaderd has no CAPABILITIES
`CAPABILITIES` answers `500 What?`. Clients that need it can't detect `READER`, `OVER`,
`MODE-READER`, `STREAMING` or `AUTHINFO`, and nntp-testkit's RFC 3977 conformance checks
skip almost everything (MODE STREAM works but isn't advertised).

### D-16 Linux build fails with the default lib/vendor.h
`RADIUS_ENABLED`, `DB_ENABLED` and `REJECT_DB` come from the XS4ALL site-configuration patch
(30_diablo-xs4all-config.patch). Linux has no BSD libradius (`radlib.h`), so the default
build fails there. The maintainer keeps them as admin opt-ins; the README should say to turn
RADIUS off on Linux (`tests/e2e/ci/build-on-host` does).

### D-17 Unchecked results under _FORTIFY_SOURCE
Ubuntu and Gentoo enable `_FORTIFY_SOURCE` by default: 122 to 133 `-Wunused-result` warnings
on 6-CUR-20261024-BWH, 80 after #5 to #9 (mostly `write()` in `util/` and
`dreaderd/main.c`). Debian, Fedora, RHEL-family and openSUSE show none.

### D-18 Debian GNU/Hurd build
`PATH_MAX` is undefined on Hurd (`lib/defs.h:580`); `dreaderd/main.c` uses `setpgrp(0, 0)`
because `USE_SYSV_SETPGRP` isn't set for Hurd; `dreaderd/dns.c:19` picks `db_185.h` only for
`__linux__`/`__sun`. Low priority for the maintainer.

### D-19 CHANGELOG links a missing document
`CHANGELOG.md` links `BUILD-PORTABLE-MAKE.md`, which isn't in the repository. The maintainer
has the file.

### D-20 strncpy truncation warning in dreaderd/dns.c
GCC 16 (Fedora 46): `-Wstringop-truncation` at `dreaderd/dns.c:2143`, copying 127 bytes into
`dr_ReaderName`.

### D-21 dactive.kp write errors now exit the process
With #7, `KPDBFatalIO` calls `exit(1)` on any failed dactive.kp write, which is on the
per-article path of diablo children and dreaderd feeds. A full disk now stops those
processes instead of silently corrupting the file. Probably intended; needs a decision.

### D-22 GNU make install ignores PREFIX and DESTDIR
`install-portable.sh` reads PREFIX, DESTDIR, owner and group as arguments; the GNU `Makefile`
passed them as environment variables. `gmake DESTDIR=/tmp/stage install` (as README.md
documents it) installed into the live `/news`, `gmake DESTDIR=... uninstall` removed the live
`/news/dbin/*`. BSDmakefile passes arguments. Verified, and tested before/after on Debian 13.

### D-23 feeder+reader sample config is ignored
`samples/feeder+reader/diablo.config` has no `version 2.0` line, so `lib/config.c` skips every
directive silently: `feederbindport 120` and `active on` don't apply, and diablo binds 119 next
to dreaderd. Verified (no `version` line). The other two samples got it in 6-CUR-20261024-BWH.
Fixed in #26.

### D-24 Sample dreader.access allows 1.0.0.0/8, not localhost
`samples/dreader.access:415` has `access 1/8 ro`: that is the APNIC block 1.0.0.0/8, and there is
no localhost or RFC 1918 entry, so the first `telnet localhost 119` is refused. From review.

### D-25 Installed config files are read-only symlinks into samples
`install-portable.sh` symlinks `/news/diablo.config` and the other config files to `samples/*`
(mode 444) and overwrites `samples/*` on every install or bininstall, so edits made through the
symlink are lost. The installed `dactive.kp` is a 1998 sample with 44 comp.sys.amiga groups.
From review.

Fixed in #21: config files are installed as real 0640 copies and never overwritten; symlinks
from an earlier install become real files before `samples/` is replaced; `dactive.kp` starts
empty. Tests: `TestInstallConfigFiles`, `TestInstallConvertsConfigSymlinks`.

### D-26 ITER written as decimal, read as hex
`dreaderd/group.c:904` and `util/dcancel.c` write `ITER` with `%06d`; `util/dexpireover.c:414`
reads it with base 16. From iteration 10 on they disagree, and the value is used for overview
data file names, so dexpireover may work on the wrong files. Verified (the format mismatch);
the consequences still need tracing.

### D-27 Documentation is stale or wrong
- INSTALL: "XMake is required", `xmake install`, config files "in /news/etc" (they are read
  from `/news`), `/news/logs` (is `/news/log`), no `/news/run`, `/news/feeds`, csh.
- README.md: no Linux step to turn RADIUS off (D-16); package lists only for dnf, without
  compiler; no step to create user, group and `/news`; a non-default PREFIX doesn't change
  runtime paths (`/news` is compiled in, `-C`/`DIABLO_CONFIG_PATH` needed); names
  `compat/illumos_radlib.h` (is `compat/smartos_radlib.h`); CHANGELOG says the installer does
  `clean`/`distclean` (Make targets).
- README.READER: PGP setup for pgp 2.6.3i from dead FTP sites; the default `PGP_PATH` makes
  Big-8 control verification never pass on modern systems.
- README.SERVER: dead download and bug-report addresses. TUNING_NOTES: `quadhr.expire`, old
  MAXFORKS and hsize, a leftover `<<<DONE TO HERE>>>`.
From review.

### D-28 Man pages
Checked all 24 pages against the code; none is fully accurate. 17 built programs have no page
(didate, diconvhist, diload, dspaminfo, pgpverify, dprimehostcache, dstart, dclient,
dlockhistory, dfeedtest, dhisbench, dhisctl, dexpirecache, dcancel, dexpirescoring, plock,
showlocks); `-C` and `-V` are undocumented almost everywhere; the shared SEE ALSO footer lists
missing pages and leaves out existing ones. Largest gaps: diablo.8 (options, NNTP commands,
stale expire and logging sections), dicmd.8 and drcmd.8 (most commands missing),
diablo-files.5 (almost no diablo.config directives, a "ME" label that doesn't exist, obsolete
diablo.hosts lookup), dspoolout.8, dnewslink.8, dexpire.8. From review; the per-page list is in
the description of the pull request that added this file.

### D-29 dreaderd -S<digits> sets a mangled receive buffer
In `dreaderd/main.c` `case 'S'`, the digit cases set TxBufSize and fall through into `case 'R'`,
which skips one character and sets RxBufSize from the rest: `-S8192` sets a 192-byte receive
buffer. Verified.

### D-30 diablo -x sets the Xref host, not xrefsync
`util/diablo.c` `case 'x'` calls `SetCommand("feederxrefhost", ...)` like `-X`, so
`feederxrefsync` can't be set from the command line, although Usage says `-x xrefsync`.
Verified.

### D-31 Option parsing bugs in utilities
- `dilookup -f FILE` / `-h FILE` use `*ptr ? ptr : ptr + 1` and only work by accident (reading
  past the option string). Verified.
- `-C file` with a space: drequeue, dfeedinfo and dreadover take the file name as the next
  argument (label, host or ID); dicmd and drcmd skip only `-C`. From review.
- `dhisexpire -o file` with a space silently uses `.bak` and takes `file` as the history file;
  `drequeue -d` eats the label; `diloadfromspool -e` requires `-F` and `-u`. From review.
- Follow-up: `dlockhistory -s` did not consume its sleep-time argument, and `dexpirescoring`
  and `dprimehostcache` did not skip the argument to `-C file`. Fixed in #70.

### D-32 drcmd exits 1 after a good status
`util/drcmd.c` exits 0 only after a `.` line, and dreaderd sends none after `status`, so
`drcmd status` exits 1 even when dreaderd answers. Verified (found while writing diablo-ctl,
which works around it).

### D-33 dreaderd -B host without port binds a random port
With INET6 the `-B host` branch passes a NULL port; the `readerbindport` fallback exists only in
the non-INET6 branch. Any `-P`/`-B` also drops `readerbindhost`/`readerbindport` from
diablo.config. From review.

### D-34 Installed adm scripts are not executable
`install-portable.sh` copies `samples/` with mode 444 (xmake kept 755 for executable sources);
nothing creates `/news/adm`, but `crontab.sample` runs `cd adm; ./$i`. All adm scripts are
csh, which Debian, Ubuntu and Alpine don't install by default. From review.

Fixed in #21 (scripts installed executable into `<prefix>/adm`; a changed script gets the new
version as `.new`) and #22 (the scripts are POSIX sh). Tests: `TestInstallConfigFiles`,
`TestAdm*`.

### D-35 Samples with wrong directives or options
`samples/dexpire.ctl` says `dexpireover -w` compresses (it's `-z`); `samples/diablo.config:60`
`path_dspoolctl %s/dspoolctl` (file is `dspool.ctl`); `samples/feeder/diablo.config`
`internalfilter 16` (invalid) and `feederfilter /news/dbin/filter/spamfilter` (never built);
`crontab.sample` `biweekly.*` also matches `biweekly.atrim.dexpire`, a duplicate of
`old.biweekly.atrim.dexpire`; the `dcontrol.ctl` header describes `mail`/empty-value semantics
the code doesn't implement. From review.

#22 removed `biweekly.atrim.dexpire` and gave `crontab.sample` absolute paths; the other
points are open.

### D-36 dgrpctl checkgroups exec
New groups created by `checkgroups exec` get no S or GD fields, and the lock order is wrong when
modifying (`util/dgrpctl.c:770-790`). From review.

### D-37 Articles lost when the spool directory is removed
`util/diablo.c` `ArticleFile()` keeps the spool file it writes into open, and `lib/xopen.c`
caches the current directory. When the directory was removed (by dexpire, see D-38, or by
hand), diablo kept appending to the unlinked file and answered 235, but the article was gone
(STAT 430) and the log showed IOError and "Missing dir" lines. Found by the maintainer on a
ZFS feeder. Fixed in #24: a cached file with no links left is reopened, the directory cache is
reset, and a missing directory is created again ("spool directory ... was missing, created
it"). Test: `TestSpoolDirRemovedWhileWriting`.

### D-38 dexpire removes the directory diablo writes into
`util/dexpire.c` `cleanupDirectories()` removes the oldest directories of a partition until
`minfree` is met, and could reach the newest one, which diablo is writing into. With a large
`minfree` and ZFS, which reports freed space late, it emptied whole partitions. Fixed in #25:
each partition's newest directory is kept ("Keeping ..."). Test: `TestDexpireKeepsNewestDir`.

### D-39 dreaderd spool server address may be UDP
`dreaderd/server.c` `AddServer()` calls `getaddrinfo()` without `ai_socktype` and connects to
the first result. FreeBSD lists the UDP address first, so the reader could not reach its spool
servers. Found by the maintainer on FreeBSD; fixed in #27 (`SOCK_STREAM`).

### D-40 Feeder children hang on SIGHUP
`dicmd exit` and `dicmd pause` send SIGHUP to every feeder child. The child's `sigHup()` did
its cleanup in the handler (fflush, `LogSession()` through syslog, `exit()`). When the
signal arrived while the child was itself in syslog, stdio or malloc (typically logging its
session after QUIT), it waited forever on that lock and kept its client socket and dhistory
open. Found by the 20-VM run (two children left on Gentoo). Fixed in #32: the handler only
sets a flag and shuts the session socket down. Test: `TestFeederChildSighupAfterQuit` (85 to
169 of 1000 children hung before on glibc, 6 of 2000 on musl).

### D-41 diablo-ctl stops other sites' daemons
diablo-ctl (#17, #23) took "the oldest diablo of the user" for its site's master, and
stopped every dnewslink of the user. Running the e2e tests as `news` on hosts with a live
site stopped the live diablo, dreaderd and dnewslink (the SmartOS feeder for 28 minutes).
PR #31: diablo writes `<path_log>/diablo.pid`, diablo-ctl uses only pid files and the
site's dqueue for dnewslink. Test: `TestDiabloCtlOnlyItsOwnSite`.

### D-42 dnewslink ignores SIGTERM on a stalled peer
dnewslink's handler only sets `TermFlag`, but its waits for the peer are `poll()` calls with
the `-T` timeout (600 s); a signal interrupted one poll and the next started. A SIGTERM'd
worker on a hung peer stayed for many minutes (the maintainer had to kill -9). PR #37.
Test: `TestDnewslinkTermStalledPeer`.

### D-43 Connlife retires one rotation late
A connlife worker can only retire at a queue rotation and retired at the first one at or
after connlife; a replacement starts just after a rotation, so at the next multiple it is
a second short and serves a whole extra interval. Found by the maintainer. PR #30 (the
rotation nearest connlife), test #38 (`TestDnewslinkConnlifeNearest`).

### D-44 LISTGROUP reply and range
Found by the conformance sweep with nntp-testkit (RFC 3977 section 6.1.2). `LISTGROUP group`
answers `211 Article list follows` instead of `211 count low high group`
(`dreaderd/list.c:512`); a range (`LISTGROUP g 2-2`) is ignored and the whole group listed
(list.c:494-500); LISTGROUP with no group selected gives 481, an authentication code, instead
of 412 (list.c:520).

### D-45 HDR reply code and LIST HEADERS
`HDR` answers 221, XHDR's code, instead of 225 (`dreaderd/xover.c:205`), and `LIST HEADERS`,
which RFC 3977 section 8.6 requires with HDR, gives `501 Bad command use`
(`dreaderd/reader.c:1318`). #41 doesn't advertise HDR because of this.

### D-46 AUTHINFO reply codes
RFC 4643 section 2.3.2: a wrong password should get 481 and let the client retry; dreaderd
answers `502 Access denied to your node` (`dreaderd/main.c:1739`). AUTHINFO PASS before USER
gives 501 instead of 482 (nntp.c:104), and AUTHINFO after a successful login is accepted
again (381) instead of 502 (nntp.c:86).

### D-47 Other reader reply codes
RFC 3977: NEXT with no group selected gives 421 instead of 412 (`dreaderd/nntp.c:264`);
ARTICLE or STAT in an empty group 423 instead of 420 (nntp.c:423); `ARTICLE abc` or `-1` 430
instead of 501 (nntp.c:331-334); OVER past the high mark (or a range like 3-1) sends 224 and an
empty list instead of 423, because 224 goes out before the range is checked (xover.c:208);
LIST ACTIVE.TIMES gives 500 instead of 215 or 503 (list.c:364).

### D-48 Feeder reply codes
diablo answers MODE READER with 500 instead of 502 (a transit-only server, RFC 3977 section
5.3; `util/diablo.c:2534`), and IHAVE or CHECK without an argument with 435/438 instead of 501.
All 15 of nntp-testkit's RFC 4644/IHAVE checks pass otherwise.

### D-49 Moderation handling
The maintainer's list (agent-chatroom, 2026-10-26), confirmed live: any reader can post with
Approved: straight into a moderated group (no readerdef setting; `dreaderd/post.c:256`); a POST
to a status "n" group is accepted; a crosspost to two moderated groups mails both moderators
(RFC 5537: the first only); with no moderators match the post goes to diablo, which rejects it
as "NotInActive" (and only `incoming.log` has the reason); mail failures are ignored, and
without any sendmail the client still gets "240 Article mailed to moderator" while the article
is lost; the moderators address goes through snprintf as the format (post.c:957). Fixes in #42,
#43, #45 and #46; tests in #44.

### D-50 CodeQL robustness findings
From the triage of the CodeQL alerts (advanced setup, #48). None of these is reachable from
the network; the sources are configuration and the command line of non-setuid programs.
- `util/dspoolout.c:471` and `:689`: the feed label (dnewsfeeds, dnntpspool.ctl) becomes the
  format of `xopen()`. A `%` in a label opens the wrong file or crashes dspoolout. Fix:
  `xopen(..., "%s", spoolFile)`, and reject `%` in labels.
- `util/dspoolout.c:774` / `util/dnewslink.c:597`: the label goes unescaped into dnewslink's
  `-b` template (`label.S%05d`), and dnewslink expands it with an unbounded `sprintf` into a
  1024-byte buffer. Fix: escape `%` in the label and use `snprintf`.
- `lib/config.c:187`: the diablo.config path (`DIABLO_CONFIG_PATH`, `-C`) is the format of
  `xfopen()`, the same class as the path patterns #62 fixed. A `%` in that path gives a wrong
  path. Fix: the same substitution as #62's `PatSubst()`.
- `dreaderd/reader.c:1100`: the bytelimit log line prints a `double` with `%d`, so both logged
  numbers are wrong. Fix: `%.0f`.
- `dreaderd/reader.c:215`: the select error log passes two `fd_set` structures to `%x`, so the
  values are meaningless. Fix: log something else.
Of the 72 format-string alerts, 52 went away with #62; the rest are these and integer-width
mismatches that only shorten a printed number.

## Feature requests

### F-01 Cancel-Lock (RFC 8315)
Generate `Cancel-Lock` for local posts in dreaderd and verify `Cancel-Key` before executing a
cancel or supersede. The maintainer's main reason not to replace INN with diablo plus
dreaderd yet.

**Verification: done in #19.** `CtlCancel` (`dreaderd/control.c`) checks the Cancel-Key of a
cancel or supersede against the target's Cancel-Lock (`lib/canlock.c`; sha256, sha512 and
sha1). `readercanlock` in diablo.config:

- `require-auth` (default): executed only when the target has a Cancel-Lock and the Cancel-Key
  matches it; a target without a lock is not cancelled.
- `auth`: a target with a Cancel-Lock needs a matching key; a target without one is cancelled.
- `none`: no cancel or supersede is executed.
- `all`: no checks (the behaviour before #19).

A cancel that arrives before its target is dropped unless the mode is `all`. Trackonly mode
is refused with a Cancel-Lock check enabled. Tests: `TestCanLockVectors`, `TestCancelLock*`.
Upgrades: a reader that relied on unchecked cancels must set `readercanlock all`.

**Generation: open.** Adding `Cancel-Lock` (and `Cancel-Key` on a poster's own cancels) in
`NNPostBuffer` (`dreaderd/post.c`) with OpenSSL HMAC, byte-identical to INN's locks; then a
key tool. Waiting for the decision on sha1 (INN still generates it next to sha256) and on
secret storage.
