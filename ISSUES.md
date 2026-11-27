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

Last updated 2026-11-18: D-213 to D-290 added (open items from the reply-code work and the overview analysis, and go-while's deep review round 2 of `main` at 1c80d48f), their line numbers checked against `main` at 9dc51c18 (D-226 at 9aa7c5e3 and D-227 at 0dd6330e, before their fixes); D-226 and D-227 marked fixed (#416, #417); F-09: the `OverArt` slot is 32 bytes. Before that, 2026-11-05: F-07 added (dreaderd POST against RFC 5536/5537); D-174 added (empty overview Lines column; plan: the line count from diablo through the queue line); D-93, D-95, D-107, D-119 and D-126 marked fixed (#300); D-90, D-117, D-118 and D-122 marked fixed (#301). Before that, 2026-11-04: D-97, D-104, D-105, D-111, D-112, D-115 and D-121 marked fixed (#291); audit of the open items against main: D-70, D-77, D-88, D-102, D-106, D-108, D-109, D-113 and D-114 marked fixed (their PRs had not updated this file), D-100 marked fixed (#230), D-55 and D-101 noted as partly fixed, six index links and the F-02/F-06 status corrected; D-172 (retained control connections inherited) and D-173 (NETREMOTE accounting destination) added from go-while's review; D-164 to D-166 marked fixed (#280: readerdef typos, access cache reload); D-167 marked fixed (#169: one Perl interpreter per script); D-134 marked fixed (#257: dkp -t); D-168 and D-169 marked fixed (#185); D-170 added (illumos loses a hand-back at the descriptor limit); D-171 added (OpenBSD shared maps, won't fix). Before that, 2026-10-30: D-167 to D-169 added (Perl authentication, the file and cdb deny lists); F-06 (Perl authentication) added; F-02 noted in progress (#167). Before that, 2026-10-29: D-16 marked fixed (#127). D-135 to D-146 and D-156, D-157 marked fixed (#145, #151 to #154); D-158 won't fix; D-150 to D-155 marked fixed (#158); D-147 to D-149 marked fixed (#159); D-159 to D-162 marked fixed (#160); D-164 to D-166 added from go-while's review of #160. Before that, 2026-10-28: D-91, D-96, D-98 and D-131 to D-133 marked fixed (#146 to #149). F-01 marked done, F-04 (NoCeM) and F-05 (init scripts, done) added, D-163 (file modes, fixed in #139) added; D-135 to D-162 added from go-while's batch of `main` at `0401451`, and a note on D-94. Before that, D-110 to D-134 added from go-while's third review pass of `main` at `6a4b97d`. Before that, 2026-10-27: D-66 to D-109 added from the second review pass and the reviews of the fixes
(D-77 is documented behaviour and only needs a note); the fixed ones are marked with their PR. Before that, D-53 to D-65 added from the review of `main` at `ca648d4`. Before that, 2026-10-26, after batch 1 (#30, #31, #37 to #44, #47 to #49) and the first part
of batch 2 (#46, #50, #52, #62) were merged. Open: #35 (GnuPG pgpverify, held for a private
advisory), #45 with its tests in #56, #53 to #55, #57 to #61, #63 to #65.

| ID | Severity | Summary | Status |
|---|---|---|---|
| [D-01](#d-01-host-cache-never-rebuilt-after-a-start-without-dhostscache) | Medium | Host cache never rebuilt after a start without `dhosts.cache` | fixed (#10) |
| [D-02](#d-02-privilege-drop-results-not-checked) | Medium | `setgroups`/`setgid`/`setuid` results not checked | fixed (#6) |
| [D-03](#d-03-non-root-start-refused-after-d-02) | High | A non-root start refused after the first D-02 fix | fixed (#6, 5a6e877) |
| [D-04](#d-04-write-through-a-null-pointer-in-feedwrite) | High | Write through a NULL pointer in `FeedWrite()` | fixed (#8) |
| [D-05](#d-05-const-strchr-warnings-with-glibc-243-or-later) | Low | `const strchr()` warnings with glibc 2.43 or later | fixed (#8) |
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
| [D-16](#d-16-linux-build-fails-with-the-default-libvendorh) | Medium | Linux build fails with the default `lib/vendor.h` (RADIUS) | fixed (#127) |
| [D-17](#d-17-unchecked-results-under-_fortify_source) | Low | 80 `-Wunused-result` warnings under `_FORTIFY_SOURCE` | fixed (#71) |
| [D-18](#d-18-debian-gnuhurd-build) | Low | Debian GNU/Hurd build: `PATH_MAX`, `setpgrp`, `db_185.h` | fixed (#335) |
| [D-19](#d-19-changelog-links-a-missing-document) | Low | CHANGELOG links the missing `BUILD-PORTABLE-MAKE.md` | fixed (#61) |
| [D-20](#d-20-strncpy-truncation-warning-in-dreaderddnsc) | Low | `strncpy` truncation warning in `dreaderd/dns.c` (GCC 16) | fixed (#65) |
| [D-21](#d-21-dactivekp-write-errors-now-exit-the-process) | — | dactive.kp write errors now exit the process (#7) | won't fix (kept on purpose) |
| [D-22](#d-22-gnu-make-install-ignores-prefix-and-destdir) | High | GNU Make `install`/`uninstall` ignore `PREFIX` and `DESTDIR` (live `/news` used) | fixed (#16) |
| [D-23](#d-23-feederreader-sample-config-is-ignored) | Medium | `samples/feeder+reader/diablo.config` has no `version 2.0`: every directive ignored | fixed (#26) |
| [D-24](#d-24-sample-dreaderaccess-allows-10008-not-localhost) | Medium | Sample `dreader.access` allows 1.0.0.0/8, not localhost | fixed (#55) |
| [D-25](#d-25-installed-config-files-are-read-only-symlinks-into-samples) | Medium | Installed config files are read-only symlinks into `samples/`; edits are lost on reinstall | fixed (#21) |
| [D-26](#d-26-iter-written-as-decimal-read-as-hex) | Medium | Overview `ITER` written as decimal, read as hex by `dexpireover` | fixed (#54) |
| [D-27](#d-27-documentation-is-stale-or-wrong) | Medium | INSTALL, README.md, README.READER: xmake, missing Linux steps, news user, PGP | fixed (#80) |
| [D-28](#d-28-man-pages) | Low | Man pages: none fully accurate, 17 programs without one, broken SEE ALSO | fixed (#81) |
| [D-29](#d-29-dreaderd--sdigits-sets-a-mangled-receive-buffer) | Low | `dreaderd -S<digits>` also sets a mangled receive buffer | fixed (#58) |
| [D-30](#d-30-diablo--x-sets-the-xref-host-not-xrefsync) | Low | `diablo -x` sets the Xref host, not xrefsync | fixed (#59) |
| [D-31](#d-31-option-parsing-bugs-in-utilities) | Low | Option parsing bugs in utilities (`-C file`, `dilookup -f`, `dhisexpire -o`, ...) | fixed (#64, #70) |
| [D-32](#d-32-drcmd-exits-1-after-a-good-status) | Low | `drcmd` exits 1 after a good `status` reply | fixed (#57) |
| [D-33](#d-33-dreaderd--b-host-without-port-binds-a-random-port) | Low | `dreaderd -B host` without a port binds a random port; `-P`/`-B` drop the config bind settings | fixed (#60) |
| [D-34](#d-34-installed-adm-scripts-are-not-executable) | Low | Installed adm scripts are mode 444; no `/news/adm`; csh needed | fixed (#21, #22) |
| [D-35](#d-35-samples-with-wrong-directives-or-options) | Low | Samples with wrong directives or options (dexpire.ctl, diablo.config, crontab, dcontrol.ctl) | fixed (#22, #79) |
| [D-36](#d-36-dgrpctl-checkgroups-exec) | Low | `dgrpctl checkgroups exec`: new groups without S/GD, lock order | fixed (#74) |
| [D-37](#d-37-articles-lost-when-the-spool-directory-is-removed) | High | Articles accepted (235) but lost when their spool directory is removed while diablo writes into it | fixed (#24) |
| [D-38](#d-38-dexpire-removes-the-directory-diablo-writes-into) | High | `dexpire` removes a spool's newest directory, the one diablo writes into | fixed (#25) |
| [D-39](#d-39-dreaderd-spool-server-address-may-be-udp) | Medium | dreaderd connects its spool servers with the first `getaddrinfo()` result, which is UDP on FreeBSD | fixed (#27) |
| [D-40](#d-40-feeder-children-hang-on-sighup) | High | Feeder children hang forever on the SIGHUP of `dicmd exit`/`pause` | fixed (#32) |
| [D-41](#d-41-diablo-ctl-stops-other-sites-daemons) | High | diablo-ctl stops the daemons of another site of the same user | fixed (#31) |
| [D-42](#d-42-dnewslink-ignores-sigterm-on-a-stalled-peer) | Medium | dnewslink ignores SIGTERM for up to `-T` (600 s) per wait while a peer is stalled | fixed (#37) |
| [D-43](#d-43-connlife-retires-one-rotation-late) | Low | A connlife worker retires one rotation late (connlife 60 gives about 120 s) | fixed (#30, test #38) |
| [D-44](#d-44-listgroup-reply-and-range) | Medium | dreaderd LISTGROUP: wrong 211 line, range ignored, 481 without a group | fixed (#67) |
| [D-45](#d-45-hdr-reply-code-and-list-headers) | Medium | dreaderd HDR answers 221 (XHDR's code); LIST HEADERS missing | fixed (#67; HDR stays unadvertised, see below) |
| [D-46](#d-46-authinfo-reply-codes) | Medium | dreaderd AUTHINFO: a wrong password gets 502 (no retry); PASS before USER, and a second login | fixed (#67; retry needs a reconnect, see below) |
| [D-47](#d-47-other-reader-reply-codes) | Low | dreaderd reply codes: NEXT, ARTICLE in an empty group, bad article numbers, OVER past the high mark, LIST ACTIVE.TIMES | fixed (#67) |
| [D-48](#d-48-feeder-reply-codes) | Low | diablo: MODE READER 500, IHAVE/CHECK without an argument | fixed (#67) |
| [D-49](#d-49-moderation-handling) | Medium | Moderation: Approved: from any reader, posts to "n" groups, crosspost mails every moderator, failures not reported | fixed (#42, #43, #45 via #56, #46, #50) |
| [D-50](#d-50-codeql-robustness-findings) | Low | CodeQL: a `%` in a feed label or in the diablo.config path is used as a format; two log lines with wrong format types | fixed (#72) |
| [D-51](#d-51-logins-beyond-readerdns-are-turned-away-not-queued) | Medium | Logins beyond `readerdns` get 400 "Server too busy" instead of waiting for a worker | fixed (#83) |
| [D-53](#d-53-checkgroups-creates-half-formed-groups) | Low | checkgroups control message creates half-formed groups; `GROUP` answers `211 1 -1 -1` | fixed (#105) |
| [D-54](#d-54-transient-list-active-gap-after-a-record-is-rewritten) | Low | First LIST ACTIVE after a control message rewrote a group record omits that group | fixed (#107) |
| [D-55](#d-55-dreaderd-small-items) | Low | dreaderd small items: descriptor leaks on `!` deny files, OVER range clamp | fixed (#298) |
| [D-56](#d-56-feedermaxheadersize-is-never-enforced) | Medium | `feedermaxheadersize` is never enforced | fixed (#102) |
| [D-57](#d-57-dhisexpire-leaves-the-history-dead-when-it-fails) | Medium | `dhisexpire` leaves the live history marked dead when it fails after the mark | fixed (#95) |
| [D-58](#d-58-dnewsfeeds-directive-without-an-argument-crashes) | Medium | dnewsfeeds directive without its argument crashes every program that loads the file | fixed (#94) |
| [D-59](#d-59-per-label-maxconnect-allows-one-too-many) | Low | Per-label `maxconnect N` lets N+1 connections in | fixed (#96) |
| [D-60](#d-60-host-cache-end-does-not-end-a-label) | Low | Host cache: `end` does not end a label; a stray `inhost` joins the previous one | fixed (#97) |
| [D-61](#d-61-diablohosts-cannot-hold-an-ipv6-address) | Low | `diablo.hosts` cannot hold an IPv6 address | fixed (#97) |
| [D-62](#d-62-stat-uses-a-128-byte-path-buffer) | Low | `STAT` uses a 128-byte path buffer and answers 430 for existing articles | fixed (#98) |
| [D-65](#d-65-dnewsfeeds-label-name-overflows-the-host-cache-buffer) | Low | A label name over 254 characters overflows a buffer when the host cache is built | fixed (#104) |
| [D-63](#d-63-dnewslink-refile-loses-flags) | Medium | `dnewslink` refiles pending streams without their H and C flags and writes one entry twice | fixed (#103) |
| [D-64](#d-64-weighted-spool-allocation-divides-by-zero) | Low | Weighted spool allocation divides by zero with weight 0 | fixed (#99) |
| [D-66](#d-66-dnewslink-loses-431-deferred-entries) | Medium | `dnewslink` loses entries the peer deferred with 431 when the session ends early | fixed (#111) |
| [D-67](#d-67-dcancel--s-does-not-update-the-history) | Medium | `dcancel -s` rewrites the spool file but not the history | fixed (#112) |
| [D-68](#d-68-dnewslink-with-a-small-maxstream-sends-one-window) | Low | `dnewslink` with `maxstream` 2 to 5 sends one window and hands the rest back | fixed (#117) |
| [D-69](#d-69-dnewslink-spins-on-a-compression-level-zlib-rejects) | Low | `dnewslink` spins when zlib rejects the compression level | fixed (#118) |
| [D-70](#d-70-dgrpctl-checkgroups-reads-long-lines-as-two) | Medium | `dgrpctl checkgroups`: lines of 128 bytes or more become two lines; an empty list crashes | fixed (#172) |
| [D-72](#d-72-dnewslink-deletes-the-queue-after-an-overlong-line) | Low | `dnewslink` removes the whole queue file after a queue line of 4 to 8 KB without a newline | fixed (#113) |
| [D-73](#d-73-dnewslink-queue-fields-leak-into-the-next-line) | Low | `dnewslink`: `extractFeedLine` keeps fields of the previous line; `readretry` drops H and C flags | fixed (#114) |
| [D-74](#d-74-dnewslink-queue-rewrite-write-errors-are-unchecked) | Low | `dnewslink` does not check the writes of the rewritten queue file before the rename | fixed (#113) |
| [D-76](#d-76-dexpire-removes-a-directory-younger-than-keeptime) | Medium | `dexpire` removes a spool directory younger than its keep time | fixed (#109) |
| [D-77](#d-77-dhisexpire-without--a-leaves-the-history-dead) | Low | `dhisexpire` without `-a` leaves the history marked dead without saying so | fixed (#134) |
| [D-78](#d-78-dicmd-config-internalfilter-disables-the-spam-filter) | Medium | `dicmd config internalfilter` silently disables the internal spam filter | fixed (#124) |
| [D-79](#d-79-kpdb-blank-or-short-line) | Low | A blank or very short line in a KP database corrupts the sort and overflows the lookup stack | fixed (#123) |
| [D-80](#d-80-kpdb-torn-tail-loses-appends) | Medium | A KP database with a torn or NUL tail loses the first append | fixed (#110) |
| [D-81](#d-81-the-sample-internalfilter-line-turns-the-filter-off) | Low | `samples/diablo.config` shows an `internalfilter` option (`t`) that does not exist | fixed (#121) |
| [D-82](#d-82-long-config-lines-are-split) | Low | A long line in `diablo.config`, `dspool.ctl` or `dexpire.ctl` is read as two lines | fixed (#125) |
| [D-83](#d-83-indented-dexpirectl-line-is-ignored) | Low | An indented line in `dexpire.ctl` matches nothing | fixed (#119) |
| [D-85](#d-85-dhisexpire-treats-a-read-error-as-end-of-file) | Low | `dhisexpire` treats a read error as end of file | fixed (#122) |
| [D-86](#d-86-spool-number-off-by-one) | Low | Off-by-one in the spool number checks reads `SpoolObjectMap[100]` | fixed (#120) |
| [D-87](#d-87-numeric-ipv6-host-in-diablohosts-dropped-on-a-machine-without-ipv6) | Low | `getaddrinfo()` with NULL hints in `lib/hostauth.c` drops a numeric IPv6 `diablo.hosts` entry on glibc 2.39 without a global IPv6 address | fixed (#115) |
| [D-88](#d-88-dnewslink--p-overwrites-leftover-lines-of-an-earlier-run) | Low | `dnewslink -p`: a later pipe run overwrites the leftover lines an earlier run wrote to the batch file | fixed (#256) |
| [D-89](#d-89-ai_addrconfig-still-passed-for-listen-bind-and-connect-addresses) | Low | `dreaderd` listen, bind and connect and `dsyncgroups` still pass `AI_ADDRCONFIG` | fixed (#306) |
| [D-90](#d-90-kpdb-sort-writes-the-offsets-one-record-at-a-time) | Low | `KPDBSort()` does one 8-byte write per record; on a full ZFS dataset a sort of 259 records took about 51 s | fixed (#301) |
| [D-91](#d-91-takethis-without-an-argument-answers-439-instead-of-501) | Low | TAKETHIS without an argument answers 439 instead of 501 | fixed (#146) |
| [D-92](#d-92-header-only-feed-reports-unknownreject-for-laerr_nobytes) | Low | Header-only feed reports UnknownReject for LAERR_NOBYTES | fixed (#306) |
| [D-93](#d-93-kpdbgetfield-key-scan-is-not-bounded-by-the-record-length) | Low | KPDBGetField key scan is not bounded by the record length | fixed (#300) |
| [D-94](#d-94-dcontrolctl-dnewsfeeds-and-dserverhosts-split-long-lines) | Low | dcontrol.ctl, dnewsfeeds and dserver.hosts split long lines | fixed (#306) |
| [D-95](#d-95-maxinboundrate-value-is-read-past-its-end) | Low | MaxInboundRate value ending in ';' or in an hour number is read past its end | fixed (#300) |
| [D-96](#d-96-spamfilter-s0-turns-the-filter-off-and-use_spam_rw_map-0-does-not-compile) | Low | spamfilter: s0 turns the filter off, and USE_SPAM_RW_MAP 0 does not compile | fixed (#147) |
| [D-97](#d-97-diab-filter-writes-one-byte-past-its-reply-buffer-writeall-loops-on-a-zero-write) | Low | diab-filter writes one byte past its reply buffer; writeAll loops on a zero write | fixed (#291) |
| [D-98](#d-98-ctype-calls-on-plain-char) | Low | ctype calls on plain char | fixed (#148) |
| [D-100](#d-100-cachefile-builds-the-path-without-bounds) | Low | cacheFile builds the path without bounds | fixed (#230) |
| [D-101](#d-101-spool-server-replies-have-no-line-cap-other-small-items-in-dreaderdserverc) | Low | Spool server replies have no line cap; other small items in dreaderd/server.c | fixed (#298) |
| [D-102](#d-102-readaccesscache-leaks-descriptors-when-a-cache-open-fails) | Low | ReadAccessCache leaks descriptors when a cache open fails | fixed (#160) |
| [D-103](#d-103-access-cache-reload-leaks-the-old-groupdef-and-grouplist-nodes) | Low | Access cache reload leaks the old GroupDef and GroupList nodes | fixed (#306) |
| [D-104](#d-104-dhisexpire-uses-the-history-record-size-without-a-check) | Low | dhisexpire uses the history record size without a check | fixed (#291) |
| [D-105](#d-105-dexpire--o-uses-a-null-file-after-a-failed-fopen) | Low | dexpire -o uses a NULL FILE after a failed fopen | fixed (#291) |
| [D-106](#d-106-dexpireover-getfirstartage-trusts-the-over-file-header) | Low | dexpireover getFirstArtAge trusts the over file header | fixed (#162) |
| [D-107](#d-107-metaspool-with-more-than-100-spool-lines-or-dontstore-with-no-spools) | Low | metaspool with more than 100 spool lines, or dontstore with no spools | fixed (#300) |
| [D-108](#d-108-dcancel-stores-the-history-offsets-before-the-rewritten-spool-file-is-in-place) | Medium | dcancel stores the history offsets before the rewritten spool file is in place | fixed (#170) |
| [D-109](#d-109-dsyncgroups-use-after-free-of-the-address-lists-unchecked-strtok-results) | Medium | dsyncgroups: use after free of the address lists, unchecked strtok results | fixed (#171) |
| [D-110](#d-110-dcontrolctl-ct_verify-leaks-or-keeps-a-stale-value-when-two-rules-match) | Low | dcontrol.ctl: ct_Verify leaks or keeps a stale value when two rules match | fixed (#306) |
| [D-111](#d-111-pgpverify-does-not-check-fdopen-and-realloc-results) | Low | pgpverify does not check fdopen() and realloc() results | fixed (#291) |
| [D-112](#d-112-strcpy-into-sun_path-in-the-filter-socket-code) | Low | strcpy() into sun_path in the filter socket code | fixed (#291) |
| [D-113](#d-113-cached-head-and-body-of-an-article-with-an-lf-only-headerbody-separator) | Medium | Cached HEAD and BODY of an article with an LF-only header/body separator | fixed (#173) |
| [D-114](#d-114-egets-loses-the-prefix-of-an-overlong-line) | Medium | egets() loses the prefix of an overlong line | fixed (#166) |
| [D-115](#d-115-dnewslink-flushcommandbuffer-can-spin-on-a-zero-write) | Low | dnewslink flushCommandBuffer() can spin on a zero write | fixed (#291) |
| [D-116](#d-116-dnewslink-writelarge-reports-a-write-timeout-as-success) | Low | dnewslink writeLarge() reports a write timeout as success | fixed (#306) |
| [D-117](#d-117-kpdbsort-is-not-synchronized-with-readers) | Medium | KPDBSort() is not synchronized with readers | fixed (#301) |
| [D-118](#d-118-kpdbwrite-with-a-null-token-the-documented-behaviour-does-not-exist) | Low | KPDBWrite() with a NULL token: the documented behaviour does not exist | fixed (#301) |
| [D-119](#d-119-articlefilename-builds-a-relative-path-for-an-unconfigured-spool) | Low | ArticleFileName builds a relative path for an unconfigured spool | fixed (#300) |
| [D-120](#d-120-unknown-options-inside-spool-and-metaspool-blocks-are-only-logged) | Low | Unknown options inside spool and metaspool blocks are only logged | fixed (#306) |
| [D-121](#d-121-getspoolfrompath-is-dead-code-with-a-stray-printf-and-a-prefix-match) | Low | GetSpoolFromPath is dead code with a stray printf and a prefix match | fixed (#291) |
| [D-122](#d-122-kp-files-above-2-gb-wrap-the-int-offsets) | Low | KP files above 2 GB wrap the int offsets | fixed (#301) |
| [D-123](#d-123-authinfo-user-accepted-again-after-a-passwordless-authentication) | Low | AUTHINFO USER accepted again after a passwordless authentication | fixed (#297) |
| [D-124](#d-124-capabilities-with-an-argument-answers-101) | Low | CAPABILITIES with an argument answers 101 | fixed (#297) |
| [D-125](#d-125-reader-only-command-on-a-transit-connection-answers-500-not-502) | Low | Reader-only command on a transit connection answers 500, not 502 | fixed (#297) |
| [D-126](#d-126-strtoll-overflow-into-artno_input) | Low | strtoll() overflow into artno_input() | fixed (#300) |
| [D-127](#d-127-dicmd-splits-lines-longer-than-255-bytes) | Low | dicmd splits lines longer than 255 bytes | fixed (#306) |
| [D-128](#d-128-dkp-parses-the-second-piece-of-a-long-input-line-as-a-new-record) | Low | dkp parses the second piece of a long input line as a new record | fixed (#288) |
| [D-129](#d-129-dclient-is-ipv4-only-gethostbyname) | Low | dclient is IPv4-only (gethostbyname) | fixed (#298) |
| [D-130](#d-130-newnews-and-newgroups-ignore-the-distributions-argument) | Low | NEWNEWS and NEWGROUPS ignore the distributions argument | fixed (#334) |
| [D-131](#d-131-dclient-does-not-know-the-202-reply-of-list-extensions) | Low | dclient does not know the 202 reply of LIST EXTENSIONS | fixed (#149) |
| [D-132](#d-132-config-dump-prints-the-bare-cr-option-under-the-nul-name) | Low | Config dump prints the bare-CR option under the NUL name | fixed (#149) |
| [D-133](#d-133-list-moderators-adds-an-extra-cr-to-a-crlf-line) | Low | LIST MODERATORS adds an extra CR to a CRLF line | fixed (#149) |
| [D-134](#d-134-dkp-trim-mode-keeps-the-tail-of-a-long-deleted-line) | Low | dkp trim mode keeps the tail of a long deleted line | fixed (#257) |
| [D-135](#d-135-maintenance-scripts-hide-failed-jobs-behind-their-end-message) | Low | Maintenance scripts hide failed jobs behind their END message | fixed (#152) |
| [D-136](#d-136-rot-accepts-an-invalid-retention-count-and-still-rotates-the-current-file) | Low | rot accepts an invalid retention count and still rotates the current file | fixed (#152) |
| [D-137](#d-137-biweeklyatrim-does-not-return-a-failed-active-file-trim) | Low | biweekly.atrim does not return a failed active-file trim | fixed (#152) |
| [D-138](#d-138-daily-rotation-writes-its-completion-message-into-dailylog0) | Low | Daily rotation writes its completion message into daily.log.0 | fixed (#152) |
| [D-139](#d-139-auto-loaded-diablo-ctlconf-can-mix-two-installations-paths) | Low | Auto-loaded diablo-ctl.conf can mix two installations' paths | fixed (#145, #151) |
| [D-140](#d-140-unknown-diablo_daemons-entries-reuse-command-state-or-abort-the-wrapper) | Low | Unknown DIABLO_DAEMONS entries reuse command state or abort the wrapper | fixed (#145, #151) |
| [D-141](#d-141-nonnumeric-diablo-ctl-wait-settings-disable-the-timeout) | Low | Nonnumeric diablo-ctl wait settings disable the timeout | fixed (#145, #151) |
| [D-142](#d-142-reinstall-resets-existing-operational-directory-modes-and-owners) | Low | Reinstall resets existing operational directory modes and owners | fixed (#153) |
| [D-143](#d-143-direct-installation-from-an-unbuilt-checkout-can-succeed-without-binaries) | Low | Direct installation from an unbuilt checkout can succeed without binaries | fixed (#153) |
| [D-144](#d-144-installer-pipelines-hide-failed-source-tree-scans) | Low | Installer pipelines hide failed source-tree scans | fixed (#153) |
| [D-145](#d-145-build-on-host-requires-the-optional-pretty_name-field) | Low | build-on-host requires the optional PRETTY_NAME field | fixed (#153) |
| [D-146](#d-146-daemon-option-strings-undergo-pathname-expansion) | Low | Daemon option strings undergo pathname expansion | fixed (#145, #151) |
| [D-147](#d-147-over-and-hdr-do-not-reject-an-invalid-converted-range-bound) | Low | OVER and HDR do not reject an invalid converted range bound | fixed (#159) |
| [D-148](#d-148-listgroup-accepts-a-failed-article-number-conversion) | Low | LISTGROUP accepts a failed article-number conversion | fixed (#159) |
| [D-149](#d-149-newnews-clears-the-previously-selected-group) | Low | NEWNEWS clears the previously selected group | fixed (#159) |
| [D-150](#d-150-doverctl-convert-leaves-compressed-overview-data-in-the-old-layout) | Medium | doverctl convert leaves compressed overview data in the old layout | fixed (#158) |
| [D-151](#d-151-doverctl-convert--m-ignores-failed-file-moves) | Medium | doverctl convert -m ignores failed file moves | fixed (#158) |
| [D-152](#d-152-doverctl-clean-removes-nonregular-files-using-the-wrong-path) | Low | doverctl clean removes nonregular files using the wrong path | fixed (#158) |
| [D-153](#d-153-doverctl-clean-keeps-misaligned-overview-data-after-an-entry-size-change) | Low | doverctl clean keeps misaligned overview data after an entry-size change | fixed (#158) |
| [D-154](#d-154-dexpireovers-parallel-parent-spins-after-a-failed-fork) | Medium | dexpireover's parallel parent spins after a failed fork | fixed (#158) |
| [D-155](#d-155-dexpireover-counts-failed-compression-as-completed) | Low | dexpireover counts failed compression as completed | fixed (#158) |
| [D-156](#d-156-a-failed-free-space-query-can-drive-excessive-spool-expiry) | Medium | A failed free-space query can drive excessive spool expiry | fixed (#154) |
| [D-157](#d-157-failed-spool-removal-can-still-expire-the-directorys-history-entries) | Medium | Failed spool removal can still expire the directory's history entries | fixed (#154) |
| [D-158](#d-158-empty-spool-home-shorthand-makes-dexpire-use-the-wrong-partition-path) | Medium | Empty spool-home shorthand makes dexpire use the wrong partition path | won't fix (not reachable) |
| [D-159](#d-159-long-whitespace-before-a-config-comment-can-reject-an-otherwise-valid-directive) | Low | Long whitespace before a config comment can reject an otherwise valid directive | fixed (#160) |
| [D-160](#d-160-access-cache-loading-treats-a-valid-descriptor-zero-as-an-open-failure) | Low | Access-cache loading treats a valid descriptor zero as an open failure | fixed (#160) |
| [D-161](#d-161-several-readerdef-numeric-settings-silently-accept-malformed-values) | Low | Several readerdef numeric settings silently accept malformed values | fixed (#160) |
| [D-162](#d-162-dserverhosts-numeric-options-lack-complete-conversion-validation) | Low | dserver.hosts numeric options lack complete conversion validation | fixed (#160) |
| [D-163](#d-163-cache-and-state-files-were-created-with-the-process-umask) | Medium | Cache and state files were created with the process umask | fixed (#139) |
| [D-164](#d-164-a-misspelled-group-list-name-in-a-readerdef-falls-back-to-the-first-list) | Low | A misspelled `groups`/`listgroups`/`postgroups` name in a readerdef falls back to the first group list | fixed (#280) |
| [D-165](#d-165-dreaderd-exits-when-one-access-cache-file-cannot-be-opened) | Low | dreaderd exits when one access-cache file cannot be opened | fixed (#280) |
| [D-166](#d-166-a-misspelled-readerdef-option-is-logged-but-the-file-is-still-installed) | Low | A misspelled readerdef option is logged but the file is still installed | fixed (#280) |
| [D-167](#d-167-perl-authentication-uses-only-the-first-script-a-worker-loads) | Low | Perl authentication uses only the first script a worker loads | fixed (#169) |
| [D-168](#d-168-a-file--deny-list-leaks-a-file-handle-for-every-listed-user-that-logs-in) | Low | A `file !` deny list leaks a file handle for every listed user that logs in | fixed (#185) |
| [D-169](#d-169-the-cdb--deny-list-does-not-work-and-rejects-every-user) | Low | The `cdb !` deny list does not work and rejects every user | fixed (#185) |
| [D-170](#d-170-on-illumos-a-hand-back-lost-at-the-descriptor-limit-stays-counted) | Low | On illumos a hand-back lost at the descriptor limit stays counted (Act) | fixed (#336) |
| [D-171](#d-171-on-openbsd-readers-do-not-see-writes-through-their-shared-maps) | Medium (OpenBSD only) | On OpenBSD readers do not see writes through their shared maps | won't fix (design) |
| [D-172](#d-172-feed-children-and-programs-diablo-runs-inherit-retained-control-connections) | Low | Feed children and programs diablo runs inherit retained control connections | fixed (#294) |
| [D-173](#d-173-netremote-session-accounting-goes-to-a-placeholder-host) | Low | NETREMOTE session accounting goes to a placeholder host | fixed (#309) |
| [D-174](#d-174-the-reader-overview-lines-column-is-empty-for-articles-without-a-lines-header) | Low | The reader overview Lines column is empty for articles without a Lines header | fixed (#312) |
| [D-175](#d-175-a-bad-dspoolctl-on-reload-stops-a-running-diablo) | Medium | A bad dspool.ctl on reload stops a running diablo | fixed (#308) |
| [D-176](#d-176-gz-header-parse-of-an-overview-data-file-is-not-bounded-by-the-file-size) | Low | .gz header parse of an overview data file is not bounded by the file size | fixed (#296) |
| [D-177](#d-177-dreaderaccess-line-longer-than-the-read-buffer-is-parsed-in-pieces) | Low | dreader.access line longer than the read buffer is parsed in pieces | fixed (#296) |
| [D-178](#d-178-dsyncgroups-cuts-a-long-authinfo-user-name-or-password) | Low | dsyncgroups cuts a long AUTHINFO user name or password | fixed (#296) |
| [D-179](#d-179-netbsd-embedded-perl-loads-no-script) | Low | NetBSD: embedded Perl loads no script | fixed (#307) |
| [D-180](#d-180-netbsd-reader-fork-and-queue-wait-tests-are-intermittent) | Low | NetBSD: reader fork and queue-wait tests are intermittent | fixed (#303) |
| [D-181](#d-181-readerdns-and-readerfeedforks-take-any-value) | Low | readerdns and readerfeedforks take any value | fixed (#305) |
| [D-182](#d-182-kpdb-lookup-misses-wait-for-the-append-lock-after-the-resort-limit) | Low | KPDB lookup misses wait for the append lock after the resort limit | fixed (#311) |
| [D-183](#d-183-history-add-after-a-partial-last-record-skews-every-later-index) | Medium | History add after a partial last record skews every later index | fixed (#314) |
| [D-184](#d-184-a-failed-read-on-a-history-chain-counts-as-found) | Medium | A failed read on a history chain counts as "found" | fixed (#314) |
| [D-185](#d-185-a-failed-history-add-is-answered-as-a-permanent-reject) | Medium | A failed history add is answered as a permanent reject | fixed (#314) |
| [D-186](#d-186-a-refused-dspoolctl-reload-is-not-tried-again) | Low | A refused dspool.ctl reload is not tried again | fixed (#318) |
| [D-187](#d-187-list-file-lines-get-rrn-at-the-read-buffer-edge) | Low | LIST file lines get "\r\r\n" at the read buffer edge | fixed (#318) |
| [D-188](#d-188-diablo-sleeps-2-s-at-every-shutdown) | Low | diablo sleeps 2 s at every shutdown | fixed (#316) |
| [D-189](#d-189-dexpire-writes-expiry-flags-into-a-swapped-history) | Low | dexpire writes expiry flags into a swapped history | fixed (#321) |
| [D-190](#d-190-hiscachesize-is-not-checked-to-be-a-power-of-two) | Low | hiscachesize is not checked to be a power of two | fixed (#321) |
| [D-191](#d-191-historyopen-does-not-check-the-headers-entry-and-header-sizes) | Low | HistoryOpen does not check the header's entry and header sizes | fixed (#321) |
| [D-192](#d-192-precommit-delcomm-evicts-another-message-ids-entry) | Low | precommit PC_DELCOMM evicts another message-id's entry | fixed (#321) |
| [D-193](#d-193-xhdr-and-xover-by-message-id-answer-an-empty-list) | Low | XHDR and XOVER by message-id answer an empty list | fixed (#323) |
| [D-194](#d-194-log-messages-ending-in-a-newline-leave-a-blank-line) | Low | Log messages ending in a newline leave a blank line | fixed (#325) |
| [D-195](#d-195-readerdef-groups-does-not-limit-access-by-message-id) | Medium | readerdef `groups` does not limit access by message-id | fixed (#333) |
| [D-196](#d-196-body-by-message-id-answers-the-current-article-number) | Low | BODY by message-id answers the current article number | fixed (#333) |
| [D-197](#d-197-a-vserverdef-without-hostname-serves-an-empty-host-name) | Low | A vserverdef without hostname serves an empty host name | fixed (#338) |
| [D-198](#d-198-an-empty-first-line-on-a-haproxy-readerdef-reaches-strcmp-with-null) | Medium | An empty first line on a haproxy readerdef reaches strcmp() with NULL | fixed (#341) |
| [D-199](#d-199-a-long-welcome-text-writes-past-the-greeting-buffer) | Low | A long welcome text writes past the greeting buffer | fixed (#341) |
| [D-200](#d-200-checkgroups-creates-and-removes-groups-the-newgroup-and-rmgroup-lines-do-not-allow) | Medium | checkgroups creates and removes groups the newgroup and rmgroup lines do not allow | fixed (#342) |
| [D-201](#d-201-decrypt-aessh-can-take-a-wrong-cryptpw) | Low | decrypt-aes.sh can take a wrong cryptpw | fixed (#344) |
| [D-202](#d-202-the-first-ldap-login-after-a-start-spends-its-deadline-on-a-host-name-lookup) | Low | The first LDAP login after a start spends its deadline on a host name lookup | fixed (#343) |
| [D-203](#d-203-readercanlock-none-looks-up-every-cancel-target-before-refusing-it) | Low | readercanlock none looks up every cancel target before refusing it | fixed (#349) |
| [D-204](#d-204-historyadd-wraps-the-record-number-past-232---1-records) | Low | HistoryAdd wraps the record number past 2^32 - 1 records | fixed (#350) |
| [D-205](#d-205-a-supersede-is-logged-as-a-cancel) | Low | A supersede is logged as a cancel | fixed (#351) |
| [D-206](#d-206-white-space-between-two-newsgroups-names-hides-the-names-after-it) | Low | White space between two Newsgroups names hides the names after it | fixed (#358) |
| [D-207](#d-207-post-refuses-white-space-after-a-comma-in-newsgroups) | Low | POST refuses white space after a comma in Newsgroups | fixed |
| [D-208](#d-208-the-feeder-reads-only-the-first-line-of-a-folded-newsgroups-header) | Medium | The feeder reads only the first line of a folded Newsgroups header | fixed |
| [D-209](#d-209-the-feeder-reads-only-the-first-piece-of-a-newsgroups-line-longer-than-its-buffer) | Low | The feeder reads only the first piece of a Newsgroups line longer than its buffer | fixed (#442) |
| [D-210](#d-210-the-crc-history-hash-loses-a-bit-so-neighbouring-message-ids-collide) | Medium | The crc history hash loses a bit, so neighbouring Message-IDs collide | fixed (`msgidhash siphash`) |
| [D-211](#d-211-the-reader-takes-a-message-id-hash-match-for-the-article) | Medium | The reader takes a Message-ID hash match for the article | partly fixed (#464: the reader cache; the overview lookups: this change; the spool lookups and the pre-cancel cache are open) |
| [D-212](#d-212-a-busy-group-in-a-large-readerovcache-stays-locked-until-the-feed-only-flush) | Low | A busy group in a large `readerovcache` stays locked until the feed-only flush: dexpireover skips it or waits | open |
| [D-213](#d-213-dnewslink-waits-for-the-read-timeout-after-a-stream-reply-that-matches-no-pending-article) | Low | dnewslink waits for the read timeout after a stream reply that matches no pending article | fixed (#498) |
| [D-214](#d-214-dnewslink-drops-an-article-the-peer-answers-431-after-takethis-while-the-deferred-articles-are-sent) | Low | dnewslink drops an article the peer answers 431 after TAKETHIS while the deferred articles are sent | partly fixed (#498: batch feeds; a realtime feed (`-r`) still rejects) |
| [D-215](#d-215-a-deferred-article-that-is-pending-at-a-lost-connection-can-be-deferred-again) | Low | A deferred article that is pending at a lost connection can be deferred again | fixed (#498) |
| [D-216](#d-216-a-431-reject-after-takethis-adds-no-bytes-to-the-rejected-bytes) | Low | A 431 reject after TAKETHIS adds no bytes to the rejected bytes | fixed (#468) |
| [D-217](#d-217-dclient-reads-the-rest-of-a-long-reply-line-as-the-next-reply) | Low | dclient reads the rest of a long reply line as the next reply | open |
| [D-218](#d-218-dclient-reads-one-byte-before-its-buffer-for-a-blank-line-in-the-dump-file) | Low | dclient reads one byte before its buffer for a blank line in the dump file | fixed (#459) |
| [D-219](#d-219-a-filter-restart-can-stop-the-diablo-master-for-up-to-10-s) | Low | A filter restart can stop the diablo master for up to 10 s | open |
| [D-220](#d-220-diablo-waits-without-a-time-limit-for-the-answer-of-the-external-filter) | Low | diablo waits without a time limit for the answer of the external filter | open |
| [D-221](#d-221-dgrpctl-masscancel-misses-articles-with-numbers-above-31-bits) | Low | dgrpctl masscancel misses articles with numbers above 31 bits | open |
| [D-222](#d-222-a-write-error-in-one-group-drops-the-reader-feed-connection-after-every-group-got-a-number) | Low | A write error in one group drops the reader feed connection after every group got a number | open |
| [D-223](#d-223-group-and-cancels-create-an-overview-index-for-every-group-they-touch) | Low | GROUP and cancels create an overview index for every group they touch | partly fixed (#460: reader commands and cancels in dreaderd; `dcancel -r` and `dgrpctl masscancel` are open) |
| [D-224](#d-224-the-locking-comment-in-dexpireover-describes-locks-dreaderd-does-not-take) | Low | The locking comment in dexpireover describes locks dreaderd does not take | open |
| [D-225](#d-225-a-post-whose-last-body-line-ends-in-a-bare-lf-hangs-the-readers-post-connection) | High | A POST whose last body line ends in a bare LF hangs the reader's post connection | fixed (#444; the timeout in the post states is open, see below) |
| [D-226](#d-226-a-group-repeated-in-newsgroups-gets-one-number-and-one-overview-record-per-repeat) | High | A group repeated in Newsgroups gets one number and one overview record per repeat | fixed (#416) |
| [D-227](#d-227-the-feeder-leaks-the-article-map-for-every-430-article-prohibited) | High | The feeder leaks the article map for every 430 Article prohibited | fixed (#417) |
| [D-228](#d-228-a-queued-post-leaves-the-client-in-article-mode) | High | A queued POST leaves the client in article mode | fixed (#446; a check in `NNServerRequest()` and an ASan run are open, see below) |
| [D-229](#d-229-wireformat-misses-the-article-end-when-cr-and-lf-arrive-in-two-buffer-pieces) | Medium | wireformat misses the article end when CR and LF arrive in two buffer pieces | fixed (#419) |
| [D-230](#d-230-a-readers-age-argument-replaces-maxagepolicy) | Medium | A reader's age argument replaces maxagepolicy | fixed (#426) |
| [D-231](#d-231-a-spool-request-requeued-mid-article-sends-a-second-status-line-and-article) | Medium | A spool request requeued mid-article sends a second status line and article | open |
| [D-232](#d-232-the-reader-feed-leaks-supersedes-and-repeated-headers) | Medium | The reader feed leaks Supersedes and repeated headers | fixed (#421) |
| [D-233](#d-233-localspool-serves-the-raw-spool-record) | Medium | localspool serves the raw spool record | fixed (#501) |
| [D-234](#d-234-two-fetches-of-one-new-article-in-one-reader-process-corrupt-its-cache-file) | Medium | Two fetches of one new article in one reader process corrupt its cache file | fixed (#428) |
| [D-235](#d-235-dexpireover--r-empties-trackonly-indexes) | Medium | dexpireover -R empties trackonly indexes | fixed (#423) |
| [D-236](#d-236-a-dns-failure-during-the-host-cache-rebuild-locks-out-peers-named-by-host-name) | Medium | A DNS failure during the host cache rebuild locks out peers named by host name | fixed (#424) |
| [D-237](#d-237-pam-authentication-skips-the-account-phase) | Medium | PAM authentication skips the account phase | fixed (#487) |
| [D-238](#d-238-drequeue-keeps-writing-into-a-rotated-queue-file) | Medium | drequeue keeps writing into a rotated queue file | fixed (#435) |
| [D-239](#d-239-a-trailing-slash-in-path_spool-makes-dexpire-mark-the-spool-expired-in-the-history) | Medium | A trailing slash in path_spool makes dexpire mark the spool expired in the history | fixed (#433) |
| [D-240](#d-240-an-unstuffed-first-line-desyncs-the-external-filter) | Medium | An unstuffed first line desyncs the external filter | fixed (#432) |
| [D-241](#d-241-articles-the-external-filter-cannot-read-pass-unjudged) | Medium | Articles the external filter cannot read pass unjudged | fixed (#434; no count of skipped articles, see below) |
| [D-242](#d-242-the-feeder-forks-and-does-reverse-dns-before-the-access-check) | Medium | The feeder forks and does reverse DNS before the access check | fixed (#497) |
| [D-243](#d-243-a-queued-request-whose-client-went-away-crashes-the-reader-process) | Medium | A queued request whose client went away crashes the reader process | fixed (#422) |
| [D-244](#d-244-a-bare-lf-blank-line-after-the-header-breaks-body-and-the-reader-cache-in-wire-mode) | Low | A bare-LF blank line after the header breaks BODY and the reader cache in wire mode | open |
| [D-245](#d-245-commands-buffered-before-the-session-byte-limit-still-run) | Low | Commands buffered before the session byte limit still run | fixed (#463) |
| [D-246](#d-246-nnspoolresponsescrap-continues-as-the-220-path) | Low | NNSpoolResponseScrap continues as the 220 path | open |
| [D-247](#d-247-a-requeue-skips-a-same-priority-spool-server-that-has-the-article) | Low | A requeue skips a same-priority spool server that has the article | open |
| [D-248](#d-248-the-newgroups-cache-is-built-from-the-first-connections-listgroups) | Low | The NEWGROUPS cache is built from the first connection's listgroups | open |
| [D-249](#d-249-newnews-runs-the-next-pipelined-command-inside-its-list) | Low | NEWNEWS runs the next pipelined command inside its list | fixed (#461) |
| [D-250](#d-250-newnews-with-a-wide-wildmat-walks-all-groups-in-one-turn) | Low | NEWNEWS with a wide wildmat walks all groups in one turn | open |
| [D-251](#d-251-an-overview-data-file-over-2-gb-stops-the-reader-process) | Low | An overview data file over 2 GB stops the reader process | open |
| [D-252](#d-252-dexpireover--o-deletes-a-group-that-got-an-article-during-the-run) | Low | dexpireover -O deletes a group that got an article during the run | fixed (#431) |
| [D-253](#d-253-xzver-leaks-its-deflate-state-when-a-timeout-closes-the-connection) | Low | XZVER leaks its deflate state when a timeout closes the connection | open |
| [D-254](#d-254-group-creation-can-reset-ne-of-a-numbered-group) | Low | Group creation can reset NE of a numbered group | fixed (#507) |
| [D-255](#d-255-an-opener-waiting-for-dkp--t-keeps-the-old-dactivekp) | Low | An opener waiting for dkp -t keeps the old dactive.kp | open |
| [D-256](#d-256-a-power-loss-can-leave-history-chain-heads-past-the-end-of-the-file) | Low | A power loss can leave history chain heads past the end of the file | open |
| [D-257](#d-257-dnewslink-removes-a-rotated-queue-file-before-its-deferred-entries-are-sent) | Low | dnewslink removes a rotated queue file before its deferred entries are sent | open |
| [D-258](#d-258-a-look-ahead-dnewslink-takes-a-queue-file-before-the-master-flushes-it) | Low | A look-ahead dnewslink takes a queue file before the master flushes it | open |
| [D-259](#d-259-one-failed-fork-stops-host-cache-rebuilds-until-a-restart) | Low | One failed fork stops host cache rebuilds until a restart | open |
| [D-260](#d-260-a-peer-that-repeats-feedrset-and-feedcommit-starves-the-dnewsfeeds-reload) | Low | A peer that repeats feedrset and feedcommit starves the dnewsfeeds reload | open |
| [D-261](#d-261-altfeed-clears-the-labels-requiregroup-list) | Low | AltFeed clears the label's requiregroup list | fixed (#477) |
| [D-262](#d-262-groupref-inside-ifilter-ispam-and-espam-is-ignored) | Low | groupref inside IFILTER, ISPAM and ESPAM is ignored | fixed (#477) |
| [D-263](#d-263-global-is-applied-again-at-every-groupref) | Low | GLOBAL is applied again at every groupref | fixed (#477) |
| [D-264](#d-264-the-host-cache-rebuild-child-keeps-the-listening-socket) | Low | The host cache rebuild child keeps the listening socket | open |
| [D-265](#d-265-a-nul-in-newsgroups-hides-the-names-after-it) | Low | A NUL in Newsgroups hides the names after it | open |
| [D-266](#d-266-nortflush-and-domismatch-do-the-opposite-on-unsigned-char-platforms) | Low | nortflush and domismatch do the opposite on unsigned char platforms | fixed (#477) |
| [D-267](#d-267-dexpire-keeps-history-entries-of-directories-removed-in-a-later-pass) | Low | dexpire keeps history entries of directories removed in a later pass | fixed (#438) |
| [D-268](#d-268-a-failed-opendir-or-readdir-in-dexpire-marks-the-whole-spool-expired) | Low | A failed opendir or readdir in dexpire marks the whole spool expired | fixed (#437) |
| [D-269](#d-269-record-updates-during-the-dhisexpire-copy-pass-are-lost) | Low | Record updates during the dhisexpire copy pass are lost | open |
| [D-270](#d-270-an-unreadable-dexpirectl-at-its-reload-stops-every-reader-process) | Low | An unreadable dexpire.ctl at its reload stops every reader process | fixed (#458) |
| [D-271](#d-271-compresslvl-0-stores-gzip-records-that-the-spool-reads-as-plain) | Low | compresslvl 0 stores gzip records that the spool reads as plain | open |
| [D-272](#d-272-dirtime-is-one-global-for-all-metaspools) | Low | DirTime is one global for all metaspools | open |
| [D-273](#d-273-minfreefiles-on-btrfs-removes-every-directory-but-the-newest) | Low | minfreefiles on btrfs removes every directory but the newest | fixed (#481) |
| [D-274](#d-274-the-t-size-suffix-in-dspoolctl-multiplies-only-once) | Low | The t size suffix in dspool.ctl multiplies only once | fixed (#462) |
| [D-275](#d-275-an-unconfirmed-ptr-name-becomes-the-readers-host-name) | Low | An unconfirmed PTR name becomes the reader's host name | open |
| [D-276](#d-276-a-vserver-accessfile-uses-only-the-first-pattern-of-a-comma-list) | Low | A vserver accessfile uses only the first pattern of a comma list | open |
| [D-277](#d-277-a-110-readerdef-answer-from-one-access-line-is-lost-when-another-lines-backend-runs) | Low | A 110 readerdef answer from one access line is lost when another line's backend runs | open |
| [D-278](#d-278-with--c-n-refused-connections-stay-counted-and-switch-listeners-off) | Low | With -c N refused connections stay counted and switch listeners off | open |
| [D-279](#d-279-addrealm-of-one-access-line-rewrites-the-user-name-for-later-lines) | Low | addrealm of one access line rewrites the user name for later lines | open |
| [D-280](#d-280-with-realm--one-user-counts-as-separate-users-for-maxconnperuser) | Low | With realm * one user counts as separate users for maxconnperuser | open |
| [D-281](#d-281-a-slow-transfer-resets-a-tripped-spam-filter-slot) | Low | A slow transfer resets a tripped spam filter slot | fixed (#476) |
| [D-282](#d-282-the-spam-filter-decay-keeps-idle-minutes-as-credit) | Low | The spam filter decay keeps idle minutes as credit | fixed (#476) |
| [D-283](#d-283-a-new-spam-filter-cache-size-truncates-the-mapped-cache) | Low | A new spam filter cache size truncates the mapped cache | fixed (#476) |
| [D-284](#d-284-dcancel--a-and--s-rewrite-a-spool-file-a-feeder-still-appends-to) | Low | dcancel -a and -s rewrite a spool file a feeder still appends to | open |
| [D-285](#d-285-doverctl-clean-deletes-the-overview-of-a-group-created-after-its-scan) | Low | doverctl clean deletes the overview of a group created after its scan | fixed (#506) |
| [D-286](#d-286-dcancel--r-leaks-two-descriptors-and-a-map-per-line) | Low | dcancel -r leaks two descriptors and a map per line | fixed (#436) |
| [D-287](#d-287-didump-prints-the-end-mark-and-exits-0-after-a-read-error) | Low | didump prints the end mark and exits 0 after a read error | fixed (#441) |
| [D-288](#d-288-a-shim-compile-error-makes-23-dns-and-ipc-tests-skip) | Low | A shim compile error makes 23 DNS and IPC tests skip | fixed (#443) |
| [D-289](#d-289-the-writers-waited-check-in-testkpdbconcurrentupdate-can-never-fail) | Low | The writers-waited check in TestKPDBConcurrentUpdate can never fail | fixed (#447) |
| [D-290](#d-290-the-30-s-wait-in-the-dnewsfeeds-no-argument-test-misses-the-reload) | Low | The 30 s wait in the dnewsfeeds no-argument test misses the reload | fixed (#449) |
| [D-291](#d-291-dclient--f-a-nul-in-a-line-a-read-error-and-the-unread-replies) | Low | dclient -f: a NUL byte in a line ends the piece early, a read error is not reported, replies are not read | open |
| [D-292](#d-292-the-size-parsers-in-libsubsc-mis-convert-large-values-and-their-own-output) | Low | The size parsers in lib/subs.c mis-convert large values and their own output | open |
| [D-293](#d-293-repeated-overview-index-misses-repeat-the-lookup-work-and-a-silent-enoent-hides-a-missing-spool) | Low | Repeated overview index misses repeat the lookup work, and a silent ENOENT hides a missing spool | open |
| [D-294](#d-294-dnewslink-miscounts-offers-during-replay-and-bytes-on-pipelined--h-feeds-a-retried-fresh-article-can-be-marked-deferred) | Low | dnewslink miscounts offers during replay and bytes on pipelined -H feeds; a retried fresh article can be marked deferred | open |
| [D-295](#d-295-a-crlf-separator-can-be-taken-for-a-folded-header-in-a-fed-control-article) | Low | A CRLF separator can be taken for a folded header in a fed control article; no daemon-level test found for its From: and Newsgroups: duplicate refusal | open |
| [D-296](#d-296-reported-five-second-startup-log-timeout-in-testaccesserrorloggedonceperchange) | Low | Reported five-second startup-log timeout in TestAccessErrorLoggedOncePerChange | open |
| [D-297](#d-297-postbytes-counted-an-accepted-post-twice) | Low | `postbytes=` counted an accepted POST twice | fixed (#457) |
| [D-298](#d-298-header-copies-of-from-and-newsgroups-are-not-released-in-the-control-scan) | Low | Header copies of From: and Newsgroups: are not released in the control scan | fixed (#465) |
| [D-299](#d-299-dexpirecache--i-on-btrfs-removes-the-oldest-empty-cache-files-at-every-run) | Low | dexpirecache -i on btrfs removes the oldest empty cache files at every run | fixed (#490) |
| [D-300](#d-300-a-global-label-without-end-is-linked-as-a-feed) | Low | A GLOBAL label without `end` is linked as a feed | fixed (#483) |
| [D-301](#d-301-a-groupref-to-its-own-label-recurses-to-the-limit) | Low | A groupref to its own label recurses to the limit | fixed (#483) |
| [D-302](#d-302-dexpirecache--i-n-removes-the-first-empty-file-met-and-older-ones-not-the-n-oldest) | Low | dexpirecache -i N removes the first empty file met and older ones, not the N oldest | fixed (#492) |
| [D-303](#d-303-dexpirecaches-statistics-miss-the-empty-files-and-links-removed-and-give-removed-file-ages-when-none-was-removed) | Low | dexpirecache's statistics miss the empty files and links removed and give removed-file ages when none was removed | fixed (#493) |
| [D-304](#d-304-on-netbsd-a-reader-connection-right-after-another-ones-quit-can-be-reset-or-closed) | Low | On NetBSD a reader connection right after another one's QUIT can be reset or closed | open |
| [D-305](#d-305-dexpirecache--i-with-a-large-n-walks-the-list-for-every-entry-kept) | Low | dexpirecache -i with a large N walks the list for every entry kept | fixed (#502) |
| [D-306](#d-306-dexpirecache8-says--i-removes-cache-files-by-access-time) | Low | dexpirecache.8 says -i removes cache files by access time | fixed (#494) |
| [D-307](#d-307-dclient-waits-forever-for-a-server-that-sends-nothing) | Low | dclient waits forever for a server that sends nothing | fixed (#495) |
| [D-308](#d-308-mbread-adds-a-failed-reads--1-to-its-pointer-and-count) | Low | MBRead() adds a failed read's -1 to its pointer and count | open |
| [D-309](#d-309-localspool-and-the-nntp-path-still-differ-in-the-path-and-xref-rewrite-and-in-line-ends) | Low | localspool and the NNTP path still differ in the Path and Xref rewrite and in line ends | open |
| [D-310](#d-310-doverctl-clean-removes-the-over-file-of-a-live-group-while-dreaderd-is-setting-it-up) | Low | doverctl clean removes the over file of a live group while dreaderd is setting it up | open |
| [D-311](#d-311-doverctl-a-signal-during-the-active-scan-still-lets-clean-delete-files) | Low | doverctl: a signal during the active scan still lets clean delete files | open |
| [D-312](#d-312-checkgroups-skips-the-flag-and-description-of-a-group-made-meanwhile) | Low | checkgroups skips the flag and description of a group made meanwhile | fixed (#509) |
| [D-313](#d-313-doverctl-clean-takes-a-populated-groups-files-for-an-empty-group-with-the-same-crc-name) | Medium | doverctl clean takes a populated group's files for an empty group with the same crc name | fixed (#508) |
| [D-314](#d-314-doverctl-convert-crc-hierarchy-clean-deletes-the-overview-of-every-group-with-iter-above-0) | Medium | doverctl convert crc hierarchy clean deletes the overview of every group with ITER above 0 | fixed (#508) |
| [D-315](#d-315-dsyncgroups--v-prints-while-it-holds-a-new-groups-record-lock) | Low | dsyncgroups -v prints while it holds a new group's record lock | fixed (#509) |
| [D-316](#d-316-a-killed-dsyncgroups-leaves-a-new-group-with-nb-but-no-ne-or-nx) | Low | A killed dsyncgroups leaves a new group with NB but no NE or NX | fixed (#509) |
| [D-317](#d-317-doverctl-convert-back-to-crc-or-md5-moves-two-colliding-groups-with-iter-0-onto-the-same-file-name) | Medium | doverctl convert back to crc or md5 moves two colliding groups with ITER 0 onto the same file name | open |
| [D-318](#d-318-hierarchy-layout-clean-removes-every-compressed-data-file-the-gz-suffix-is-taken-into-the-group-name) | Medium | Hierarchy layout: clean removes every compressed data file (the .gz suffix is taken into the group name) | fixed (#511, #513) |
| [D-319](#d-319-dexpireover-removes-the-files-of-a-new-iteration-while-the-iter-write-has-not-landed) | Low | dexpireover removes the files of a new iteration while the ITER write has not landed | fixed (#510) |
| [D-320](#d-320-dsyncgroups-splits-a-list-newsgroups-line-over-4095-bytes-and-makes-a-group-of-its-tail) | Low | dsyncgroups splits a LIST NEWSGROUPS line over 4095 bytes and makes a group of its tail | fixed (#512) |
| [D-321](#d-321-dgrpctl-checkgroups-without-exec-and-masscancel-print-while-they-hold-a-record-lock) | Low | dgrpctl checkgroups without exec and masscancel print while they hold a record lock | fixed (#512) |
| [D-322](#d-322-dexpireover--w-with-two-selected-empty-colliders-still-takes-an-unselected-groups-files) | Medium | dexpireover -w with two selected empty colliders still takes an unselected group's files | fixed (#510) |
| [D-323](#d-323-doverctl-clean-and-dexpireover-keep-a-removed-groups-files-of-an-iteration-at-or-above-a-same-hash-groups-iter) | Low | doverctl clean and dexpireover keep a removed group's files of an iteration at or above a same-hash group's ITER | open |
| [D-324](#d-324-doverctl-clean-keeps-a-hierarchy-data-file-that-is-stale-for-its-own-group-while-the-other-gz-reading-covers-it) | Low | doverctl clean keeps a hierarchy data file that is stale for its own group while the other .gz reading covers it | open |
| [D-325](#d-325-hierarchy-layout-a-group-with-a-one-character-last-component-and-the-same-name-plus-gz-share-one-data-file-path) | Medium | Hierarchy layout: a group with a one-character last component and the same name plus .gz share one data file path | open |
| [D-326](#d-326-doverctl-clean-skips-group-directories-whose-name-does-not-start-with-two-alphanumeric-characters) | Low | doverctl clean skips group directories whose name does not start with two alphanumeric characters | fixed (#515) |
| [D-327](#d-327-doverctl-clean-re-reads-the-whole-active-file-for-every-file-that-triggers-a-rescan) | Low | doverctl clean re-reads the whole active file for every file that triggers a rescan | open |
| [D-328](#d-328-dsyncgroups--d-with-a--g-file-that-ends-early-deletes-every-local-group-it-does-not-name) | Medium | dsyncgroups -D with a -G file that ends early deletes every local group it does not name | open |
| [D-329](#d-329-dexpireover--w-never-cleans-compressed-hierarchy-data-files) | Low | dexpireover -w never cleans compressed hierarchy data files | open |
| [D-330](#d-330-hierarchy-groups-whose-first-character-is----or-_-are-never-cleaned-or-expired) | Low | Hierarchy groups whose first character is +, - or _ are never cleaned or expired | open |
| [D-331](#d-331-doverctl-docs-do-not-say-which-old-layout-directories-clean-enters-after-a-convert) | Low | doverctl docs do not say which old-layout directories clean enters after a convert | open |

Feature requests are at the end: [F-01 Cancel-Lock](#f-01-cancel-lock-rfc-8315), [F-02 configure script](#f-02-configure-script-for-the-build-options), [F-03 refeed a whole spool](#f-03-refeed-a-whole-spool-to-another-server), [F-04 NoCeM notices](#f-04-nocem-notices), [F-05 init scripts](#f-05-init-scripts-and-adm-script-replacements), [F-06 Perl authentication](#f-06-perl-authentication-for-dreaderd), [F-07 POST as an injecting agent](#f-07-dreaderd-post-as-an-rfc-55365537-injecting-agent), [F-08 smaller overview](#f-08-smaller-overview-overview-fields-only-one-copy-per-article).

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
RADIUS off on Linux (`tests/e2e/ci/build-on-host` does). Decided 2026-10-29 (go-while and the maintainer): RADIUS is off by default, and fixed in #127; `DB_ENABLED` and `REJECT_DB` are unchanged.

### D-17 Unchecked results under _FORTIFY_SOURCE
Ubuntu and Gentoo enable `_FORTIFY_SOURCE` by default: 122 to 133 `-Wunused-result` warnings
on 6-CUR-20261024-BWH, 80 after #5 to #9 (mostly `write()` in `util/` and
`dreaderd/main.c`). Debian, Fedora, RHEL-family and openSUSE show none.

### D-18 Debian GNU/Hurd build
`PATH_MAX` is undefined on Hurd (`lib/defs.h:580`); `dreaderd/main.c` uses `setpgrp(0, 0)`
because `USE_SYSV_SETPGRP` isn't set for Hurd; `dreaderd/dns.c:19` picks `db_185.h` only for
`__linux__`/`__sun`. Low priority for the maintainer.

Fixed in #335: Hurd also has no `sys/mount.h`. `compat/pathmax.h` sets `PATH_MAX` to 4096 where the system has none (system values are kept), and four path joins that relied on it (`MakeGroupDirectory()`, dexpire's `findSize()`, `scanDirectory()` in dexpireover and doverctl) refuse an overlong name. diablo and dreaderd call `setpgid(0, 0)`; configure records which DB header its `dbopen()` probe linked; `sys/mount.h` is left out where `sys/vfs.h` gives `statfs()`; `lib/config.h` has a GNU/Hurd block and the Makefile sets 64-bit file offsets there. The full C build passes on Debian GNU/Hurd amd64. Go has no Hurd port, so the e2e suite does not run there; `tests/portability/d18.sh` checks the fallback and the overlong refusal.

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

**Decision (maintainer, 2026-10-27): kept.** Data integrity comes first, so a process that cannot
write `dactive.kp` stops. Run diablo and dreaderd under a supervisor or a restart loop on hosts
where a full disk is possible, and watch the free space. The full-spool tests on 2026-10-27
(diablo keeps running and answers 436 when the spool is full) did not exercise this path.

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
Fixed in #67: `211 count low high group` (GROUP and LISTGROUP share `SelectGroup()`), ranges,
and 412. Test: `TestReaderReplyCodes`.

### D-45 HDR reply code and LIST HEADERS
`HDR` answers 221, XHDR's code, instead of 225 (`dreaderd/xover.c:205`), and `LIST HEADERS`,
which RFC 3977 section 8.6 requires with HDR, gives `501 Bad command use`
(`dreaderd/reader.c:1318`). #41 doesn't advertise HDR because of this.
Fixed in #67: HDR answers 225 (XHDR keeps 221) and LIST HEADERS answers 215. Left out on
purpose: the message-id form of HDR and OVER (they answer 503), so HDR and LIST HEADERS stay
unadvertised in CAPABILITIES, as RFC 3977 requires that form.
Since #323 (D-193) the message-id form answers (from the current group), and since #329
CAPABILITIES lists HDR and LIST HEADERS, as INN does; OVER stays without MSGID, as INN's
(jesse's pick, 2026-11-06). The D-193 follow-up below takes HDR/XHDR by message-id through
the spool's HEAD, so it covers any article as INN's does.

### D-46 AUTHINFO reply codes
RFC 4643 section 2.3.2: a wrong password should get 481 and let the client retry; dreaderd
answers `502 Access denied to your node` (`dreaderd/main.c:1739`). AUTHINFO PASS before USER
gives 501 instead of 482 (nntp.c:104), and AUTHINFO after a successful login is accepted
again (381) instead of 502 (nntp.c:86).
Fixed in #67: 481, 482 and 502. Left out on purpose: a retry in the same session. After AUTHINFO
PASS the reader hands the connection back to the master for a fresh access check, and on a
failure the master writes 481 and closes the connection, so the client reconnects to try
again. Tests: `TestReaderAuthinfoCodes`, and nntp-testkit's six RFC 4643 checks in
`TestReaderConformanceAuthinfo`.

### D-47 Other reader reply codes
RFC 3977: NEXT with no group selected gives 421 instead of 412 (`dreaderd/nntp.c:264`);
ARTICLE or STAT in an empty group 423 instead of 420 (nntp.c:423); `ARTICLE abc` or `-1` 430
instead of 501 (nntp.c:331-334); OVER past the high mark (or a range like 3-1) sends 224 and an
empty list instead of 423, because 224 goes out before the range is checked (xover.c:208);
LIST ACTIVE.TIMES gives 500 instead of 215 or 503 (list.c:364).
Fixed in #67 (ACTIVE.TIMES is now listed in CAPABILITIES; its creator is `unknown`, which Diablo
doesn't record). Test: `TestReaderReplyCodes`.

### D-48 Feeder reply codes
diablo answers MODE READER with 500 instead of 502 (a transit-only server, RFC 3977 section
5.3; `util/diablo.c:2534`), and IHAVE or CHECK without an argument with 435/438 instead of 501.
All 15 of nntp-testkit's RFC 4644/IHAVE checks pass otherwise.
Fixed in #67. Test: `TestFeederReplyCodes`.

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

### D-51 Logins beyond `readerdns` are turned away, not queued
Each new reader connection, and each AUTHINFO login, goes through one of dreaderd's DNS/auth
workers (`readerdns`, default 5). When all of them are busy, the next one is not queued: the
client gets `400 <host>: Server too busy - please try later` and is disconnected
(`dreaderd/main.c:1536`, logged as "Unable to find spare DNS thread"). A short burst of logins
larger than `readerdns` therefore disconnects some clients, even though each worker is only
busy for a moment. Seen in the e2e suite: with `readerdns 1`, two conformance checks logging in
at the same time got 400. Fix: queue the request until a worker is free (with a time limit),
or at least document that `readerdns` must cover the expected number of logins arriving at
once.
Fixed: SendDns() keeps such a request in a queue in the master (at most 4 * `readerdns` + 4
entries, in order) until a worker is free; a request that waits 10 s, or finds the queue full,
gets the same 400, and a client that closes while waiting is dropped. Tests:
`TestReaderLoginsQueueForDNSWorker`, `TestReaderDNSQueueLimit`.
Changed by the DNS overload work: AUTHINFO logins have their own queue, served first, with no
length limit (at most one per connection) but a descriptor cap; new connections are queued
while their expected wait is within half of `readerdnswait` (default 5 s, the longest any
request waits), up to `readerdnsqueue` (default 256), and otherwise get the 400 at once.
The listener stays on while the workers are busy. Tests: `TestReaderDNSOverload*`,
`TestReaderDNSQueueOptions`, `TestReaderDNSAuthFdLimit`.

### D-53 checkgroups creates half-formed groups
`dreaderd/control.c:951`. A checkgroups control message that is accepted (`doit`) writes the
description of a group named in its body that does not exist. The group then appears in LIST
ACTIVE as `0 0 n`, and `GROUP` answers `211 1 -1 -1`: the record has no creation status or
article numbers. Fix: write the description only when the record exists, or create a complete
record. Reproduced.

### D-54 Transient LIST ACTIVE gap after a record is rewritten
`lib/kpdb.c:529` (`KPDBScanFirst`), triggered by `CtlNewGroup` and `CtlCheckGroups` in
`dreaderd/control.c`. When a control message rewrites an existing group record, the next LIST
ACTIVE omits that group and the one after lists it again; appended groups are seen at once.
Reproduced. Cause (from reading): the scan walks the mapping it already has, and the append
counter does not change when a record is rewritten. Fix: bump the counter on a rewrite, or
remap at the start of a scan.

### D-55 dreaderd small items
From reading, not run: `dreaderd/dns.c:365`, `:491`, `:560` and `:584`: a `!`-prefixed deny
file, cdb or db returns without closing its handle, which leaks a descriptor in the DNS worker
for each matching login (the `!` form is undocumented). `dreaderd/xover.c`
(`NNStartListOverviewRange`): the OVER/XOVER range is only clamped to the group's low and high
marks, so a very wide range (possible in xref-slave mode) is walked in one pass.

Partly fixed: the `!` deny-list handles are closed again, for file and cdb in #185 (D-168, D-169) and for db in #200. The OVER/XOVER range walk is bounded per pass in #298: `NNListOverviewRange` looks at no more than 10000 records, then yields (as XPAT does) and goes on in the next turn, so a very wide range of missing articles does not hold the process.

### D-56 `feedermaxheadersize` is never enforced
`util/diablo.c:3004-3007`, `:2824`, `:3060`. `headerLen` is still 0 when it is compared with
`FeederMaxHeaderSize` (it is set after the blank line, once `inHeader` is cleared), so
`LAERR_HEADERTOOBIG` never fires and headers can be as large as `feedermaxartsize` (10 MB by
default). Reproduced: with `feedermaxheadersize 2k`, an article with about 130 KB of headers is
accepted (235). Fix: compare the running header byte count.

### D-57 dhisexpire leaves the history dead when it fails
`util/dhisexpire.c:311-350` and `Fail()` at `:512-521`. With `USE_DEADMAGIC` (the default),
dhisexpire writes the dead magic into the live dhistory and only then does the backup `link()`
and `rename()`. A failure after that (an unwritable `-o` directory, another file system, a write
error, a kill) prints "keeping old history" but never restores the magic, and every new feeder
connection exits in `HistoryOpen` until `dhisexpire -u`. Reproduced with
`dhisexpire -a -o /missing/dir/x.bak dhistory`. The previous backup is also removed before the
link. Fix: call `DoUnDead()` from `Fail()` and from a signal handler once the mark is written, and
do the link checks before the mark.

### D-58 dnewsfeeds directive without an argument crashes
`lib/newsfeed.c:1626-1812`. Directives such as `feedertxbuf`, `feederrxbuf`, `startdelay`,
`delayfeed`, `delayinfeed`, `transmitbuf`, `receivebuf`, `setqos`/`settos`, `maxparallel`,
`maxqueue`, `compress`, `maxstream`, `queueskip`, `priority` and `port` pass a NULL argument to
`atoi()`/`strtol()` when the value is missing. `dfeedtest` and every other program that loads
dnewsfeeds crash with SIGSEGV, and diablo, which reloads dnewsfeeds about once a minute, loses
its master when such a line is saved. Reproduced for `maxparallel`, `port`, `priority`,
`feederrxbuf` and `startdelay`, and for the reload. `lib/spool.c` `loadSpoolCtl` has the same
pattern (from reading). Fix: `if (nf && s2)`, as the other directives do.

### D-59 Per-label maxconnect allows one too many
`lib/newsfeed.c:306` tests `*pcount > nf_MaxConnect`, but the count (`util/diablo.c:1170-1177`) is
the number of existing connections, without the new one. `maxconnect 2` on a label lets three
connections in (the fourth gets 502); the global `maxconnect` uses `>=` and is right.
Reproduced. Fix: `>=`.

### D-60 Host cache: `end` does not end a label
`lib/hostauth.c:344-358` (`loadDnewsFeeds`). The skip for lines without an argument runs before
the test for `end`, and `end` has no argument, so the current label is never cleared; an `inhost`
or `host` line after `end` is authorised under the previous label. `char label[255]` is also
never initialised. Reproduced with `dprimehostcache -dump`. diablo's own parser only logs
"unexpected position". Fix: test `end` first and initialise `label`.

### D-61 diablo.hosts cannot hold an IPv6 address
`lib/hostauth.c:303-306` splits the line with `strtok(p, ": \t\r\n")`, so `2001:db8::25 mylabel`
becomes host `2001` (which resolves to 0.0.7.209) and label `db8`. The IPv6 peer is not
authorised and a made-up IPv4 address is. The `IP:` and `FQDN:` prefix is stripped before the
address is converted. Verified in the host cache dump. Fix: split on white space only.

### D-62 STAT uses a 128-byte path buffer
`util/diablo.c:2384` has `char path[128]`. When the spool path plus `/D.xxxxxxxx/B.xxxx` is
longer than 127 characters, `STAT` of an existing article answers `430 Article expired` while
`HEAD` works. Reproduced with a 125-character spool path. Fix: `PATH_MAX`.

### D-63 dnewslink refile loses flags
`util/dnewslink.c:2640-2648` (`RefilePendingStreams`) and `:1161-1166`. After a lost peer with
streaming entries pending, the refiled lines have no ` H` (header only) and ` C<size>`
(compressed spool) fields, so a later run sends a header-only record as a full article and a
compressed article as plain bytes. The entry in `buf` is also written twice. Reproduced with a
queue of two `... H C50` lines and a peer that drops after the CHECKs. Fix: keep the flags in the
Stream struct and write them back in the queue format.

### D-64 Weighted spool allocation divides by zero
`lib/spool.c:357-363` computes `random() % totw`. The default weight (`spoolSizeGb()`, `:451-452`)
is 0 for a partition under 1 GiB, and `weight 0` (`:738`) is accepted. Reproduced: `allocstrat
weighted` with `weight 0` makes diablo die with SIGFPE at start; the small-partition case is
from reading. Fix: treat a weight <= 0 as 1, or fall back to sequential when the total is 0.

### D-65 dnewsfeeds label name overflows the host cache buffer
`lib/hostauth.c` `loadDnewsFeeds()` copied the label name with `strcpy(label, p)` into `char label[255]`.
A label name of 255 characters or more in dnewsfeeds (a local file) overflowed it when the host cache
was built. Found by go-while in review of #97. Reproduced with a 3000-character name (dprimehostcache
aborts). Fix: a name that does not fit is skipped, with the hosts of its label.

### D-66 dnewslink loses 431-deferred entries
Entries answered 431 sit in the in-memory `deferbuf` (up to 100) and are retried only at the end
of the file or when it reaches 80 (`util/dnewslink.c:1062`, `:1160-1180`, `:1275-1321`). If the
session ends first (the peer answers 400 or goes away and the reconnect fails, SIGTERM) the queue
is rewritten without them. Reproduced: queue a1, a2, a3; the peer answers 431, 431, 400 and closes;
only a3 is left in the queue. Fix: write the deferred entries back with the unsent ones.

### D-67 dcancel -s does not update the history
`dcancel -s` (spool only, no `-h`) rewrites the spool file and shifts the later articles but does
not update the history (`util/dcancel.c:586`). Afterwards the cancelled id still answers `STAT 223`,
and `ARTICLE` of the second id returns the third article's text. `-a` is fine. Reproduced on the
test zone. Fix: refuse `-s` without `-h`, or update the history in every mode.

### D-68 dnewslink with a small maxstream sends one window
`HiWaterMark` is 5, so `StreamPend < NumStream - HiWaterMark` is never true again after the first
window (`util/dnewslink.c:835`, `:2391`). `-M 2` with 6 queued articles sends 2 CHECKs and leaves
4 in the queue. Reproduced. Fix: scale the mark to `NumStream`.

### D-69 dnewslink spins on a compression level zlib rejects
A level zlib rejects (dnewsfeeds `compress 12`, giving `-Z12`) makes `deflateInit` fail while
`CompressOn` stays set, and `flushCompressBuffer` loops forever (`util/dnewslink.c:1945-1948`,
`:2877`). Reproduced. Fix: clear `CompressOn` (and log) when `deflateInit` fails. The session-end
compression line also logs `-nan%` or `-inf%` for a session without data and passes int32
counters to `%ld` (`util/dnewslink.c:2799-2805`, found by go-while); fixed in the same PR.

### D-70 dgrpctl checkgroups reads long lines as two
`fgets` into `buf[128]` (`util/dgrpctl.c:683`) reads a line of 128 bytes or more as two lines, and
the tail becomes a phantom group on `exec`. An empty list leaves `hierarchy` NULL and the delete
scan calls `WildCmp(NULL, ...)`: SIGSEGV (`:843`). Both reproduced. Fix: read whole lines and
skip a list without a hierarchy.

Fixed in #172: dgrpctl checkgroups reads whole lines and skips a list without a usable group. Tests: `TestCheckgroupsLongLine`, `TestCheckgroupsEmptyList`.

### D-72 dnewslink deletes the queue after an overlong line
A damaged queue file with a line of 4 to 8 KB and no newline makes `readline` fail for good and
`dnewslink` removes the whole queue file (`util/dnewslink.c:3434`, `:1289`, `:1321`). Reproduced.
Fix: skip the line, keep the file.

### D-73 dnewslink queue fields leak into the next line
`extractFeedLine` sets `cSize`, `fileOff`, `fileSize` and `queuedTime` only when the field is
present, so a plain line after a `C50` line inherits `C50`; `readretry()` rebuilds retried lines
without the H/C flags (`util/dnewslink.c:3890-3910`, `:3329`). Reproduced: z0 plain, z1 `C50`, z2
plain, with a reconnect: z2 is written back as `200,100 C50`. D-63 fixed only the refile text.
The refile path (`util/dnewslink.c:2688`) writes " H" from the global `HeaderOnlyFeed`, so
keeping the flags in `readretry` is not enough on its own. Fix: reset the fields per line; keep the
flags in `readretry`.

### D-74 dnewslink queue rewrite write errors are unchecked
`fputs`, `fflush` and `fclose` of the rewritten queue file are not checked before the rename over
the queue (`util/dnewslink.c:1289-1321`); a full disk replaces the queue with a truncated one.
From reading. Fix: check them and keep the old queue on failure; same PR as D-72.


### D-76 dexpire removes a directory younger than keeptime
After removing a directory, `part->age` is taken from `SpoolEntries[i+1]`, the next entry of the
globally sorted list, which can belong to another spool or partition when the directory times
interleave (`util/dexpire.c:739-740`, age test `:675`). Reproduced: spool 01 with directories 180,
42 and 12 minutes old, spool 02 with 120 and 90, keeptime 3600: the 42-minute directory is removed.
Fix: take the next entry of the same partition, as the newest-directory guard at `:706` does.

### D-77 dhisexpire without -a leaves the history dead
A successful `dhisexpire` without `-a` leaves the live `dhistory` marked dead (`util/dhisexpire.c`
`DeadFd`); new connections get no greeting until `dhisexpire -u`. **Documented behaviour**, not a
regression from #95: `man/dhisexpire.8` says the old file's magic is marked invalid when the
rewrite completes and `-u` makes it usable again (checked by go-while on the tree before #95 and on
main). What is missing is a note: print one at the end of a run without `-a` and in the usage text.

Fixed in #134: dhisexpire says at the end of a run without `-a`, and in its usage text, that the old history stays marked dead until `dhisexpire -u`.

### D-78 dicmd config internalfilter disables the spam filter
Even for an unchanged value: `termSpamData()` sets `fi->Fd = -1` but not `BodyFilterFd` and
`NphFilterFd`, so the following `InitSpamFilter()` skips `openFilterFile()`
(`lib/spamfilter.c:356`, `:376-403`; `lib/config.c:667-672`, `util/diablo.c:4246-4250`). `dicmd
info` still says enabled. Reproduced: `internalfilter B3`, 12 identical bodies: 2 stored before the
change, 12 after. Fix: reset all three descriptors in `termSpamData()`.

### D-79 KPDB blank or short line
The sort writes 8 hex digits at `i+1` whatever the line length;
`KPDBLookupBinary` recurses forever on a range that is just the blank line (stack overflow, SIGSEGV)
(`lib/kpdb.c:1238-1254`, the recursion at `:1168-1171`). The 8-digit write corrupts only lines
shorter than 9 bytes. Needs a hand-edited or damaged file (the header documents
manual editing). Reproduced: `dkp -r` crashes with 3 of 4 records left.

### D-80 KPDB torn tail loses appends
`KPDBWrite` appends at the end without looking at the last byte, and the lookup from the end needs
`'\n'` (`lib/kpdb.c:791-851`, `:1005-1006`). After a crash (a half-written record, or a NUL block
on ext4 or XFS) the first record appended is glued onto the torn tail, `dkp` exits 0, and no
lookup finds it (the lookup scans backwards, so later appends that end in `'\n'` are found): a lost
group creation. Reproduced. Fix: on write, if the last byte is not `'\n'`, cut back to the last newline
or write one first.

### D-81 The sample internalfilter line turns the filter off
`samples/diablo.config:400` shows `internalfilter B6s32768 N16t7200`; the expire option is `e`, so
`t` is invalid, `SetSpamFilterOpt` logs to syslog only, and the filter stays off
(`lib/spamfilter.c:134`, `:207-211`). Copying the example turns the filter off silently. Fix the
sample; maybe make an invalid string a config error.

### D-82 Long config lines are split
`diablo.config` is read with `buf[256]` (`lib/config.c:158`, `:190`): a 422-character comment line
makes every Diablo program exit with "Unknown diablo.config command". `dspool.ctl` (`lib/spool.c:575`) and `dexpire.ctl` (`lib/expire.c:73`) use `MAXGNAME+256`
buffers and split only above about 512 characters (from reading); the 256-byte case is
`diablo.config`, which is reproduced.

### D-83 Indented dexpire.ctl line is ignored
After skipping white space the pattern is copied from `buf` instead of `p` (`lib/expire.c:104`), so
`   e2e.*:a4000` matches nothing. Reproduced.


### D-85 dhisexpire treats a read error as end of file
`n = read()` returning -1 is handled like 0 (`util/dhisexpire.c:331`, the `n == 0` test at `:333`): an EIO ends the copy, and
with `-a` a truncated history is installed (the old one stays as the backup). From reading.

### D-86 Spool number off by one
`spool > MAX_SPOOL_OBJECTS` should be `>=` (`lib/spool.c:126`, `:150`); a history record with exp
200 reads `SpoolObjectMap[100]`. From reading.

### D-88 dnewslink -p overwrites leftover lines of an earlier run
With `-p` the batch file receives only the leftover lines, and a later pipe run opens it with
`fopen("w")` (`util/dnewslink.c`, final queue rewrite), so it overwrites the leftovers of the earlier
run even when nothing fails. Older behaviour, found while reviewing D-72/D-74. From reading.

Fixed in #256: `dnewslink -p` appends the unsent entries to the queue file under a lock instead of truncating it.

### D-87 Numeric IPv6 host in diablo.hosts dropped on a machine without IPv6
`lib/hostauth.c:240` called `getaddrinfo(host, NULL, NULL, ...)`. With NULL hints glibc uses
`AI_ADDRCONFIG|AI_V4MAPPED`, and on glibc 2.39 (EL10) `AI_ADDRCONFIG` also drops a numeric IPv6
address when the machine has no non-loopback IPv6 address: `getaddrinfo("2001:db8::25")` fails with
`EAI_ADDRFAMILY`, so an IPv6 `diablo.hosts` entry is silently ignored. Found by go-while's matrix
run (`TestHostCacheDiabloHostsForms` failed on AlmaLinux 10, CentOS Stream 10 and Rocky 10). Fixed
with explicit hints (#115).

### D-89 AI_ADDRCONFIG still passed for listen, bind and connect addresses
`dreaderd/main.c:620` (listen, `AI_PASSIVE|AI_ADDRCONFIG`), `dreaderd/server.c:427` (connect and
bindhost) and `util/dsyncgroups.c:915` still pass `AI_ADDRCONFIG`. On a glibc 2.39 host without a
global IPv6 address a numeric IPv6 listen, bind or connect address (for example `::1`) fails with
`EAI_ADDRFAMILY`. Same cause as D-87. From reading (found in the reviews of D-87).

Fixed (#306).

### D-90 KPDB sort writes the offsets one record at a time
`KPDBSort()` (`lib/kpdb.c`) writes the sorted offset of every record with its own 8-byte `pwrite`.
On a ZFS dataset at its quota each such write took about 0.2 s: `dkp -r` and `dgrpctl newgroup` on a
full 2 MB test dataset with 259 records spent 40 to 50 s in the sort, with the file locked, while the
same sort takes well under a second with free space. A real active file has thousands of groups. An
interrupted sort is safe (the next open sorts again), but a sort that is interrupted by a timeout
starts over each time. Found while testing a full file system on the scratch instance. Fix idea:
build the offsets in memory and write them in one or a few large writes.

Fixed (#301): `KPDBSort()` stores the offsets through a writable mapping of the file, one 8-byte store per record in memory and no system call per record (a sort of 600 records uses 3 `pwrite` calls instead of 603); the old per-record `pwrite` stays as the fallback when the mapping fails. One big `write()` of the records was not used: a field replaced in place holds only the record lock, not the append lock the sort holds, and would be overwritten with the old bytes. The "needs sort" marker is still written first and cleared last, so an interrupted sort is resorted by the next open. Not measured on a full ZFS dataset (none here): a mapped store on a COW file system at its quota may still be slow or fail at write-back. Test: `TestKPDBSortFewWrites`, `TestKPDBInterruptedSortResorts`.
On OpenBSD the sort keeps one `pwrite` per record: without a unified buffer cache (D-171) a writable map would write back stale page bytes over a field another process has just replaced with `pwrite()` (jesse/cc-1, review of #301).

### D-91 TAKETHIS without an argument answers 439 instead of 501
`util/diablo.c:2076`: `TAKETHIS` with no message-id answers 439 (MsgIdMismatch); the other commands answer 501. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-92 Header-only feed reports UnknownReject for LAERR_NOBYTES
`util/diablo.c:3629-3639`: `LAERR_NOBYTES` on a header-only feed is reported as `UnknownReject`; the `HdrOnlyNoBytes` branch cannot be reached. Cosmetic. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed (#306).

### D-93 KPDBGetField key scan is not bounded by the record length
`lib/kpdb.c:403-409`: the scan for a key (`tok == NULL`) does not stop at `recLen`. Needs a damaged file. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-94 dcontrol.ctl, dnewsfeeds and dserver.hosts split long lines
`dreaderd/control.c:354`, `:373`: `dcontrol.ctl` is read with `fgets(buf[256])`; the same class (see D-82) in `dnewsfeeds` through `lib/include.c` `igets` (no long-line signal) and in `dserver.hosts` (`dreaderd/server.c:109`, `:116`). From reading, not run (deep review of main 76578ae by go-while's reviewers). The same long-line splitting occurs when serving LIST DISTRIBUTIONS, LIST DISTRIB.PATS and LIST MODERATORS (`dreaderd/list.c:446-459`, `:474-488`, `:517-529`): each 255-byte `fgets` chunk becomes a separate protocol entry instead of preserving or rejecting a complete source line (malformed list output for long local lines; independent of D-133). Added from go-while's third batch, not run.

Partly fixed in #298: `dserver.hosts` skips a line longer than the read buffer whole and logs `dserver.hosts: line N is longer than M bytes` (as D-177 does for `dreader.access`). `dcontrol.ctl`, `dnewsfeeds` and the LIST output are still open.

Fixed (#306).

### D-95 MaxInboundRate value is read past its end
`lib/newsfeed.c:2502-2513`: a `MaxInboundRate` value ending in `;` is parsed past its NUL. The same happens for a value whose last hour number ends the string (for example `100:1`): `lib/newsfeed.c:2525-2532` steps past the NUL when the last hour ends the string. Config. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-96 spamfilter: s0 turns the filter off, and USE_SPAM_RW_MAP 0 does not compile
`lib/spamfilter.c:180`: a size of `s0` passes the power-of-two test and silently turns the filter off. With `USE_SPAM_RW_MAP` 0 the file does not compile (`:552`, `:572` use an undeclared fd). From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-97 diab-filter writes one byte past its reply buffer; writeAll loops on a zero write
`filter/diab-filter.c:386-392`: a reply that fills `filter_abuf` is written one byte past the end; `writeAll()` loops forever if `write()` returns 0. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-98 ctype calls on plain char
`lib/subs.c:500-508`, `dreaderd/cache.c:408`, `:425`, `:429`: `isalpha()` and friends are called with a plain `char`; they need a cast to `unsigned char`. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-100 cacheFile builds the path without bounds
`dreaderd/cache.c:87-118`: with a very long `path_db` and `readercachedirs` the path is built without a length check. Config. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed in #230: `dreaderd/cache.c` refuses a cache home of `PATH_MAX - 128` bytes or more; the hash levels need at most 108 bytes (`readercachedirs` is capped at 10), and every caller passes a `PATH_MAX` buffer.

### D-101 Spool server replies have no line cap; other small items in dreaderd/server.c
`dreaderd/server.c`: spool replies have no line-length cap; the `ctl[1]` test on a one-character reply (`:821`). Found by go-while's reviewers from reading. The `ctl[1]` read is confirmed with AddressSanitizer on a Debian build: `NNFinishSReq(conn, "", 0)` (the `COM_STAT` case in `dreaderd/spool.c`) makes it read one byte past the empty string (global-buffer-overflow, reached by every STAT answered from the spool server). A normal build shows nothing, but an ASan build of dreaderd exits there, so e2e runs against one fail until it is fixed.

Partly fixed: the `ctl[1]` read past an empty reply is fixed in #255. The line-length cap on spool replies is fixed in #298: lines from a spool server are limited to 1 MiB (`mh_MaxLine`); a longer one drops the spool connection (logged as `MBReadLine line longer than`).

### D-102 ReadAccessCache leaks descriptors when a cache open fails
`dreaderd/dns.c:3335-3373`: one failed open of the five cache files leaves the others open, and every retry leaks more. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed in #160: when one cache file cannot be opened, ReadAccessCache closes the ones it did open (descriptor 0 included).

### D-103 Access cache reload leaks the old GroupDef and GroupList nodes
`dreaderd/dns.c:3420-3444`, `:3477-3480`: each reload leaks the old nodes and names in `DnsMemPool`. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed (#306).

### D-104 dhisexpire uses the history record size without a check
`util/dhisexpire.c:196-198`: `hh.henSize` is used as the record size with no zero or minimum check (SIGFPE, or a read past the buffer). Needs a damaged history. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-105 dexpire -o uses a NULL FILE after a failed fopen
`util/dexpire.c:973-976`, `:1170-1182`: with `-o`, a failed `fopen` is logged, and the later `fwrite` and `fclose` use NULL. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-106 dexpireover getFirstArtAge trusts the over file header
`util/dexpireover.c:1026-1044`: `oh_MaxArts` is not validated (SIGFPE on 0) and `oaBase[i]` is read before the `i <= n` check. Needs a damaged over.* file. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed in #162: an over file is skipped unless 1 <= `oh_MaxArts` <= the number of records it holds, so the division and the `oaBase[i]` read are in range.

### D-107 metaspool with more than 100 spool lines, or dontstore with no spools
`lib/spool.c:840-846`: more than 100 `spool` lines in one metaspool write past `ms_SpoolObjects[]`. `lib/spool.c:302-352` with `addMeta` (`:505-510`): a `dontstore yes` metaspool with no spools reaches a division by zero (allocstrat single) or index -1. Config. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-108 dcancel stores the history offsets before the rewritten spool file is in place
`util/dcancel.c:589-608`: the history offsets are stored before the rewritten file is installed; a later failure or a failed rename leaves the history pointing into a file that was not installed. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed in #170: the history entries of a rewritten spool file are stored only after the file is renamed into place. Test: tests/e2e/d108_dcancel_rewrite_test.go.

### D-109 dsyncgroups: use after free of the address lists, unchecked strtok results
`util/dsyncgroups.c:918-955`: `freeaddrinfo(dst)` at `:919` and `dst` is used at `:943`, `:949` on every connection; `:929` frees an unset `src`. `:467-469`, `:678-728`: the `strtok` results on a short `LIST ACTIVE` line from the peer are not checked; `CommandResponse` returns on EOF without setting `*pres` (`:965-989`). Checked in the code. From reading, not run (deep review of main 76578ae by go-while's reviewers).

Fixed in #171: the address list is freed after its last use, and an unset reply or a blank or short list line no longer crashes dsyncgroups. Test: tests/e2e/d109_dsyncgroups_test.go.

### D-110 dcontrol.ctl: ct_Verify leaks or keeps a stale value when two rules match
`dreaderd/control.c:420-424` clears the flags of a reused `Control` but not `ct_Verify`; `:456` assigns a new `zallocStr()` over it. With two matching lines that both say `verify-`, the first string leaks (connection mempool, so bounded) and only the last line's flags and verifier are used. Cosmetic: free before assigning and clear it in the reuse branch. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#306).

### D-111 pgpverify does not check fdopen() and realloc() results
`util/pgpverify.c:218` (`Body = realloc(...)` then `memcpy`), `:391`, `:405`, `:533`, `:572` (`fdopen()` then `fprintf`/`fgets` on the result). On out-of-memory or descriptor exhaustion pgpverify dereferences NULL and crashes (dreaderd treats that as a failed verification, so it fails closed). One-line guards. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-112 strcpy() into sun_path in the filter socket code
`filter/socket.c:76`: `strcpy(sun.sun_path, pathname)` with no length check; a configured unix-socket path longer than `sizeof(sun_path)` overflows the `struct sockaddr_un`. Config only (same class as D-100). Use `snprintf` with a length check. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-113 Cached HEAD and BODY of an article with an LF-only header/body separator
`dreaderd/cache.c:480` ends the header only on a CRLF blank line (`i == b + 2 && map[b] == '\r' && map[b+1] == '\n'`). A blank line that is a bare LF (`i == b + 1`) never clears `inHeader`. HEAD (`cache.c:371-376`) then prints the whole article, and BODY (`cache.c:398-399`, prints only when `inHeader == 0`) prints nothing. Affects articles stored with LF-only line ends. Wrong reply from the cache for such articles, no crash. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed in #173: the header of a cached article also ends at a bare-LF blank line. Test: tests/e2e/d113_cache_lf_separator_test.go.

### D-114 egets() loses the prefix of an overlong line
`lib/buffer.c:372-399`. When `bgets()` reports a full buffer without a newline it has set `bu_Beg = bu_NLScan = bu_End` (`buffer.c:198-204`). `egets()` copies `bu_End` bytes into a larger block but leaves `bu_Beg == bu_End`, and the next `bgets()` sees that and resets all offsets to 0 (`buffer.c:140-142`), so the copied prefix is dropped and the returned line is only the tail. The caller is `util/diablo.c:1461-1463` (the control pipe, one SOUT line per call). If a SOUT line can exceed the page-sized buffer (a long Newsgroups list), the master sees a tail that does not start with `SOUT` and the article is silently not fed. The mechanism is exact; whether a real SOUT line can reach that length was not checked (Newsgroups length cap). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed in #166: egets() keeps the prefix of an overlong line. Test: tests/e2e/sout_overlong_test.go.

### D-115 dnewslink flushCommandBuffer() can spin on a zero write
`util/dnewslink.c:2974-3019`. `r = write(cfd, Cmd + b, Cmde - b)`; a return of 0 is not treated as special (`b += r` at `:2989`), so `b` does not move. The `poll()` at `:3004` only leaves the loop on a timeout, so a socket that keeps returning 0 would loop without a timeout. Same pattern as D-97 (`writeAll`). A blocking Linux socket does not normally return 0, so practical reach is low. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-116 dnewslink writeLarge() reports a write timeout as success
`util/dnewslink.c:3067-3104`. `NDEBUG` is defined at `:50` (before `<assert.h>` at `:51`), so `assert(bytes == 0)` at `:3102` is compiled out. On a poll timeout the code sets `WritesLosing` and breaks at `:3084` with bytes left, then returns the last `writev()` status, which is 0 after an EAGAIN (`:3061`) or the last partial count. The caller at `:2260` (`if (writeLarge(...) < 0)`) treats any non-negative value as success, so the article's result is wrong for that record. The connection is marked losing, so the next command fails. What the code does after success (whether the record is refiled) was not checked. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#306).

### D-117 KPDBSort() is not synchronized with readers
`lib/kpdb.c:1286-1361`. `KPDBSort` takes no lock of its own and rewrites each record's offset field with one 8-byte write per record (`:1341-1361`). `KPDBLookup` locks only on a miss with `KP_LOCK` (`:1120-1122`), and the offset check at `:1163` accepts stale offsets that are in range. A reader that looks up a key during a resort can take a wrong branch and report not found for an existing key until the sort ends (a false hit is not possible, the final key compare is exact). The sort runs from `KPDBWrite` (every 128 appends), and D-90 measured it at about 51 s on a slow disk. Transient "no such group" style errors, no corruption. The caller's append lock (`kpdb.c:804-805`) and the read lock (`:261`) were not re-traced. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#301): the sort already runs under the append lock (offset 4) in all three callers (`KPDBWrite`, `KPDBReSort`, the open), but readers do not take it. A lookup that misses while the "needs sort" marker is set now waits once for the append lock and searches again. Traced: the marker is set before the first offset is stored and cleared after the last, so a miss seen with the marker clear (read after the search) cannot come from a half rewritten table. A process that holds the lock itself (the sorter, `KPDBWrite`) does not wait. Test: `TestKPDBLookupDuringSort` (stops the sorter right after its marker write, with half of the offsets stored; 114 of 600 keys missed on main, 0 now).

### D-118 KPDBWrite() with a NULL token: the documented behaviour does not exist
`lib/kpdb.c:636-640` says a NULL token unlocks a record without writing and creates a record if the key is missing. The code does neither: `strlen(tok)` at `:870` (copy-old path) and `sprintf("%s", tok)` at `:912` (new record) use the NULL, and a same-length replacement at `:744-766` can write over the key if `KPDBGetField(NULL)` returns it (that last point was taken from the report, not re-traced). Every current caller passes a token, so this is latent. Fix the comment or handle NULL. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#301): a NULL token writes nothing and creates nothing: `KP_UNLOCK` unlocks the record, `KP_LOCK` looks it up and locks it, otherwise it only looks it up; returns 0 if the record exists, -1 if not. The comment says so. All callers in the tree pass a token. Test: `TestKPDBWriteNullToken`.

### D-119 ArticleFileName builds a relative path for an unconfigured spool
`lib/spool.c:179-181`: `GetSpoolPath()` returns `""` when the spool object is missing (spool number below `MAX_SPOOL_OBJECTS` but not configured); only numbers at or above `MAX_SPOOL_OBJECTS` give `/dev/null` (`spool.c:150-151`). `ArtFileName` (`spool.c:94-108`) then builds `D.%08x/...` with no leading directory, so dexpire, dcancel and dreadart open or unlink that name in the current directory. Trigger: a history record that refers to a spool number the admin has removed from `dspool.ctl`. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-120 Unknown options inside spool and metaspool blocks are only logged
`lib/spool.c:764-767` (spool block) and `:869-872` (metaspool block) log "Unknown ... option" and go on; an unknown top-level definition exits (`spool.c:707-710`). A typo in `minfree`, `maxsize`, `keeptime`, `spooldirs` and so on is ignored and the spool runs with the default (for example no free-space floor). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#306).

### D-121 GetSpoolFromPath is dead code with a stray printf and a prefix match
`lib/spool.c:198` prints `XX:%s:%s:` to stdout on every iteration; `:199` compares `strlen(so_Path)` bytes, so "P.01" also matches "P.011". No caller in the tree (only the prototype at `spool.c:43`). Remove it or fix it. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-122 KP files above 2 GB wrap the int offsets
`lib/kpdb.c:800` (`int nlockoff` from `lseek`) and `:289` (`kp_MapSize = (int)st.st_size`). Between 2 and 4 GB the append offset goes negative (lock failure, then a controlled `KPDBFatalIO` exit); past 4 GB it could wrap to a wrong positive offset. KP files are small control databases and the format itself holds 8 hex digits, so this is a design-limit note. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#301), as a limit and not a widening: a file at or past 2 GB (`INT_MAX`) is refused with a log line, a message on stderr and exit 1 when it is opened, and an append that would pass it is refused before anything is written (same fatal path as the other KPDB write failures; a returned error would be ignored by callers such as `GenerateXRef`). Also found while testing near the limit: the binary search computed `(b + e) >> 1` in int and crashed for a file within a few bytes of 2 GB; now `b + ((e - b) >> 1)`. Test: `TestKPDBTooLarge` (sparse files; the append case takes about 15 s and is skipped with `-short`).

### D-123 AUTHINFO USER accepted again after a passwordless authentication
`dreaderd/nntp.c:92-94` answers 502 "Already authenticated" only when `dr_AuthPass[0]` is non-empty. A connection authorized by host or address in `dreader.access` with no password has an empty `dr_AuthPass`, so `AUTHINFO USER x` falls through to `:104-111`, answers `381 PASS required` and replaces `dr_AuthUser`. The following PASS re-verifies the new identity, so this is not an access bypass; it deviates from RFC 4643 (502 expected) and is a leftover of D-46. What state a passwordless connection starts with was not traced (that part is the reviewer's claim). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Checked against RFC 4643 section 2.2: a connection allowed by its address has not authenticated, so AUTHINFO is right there (INN does the same). The real gap is a login without a password: the only one is an authdef's ident + user match, which leaves `dr_AuthPass` empty, so such a connection could log in again. Fix: a connection flag set with the 281 decides "already logged in" (502).

Fixed (#297).

### D-124 CAPABILITIES with an argument answers 101
`dreaderd/reader.c:1393-1398` (`NNTPCapabilities`) never uses `pptr`, so `CAPABILITIES foo` gets the capability list; RFC 3977 section 5.2 wants 501. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Checked against RFC 3977 section 5.2.2: an argument that is a keyword the server does not know must get the normal 101 list, so `CAPABILITIES foo` was right; only an argument that is not a keyword (or more than one) must get 501. Fix: that check.

Fixed (#297).

### D-125 Reader-only command on a transit connection answers 500, not 502
`dreaderd/reader.c:1289-1316`: the dispatch loop skips entries whose flags do not match the connection type and falls through to `NNUnknownCommand` (500 "What?"). RFC 3977 section 5.3 wants 502 for a command that is known but not available. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Checked against RFC 3977 section 3.2.1: 502 means a new connection with other access is needed, and must not be used for "available after MODE READER". Fix: 502 for a transit command on a reader connection and for a reader command on a feed-only connection; a feed connection that may still use MODE READER keeps 500.

Fixed (#297).

### D-126 strtoll() overflow into artno_input()
`dreaderd/nntp.c:422` passes `strtoll(artid, NULL, 10)` to `artno_input` (`dreaderd/subs.c:103-119`) with no `errno` check. `subs.c` rejects values above 0x7FFFFFFF only for RFC 3977 numbering; for window numbering it adds a non-zero delta, so `LLONG_MAX + delta` is signed overflow (undefined behaviour). The delta is non-zero only with window numbering and a group whose high article number is at least 1.5e9 (`subs.c:27-49`); in smaller groups it is 0 and the client simply gets 423. Theoretical. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-127 dicmd splits lines longer than 255 bytes
`util/dicmd.c:19` (`char buf[256]`), `:51-64` and `:84-91`: `fgets` splits a longer line, and each piece is sent to the server or printed as its own line. Not a hang: the `".\n"` terminator test cannot match a split piece. Cosmetic for very long lines. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed (#306).

### D-128 dkp parses the second piece of a long input line as a new record
`util/dkp.c:148-149`: `fgets` into a 1023-byte buffer and `strtok` on it. A longer line is split and the second piece is parsed as a new record, so a long key or value is written as truncated records. Data-quality issue for admin input. The trim mode has a related problem, see D-134. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed in #288: `dkp` reads a stdin line of any length as one record (a growing buffer instead of `fgets()` into 1024 bytes). Test: `TestDKPStdinLongLine`.

### D-129 dclient is IPv4-only (gethostbyname)
`util/dclient.c:61-73`: `connectnews()` uses `gethostbyname()` and a `struct sockaddr_in`, so it cannot reach an IPv6-only host. The `memcpy` of `h_length` bytes into the 4-byte `sin_addr` is safe for AF_INET results. Modernization: `getaddrinfo()`. Limitation of a test client. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed in #298: `connectnews()` uses `getaddrinfo()` and tries each address in order.

### D-130 NEWNEWS and NEWGROUPS ignore the distributions argument
`dreaderd/xover.c:53-54` and `:76` store the distribution pattern in `co_ListPat`, but the NEWNEWS output path (`xover.c:445-449`, `:591-595`) and `dreaderd/list.c:663-674` (NEWGROUPS) never apply it; `co_ListPat` is read in the output path only at `xover.c:583` (the XPAT case). Without the `GMT` token the fourth argument is taken as the GMT field (`xover.c:53`, `list.c:666`). The 501 text advertises `[<distributions>]`. RFC 3977 lets the server ignore it; the behaviour does not match the usage text. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed in #334: the fourth word never changed the time zone (`SetTimeRestrict()` takes only `GMT`, in any letter case, as UTC), so the dates were right. Both commands now read `GMT`, then an optional RFC 977 `<distributions>` list in angle brackets, which they accept and ignore (no filter; RFC 3977 removed the argument); the 501 text and README.READER say so. An unbracketed list or an extra word gets 501, as INN answers 501 to a word other than `GMT`. Tests: `TestNewgroupsLegacyDistributions`, `TestNewnewsLegacyDistributions`.

### D-131 dclient does not know the 202 reply of LIST EXTENSIONS
`util/dclient.c:117-124` (`MultiLineReply`) lists no 202, while `dreaderd/list.c:138-141` answers 202 followed by HDR, OVER and the dot. dclient prints "Unexpected response from server" and exits 2. A fix with a test is ready on go-while's side (one-line `case 202`, test `TestDclientListExtensions`). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-132 Config dump prints the bare-CR option under the NUL name
`lib/config.c:892` prints `"rejectartswithnul: %d"` for `DOpts.RejectArtsWithBareCR`, so `dicmd config rejectartswithbarecr` shows a wrong label and the NUL option appears twice in a full dump. A fix with a test is ready on go-while's side (`TestDicmdConfigRejectArtsWithBareCR`). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-133 LIST MODERATORS adds an extra CR to a CRLF line
`dreaderd/list.c:523-528`: only the trailing LF is stripped, so a CRLF line from the moderators file keeps its CR and then gets `"\r\n"` appended (`\r\r\n` on the wire; the e2e reader flags it as a bare CR in the line). A fix with a test is ready on go-while's side (`TestReaderListModeratorsCRLF`). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-134 dkp trim mode keeps the tail of a long deleted line
`util/dkp.c:106-115`: the trim loop reads with `fgets` into `tmp[1024]` and drops lines that start with `-`. For such a line longer than the buffer, the first chunk is skipped by the `continue` at `:108-109` before `lnl` is updated, so `lnl` stays 1 and the next chunk (the rest of the same line) is taken as a new line start and written out unless it happens to start with `-`. So part of a line meant to be deleted stays in the KP file. Data quality only (the admin's own KP file). Fix: update `lnl` before the `continue`, or skip until the newline. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

Fixed in #257: `dkp -t` copies byte by byte and drops a deleted record up to its newline, whatever its length; a failed trim is never installed. Tests: `TestDKPTrimLongDeletedRecord`, `TestDKPTrimNULByte`.

### D-135 Maintenance scripts hide failed jobs behind their END message
`samples/adm/hourly.expire:27-29`, `samples/adm/daily.reader:22-42`, `samples/adm/weekly.reader:22-30`, `samples/adm/daily.atrim:21-31`: a failed maintenance command is followed by a successful END echo, so the script returns 0 rather than the job's failure. The sample crontab enables hourly expiry and daily rotation; the reader jobs are opt-in. Low: monitoring that checks exit status misses failed expiry, cleanup or rotation. Preserve the command status through the final message. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-136 rot accepts an invalid retention count and still rotates the current file
`samples/adm/rot:7-23`: an empty or nonnumeric first argument makes the numeric `-gt` test fail, skipping the old-copy rotation, but the script still moves the current file to `.0`. When that move succeeds the script returns success, potentially replacing the previous `.0` without retaining it. Low, admin-input validation. Reject an invalid count before touching any file. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-137 biweekly.atrim does not return a failed active-file trim
`samples/adm/biweekly.atrim:39-47`: only dhisexpire's exit status is saved and returned. If dhisexpire succeeds and the subsequent `dkp -t` fails, the script still exits 0 and its END message reports only the successful history rebuild. Low: the active-file maintenance failure is missed by exit-status monitoring. Preserve either failure while still distinguishing the two operations in the log. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-138 Daily rotation writes its completion message into daily.log.0
`samples/adm/daily.atrim:21-31`, `samples/adm/crontab.sample:19`: cron opens daily.log before starting the script, and rot renames that file to daily.log.0. The script's open output descriptor still points to the renamed inode, so its BEGIN and END messages are in daily.log.0 and daily.log does not reappear until the next append. Low, cosmetic log-location surprise, not lost output. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-139 Auto-loaded diablo-ctl.conf can mix two installations' paths
`samples/adm/diablo-ctl:74-79`, `:116`, `:124`, `:202-210`, `:237-238`, `:287-288`; `samples/adm/diablo-ctl.conf:3`, `:18-20`: the automatically loaded home configuration is sourced after home, logdir and dqueue have been obtained from the detected DIABLO_BIN. Its DIABLO_BIN override is honored by later commands but is not revalidated, so an override naming another installation mixes the new binaries with the previously derived paths. Low, configuration consistency; the override is not a no-op. A configuration passed with `-f` is read before detection and does not have this late-binding problem. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-140 Unknown DIABLO_DAEMONS entries reuse command state or abort the wrapper
`samples/adm/diablo-ctl:81-95`, `:111-117`, `:283-314`: DIABLO_DAEMONS is not checked against the supported daemon names. An unknown entry can reach `up()` without assigning cmd, causing a `set -u` abort, or reuse cmd and status line from a previous entry and report a misleading result. Start, stop and status also handle such entries inconsistently. Low, malformed admin configuration. Validate the daemon list before invoking commands. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-141 Nonnumeric diablo-ctl wait settings disable the timeout
`samples/adm/diablo-ctl:84-85`, `:179-187`, `:213-221`, `:241-249`: DIABLO_START_WAIT and DIABLO_STOP_WAIT go directly into numeric `[` comparisons. A nonnumeric value makes those comparisons error and return false on every iteration, so a daemon that never answers or a process that never exits can leave the wrapper waiting indefinitely instead of timing out. Low, invalid admin settings. Validate nonnegative integer values before starting or stopping anything. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-142 Reinstall resets existing operational directory modes and owners
`install-portable.sh:31-38`, `:48-57`: make_dir applies chmod and ownership changes even when the directory already exists. Reinstall therefore resets admin-chosen modes on the existing spool and queue directories to 0755 (log and run to 0750), as well as restoring the requested owner and group. Low, local installation-policy/operability issue. Preserve existing directory settings or make resetting them an explicit operation. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-143 Direct installation from an unbuilt checkout can succeed without binaries
`install-portable.sh:76-79`, `:128`: when dbin contains no built files, the literal unmatched `dbin/*` is skipped by the `-f` guard. The script still prints installation complete and leaves an empty binary directory. Low: direct script invocation only; make install builds first. Require the expected binaries or explain that the checkout must be built before running the installer directly. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451.

### D-144 Installer pipelines hide failed source-tree scans
`install-portable.sh:81-95`, `:146-157`: each `find | while` pipeline returns the loop's status, not find's. A failed or partial source-tree scan can therefore be followed by an install-complete or uninstall-complete message, even under `set -e`. Low, partial operation reported as success. Capture and check the scan status rather than relying on the pipeline's final command. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-145 build-on-host requires the optional PRETTY_NAME field
`tests/e2e/ci/build-on-host:16-19`: after sourcing /etc/os-release the script expands PRETTY_NAME directly under `set -u`. A valid os-release without that optional field aborts before package setup or the build; a missing os-release also aborts at the source command. Low, test-host robustness. Use a fallback name and diagnose an unsupported or missing platform file explicitly. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-146 Daemon option strings undergo pathname expansion
`samples/adm/diablo-ctl:197-211`: DIABLO_OPTS and DREADERD_OPTS intentionally undergo shell word splitting, but also pathname expansion after the script changes into path_home. A literal wildcard in an option value becomes matching directory entries when any exist, so the daemon receives different arguments depending on that directory's contents. Low, configured-option correctness. Disable globbing around option expansion or use an argument representation that preserves literal values. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-147 OVER and HDR do not reject an invalid converted range bound
`dreaderd/xover.c:251-290`, `:326-350`, `:376-404`; `dreaderd/subs.c:104-118`: strict range validation checks the parsed number before artno_input(), but does not reject the -1 that conversion returns for numbers above the RFC 3977 limit. An open-ended oversized low bound can pass validation, be clamped to the group's low mark and return the whole group instead of an error. An open-ended zero low bound is also accepted. Low, protocol/range correctness; distinct from the high-mark case fixed under D-47. Validate converted bounds and the positive-number syntax before emitting success. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-148 LISTGROUP accepts a failed article-number conversion
`dreaderd/list.c:570-619`: LISTGROUP validates the initial strtoll result but not the -1 returned by artno_input() for an oversized RFC 3977 number. An oversized low bound can leave the group's original low mark in place and list the whole range; an oversized high bound can set the end below the start and produce an empty list. Low, range correctness after the D-44 fix. Reject conversion failure rather than using it as an ordinary bound. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-149 NEWNEWS clears the previously selected group
`dreaderd/xover.c:86-105`: NEWNEWS uses co_GroupName while walking its groups and frees it when the final continuation completes, without restoring the group that the client selected earlier. A following group-dependent ARTICLE request then gets 412 instead of using that selected group. Low, connection-state correctness; requires allownewnews to be enabled. Keep the scan's group separate from the client's selected group, or preserve and restore the selection. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-150 doverctl convert leaves compressed overview data in the old layout
`util/doverctl.c:302-329`, `util/dexpireover.c:1639-1644`: conversion builds and stats only the plain overview-data filename, never its .gz variant. After overview compression and a hash-layout conversion, compressed files remain under the source layout and are unreachable under the destination layout, although the overview index has moved. Medium, article-overview availability/data loss after the hash switch; the old compressed files remain on disk. Convert both supported filename forms, including in dry-run output. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-151 doverctl convert -m ignores failed file moves
`util/doverctl.c:290-291`, `:317-329`, `lib/subs.c:861-898`: the copy-and-unlink mode discards MoveFile's failure result. If moving the over file fails, conversion can still move the data files; if moving a data file fails, that range remains behind. The group can still be reported as Converted. Medium, incomplete conversion and unavailable overview data after switching hashes. Unlike the rename branch, this path does not stop on failure; check the result and do not report success for an incomplete group. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-152 doverctl clean removes nonregular files using the wrong path
`util/doverctl.c:474-508`, `:522-529`: scanDirectory has already changed into dirname, but passes dirname again to DeleteFile for a nonregular entry. The resulting relative dirname/name targets a directory below the current one rather than the entry being scanned. The failed remove is ignored and RemovedFiles still increases. Low: unwanted entries remain while the summary claims removal. Use the correct path and count only successful removals. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-153 doverctl clean keeps misaligned overview data after an entry-size change
`util/doverctl.c:629-643`, compared with `util/dexpireover.c:960-965`: clean tests only whether a data file's article range is outside the group. It lacks dexpireover's artBase alignment check against dataEntries, so obsolete misaligned files left after changing eDATAENTRIES can be retained indefinitely. It also lacks the explicit start-greater-than-end guard. Low, stale disk usage and disagreement between cleanup tools. Apply the same stale-file criteria in both. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-154 dexpireover's parallel parent spins after a failed fork
`util/dexpireover.c:326-358`: fork returning -1 is stored as if a worker had been started, but remaining is initialized to the requested ParallelCount. Once all real children have exited, wait3 returns ECHILD and the outer while(remaining) immediately repeats forever. Medium, a stuck CPU-consuming maintenance run after process or memory exhaustion with parallel expiry enabled. Count only successful forks and handle fork/wait errors explicitly. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-155 dexpireover counts failed compression as completed
`util/dexpireover.c:1000-1002`, `lib/dgzip.c:192-254`: the return value of diablo_minigzip is ignored and CompressedFiles increases even when compression fails, for example when its temporary output cannot be created. Low, misleading maintenance summary and an unreported compression failure. Count successful compression separately and report failures; distinguish dry-run eligibility counts from completed work. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-156 A failed free-space query can drive excessive spool expiry
`util/dexpire.c:1332-1368`, `:1297-1305`, `:641-675`, `:699-737`: freeSpaceOn logs a statfs failure but returns 1 as a normal free-byte count and leaves the free-inode output unset. Cleanup treats that as insufficient free space, so failed queries can cause unnecessary expiry until only the protected newest directory remains; inode-based decisions can also use unset or stale data. Medium, article data loss on a filesystem-query failure, not a failure of the newest-directory guard. Propagate the query failure, initialize outputs and stop destructive cleanup when free space is unknown. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-157 Failed spool removal can still expire the directory's history entries
`util/dexpire.c:524-588`, `:722-737`, `:994-1020`, `:1143-1160`: cleanup unconditionally marks a directory removed and schedules history updating even when removeDirectory could not enter it or failed to remove article files. updateHistory excludes that directory from its index and can mark its records expired although their files remain. The same optimistic counts can also misstate freed space. Medium, history/article inconsistency after filesystem or access errors during real expiry. Return and check removal success before excluding a directory from the history index. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-158 Empty spool-home shorthand makes dexpire use the wrong partition path
`lib/spool.c:738-743`, `:930-949`, `util/dexpire.c:389-409`, `:994-1020`, `:1143-1160`: a spool path of `%s/` is normalized to an empty so_Path, which other spool path handling recognizes as the spool home. GetFirstSpool returns that empty value unchanged; dexpire's trailing-slash handling does not handle an empty string and can construct `/` instead of the spool-home path. The scan then misses the real spool, and a forced history update can mark valid records expired because their directories were not indexed. Medium, configuration-specific wrong-directory scanning and history data loss. Resolve the empty shorthand to the spool home before constructing the partition path. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run. Checked and not reachable: `addSpool()` (`lib/spool.c:463`) turns an empty path into the spool-home pattern before dexpire sees it (go-while agreed on the board); the empty-path guard in `util/dexpire.c` stays, it also removes a latent `path[-1]` read. Won't fix.

### D-159 Long whitespace before a config comment can reject an otherwise valid directive
`lib/config.c:192-215`: the long-line check accepts a truncated line only if a # is already present in the first buffer. A valid directive followed by enough whitespace to push its comment marker beyond the 255-character chunk is therefore rejected as too long, although its meaningful command fits. Low, narrow continuation of D-82's long-line handling, not ordinary short-command/short-comment input. Determine whether the drained tail is only whitespace and a comment before rejecting the command. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-160 Access-cache loading treats a valid descriptor zero as an open failure
`dreaderd/dns.c:3324-3327`, `:3340-3368`: the access-lock and cache-file open results are tested with <= 0 instead of < 0. With stdin closed, the lock open can legitimately return 0; the loader logs a failure and returns without closing that descriptor or loading the caches. This can abort a load attempt, rather than proving all subsequent attempts fail. Low, descriptor-handling correctness; distinct from D-102's failed-open cleanup issue. Accept descriptor zero and close successfully opened descriptors on actual failure paths. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-161 Several readerdef numeric settings silently accept malformed values
`dreaderd/dns.c:2984-3010`, `:3029-3043`: maxconn and its per-host/user/group/vserver variants, ratelimittax, bytelimit, pathcomponents, idletimeout and sessiontimeout use atoi without checking conversion or the accepted range. Nonnumeric input becomes 0, a numeric prefix with junk is accepted, and overflow is not diagnosed. Low, admin misconfiguration; validate the complete value and option-specific bounds. This does not claim ratelimitrange lacks range checking: it logs and resets values outside 1..200 at `:3012-3026`. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-162 dserver.hosts numeric options lack complete conversion validation
`dreaderd/server.c:158-197`, `:210-215`, `:242-255`, `:276-283`: numeric port/priority/timer flags and cache settings are parsed with strtol or strtod without conversion-error or overflow checks. For example cachemin=abc becomes 0 and cachemax=12junk accepts 12; invalid values can silently change the intended configuration. Low, admin-input correctness. Validate each option's numeric grammar and bounds; the flag string intentionally combines letters and numbers, so its parser must retain that syntax. txbufsize and rxbufsize already call ValidateTcpBufferSize, unlike the plain cache-size/time assignments. Found by go-while's reviewers, each checked by a second reviewer in the code at main 0401451, not run.

### D-163 Cache and state files were created with the process umask
dreaderd's cache files (`dreaderd/dns.c`; they copy what `dreader.access` holds: the AUTHINFO passwords, `cryptpw` and the Cancel-Lock secrets), `.accesslock`, the logs (`lib/log.c`, dnewslink's feedlog, the control logs) and `dreaderd.status` were created with `fopen()` and the process umask, 0644 under the usual 022, and `install-portable.sh` made the log and run directories 0755. A local user who could reach the directory could read the passwords (verified: `cache.authdef` held a test password at mode 0644). Fixed in #139: the cache files and `.accesslock` are created 0600, logs and status 0640, the log and run directories 0750, whatever a lax umask says (a stricter one narrows them further; existing files keep their mode). These modes have always been like this in the public source tarballs, so this is ordinary hardening, disclosed as it is. Found while adding the Cancel-Lock secrets (F-01) to the same files.

### D-164 A misspelled group-list name in a readerdef falls back to the first list
`dreaderd/dns.c`, `SetAuthDetails()`: the `groups`, `listgroups` and `postgroups` settings of a readerdef name a group list. When the name is not found the code takes the first group list, which is usually "everything", instead of denying. A typo in the name (or a list that was removed) gives the clients of that readerdef more groups than the admin meant. Low, admin misconfiguration with a permissive result; a missing list should deny, or refuse the file as a bad number now does. Found by DeepSeek in go-while's review of #160, not run.

### D-165 dreaderd exits when one access-cache file cannot be opened
`dreaderd/dns.c`, `ReadAccessCache()` and its callers: the five cache files are opened together; if one of them cannot be opened the load fails (`-2` when there is no older map) and dreaderd exits, instead of keeping an older map or serving what it can. Low, a damaged or momentarily unreadable cache file stops the daemon. Found by DeepSeek in go-while's review of #160, not run.

### D-166 A misspelled readerdef option is logged but the file is still installed
`dreaderd/dns.c`, `InstallAccessCache()`: an unknown option inside a readerdef is logged as invalid, but the rest of the file is installed, so the readerdef runs without the setting the admin meant to make (a typo in a limit or a deny). Since the numeric options are validated as of #160 this is the remaining way for a typo to change what a readerdef does silently; refusing the file as for a bad number would match. Low. Found by DeepSeek in go-while's review of #160, not run.

### D-167 Perl authentication uses only the first script a worker loads
`dreaderd/dns.c`, `PerlAuthenticate()`: each DNS/auth worker creates its embedded perl interpreter once, on its first Perl login, and parses the script path of the authdef that login came through; every later login in that worker calls `checkuser` of that first script, whatever `perl <script>` its own authdef names. With two authdefs that name different scripts, logins through the second authdef are answered by the first script (a refusal, or a wrong accept if both scripts know the user), and which script wins depends on which authdef a worker sees first, so with `readerdns` above 1 the result can differ from one login to the next. The script is also read only once per worker, so an edit takes effect only when the workers restart. Low (two Perl authdefs are unusual), but silent. Verified with a test on the Perl build of #167: two authdefs with two scripts, one reader process; the user of the second script is refused when the first script is used first, and the other way round when the order is reversed. A fix would keep one interpreter and load each script on first use into its own package, or one interpreter per script path. The same code never frees its interpreter, which is fine for a worker that runs until dreaderd stops.

Fixed in #169: each distinct `perl <script> [arguments]` line has its own interpreter in a worker, and a script that fails to load refuses its logins. `TestReaderHtpasswdAuth` checks two authdefs with their own interpreters. Each script is still read once per worker, so an edit takes effect when the workers restart.

### D-168 A `file !` deny list leaks a file handle for every listed user that logs in
`dreaderd/dns.c`, `fileAuthenticate()`: with a path that starts with `!` (an authdef `file !/path` is a deny list), the function returns `NULL` as soon as it finds the user in the file, without `fclose()`. Every login of a listed user leaves one open file in the DNS/auth worker. Verified on Debian with a deny file that lists a user: after 400 logins of that user one worker has 407 open descriptors, up from 7, and an unlisted user does not leak. With a limit of 1024 descriptors the worker runs out of descriptors after about a thousand refused logins and fails every later authentication until it is restarted, so a listed user (or a script) that keeps trying can lock everyone out. Low to medium. The fix is one `fclose(fp)` before that return. Fixed in #185 (`TestReaderFileDenyListFdLeak`).

### D-169 The `cdb !` deny list does not work and rejects every user
`dreaderd/dns.c`, `cdbAuthenticate()`: the `!` in front of the path marks a deny list, but unlike `fileAuthenticate()` the function does not step over it, so `open("!/path/users.cdb")` fails, the error "unable to open auth cdb" is logged, and the caller treats the `NULL` as "listed" and rejects. Verified on a build with cdb (tinycdb) and an authdef with `cdb !<file>` plus `user`/`pass`: a user who is not in the cdb file is refused (481), and so is a listed one, so the deny list refuses everybody, and the user/pass of the same authdef can never log in. Low (it fails closed, and `cdb !` is probably rarely used). Once the `!` is stepped over, the early `return(NULL)` for a listed user leaves the file descriptor open, the same leak as D-168. The fix is `conf++` after the check and a `close(fd)` before that return. Fixed in #185 (`TestReaderCdbDenyList`).

### D-170 On illumos a hand-back lost at the descriptor limit stays counted
`dreaderd/msg.c`, `RecvMsg()`, and its loop in `dreaderd/main.c`: a reader fork hands each finished connection (and each AUTHINFO login) back to the main process as a DnsRes with the client descriptor. When the main process is at its descriptor limit, the kernels differ. A probe (a socketpair, one descriptor sent, the table filled with `dup()` until EMFILE, then `recvmsg()`, run on 2026-11-03) found:
- Linux 6.12: the data arrives with MSG_CTRUNC and no descriptor. Since #266 the connection is released (`TestReaderHandbackFdLostReleased`).
- FreeBSD 15.1 (SEQPACKET and STREAM): -1/EMFILE first, and the message stays queued. The next `recvmsg()` gets the data without the descriptor and without MSG_CTRUNC, and the `recv_fd < 0` branch releases the connection, with one "recvmsg error" log line (the `emfile` case of the same test). With DGRAM the message is gone.
- illumos (SmartOS): -1/EMFILE, and the whole message, data and descriptor, is gone for all three socket types. The main process never learns which connection ended, so `NumActive` and the reader's `d_Count` (and the per-host and per-user entries) stay counted until a restart.

Low: since #266 the main process keeps headroom below its limit (queued connections to 3/4 of it, AUTHINFO to the limit minus 32), so this needs the descriptor table to be full anyway. A full fix would be a reconciliation: on EMFILE the main process asks each reader for its live connections and resets its counts. Do it if `Act` drift shows on an illumos server.

Fixed in #336, without a reconciliation: the main process keeps a spare descriptor (`/dev/null`) and closes it just before each `recvmsg()` of a hand-back, so the descriptor always has a free slot, on every system; it opens the spare again afterwards. While the spare cannot be opened, the readers are left out of `select()` until a descriptor is free, so no hand-back is read at the limit. Test: `TestReaderHandbackFullTableKept` (a shim fills the main process's table and does what illumos does when no slot is free). The MSG_CTRUNC and EMFILE handling from #266 stays.

### D-171 On OpenBSD readers do not see writes through their shared maps
History, the KPDB files (`dactive.kp`) and overview are read through `MAP_SHARED` mappings and written with `write()`/`pwrite()`. OpenBSD has no unified buffer cache, so another process's map does not see those writes. In CI on OpenBSD 7.9 (from #268 on): an article just accepted is not found (`TestDexpire`, STAT 430); a second IHAVE of a stored article gets 335 instead of 435 (`TestPeerIHAVE`); a KPDB update leaves two live records for one key (`TestKPDBConcurrentUpdate`); and most reader tests time out waiting for a group or an article. That is 70 tests at #272 (96a15fb). The KPDB, precommit and spam-cache map switches (`USE_KP_RW_MAP` and the others, on since #272) do not change that. A fix would have writers update through the shared map or readers use `pread()`, a design change the maintainer decided against (2026-11-03). Won't fix: OpenBSD builds but is not supported. CI only builds it (the `openbsd` job in `ci-matrix.yml`, no e2e run), so a build break still shows.

### D-172 Feed children and programs diablo runs inherit retained control connections
`util/diablo.c`, `DoCommand()`: for `pause`, `readonly` and `exit` the master keeps the `dicmd` connection (the `Retain` list) and answers it when the operation is done. The connection had no close-on-exec, so `path_shutdown_cleanup` (run with `system()` while an `exit` waits) and any program it leaves running kept a copy; a client that reads to the end of the connection, not only to the "." line, waited for that program. A feed child forked while a `readonly` waits closes only the descriptors below `MaxFds`, which need not include the retained ones. Found by go-while from reading. Fix: close-on-exec on both descriptors of a control connection, and the feed child closes its copies of the retained ones. Test: `TestControlConnectionNotInherited`.

### D-173 NETREMOTE session accounting goes to a placeholder host
`dreaderd/dns.c`, `netRemoteAccounting()` (only with `NETREMOTE_ENABLED`, off in `lib/vendor.h`): at every session end of a user who logged in with a password, the DNS worker sends the session's accounting to the hard-coded `Password@accounting.server:8998`, not to the `netremote` server of the authdef the user logged in through. Found by go-while from reading. Before #292 NETREMOTE did not compile with OpenSSL 1.1 or later (the old `des_*` names), and the lookup, connect and read had no timeouts; #292 maps the names to the `DES_` calls and bounds the exchange with `readerauthtimeout`. The maintainer keeps NETREMOTE now that it builds (2026-11-03). Left: the accounting destination (use the authdef's `netremote` setting, or send nothing without one).

Fixed (#309): the session end goes to the `netremote` server of the authdef the user logged in through (the main process passes the readerdef name with the report, and the DNS worker looks up its authdef); without one nothing is sent. Test: `TestNetRemoteAccountingToAuthdef` (needs dreaderd built with `NETREMOTE_ENABLED`, skipped otherwise; run here with it on: fails on main, passes with the fix).

### D-174 The reader overview Lines column is empty for articles without a Lines header
`dreaderd/xover.c` builds each OVER/XOVER line from the stored header block: the `:bytes` field comes from the overview record (`oa_ArtSize`, filled from the `Bytes:` line the header feed adds), but the `:lines` field is the stored `Lines:` header and stays empty without one. RFC 3977 8.4.2 says the 8th field MUST be the `:lines` metadata, which 8.1.2 defines as the number of lines in the body, whatever the headers say. Verified on the SmartOS test reader (2026-11-04): 97 of 353 XOVER lines of alt.test have an empty Lines column (articles without a Lines header, for example from a remailer); in a sample of overview data files 160 of 7510 stored headers had none. Readers then show no size for those articles. dreaderd gets headers only, so it cannot count the lines itself.

Plan (maintainer, 2026-11-04: "dreaderd gets :lines from the feeder"):
- diablo counts the body lines while it receives an article and writes them to the queue entry as ` L<lines>`, next to ` H` and ` C<size>` (2026-11-05, Jesse and go-while: counting in dnewslink would read every body, which a header feed otherwise does not touch on an uncompressed spool). Every queue writer keeps the field (dnewslink's requeue and refile writes, drequeue); an older dnewslink ignores it.
- `dnewslink -H` passes the count to dreaderd with the size it already sends, as feed-only data, not as an article header: `Bytes: <size> <lines>`, which an older dreaderd's `strtol()` reads as the size alone. A queue entry without ` L` gives `Bytes: <size>` as before. With `-HB` a stored Bytes line passes through unchanged.
- On a full feed dreaderd counts the body lines itself.
Fixed (#312) as planned; dreaderd keeps the count in the overview record's spare field (`oa_Lines`, the file format is unchanged), and records written before, queue entries without ` L` and header feeds from an older dnewslink keep the `Lines:` header. `dspoolrefeed` writes ` L<lines>` too. Tests: `TestReaderOverviewLinesFullFeed`, `TestReaderOverviewLinesHeaderFeed`, `TestReaderOverviewLinesOldQueue`, `TestReaderOverviewLinesOldHeaderFeed`, and the extended refile and requeue tests.
- dreaderd takes the count out as it does `Bytes:` today, and stores it in the overview record. `OverArt` has an unused field (`oa_UnusedX`, always written as 0), so the files keep their format.
- OVER/XOVER/XZVER and HDR `:lines` use the stored count. Records written before the change (count 0, or the field never set) fall back to the `Lines:` header. LIST OVERVIEW.FMT may then say `:lines` (RFC 3977 8.4.3 prefers it to `Lines:`).
- No `Lines:` header is added to any article (RFC 5536 3.3.1, F-07). `genlines` (`-L`) stays for feeds to other servers that want it, with the sample text corrected: it is not what dreaderd needs.
- Needed before F-07 stops generating Lines in POST, so local posts keep a correct Lines column.

### D-175 A bad dspool.ctl on reload stops a running diablo
`lib/spool.c`, `loadSpoolCtl()`: the master reloads `dspool.ctl` when its mtime changes (`LoadSpoolCtl(t, 0)` in the main loop of `util/diablo.c`), and every error in it calls `exit(1)`: an unknown definition, an unknown spool, a missing group, and since D-120 also an unknown spool or metaspool option. So a typo saved while diablo runs stops the feeder at the next check, with no error shown to the admin who edited the file. At start an error should still stop it. Fix (maintainer, 2026-11-04: queued as the follow-up to D-120): parse a reload into a new set of spool and metaspool objects, install it only when the whole file is valid, and otherwise log the error and keep the old set, as #283 does for `dreader.access`. The other programs that read the file once (dexpire, dcancel, dreadart and others) keep stopping on an error.

Fixed (PR #308): the reload is parsed in a forked child first; on an error the master logs it and keeps the old configuration until the file changes again. At start an error still stops the program.

### D-176 .gz header parse of an overview data file is not bounded by the file size
`dreaderd/group.c` (`MakeOverHFile()`) and `util/dexpireover.c` (`RewriteData()`): the gzip header of a `.gz` data file was walked with no check against the file size (extra field length, file name and comment strings, header CRC), so a damaged or hostile `.gz` made `inflate` read past the mapping. A wrong magic was logged and parsed anyway, and the extra-field length skipped two bytes too few. The files are written by `dexpireover -z`; needs a damaged file. Found in our own audit. Fixed by one bounded header length function in both files; a bad header is reported as an inflate error.
### D-177 dreader.access line longer than the read buffer is parsed in pieces
`dreaderd/dns.c`, `InstallAccessCache()` read `dreader.access` with `fgets(buf[1024])`; the tail of a longer line was parsed as a line of its own (the same class as D-82 and D-94). A line that does not fit is now refused with its line number and the file is not installed (as for other errors); a long comment line is skipped whole. Our own audit.
### D-178 dsyncgroups cuts a long AUTHINFO user name or password
`util/dsyncgroups.c`: `authinfo user` / `authinfo pass` were built with `snprintf()` in a 4096-byte buffer and sent cut. A cut password is another password. Now dsyncgroups says the name or password is too long and exits 1 before sending it. Our own audit.
### D-179 NetBSD: embedded Perl loads no script
With pkgsrc perl on NetBSD 11, dreaderd logs `PERL: cannot load <script> (perl_parse or perl_run failed)` for every script, even a one-line auth script. The 6 Perl e2e tests (TestReaderPerlAuth, TestReaderHtpasswdAuth, TestReaderHtpasswdNoFile, TestAuthTimeoutPerl*) fail there and are skipped on NetBSD with that reason; they pass on Linux and FreeBSD. Not diagnosed: Diablo's embedding or a pkgsrc perl build option. Found by the BSD e2e coverage work (fork CI), not run on a local NetBSD host.

**Status: fixed.** dreaderd never called `PERL_SYS_INIT3()`, which perlembed requires once before the first `perl_alloc()`. Linux and FreeBSD perl work without it; pkgsrc's perl 5.44 on NetBSD (built with `useithreads` and `usemultiplicity`) does not. A probe on fork CI: a minimal embedding program got `perl_parse` = 9 without the call and 0 (and `perl_run` 0) with it. dreaderd now calls it once per process before the first interpreter (`perlSysInit()` in `dreaderd/dns.c`), and the 6 Perl tests run on NetBSD again.
### D-180 NetBSD: reader fork and queue-wait tests are intermittent
In fork CI on NetBSD 11, TestReaderForkKeepsNoQueuedClients failed in 3 of 4 runs (once "only 3 of 60 clients greeted") and TestReaderQueueWaitLimit once ("40 greeted, 0 refused" in 3.5 s). Both pass on Linux, FreeBSD and illumos. Not skipped and no timeout raised; the NetBSD job is continue-on-error. Possible causes: NetBSD timing, the 8 KiB AF_UNIX buffer the shim asks for being ignored, or a bug in the stream-IPC fork path. Needs a NetBSD host to reproduce.

**Status: fixed** (test bug, not dreaderd). TestReaderStoppedDuringLoginBurst failed the same way (#302's CI). The three tests took the lowest child pid as the reader, but NetBSD hands out random pids: in #302's run the children were 4575 (DNS worker), 8030 and 18839 (readers) and 27944 (feed fork), so the test stopped the DNS worker and killed a reader. Every refusal and every "0 refused" follows from stopping the wrong process. The tests now take the reader pids from dreaderd's "forked reader child process N" lines (`readerPids` in `tests/e2e/streamipc_test.go`).

### D-181 readerdns and readerfeedforks take any value
`lib/config.c` and the `-D`/`-F` options of `dreaderd/main.c` read `readerdns` and `readerfeedforks` with `strtol()` and no check. `readerdns 0`, a typo such as `readerdns five`, or `-D bogus` gave no DNS worker at all: dreaderd started, and every client got `400 Server too busy` ("No DNS thread running"). A negative `readerfeedforks` was taken as well. Found while checking what was left of the DNS-queue burst work (mc-report F1-b: its sizing note and the queue that absorbs bursts are on main already).
Fixed: `readerdns` must be 1..1024 and `readerfeedforks` 0..1024, in diablo.config and as `-D`/`-F`; anything else stops dreaderd at startup with an error, as an invalid `readerforks` does. Test: `TestReaderDnsFeedForksInvalid`.

### D-182 KPDB lookup misses wait for the append lock after the resort limit
`lib/kpdb.c`, `KPDBLookup()`: the D-117 fix (#301) waited once on the append lock (offset 4) after a miss whenever the C counter was above `kp_ResortLimit`. But the sort runs only at the start of the next write, so after the write that passes the limit the counter stays above it until then, and every miss in every process (a GROUP or a newgroup check for a group that is not there) took that lock, waited for any writer holding it, and searched again. Not wrong, only slow under writes. Found in our review pass of the code merged since #296.
Fixed (#311): only the marker the sort writes first and clears last (`0FFFFFFF`) means a sort is running; a counter just past the limit leaves an unsorted tail that the search scans anyway. Test: `TestKPDBMissNoWaitPastLimit` (another process holds the append lock; the miss must come back at once). `TestKPDBLookupDuringSort` still passes.

### D-183 History add after a partial last record skews every later index
`lib/history.c`, `HistoryAdd()`: the record index is `(writePos - HEntryOff) / sizeof(History)` with `writePos` from `lseek(SEEK_END)` and no alignment check. A history file that ends inside a record (a crash or power loss during an append) skewed every later record by the same amount: the chain index pointed up to 27 bytes before the real record, lookups read garbage, and a second add of the same Message-ID was accepted, until dhisexpire rewrote the file. Same class as D-80 (KPDB), which was fixed for KPDB only. Found by a review of the feeder history path.
Fixed (#314): under the append lock a tail that is not a whole record is skipped: the new record goes to the next record boundary, the stray bytes are zeroed and become a record no chain points to (gmt 0: dhisexpire drops it as too old, and dexpire, which steps by record size, finds no article for it), and one LOG_ERR line says how many bytes. Rounding up, not truncating, so no complete record can be lost. Test: `TestHistoryAddAfterPartialRecord`.

### D-184 A failed read on a history chain counts as "found"
`lib/history.c`, `HistoryLookup()`, `HistoryLookupByHash()` and `HistoryPosLookupByHash()`: a failed or short `pread()` while following a chain left the loop with a non-zero index, which the functions took as a hit. A chain index past the end of the file (a torn append, index pages that reached the disk before the record pages) made every new article of that bucket be refused as a duplicate for good, and `HistoryExpire`/`HistoryStoreExp` wrote the exp field at the bad offset. `HistoryAdd()` treated the same condition as "not found", so the two disagreed.
Fixed (#314): an unreadable record ends the walk as a miss in all three (the existing LOG_ERR line is kept). Valid chains behave as before. Test: `TestHistoryLookupBadChainIsMiss`.

### D-185 A failed history add is answered as a permanent reject
`util/diablo.c`, commit path: `HistoryAdd()` returns `RCTRYAGAIN` for a full history volume (ENOSPC), a chain lock failure and a failed index write, but the code after `RCALREADY` turned every other value into `RCREJECT`. The peer got 437 or 439, dropped an article that was already in the spool, and did not offer it again, while the spool-open failure a few lines earlier already answered 436/431.
Fixed (#314): `RCTRYAGAIN` is passed on (436 to IHAVE, 431 to TAKETHIS), counted as an I/O error and logged as `HistoryTryAgain`; `RCREJECT` stays for any other value. Test: `TestFeederHistoryWriteFailureTryAgain` (a shim fails the history pwrite with ENOSPC).
### D-188 diablo sleeps 2 s at every shutdown
`util/diablo.c`, the end of the main loop: after the feed children are gone and the logs are flushed, diablo slept 2 s unconditionally before running `path_shutdown_cleanup`, also when none is set. Every stop and restart took 2 s longer than needed, and so did the cleanup of every feeder e2e test (about 85 tests sat at 2.0x s, roughly 170 s of a suite run). Found while measuring the e2e suite's time.
Fixed (#316): the pause stays only before a cleanup program, which may count on it. Test: `TestFeederExitWithoutCleanupIsQuick` (2.0 s on main, 0.03 s here).
### D-189 dexpire writes expiry flags into a swapped history
`util/dexpire.c`, `setExpired()`: dexpire opens the history once at the start of a run and later writes the expired flag of each removed article under its hash chain lock, but it never checked that its open descriptor is still the live history. A dhisexpire that installed a new history mid-run left the descriptor on the old (dead) file, so the flags went to the `.bak` and the new history kept the removed articles live until the next dexpire run. Found in the history review (F4).
Fixed (#321): before each scan batch dexpire compares `fstat()` of the open descriptor with `stat()` of the history path and, when they differ, stops with a clear message to run dexpire again, so no flag goes to the dead file. Test: `TestDexpireHistorySwappedMidScan`.
### D-190 hiscachesize is not checked to be a power of two
`lib/config.c`: `hiscachesize` was passed straight to `SetPCommitExpire()`, which used `hsize - 1` as the precommit cache mask (`lib/precommit.c`). A value that is not a power of two left the table only partly reachable (e.g. 100000 used about 1024 of its 100000 slots). Found in the history review (F6).
Fixed (#321): a non-power-of-two is refused at config load with a message naming the setting, as an out-of-range `readerdns` is. Test: `TestHiscachesizePowerOfTwo`.
### D-191 HistoryOpen does not check the header's entry and header sizes
`lib/history.c`, `HistoryOpen()`: it validated the version and the hash size but not `henSize` or `headSize` from the header, so a history written by a different ABI (or a damaged header) was walked at the wrong record stride, and a header too small for this build's fields was read past its end. `dhisexpire` already refused a bad record size (D-104). Found in the history review (F9b).
Fixed (#321): a version 2 header whose `henSize` is not `sizeof(History)`, or whose `headSize` is smaller than `sizeof(HistHead)`, is refused with a message to regenerate the file. A larger `headSize` stays allowed: the hash table is mapped at `headSize`, so the format lets the header grow. Test: `TestHistoryOpenBadHeaderSizes`.
### D-192 precommit PC_DELCOMM evicts another message-id's entry
`lib/precommit.c`, `PreCommit()`: a `PC_DELCOMM` for a message-id whose hash slot held a different message-id (a collision) wrote a cleared entry into the slot, evicting the other article's still-live precommit/postcommit cache entry. Found in the history review (F7).
Fixed (#321): a `PC_DELCOMM` that does not find its own message-id in the slot leaves the slot alone. Test: `TestPrecommitDelcommLeavesOtherSlot`. (The precommit entries are still read and written without a lock or a seqlock — a reader can see a new hash with the previous time/pid for a moment; only the cache window is lost, the history check still stops committed duplicates, so it is documented rather than fixed.)

### D-186 A refused dspool.ctl reload is not tried again
`lib/spool.c`, `LoadSpoolCtl()`: the D-175 fix (#308) set `SpoolMTime` before the reload was checked, so a file refused for a reason that passes without a change to it (a spool partition not mounted yet: "Missing spool partition") was not looked at again until its mtime changed; diablo kept the old spool set without saying so again. Found in go-while's review pass of the code merged since #296.
Fixed (#318): a refused file is tried again after 1, 2, 4 ... minutes, then every 60 minutes, and each refusal logs the time of the next try; a changed file is tried at the next minute check as before. Test: `TestSpoolCtlBadReloadRetried`.

### D-187 LIST file lines get "\r\r\n" at the read buffer edge
`dreaderd/list.c`, `ListFileLines()` (#306): a local file with CRLF line ends is read in 255-byte chunks; when a chunk ended between the "\r" and the "\n", the "\r" went out as part of the line, and the line ended "\r\r\n". Only lines of 254 bytes or more. Found in go-while's review pass of the code merged since #296.
Fixed (#318): a "\r" at the end of a chunk is held back and dropped when the next chunk is the "\n". Test: `TestReaderListLongFileLines` (a 254-byte CRLF line).

### D-193 XHDR and XOVER by message-id answer an empty list
`dreaderd/xover.c`, `NNStartListOverviewMsgId()`: it was a stub, so `XHDR <header> <message-id>` and `XOVER <message-id>` answered "221/224 ... follows" and an empty list, and the RFC 3977 `HDR`/`OVER` forms answered 503. The reader does not index overview by message-id, but the article's overview is in the current group. Found by a pynntp run (test_xhdr_by_message_id_and_number).
Fixed (#323): the message-id is resolved against the current group's overview index before the "follows" response (an unknown id is 430 No such article, not an empty list), and the line is output with the message-id as the first field for `XHDR` (RFC 2980 2.6) and 0 for `HDR`, `OVER` and `XOVER` (RFC 3977 8.3.2 and 8.5.2: the article number is replaced by 0; INN does the same). `HDR`/`OVER` by message-id use the same path. The lookup (`FindMsgInGroup()`) reads NB/NE under the record lock and walks the overview index after unlocking, newest first, over at most 100000 articles (`READER_MSGID_SCAN`); an older article, or one in another group, answers 430 (jesse/cc-1's review: an unbounded walk under the lock let any reader stall its process and hold the record the feed side needs). Tests: `TestXhdrXoverByMessageId`, and `TestReaderReplyCodes`/`TestReaderCapabilities` updated (the 503 case is gone).
Fixed (#331) further: `HDR`/`XHDR <header> <message-id>` keep the current-group overview fast path, then fetch `HEAD <message-id>` asynchronously from the spool when that lookup misses. They cover an article in another group, with no group selected, or without local overview, as INN does. The existing header formatter handles case-insensitive names, folded values and absent headers; an unknown article is 430. Metadata (`:bytes`/`:lines`) unavailable outside the overview path is 503. `OVER`/`XOVER` stay current-group-only, and neither HDR lookup changes the selected group or current article. The HEAD path does not populate the full-article cache or perform synchronous localspool reads; it buffers at most 1 MiB of headers, draining an oversized response before returning 503 so the backend remains aligned. Tests: `TestHdrMessageIdSpool`, `TestHdrMessageIdSpoolAsync`, `TestHdrMessageIdSpoolBound`.

### D-194 Log messages ending in a newline leave a blank line
`lib/log.c`, `VLog()`: the log writer adds a newline to each message, and 64 `logit()` formats end in a newline of their own (LDAP errors, "Invalid vserverdef option ...", "PERL auth request for <user>: <result>", and more), so general.log got an empty line after each of them. Reported by Jesse from the SmartOS reader (after every Perl login).
Fixed (#325): `VLog()` strips newlines at the end of the message before it adds its own, which covers every caller and a Perl result that ends in a newline, and turns a CR or LF inside a message (a logged value such as script output) into a space, so each entry is one line. Syslog is unchanged. Test: `TestLogNoBlankLines`.

### D-195 readerdef groups does not limit access by message-id
`dreaderd/nntp.c`, `SelectGroup()`, checks a reader's `groups` groupdef (`dr_GroupDef`) only when a group is selected (GROUP, LISTGROUP). A request by message-id never looks at it: `NNArticleRetrieveByMessageId()` (`dreaderd/server.c`) fetches the article from the reader cache or the spool for ARTICLE, HEAD, BODY and STAT, and since #331 HDR/XHDR `<message-id>` fetch the header from the spool the same way. So a reader limited to some groups can read any article on the server whose message-id it knows. INN checks the reader's read access per article (`PERMartok()` in nnrpd/article.c) when it retrieves by message-id. `samples/dreader.access` documents the gap for ARTICLE ("This option doesn't limit articles retrieved with the ARTICLE command"), but not for HEAD, BODY, STAT or HDR.
Confirmed on main 523b814 (Debian test VM, e2e harness): with `groupdef onlytest` (`group e2e.test`) and `groups onlytest` in the readerdef, `GROUP e2e.other` is 411, but for an article posted only to e2e.other, `ARTICLE <id>` is 220 with the full article, `HEAD <id>` 221, `BODY <id>` 222, `STAT <id>` 223, `HDR Subject <id>` 225 and `XHDR Subject <id>` 221 with its Subject. `OVER <id>` is 430 (it only looks in the current group). Found while reviewing #331.
Fixed (#333): a request by message-id from a reader with a `groups` list is checked as INN's `PERMartok()` does (go-while/cc-1 read nnrpd/misc.c and nnrpd/article.c): the groups are taken from Xref (the server word and each `:number` dropped), else from Newsgroups (folded lines too); the article is readable if any of them is in the list, and also if it has neither header. Otherwise the answer is `480 Read access denied for this article` while the client has not logged in (a connection allowed by its address may still use AUTHINFO, and the login can select a readerdef with other groups) and `502` after a login, as INN answers (480 while the client can authenticate); nothing of the article is sent, and an unknown id stays 430. `ArtAccessDenied()` (`dreaderd/nntp.c`) is called on each path: a reader cache hit (`NNArticleRetrieveByMessageId()`), the spool's answer at the end of the header (`NNSpoolResponse2()`, which keeps only the Xref and Newsgroups lines, 16 KB at most; a line that does not fit, or no memory, sets `sr_AccCut` and the article is then refused unless an allowed group was kept, so the check fails closed (go-while/cc-1's review); the status line and header are still only in `co_ArtBuf` then, and the body is drained), a localspool file (its header is read with `pread()` before `NewDFA()`, 256 KB at most; a header cut short there without either header is refused), and the HDR/XHDR spool fallback of #331. A reader without a `groups` list, and requests by article number (the group was checked by `SelectGroup()`), are not checked, so they cost nothing more. Test: `TestReaderGroupsByMessageId`.

### D-196 BODY by message-id answers the current article number
`dreaderd/nntp.c`, `NNArticleRetrieve()`: for a request by message-id it turns `COM_ARTICLEWVF` into `COM_ARTICLE`, but leaves BODY's `COM_BODYWVF` as it is. When the body comes from the spool server, `NNSpoolResponse1()` and `NNGetLocal1()` (`dreaderd/spool.c`) then write the `COM_BODYWVF` status line with `artno_art(... co_ArtNo ...)`, the number of the current article of the selected group, not 0: after `GROUP e2e.test` (current article 1), `BODY <id>` of another article answered `222 1 <id>` (seen on main 523b814 while testing D-195; ARTICLE, HEAD and STAT by message-id answer 0). RFC 3977 6.2.3 (and 6.2.1): for a request by message-id the article number in the response is 0. From the reader cache the line already has 0 (`NNArticleRetrieveByMessageId()` writes the number only for `COM_ARTICLEWVF`). A client that takes the number as the current article, or checks it, is misled.
Fixed (#333): `NNArticleRetrieve()` turns `COM_BODYWVF` into `COM_BODY` for a message-id, next to `COM_ARTICLEWVF`, so every path answers `222 0 <id>`; BODY by number is unchanged. Test: `TestReaderBodyByMessageIdNumber`.

### D-197 A vserverdef without hostname serves an empty host name
`dreaderd/dns.c`, `InstallAccessCache()`: the built-in DEFAULT vserver gets its host and cluster name from `-h` (else the system's host name), its postpath from `-p` and its newsadmin from `-A` (else news@host), but the `vserverdef` keyword starts each block with `bzero()`, and only a literal `hostname DEFAULT` took the `-h` host back at `end`. So a block without a `hostname` line, also `vserverdef DEFAULT`, had an empty host and cluster name: the banner read `200  NNRP Service Ready (posting ok).` (two spaces, no name), and the 400/502 refusals and the status reply named no host. With an empty cluster name dreaderd also left out its host in front of the Path of articles it serves and the Xref rewrite (`dreaderd/cache.c`, `dreaderd/dfa.c`, both skip on an empty name); local posts got no host in X-Trace (and, without a postpath, none in Injection-Info), and an empty cluster name was the key of the hashed NNTP-Posting-Host. `samples/dreader.access` says hostname "defaults to the current hostname" and newsadmin to news@hostname; a block with its own hostname and no newsadmin line had no newsadmin either. Found on a fresh install of main 371a1c7 (an archive zone whose `vserverdef DEFAULT` set only `postpath`); worked around there with explicit `hostname` and `clustername`.
Fixed (#338): at `end` of a vserverdef, an empty (or `DEFAULT`) hostname is the `-h` host, and then an empty newsadmin is dreaderd's (`-A`, else news@host); a block with its own hostname and no newsadmin gets news@<its hostname> (none, with a warning, when that does not fit the 64-byte field), so the sample text is true. The cluster name falls back to the hostname as before. The postpath is left as it is: its documented default in a vserverdef is none, `postpath 0` clears it, and a feeder adds its path identity anyway. Side effects: such configurations now get the host in front of Path and in Xref on read, as the built-in DEFAULT always did (`noreadpath` and `noxrefhostupdate` turn that off); their hashed NNTP-Posting-Host values change, because the key was the empty cluster name (go-while/cc-1's review); banners and refusals of blocks without newsadmin now name news@host. Tests: `TestVserverdefWithoutHostname` (DEFAULT and a named vserverdef with only `postpath`: banner and Path), `TestVserverdefOwnHostname` (news@ its hostname, or its own newsadmin).

### D-198 An empty first line on a haproxy readerdef reaches strcmp with NULL
`dreaderd/reader.c`, `NNCommand2()`: with `haproxy yes` the first line of a connection is parsed as a PROXY v1 header with `parseword()`, which returns NULL for an empty line, and `strcmp(cmd, "PROXY")` then got the NULL. Whether that crashed depended on the compiler: gcc -O2 tests `proto` (also NULL) first and skips the call, but an -O0 build died with SIGSEGV in `strcmp()`, and with it the reader process and every connection it served. Without `proxyfrom` any client that matches the readerdef could send that line.
Fixed (#341): `cmd` is checked for NULL, so an empty line gets the same `400 <host>: failed to read haproxy PROXY v1 header.` as any other bad header. Test: `TestReaderHAProxyEmptyLine` (fails on an -O0 or AddressSanitizer build of main).

### D-199 A long welcome text writes past the greeting buffer
`dreaderd/reader.c`, `WelcomeString()`: the `welcome` text of a vserverdef is expanded into a 512-byte static buffer, and the length `snprintf()` returned (the length it wanted, not what it wrote) was added to the position unchecked, so with expansions like `%o` (organization, up to 127 bytes) the closing `'\0'` was written past the buffer, up to about 130 bytes beyond it; text that filled the buffer exactly wrote it one byte past. A welcome ending in `%` stepped over the end of the text. Only dreader.access sets the text.
Fixed (#341): the position is held at the last byte of the buffer, so the greeting is cut at 511 bytes, and a `%` at the end ends the text. Test: `TestReaderWelcomeLong` (fails on an AddressSanitizer build of main: global-buffer-overflow in `WelcomeString()`).

### D-200 checkgroups creates and removes groups the newgroup and rmgroup lines do not allow
`dreaderd/control.c`, `CgCheck()`: a checkgroups that a checkgroups line acts on (doit or a passed verify) creates every missing group it lists, corrects the moderated flag of those that exist and removes the other groups of its hierarchies, decided by the checkgroups lines alone. The newgroup and rmgroup lines are not looked at, so a site whose dcontrol.ctl drops every newgroup and rmgroup (`newgroup:*:*:drop`, `rmgroup:*:*:drop`) still gets groups created and removed by checkgroups. Found on the archive zone (2026-11-07): Jesse set both to drop there and wants no group created or removed outside those settings. INN's controlchan does the same as diablo did (a checkgroups doit runs docheckgroups without the newgroup/rmgroup lines); this is a deliberate difference.
Fixed (#342): an addition or a moderated-flag correction is made only where a newgroup of that group from the sender would be executed, a removal only where an rmgroup would: the last newgroup (rmgroup) or `all` line whose `<from>` matches the `From:` header as for a real newgroup or rmgroup (`CtlFromMatches()`: the header as it is, case-sensitive, not the reduced lower-case `Sender:` address the checkgroups lines are matched against) and whose `<newsgroups>` matches the group, as `ParseControl()` finds it (`break` ends the scan), must be `doit` or `verify-<key>`, and `verify-<key>` only when the checkgroups line was verified with that key. No such line, `drop`, `log`, `mail` and `dgpverify` allow nothing. What is not made is logged with the counts and the dgrpctl commands that make it; it does not count for the `/maxdocheckgroups/` limits. Descriptions of groups that exist are updated as before. `CgReadGrpRules()` reads those lines once per checkgroups, `CgGrpAllowed()` decides per group. All three shipped dcontrol.ctl samples give every checkgroups doit/verify line a newgroup and rmgroup line that allows it (225 lines each, checked with a script), so a site on a sample sees no change; a dcontrol.ctl with checkgroups lines and no newgroup/rmgroup lines no longer creates or removes groups by checkgroups. Tests: `TestCheckgroupsFollowsNewgroupRmgroup` (10 cases; the last three check the `CtlFromMatches()` matching); the dcontrol.ctl of the other checkgroups tests ends with `newgroup:*:*:doit` and `rmgroup:*:*:doit` (`cgGroupLines`).

### D-201 decrypt-aes.sh can take a wrong cryptpw
`contrib/xtrace/decrypt-aes.sh` (#328), without a password, tries each `cryptpw` of dreader.access and took the first one whose output passed `printable()`. A wrong password gives valid CBC padding about 1 time in 256, and `printable()` let every byte above 127 through, so for a one-block value such as a posting-account the wrong password of another vserverdef listed first was taken about once in 2,000 posts, and garbage was printed as the value. Seen as the `TestReaderPostInjectionInfo/cryptpw` flake on rockylinux-9 (run 42320411955; go-while's v7 list). `pass:<wrong password>` printed the garbage the same way.
Fixed (#344): the output must be readable text (not empty, no control characters, bytes above 127 only as valid UTF-8, checked with `od` and `awk` because illumos `iconv` does not check UTF-8), it is checked on a file instead of a shell variable (which drops NUL bytes), and every cryptpw is tried: if two give different readable text, the value is reported as ambiguous instead of guessed. Test: `TestDecryptAesWrongCryptpw` (a crafted salt for which the wrong cryptpw gives printable non-UTF-8 garbage; fails on main).

### D-202 The first LDAP login after a start spends its deadline on a host name lookup
`dreaderd/dns.c`: OpenLDAP initializes its process-global options on the first API call, and that includes a canonical lookup of the machine's own host name (`ldap_pvt_get_fqdn()`: `gethostname()`, then `getaddrinfo(..., AI_CANONNAME)`), even when the LDAP URL is a numeric address. In the DNS/auth worker that first call came inside `AuthCall()`, so the lookup counted against the per-login authentication deadline: with a slow resolver, or a host name that is not in /etc/hosts, the first LDAP login after a start (or after a worker was replaced) was refused with 481 although the LDAP server answered at once. Seen as the NetBSD CI flake in `TestAuthTimeoutLDAPSlowSteps` (#335 run, 2026-11-06), where the VM's name went to external DNS.
Fixed (#343): the worker makes one harmless libldap call (`ldap_get_option(NULL, LDAP_OPT_PROTOCOL_VERSION, ...)`) when its access map first holds an LDAP authdef, at start or after an access reload, outside any login deadline; without LDAP nothing changes. The deadline, its grace and the LDAP exchange are unchanged; a slow lookup can still delay the worker's first answer. Tests: `TestLDAPInitSlowHostname`, `TestLDAPInitWithoutLDAP`, `TestLDAPInitOnAccessReload` (an LD_PRELOAD shim delays only that lookup by five seconds; on main the first login gets 481).

### D-203 readercanlock none looks up every cancel target before refusing it
`dreaderd/control.c`, `CtlCancel()`: with `readercanlock none` no cancel or supersede is executed, but `CtlCancel()` first scanned the overview of every group the cancel names for its target (`FindCanceledMsg()`), and only then `canLockDecide()` refused it. A cancel whose target was not there went through the whole scan and was logged as `target not here yet, dropped (readercanlock)`, not as refused. On a server that refuses all cancels, such as an archive, every cancel (and every Supersedes:) cost those lookups for nothing. Seen by Jesse on the archive zone (2026-11-07):
`cancel <...@rutgers.rutgers.edu>: target not here yet, dropped (readercanlock), 0 item(s)` next to `cancel <...>: refused (readercanlock none), 0 item(s)`.
Fixed (#349): `CtlCancel()` returns before any lookup when `readercanlock` is `none`, logging `cancel <id>: refused (readercanlock none), 0 item(s)` for every cancel and supersede, as INN's `docancels none` processes none. Test: `TestCancelLockNoneNoLookup`.

### D-204 HistoryAdd wraps the record number past 2^32 - 1 records
`lib/history.c`, `HistoryAdd()`: in a version 2 history file the new record's number is `(HistIndex)((writePos - HEntryOff) / sizeof(History))`, and `HistIndex` is a uint32. Nothing checks the count, so the record after number 2^32 - 1 (about 120GB of 28-byte records) gets a number that wraps to a low one. The hash slot (`HAry[hi]`) then points at that old record of another article: a lookup of the new Message-ID compares the old record's hash and follows the old record's chain (`HistoryLookup()`), so the new article is not found. The records the slot held before, which the new record's `next` points to, are not reachable any more either, so offers of all those articles are accepted again as new ones. A number that wraps to exactly 0 (the dummy record) ends the chain at once. Far away for current sites (the archive reader expects about 1.5 billion records), but a silent corruption when it happens. Found by go-while/cc-1 in the review of #347, which documents the limit in the header comment of `lib/history.c`.
Plan (go-while/cc-1's proposal): `HistoryAdd()` refuses to add when the next record number would reach 2^32 (RCTRYAGAIN, so the article is offered again later) and logs that once, instead of wrapping.
Fixed (#350), as planned: `HistoryAdd()` asks `historyFull()` before the append. In a version 2 file that is true from record number 2^32 - 1 on, in a version 1 file from byte offset 2^32 - 1 on: 2^32 - 1 is `(HistIndex)-1`, the "not found" value of `HistoryPosLookupByHash()`, so a record there would be stored but never updated, expired or cancelled. The last usable record number is 2^32 - 2. Then nothing is written and `RCTRYAGAIN` is returned, so IHAVE gets `436 I/O error, try again later` and TAKETHIS `431` (the existing `HistoryTryAgain` reject in `util/diablo.c`). The article is offered again later, and once the history has been rebuilt with fewer records it can be stored. `dhistory is full (no record number left after N records): articles are refused until the history is rebuilt with fewer records` is logged once per process (LOG_CRIT). Test: `TestHistoryFullRefused`, which grows the dhistory sparse to 2^32 - 2 records: the article with the last number (2^32 - 2) is stored, found and can be cancelled with `dcancel -h`, the next one gets 436 twice, and the line is logged once.

### D-205 A supersede is logged as a cancel
`dreaderd/control.c`: `ExecuteSupersedes()` turns the `Supersedes:` header of an article into a made-up `cancel <target>` command and runs it through `ExecuteControl()` and `CtlCancel()`, so the readercanlock decision is logged as `cancel <target>: <why>, N item(s)`, the same line as for a real cancel control message, and the dcontrol.ctl log gets `>> control cancel, ...`. Neither says that the request came from a Supersedes header, nor which article superseded the target, so a refused supersede (under the default `readercanlock require-auth` most targets have no Cancel-Lock and are not withdrawn) can't be told apart from a refused cancel. With `readercanlock all` `CtlCancel()` logs no decision at all, for cancels and supersedes alike, although samples/diablo.config says "Every decision is logged". Found by Jesse while looking at supersedes after #19.
Plan: `ExecuteSupersedes()` tells `CtlCancel()` that the request is a supersede, and the log line says `supersede <target> by <Message-ID of the new article>: <why>, N item(s)`; the dcontrol.ctl log the same. Decide whether `readercanlock all` logs its decisions too (then at LOG_INFO), or the sample says that it doesn't.
Fixed (#351): `ExecuteSupersedes()` gets the new article's Message-ID from `group.c` and sets it for its made-up cancel; `CtlCancel()` then logs `supersede <target> by <new Message-ID>: <why>, N item(s)` and the dcontrol.ctl log gets `>> control supersede, ...`. A real cancel still logs `cancel <target>: ...`. `readercanlock all` still logs no decision; samples/diablo.config and README.READER say so for supersedes ("nothing is logged under 'all'"). Tests: `TestCancelLockNoneNoLookup` and `TestSupersedeLogLine`.

### D-206 White space between two Newsgroups names hides the names after it
`lib/active.c`, `util/diablo.c`, `dreaderd/group.c`: RFC 5536 3.1.4 allows white space in the `Newsgroups:` list only around the commas. The feeder's walks over the list (`InActive()`, `GenerateXRef()` and the others) end a name at a space or tab but go on only after a comma, so in `x a,b` they stop after `x`: with `activedrop yes` the article is refused NotInActive although `b` is in the active file. dreaderd splits the list at the commas and cuts each entry at its first space or tab, so `a b` is filed under `a` only and `x a,b` not under `a`.
Fixed (#358): `NormalizeNewsgroups()` (`lib/subs.c`) rewrites the value as a plain comma list (each run of commas and white space becomes one comma); the feeder calls it on its copy of the list and dreaderd on the header value, so every name is looked up. dreaderd's POST refuses a list with white space between two names (441 Invalid newsgroup(s) line, `NewsgroupsSpaceInName()`) and runs its group checks on the plain list. Tests: `TestNewsgroupsSpaceInsideList`, `TestReaderNewsgroupsSpace`, `TestReaderPostNewsgroupsSpace`.

### D-207 POST refuses white space after a comma in Newsgroups
`dreaderd/post.c`: RFC 5536 3.1.4 allows white space on both sides of the commas in the `Newsgroups:` list, but POST compared the trimmed value with the value whose white space after commas was removed and, without `POST_CLEANUPNEWSGROUPSLINE` (defined nowhere), refused any difference with 441 Invalid newsgroup(s) line. So `a, b` was refused while `a ,b` was accepted (after D-206). Found by jesse/cc-1 in the review of #358.
Fixed: the comparison is gone; the list is checked by its plain comma list (D-206), and `POST_CLEANUPNEWSGROUPSLINE` only decides whether the header is rewritten as that list. Test: `TestReaderPostNewsgroupsSpace`.

### D-208 The feeder reads only the first line of a folded Newsgroups header
`util/diablo.c`: RFC 5536 3.1.4 allows folding white space around the commas of the `Newsgroups:` list, so the header may go on over continuation lines. `LoadArticle()` copied only the first line into its group list and wrote the continuation lines thru, so the groups on them were missing from the active check, the moderation check, `Xref:`, feed matching, the incoming filter and spool choice. With `activedrop yes` an article whose only groups in the active file were on a continuation line was refused NotInActive, and an unapproved article with a moderated group there was accepted. dreaderd was not affected: its header scan joins the continuation lines. Found by Jesse on a 1995 article from the archive feed (`<21D51CD4H0002871FH@cvm.groenld.sb.sub.de>`, 26 groups over two lines).
Fixed: `ngAddFolded()` adds each continuation line of `Newsgroups:` to the list, with a comma before it, and normalizes it again (D-206); a name that does not fit whole in the list is left out. A continuation line longer than the input buffer is added only up to the last whole name of its first piece (D-209). Tests: `TestNewsgroupsFoldedActive`, `TestNewsgroupsFoldedXref`, `TestNewsgroupsFoldedLongLine`.
Since #442, every piece of a continuation line longer than the input buffer is added whole by `ngAdd()`, which `LoadArticle()` calls for each piece (`ngAddFolded()`, for a continuation line that fits in one piece, calls it too) (D-209).

### D-209 The feeder reads only the first piece of a Newsgroups line longer than its buffer
`util/diablo.c`: `LoadArticle()` reads the article with `bgets()`, whose buffer is `feederbuffersize` rounded up to a page (one page with the default `feederbuffersize 1`, about 4 KB). A longer line comes in pieces, and a piece that does not end the line (`lastNl == 0`) is only written thru. For a `Newsgroups:` line only the first piece reaches `nglist`, and `diablo_strlcpynl()` drops its last byte as if it were the line end (`aflen` is 1 for a piece too). So the groups after the first piece are missing from the active and moderation checks, `Xref:`, feed matching, the incoming filter and spool choice (as in D-208), and the last name of the first piece is looked up cut short, so it can name another group. The same applies to a continuation line of that length after D-208: `ngAddFolded()` gets only its first piece; it leaves out the name at the end of that piece, which may be cut (the first D-208 version kept it, so `e2e.test.missing` was filed under `e2e.test`; found by jesse/chatgpt-1 in council r1 of #404), but the names after it are lost. A list of that length takes about 100 or more names. Found by jesse/cc-1 while fixing D-208; shown from the code, not run.
Plan: while the `Newsgroups:` header (or one of its continuation lines) is in pieces, add each piece to `nglist` without a separator before it and without cutting a byte, and normalize once the line has ended; a name cut by the `MAXLINE` limit is dropped, as in `ngAddFolded()`. Check whether `feederautoaddtoactive` adds a cut name to the active file today. Test: a `Newsgroups:` line longer than the buffer whose only group in the active file comes after the first piece, and one whose first piece ends inside a name.
Fixed (#442): a new `ngAdd()` (`util/diablo.c`) adds the value of the `Newsgroups:` line, a continuation line and each further piece of either to the list; a piece is added whole, CR and LF are left out, and the list is normalized when the line has ended. The limit stays `MAXLINE` less the NUL: the name the limit cuts is left out and nothing after the cut is added. A `Newsgroups:` value that is a single name longer than the limit now gives an empty list, so the article is refused (NotInActive with `activedrop yes`, NoNewsgroups without it); Jesse decided on 2026-11-18 that this new refusal stays. Tests: `TestNewsgroupsLongLine`, `TestNewsgroupsLongLineAutoAdd`, `TestNewsgroupsLongLineLimit`, `TestNewsgroupsFoldedLongLine`.

### D-210 The crc history hash loses a bit, so neighbouring Message-IDs collide
`lib/hash.c`, `hhash()` with `hash crc` (the default and the sample setting): the CRC64 is folded into the two 31-bit words of `hash_t` with `h = (h & 0x7FFFFFFF) | 1` when a word's top bit is set. The `| 1` loses bit 0; the comment in the code says it should have been `^ 1`, kept so that existing history files stay valid. Bit 0 of `h1` comes from bit 0 of the 5th byte from the end of the Message-ID (bit 0 of `h2` from the last byte, always `>`). So two Message-IDs of the same length that differ only in bit 0 of that byte (`0`/`1`, `2`/`3`, ... before a 2-character domain) get the same hash whenever `h1`'s top bit is set. The history stores only the hash, so the second one offered is refused as Duplicate and the article is lost; `dreadart -h <Message-ID>` shows `messageID-MisMatch` (the entry points at the other article). Found by Jesse on the archive feed: `<1510900044@gn>` was refused because `<1510900045@gn>` was in the history; all 51 Duplicate rejects of the feed's last four hours were such pairs (46 `@gn`, 5 `@ax`). A port of `hhash()` gives the same hash for every pair `<2k@gn>`/`<2k+1@gn>` from 1510900000 to 1510920000 (10000 of 10000), and for random IDs a flip of bit 0 of the 5th byte from the end collides 1997 of 4000 times; flips at other positions did not collide. Analysed by jesse/cc-1; shown on the archive (dilookup, dreadart) and with the port, nothing changed.
Options (to be decided): (a) a new history-only hash method without the structure, for example 62 bits of the MD5 of the Message-ID (`md5hash()` exists; INN keys its history on MD5); (b) the fold with `^ 1`, still a CRC; (c) on a history hit, read the stored article's Message-ID before refusing (no format change, but a spool read per real duplicate, and every lookup by Message-ID must skip an entry for another ID). (a) and (b) need a new history file; the `compathash` fallback does not help, since a miss with the new method would find the colliding old entry. The group-name hashes that name the reader's overview files also use `hhash()` and must not change with it. Articles already refused come back only by feeding them again after the fix.
Decided (Jesse, 2026-11-15): option (a), after go-while/cc-1's analysis (board 20261116-040755Z). Plan:
- A new Message-ID hash beside `hhash()`: 62 bits of the MD5 of the Message-ID, in the `hash_t` layout. `hhash()` stays as it is for group names and every on-disk name made from it (the reader's group files via `GFHash()`, `dexpireover`, `doverctl`, `dgrpctl`), so the `hash` setting keeps its meaning.
- Chosen by a new diablo.config option (name to be settled); the default stays `crc`, so a site changes nothing until it switches.
- Used for every Message-ID hash: the history and every tool that reads or writes it (`lib/history.c`, `util/diablo.c`, `dilookup`, `dreadart`, `didump`, `dhisctl`, `dcancel`, `dspoolrefeed`, `diloadfromspool`, `diconvhist`, `dhisbench`), the overview index's `oa_MsgHash` (`dreaderd/group.c`, written per article in each `over.*` file) and the Message-ID caches (`dreaderd/cache.c`, `dreaderd/cancel.c`, `lib/precommit.c`, the spam filter's `MsgIdHash`).
- The history header records the method, and diablo and the tools refuse a history written with another one (today nothing records it, so a changed setting would silently miss every entry).
- Switching (Jesse, 2026-11-15: the change may require rebuilding both): the history and the overview are rebuilt for the new method (`diloadfromspool`, and the overview from the spool), or the server is fed again; the reader caches are emptied. No fallback to the old history and no `compathash`. A rebuilt history has no entries for articles already gone from the spool, so until the `remember` time has passed such an article offered again is accepted; the release note says so.
- Tests: the pair `<1510900044@gn>`/`<1510900045@gn>` is refused as Duplicate with `crc` (pins today's behaviour) and both are accepted and found by `dilookup`/`dreadart` with the new method; a history of the other method is refused.
Changed (Jesse, 2026-11-16, with go-while/cc-1's opinion, board 20261116-193612Z): SipHash-2-4 with a fixed key instead of MD5, because diablo must stay fast. Measured on the Debian test VM, 1M Message-IDs of mean length 37: `hhash()` crc 81-122 ns per ID, MD5 (`md5hash()`) 198-239 ns, SipHash-2-4 28 ns. A fixed key gives the same hash on every host, so no per-site key is needed.
Fixed: `msgidhash()` (`lib/hash.c`) with the new option `msgidhash hash|siphash` (default `hash`, so nothing changes until a site switches). `siphash` is SipHash-2-4 (`siphash24()` matched the paper's 64 test vectors in a check on the Debian test VM; the e2e test checks its own Go SipHash against two of them and the C one against that), with the 64 bits kept as 62 in the crc `hash_t` layout. It is used for every Message-ID hash: the history and its tools, the overview's `oa_MsgHash`, the reader cache, the cancel cache, precommit, the spam filter and `dgrpctl masscancel`. The group-name hashes stay on `hhash()`. A history of `msgidhash siphash` is written as version 3 (`HVERSION`) with its own magic `HMAGIC3` (`a1b2c3d5`), with the method in the header field `msgidHash` (it uses the header's padding, so `sizeof(HistHead)` stays 16). The magic makes every older program refuse it: the older diablo and `diload` refused a newer version in `HistoryOpen()`, but the older `dhisexpire` and `didump` only warned: main's `dhisexpire -a` rewrote it as a version 2 history of the other method, and so did main's `didump` piped into a new `diload` (found by go-while/cc-1 in council r1 of #412; shown with main's binaries on the Debian test VM). A header whose magic does not match its version (a version 3 header with the old magic, or the reverse) is refused as damaged, by `HistoryOpen()` and by `dhisexpire`, `didump`, `dspoolrefeed` and `dexpire`, which read the header themselves (`HistHeadMagicOK()`). The hot paths compare the mapped header with the magic saved at open: no measurable cost (`dhisbench`, 2M lookups: main crc 609-644k/s, this branch crc 632-639k/s, siphash 678-683k/s). A `hash` history is still written as version 2. `HistoryOpen()` refuses a history of the other method; `dhisexpire` refuses to expire into one, and its un-dead path takes versions 2 and 3. `dhisctl`, `didump` and `dspoolrefeed` hash with the file's method, and `dhisctl -h` shows it. `diload` takes the hashes of a dump as they are, so `didump` of a `siphash` history writes a `MSGIDHASH siphash` line first and `diload` refuses a dump of another method than its own before it makes a file (an older `diload` stops at the unknown line); a `hash` dump is unchanged. compathash is not used with `siphash`. Tests: `TestMsgIdHashSipHash`, `TestMsgIdHashNeighbours`, `TestMsgIdHashMismatch`, `TestMsgIdHashDumpLoad`, `TestMsgIdHashMagicVersion`, `TestMsgIdHashDhisexpire`, `TestMsgIdHashReaderOverview`.

### D-211 The reader takes a Message-ID hash match for the article
`dreaderd/cache.c`, `dreaderd/server.c`, `dreaderd/group.c`: dreaderd finds some articles by `hhash()` of the Message-ID alone and never compares the Message-ID, so for a colliding pair (D-210) it can act on the other article. `OpenCache()` opens the reader cache file named by the hash, and its only caller (`dreaderd/server.c:1296-1330`, with `readercache` on) sends the status line with the requested Message-ID and the cached article's text: the second ID of a pair gets the first one's article. The overview index stores `oa_MsgHash = hhash(msgid)` for each article (`group.c:709`); `FindCanceledMsg()` (`:1514`) and `FindMsgInGroup()` (`:1608`) take the first entry in the group whose hash matches, so within one group a cancel, or an `XHDR`/`HDR`/`OVER` by Message-ID, for one ID of a pair can act on the other article. Found by go-while/cc-1 (reader cache, board 20261116-040755Z) and jesse/cc-1 (overview lookups); shown from the code at main 5a4f351, not run.
Plan: compare the Message-ID before using a match: the cache file's `Message-ID:` header, and for an overview entry the article's own header (spool, or the overview data's Message-ID field); a mismatch is a miss. With the new Message-ID hash (D-210) such a pair is very unlikely, but `crc` stays the default and sites that keep it still have the pairs, so the compare is needed either way; it also makes any remaining collision a miss instead of the wrong article. Test: a colliding pair in one group with `readercache` on: each ID is served its own article, and a by-ID `XHDR` and a cancel act on the right one.
Partly fixed (#464): `NNArticleRetrieveByMessageId()` (`dreaderd/server.c`) maps the cache file right after `OpenCache()`, and the new `CacheMsgIdMatches()` (`dreaderd/cache.c`) compares the first `Message-ID:` header of the cached article with the requested ID byte for byte; on a mismatch the hit is dropped, and the other ID is requested from the spool server while the conflicting cache file stays (the second ID of a pair is not cached). Test: `TestReaderCacheMsgIdCollision` (a fake spool server). Overview half fixed: `FindMsgInGroup()` and `FindCanceledMsg()` (`dreaderd/group.c`) compare the last `Message-ID:` header of the record kept in the overview data (the one the index hash comes from; a fed article can carry two, the reader POST refuses that) with the requested ID (`OverMsgIdCheck()`, using `CacheMsgIdMatches()`); an entry whose header can be read and has another Message-ID is skipped and the scan goes on. An entry whose header cannot be read (`readerxover trackonly` writes `oa_SeekPos` -1 for every record; an article older than the group's age limit; a corrupt record) still matches by its hash for a cancel, because dreaderd starts with `trackonly` only under `readercanlock all` or `none`, a cancel is not checked under `all`, and under the checking modes `canLockDecide()` refuses a target whose headers it cannot read for the group that decides (`CtlCancel()` asks it only while no earlier group has decided, so once the deciding group's check has passed the candidates of later groups are cancelled without a check, and after a refusal no later group is cancelled); for a reader lookup (`XHDR`, `XOVER` by Message-ID) such an entry counts as not found, so the existing spool fallback or 430 answers. `FindCanceledMsg()` reads NB and NE under the record lock and scans after the unlock, and checks the slot again after the header read. Tests: `TestOverviewMsgIdCollisionXhdr`, `TestOverviewMsgIdCollisionCancel`. Still open, by reading: the spool lookups (`HistoryLookup()` in `lib/history.c` compares the hash only, and the `article` command and `ArticleOpen()` in `util/diablo.c` do not compare the Message-ID, so a spool with `msgidhash hash` can answer the second ID of a pair with the first one's article); and the pre-cancel cache (`FindCancelCache()` and `EnterCancelCache()` in `dreaderd/cancel.c`, used at `dreaderd/group.c:490`, compare `msgidhash()` only, so a cancel for one ID of a pair that arrives first can make dreaderd refuse the other ID at feed time). From review, not run. `msgidhash siphash` (D-210) removes the CRC weakness, not every possible collision.

### D-212 A busy group in a large readerovcache stays locked until the feed-only flush
`dreaderd/group.c`, `dreaderd/reader.c`, `util/dexpireover.c`: a group in a dreaderd process's overview cache keeps a shared lock on its `over.*` index (offset 4). Since `readerovcache` (#413) the cache closes the group used least recently when it is full; with a large number, a group that gets articles all the time never becomes the least recently used one, so only `FlushOverCache()` closes it: every 30 seconds in reader processes (`reader.c:307`), every 300 seconds in feed-only ones (`reader.c:350`). `dexpireover -s` and `-R` need the exclusive lock and try each group once per run, waiting at most `-l` seconds (`alarm(LockWaitTime)`: `dexpireover.c:2564` for `-s`, `:1897` for `-R`). With the default `-l 1` such a group is skipped ("file in use"), so a busy group's index is not resized (`-s`, also part of `-a`) and its data is not rewritten (`-R`); with `-l` longer than 300 each such group can add up to 5 minutes to the run. With the default `readerovcache 8` the cache closes groups often and nothing changes. The behaviour is documented in `samples/diablo.config` (#413). Found by go-while/cc-1 in council r2 of #413; shown from the code, not run.
Possible fix (to be decided): flush more often in feed-only processes when `readerovcache` is above 8, or let dexpireover ask the processes to close a group (there is no channel for that today). Test: a feed-only process with a large `readerovcache` and one group fed all the time; `dexpireover -s` with a short `-l` resizes that group's index within one run.

### D-213 dnewslink waits for the read timeout after a stream reply that matches no pending article
`util/dnewslink.c:2971-3137`, the reply switch of `StreamTransact()` (`:2885`): each reply is matched to a pending article by Message-ID and state. A reply in the wrong state (238 or 438 for an article sent with TAKETHIS, 239 or 439 for one still at CHECK) or for a Message-ID that is not pending is read and dropped; no entry is resolved and `StreamPend` stays. The reply the pending article waits for does not come, so the connection hangs until the read timeout (`-t`, 600 s by default), fails and reconnects; the pending entries go back through `RefilePendingStreams()` (`:3231`) and are offered again, so after an unmatched 239 the article is sent twice. Found by the round-0 reviewers (Haiku, Sonnet) of the dnewslink reply codes (#410), checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: a reply that matches no pending entry in the right state is a peer out of step: close the connection at once and refile the pending entries, as for an unknown code. Test: a peer that answers 239 to a CHECK: dnewslink reconnects without waiting for `-T`.
Fixed (#498): `StreamTransact()` fails the connection when a reply matches no pending entry in the state it answers (238/438 for an article sent with TAKETHIS, 239/439/437 for one at CHECK, or a Message-ID that is not pending), as for an unknown reply code: it logs the reply, and the pending entries are refiled and offered again on the new connection, with no wait for the read timeout. A first reply on a new connection that is out of step still ends the batch, as an unknown code does (nothing was exchanged to reconnect for). Test: TestDnewslinkStreamReplyOutOfStep.

### D-214 dnewslink drops an article the peer answers 431 after TAKETHIS while the deferred articles are sent
`util/dnewslink.c:2971-2998`: since #410 a 431 to TAKETHIS defers the article once, as for a 431 to CHECK. While the deferred articles are sent (the replay starts with articles pending when the defer buffer reaches `MAXDEFER - 20`, `:980-992`), and for a deferred article sent again, no second defer is taken: the 431 is a reject (`T_REJECTED`, `:2985-2986`; for a full defer buffer `:1205`), so the article is not kept or requeued. Diablo's own feeder answers 431 to TAKETHIS when the history add fails for a time (D-185, #314), so between two Diablos such an article is lost. RFC 4644 has no 431 for TAKETHIS. Found by the round-0 reviewers of #410, checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: in the replay case write the queue line back with `keepQueueLine()` instead of the reject. Test: a peer that answers 431 to a TAKETHIS sent while the deferred articles are sent: the article is in the queue file afterwards.
Partly fixed (#498, batch feeds): a 431 to TAKETHIS for an article that was deferred already, or answered while the deferred articles are sent, is returned as `T_REQUEUE` and `keepQueueLine()` writes the queue line back (log "kept"), so the article is sent on a later run. Still open: a realtime feed (`dnewslink -r`, as diablo's `-r-1` call and `dspoolout -r` use it) has no queue file to keep the article in and still rejects on a second 431 to TAKETHIS, which is the Diablo-to-Diablo loss described above. (A fresh article answered during the replay was kept already since #468.) A full defer buffer is not changed: it is not reached with a stream window of at most 16 (80 defers start the replay), so there is no test for it. Test: TestDnewslinkTakethis431KeptDuringReplay, TestDnewslinkTakethis431Defers (431 again).

### D-215 A deferred article that is pending at a lost connection can be deferred again
`util/dnewslink.c`: a deferred article that is sent again (`deferbegin > 0`, `:980-992`) and still pending when the connection fails goes through `RefilePendingStreams()` (`:3231`) and `readretry()` (`:3906`), which keep only the queue line and clear the entry (`:3934`), so the per-entry mark `st_Defers` (#410) is lost. After the reconnect `deferbegin` starts at 0, so the article is offered as a new one and can be deferred once more. Bounded: one more defer per reconnect. Found by the round-0 reviewers of #410, checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: keep the mark in the retry entry and pass it on when `readretry()` hands the entry back. Test: a peer that defers an article, then drops the connection while the deferred article is pending: after the reconnect a second 431 is a reject.
Fixed (#498): `readretry()` hands the entry's mark `st_Defers` back (its new `again` argument) and the main loop passes it to `Transact()`/`StreamTransact()`, which keep it in the new entry: after a reconnect a deferred article that was pending is offered as one deferred already, so a 431 to its CHECK is taken as 238 and no second defer is made. Test: TestDnewslinkDeferMarkSurvivesReconnect.

### D-216 A 431 reject after TAKETHIS adds no bytes to the rejected bytes
`util/dnewslink.c:2971-2998`: the 431-to-TAKETHIS path does not set `*sentSize`, which `Transact()` sets to 0 for each article (`:2438`), so when that 431 is a reject (a deferred article sent again, or any article while the deferred ones are sent: D-214), the reject (`:1232-1253`) adds 0 to `RejectedBytes` although the article was sent. Statistics only. Found by the round-0 reviewers of #410, checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: set `*sentSize = s->st_Size` on that path, as the 439 path does (`:3115`).
Fixed (#468): the 431-to-TAKETHIS path sets `*sentSize = s->st_Size` (go-while's change in #468, found already on main when this was worked on). With #498 a batch feed keeps such an article instead of rejecting it, so only a realtime feed still counts a 431 reject; no other reject path counts 0 bytes for an article that was sent (437/439 and the IHAVE paths set the size). Test: TestDnewslinkTakethis431RejectedBytes (now a realtime feed).

### D-217 dclient reads the rest of a long reply line as the next reply
`util/dclient.c:149-163`, `newscommand()`: replies are read with `fgets()` into `MAXLINE` bytes. For a longer line only the first piece is judged (`MultiLineReply()`); a one-line reply ends the command there, and the rest of the line is read as the reply to the next command. Since #411 that rest has no reply code in most cases, so dclient prints "Unexpected response from server" and exits 2 at the next command; a rest that starts with three digits and a space is taken as the next reply, and every later reply is one off. Found by the round-0 reviewers of the dsyncgroups and dclient reply codes (#411), checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: read to the end of the line and drop what does not fit, or exit with an error for a line that does not fit.

### D-218 dclient reads one byte before its buffer for a blank line in the dump file
`util/dclient.c:182-189`, `dodumpfile()` (`-f`): `strncmp(&line[strlen(line) - 2], "\r\n", 2)` reads `line[-1]` for a blank line (`"\n"`, length 1). The same code also cuts a byte that is not a newline: the last line of a file without a final newline loses its last character, and a line longer than `MAXLINE` loses one byte at each piece and gets a CRLF inside it. Local file, local tool. The read before the buffer found by the council r2 reviewer (Sonnet) of #411; the cut bytes found by go-while/opus-high-B in the re-check at main 1c80d48f; shown from the code, not run.
Plan: strip a trailing `"\n"` or `"\r\n"` only when it is there, and send the line with `"\r\n"`. Test: a dump file with a blank line, a last line without a newline and a long line.
Fixed (#459): `dodumpfile()` (`util/dclient.c`) strips a newline, and a CR in front of it, only if there is one, and sends CR LF once, at the end of the line or of the file; a line longer than the buffer goes out in pieces of 16383 bytes with nothing inserted between them (a CR at the end of a piece is held back until the next piece shows whether it belongs to the CR LF), so the last byte of a last line without a newline and the bytes of long lines are no longer cut or split. It also calls `fflush()` before its first write. The read before the buffer for a blank line is gone by reading: the loop (`util/dclient.c:196-224`) looks at `line[len - 1]` only when `len > 0`; no test checks memory accesses, and `TestDclientDumpFileLines` checks that a blank line stays blank. Tests: `TestDclientDumpFileLines`, `TestDclientDumpFileLastLineSizes`. The separate defects of the same loop (a NUL byte, a read error, the unread replies) are D-291.

### D-219 A filter restart can stop the diablo master for up to 10 s
`filter/diab-filter.c:113-156`, `DiabFilterClose(1)`: after a failed write to or read from the external filter, it closes the pipes and polls `waitpid(WNOHANG)` with `sleep(1)` between tries, up to 10 times. The call comes from `DoPipe()` in the diablo master (`util/diablo.c:1678`), so the master handles no feed for about 1 s when the filter has not exited yet, and for 10 s when it ignores EOF. The restart backoff (`filter_failed()`) bounds how often. The child is not left unreaped: the master loop's `wait3()` (`util/diablo.c:1027`) takes it. Found by the round-0 reviewer (Haiku) of the filter reply codes, checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: do not wait in the master: send the filter SIGTERM after the close and leave the reaping to the master loop's `wait3()`.

### D-220 diablo waits without a time limit for the answer of the external filter
`filter/diab-filter.c:396`, `DiabFilter()`: the filter's answer is read with one blocking `read()` and no timeout, from a pipe or, for a `host:port` filter, from a TCP socket. A filter that takes the article but does not answer stops the diablo master (`DoPipe()`, `util/diablo.c:1678`) until it answers or closes. Found by the round-0 reviewer (Haiku) of the filter reply codes, checked by go-while/cc-1; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: wait for the answer with `poll()` and a time limit; on expiry take the filter as failed (`filter_failed()`, close), as for a read error. Test: a filter that reads the article and never answers: the feed goes on after the limit.

### D-221 dgrpctl masscancel misses articles with numbers above 31 bits
`util/dgrpctl.c:198`: `masscancel` tests `oa->oa_ArtNo == artEnd`, but `oa_ArtNo` keeps only 31 bits of the number (`dreaderd/defs.h:733-739`). For a group whose numbers are above 2^31 - 1 no slot matches, so nothing is cancelled and no error is given. Other lookups use `OA_ARTNOEQ()`. `masscancel` is also missing from `man/dgrpctl.8` (it is in the usage text, `util/dgrpctl.c:68`). Found by go-while/cc-1 in the overview analysis; re-checked at main 1c80d48f by go-while/opus-high-B, shown from the code, not run.
Plan: compare with `OA_ARTNOEQ(artEnd, oa->oa_ArtNo)`, and add `masscancel` to the man page. Test: `masscancel` in a group whose numbers start above 2^31.

### D-222 A write error in one group drops the reader feed connection after every group got a number
`dreaderd/group.c`, `NNFeedOverview()`: the first pass (`:531-583`) gives the article a number in every group of `Newsgroups:` and writes the new NE (`AssignArticleNo()`, `:839`); the second pass (`:598-635`) writes the overview. When the write fails for one group, `WriteOverview()` answers `400` and closes the connection (`:923-956`, `:1084-1104`), and the loop goes on with the other groups. The failed group keeps a number with no entry (a gap), and no 235 or 239 is sent, so the feeder offers the article again later, and the groups already written get it a second time under a new number (the reader feed has no duplicate check for it). Found by go-while/cc-1 in the overview analysis; re-checked at main 9aa7c5e3 (first at 1c80d48f) by go-while/opus-high-B, shown from the code, not run.
Plan: answer per article, not per group: log the failed group and go on, and answer 235/239 when at least one group was written; close the connection only for an error that is not limited to one group.

### D-223 GROUP and cancels create an overview index for every group they touch
`dreaderd/group.c:1196`, `GetOverInfo()` opens the group's `over.*` index with `O_CREAT` and initialises a new one (`:1282-1309`: the header and `i` slots, 512 by default, allocated, with two `fsync()`). It is called for any group in the active file by `GROUP` and `LISTGROUP` (`SelectGroup()`, `dreaderd/nntp.c:392`), by cancels (`FindCanceledMsg()`, `group.c:1717-1721`) and by `dcancel -r` (`util/dcancel.c:763`, a copy). So a reader that selects every group creates an index file, with two `fsync()`, for each group that never had an article. Found by go-while/cc-1 in the overview analysis; re-checked at main 9aa7c5e3 (first at 1c80d48f) by go-while/opus-high-B, shown from the code, not run.
Plan: open without `O_CREAT` on the read and cancel paths, and treat a missing index as an empty group; only the feed creates it.
Partly fixed (#460): `GetOverInfo()` (`dreaderd/group.c`) now opens an existing index without `O_CREAT`; on ENOENT it returns NULL without a log line, which its callers already take as a group without articles. The feed creates the index with the group's first article through the new static `OpenOverInfo(group, create)`. The 211 line of `GROUP` and `LISTGROUP` for a group with a missing index gives the low number and count from `max(NB, NE-1024)` (the bound `nntp.c` already uses without an index); main gave `max(NB, NE-A)` from the index it had just created. Test: `TestReaderGroupNoIndexCreate`. Still open: the copies of the old open in `util/dcancel.c` (`dcancel -r`) and `util/dgrpctl.c` (`dgrpctl masscancel`) still create the index (one file per group named, local admin). See also D-293 for the lookup work of a missing index.

### D-224 The locking comment in dexpireover describes locks dreaderd does not take
`util/dexpireover.c:28-33` says dreaderd holds an advisory lock at offset 4 on the data file while it has it open, and one at offset 4 on the info file. The code: dreaderd takes offset 4 shared on the `over.*` index only (`dreaderd/group.c:1217`), offset 0 exclusive on the data file for an append (`:946`) and on the index for a slot write (`:1039`); dexpireover takes offset 4 shared on the index (`util/dexpireover.c:1043`), exclusive for a resize (`:2565`) or a rewrite (`:1898`), and offset 0 per slot (`:723`). Comment only, but it misleads a change of the lock order. Found by go-while/cc-1 in the overview analysis; re-checked at main 9aa7c5e3 (first at 1c80d48f) by go-while/opus-high-B.
Plan: replace the comment with the locks above.

### D-225 A POST whose last body line ends in a bare LF hangs the reader's post connection
`dreaderd/feed.c:261-265` ends a POST on "." whatever the line before ended with, and `:354-367` turns only header lines into CRLF (`bareLF` needs `COF_INHEADER`, `:297-298`); `post.c:1552` sends the body as stored. The feeder (`util/diablo.c:3313`, `:3359`) ends an article only on CRLF before ".", so it takes ".\r\n" as data and waits. dreaderd waits in `NNPostResponse2()` with no timeout. That reader process's post connection never returns: every later POST of its clients hangs, and the article is lost. Who: a client allowed to POST; an old client with LF line ends does it by accident. Crash: none. Effect: posting stops for one reader process, shown. Conditions: default config, diablo as the post server. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestPostBareLFLastLineToFeeder` on gw-r2-repro-a (control 240, then no reply within 15 s for the bare-LF POST and the POST after it). Effort: S.
Plan: send POST body lines with CRLF like the header lines (drop `COF_INHEADER` from the `bareLF` test); optionally a timeout in the post states.
Fixed (#444): `NNAcceptArticle()` (`dreaderd/feed.c`) stores a POST body line that ends in a bare LF with CR LF, as header lines already were: the `bareLF` test no longer needs `COF_INHEADER`. The line contents are unchanged, a line that already ends in CR LF is stored as before, and fed articles (IHAVE, TAKETHIS) are not touched. Tests: `TestPostBareLFLastLineToFeeder`, `TestPostLineEndsToPostServer`. Not done: the optional timeout in the post states; with a post server that never answers, a posting client keeps its place, descriptor and article until a readerdef `idletimeout` or `sessiontimeout`, if one is set, ends it.

### D-226 A group repeated in Newsgroups gets one number and one overview record per repeat
`dreaderd/group.c`, `NNFeedOverview()`: pass 1 (`:530-576`) calls `AssignArticleNo()` (`:565`) for every name, so NE grows by one per repeat (`:819`); pass 2 (`:591-628`) writes an index slot and a copy of the header per name (`:849`). An article with `e2e.test` 600 times takes 600 numbers and overwrites every older slot of a 512-slot index: the older articles answer 423 and vanish from OVER although the spool has them. `NormalizeNewsgroups` does not remove repeats and `maxcross` is off by default. Lines are at main 9aa7c5e3, before the fix. Who: any poster anywhere, via any peer. Crash: none. Effect: wrong data, shown. Conditions: default config (no xref slave, `maxcross` unset); 512 or more repeats (about 5 KB of header) for the full wipe. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproRepeatedGroupOverview` on gw-r2-repro-b (GROUP `211 513 99 611` instead of `211 12 1 12`; STAT 1..10 all 423). Effort: S.
Plan: pass 1 skips a name that already has an `ArtNumAss` entry (compare the names), so a repeated group counts once.
Fixed (#416): pass 1 of `NNFeedOverview()` skips a name that already has a number for this article (`ArtNumAssigned()` compares the names); pass 2 finds no number for the repeat and writes nothing for it. The stored header is unchanged. Tests: `TestReaderRepeatedGroupOneNumber`, `TestReaderRepeatedGroupNotActive`, `TestReaderRepeatedGroupXrefSlave`.

### D-227 The feeder leaks the article map for every 430 Article prohibited
`util/diablo.c:2630` opens the article (`ArticleOpen()`, which maps it, or a malloc on a gzip spool); the age refusal at `:2632-2645` sets `data = NULL`, so the release at `:2750-2755` is skipped. dreaderd passes a reader's `a<N>` argument through (`dreaderd/server.c:1392`), and a 430 is requeued and not cached, so it can be repeated. One mapping leaks per request in the long-lived spool connection child; at `vm.max_map_count` every fetch on that connection answers 430 for stored articles. Who: a reader client (`ARTICLE <id> a1`), and every fetch under readerdef `maxagepolicy`. Crash: none. Effect: lost service, shown. Conditions: default config. Lines are at main 0dd6330e, before the fix (the same at 9dc51c18, where `:2645` is now a comment). Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestFeederAgeRefusalLeaks` on gw-r2-repro-a (41 -> 441 mappings for 400 refused requests). Effort: S.
Plan: test the age before `ArticleOpen()`, or release the map before clearing `data`; together with D-230 (same request path).
Fixed (#417): the "430 Article prohibited" branch keeps `data`, so the common release at the end of the handler frees the mapping (or the copy of a compressed article), as on the "not found" paths. D-230 stays open. Test: `TestFeederAgeRefusalReleasesArticle` (a plain spool and one with `compresslvl 6`; the mapping count and the resident memory of the connection's diablo process stay flat over 300 refused requests).

### D-228 A queued POST leaves the client in article mode
`dreaderd/post.c:1439` queues the POST (`NNServerRequest()` with `SREQ_POST`) without `NNWaitThread()`, so `co_Func` stays `NNAcceptArticle`. Lines the client sends after its "." while the post is in flight join the article being sent; a second "." runs `NNPostBuffer()` on the in-flight buffer; a later ARTICLE overwrites `co_SReq` (`server.c:825`), and when the client closes, `NNTerminate()` (`reader.c:764-773`) leaves the other request writing through the freed Connection. Lines are at main 9dc51c18. Who: a client allowed to POST that pipelines after ".". Crash: not shown; by reading, the reader process with all its clients (about 40). Effect: an altered posted article, shown; a write through freed memory, by reading only. Conditions: default config; timing: the post must still be in flight at a forceallcheck pass (a slow post server or D-225 widens it). Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestPostPipelinedCommandWhileQueued` on gw-r2-repro-a (a GROUP sent after "." got no reply and became the last body line of the posted article). Effort: M.
Plan: `NNWaitThread()` after the POST request, as the fetch paths do; `NNServerRequest()` refuses a client whose `co_SReq` is set. ASan run of the chain on a test host.
Fixed (#446): `NNPostBuffer()` (`dreaderd/post.c`) calls `NNWaitThread()` after `NNServerRequest()` for the POST, as the fetch paths do, and `NNFinishSReq()` gives the client back to `NNCommand()` after the 240 or 441, so commands sent after the "." are answered then, in order. A waiting client is looked at with `recv(MSG_PEEK)` (`waitPostClientGone()`, `dreaderd/reader.c`) so that a client that went away is let go; #448 and #450 changed that look later (only a socket error lets the client go, and it also takes a post still queued for a post server). Tests: `TestPostPipelinedCommandWhileQueued`, `TestPostClientGoneWhileQueued`. Not done: `NNServerRequest()` does not refuse a client whose `co_SReq` is set (defensive only: a client that waits for its post is not read); the ASan run of the chain on a test host.

### D-229 wireformat misses the article end when CR and LF arrive in two buffer pieces
`util/diablo.c:3312-3313` needs CR and LF in one piece; text mode puts a trailing CR back (`:3337-3345`), wire mode does not (`:3346-3349`), and `bgets()` returns a full buffer without LF (`lib/buffer.c:245-260`). A line of k x DataMax - 1 octets (4027 on a 64-bit host with zlib) before the final dot hides the end: IHAVE stalls, and by reading, with TAKETHIS the next commands and article become body. Who: any article author, via any peer. Crash: none. Effect: that connection stalls, shown; merged articles, by reading. Conditions: `wireformat on` (an option). Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestWireFormatCRLFSplitAcrossBuffer` on gw-r2-repro-a (IHAVE: no reply, `ArtIncomplete` logged; TAKETHIS not run). Effort: S.
Plan: remember that the previous piece ended in CR, and take a piece that starts with LF after it as CRLF (wire mode).
Fixed (#419): in wire mode `LoadArticle()` (`util/diablo.c`) reads a full piece that ends in CR together with the octet after it, as text mode does, so a CR LF split over two pieces ends the line; the article size counts that CR once and the stored bytes are unchanged. A bare CR at the end of a full piece is now also refused under `rejectartswithbarecr on` in wire mode, as text mode already did. Tests: `TestWireFormatCRLFSplitAcrossBuffer`, `TestWireFormatBareCRAtBufferBoundary`.

### D-230 A reader's age argument replaces maxagepolicy
`dreaderd/server.c:1310` starts from `rd_MaxAgePolicy`, and `:1390-1393` replaces it with the client's `a<N>`; `a0` sends no limit (`dreaderd/spool.c:74`). Any reader then gets articles beyond its retention, by Message-ID. Who: a reader client. Crash: none. Effect: the retention limit is not applied, shown. Conditions: readerdef `maxagepolicy` set (an option). Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestReaderAgeArgOverridesPolicy` on gw-r2-repro-a (`maxagepolicy 1`: plain ARTICLE 430, `a0` and `a999999999` 220). Effort: S.
Plan: the client value may only narrow: min(policy, N) when the policy is set, ignore N < 1.
Fixed (#426): `NNArticleRetrieveByMessageId()` (`dreaderd/server.c`) uses the client's `a<N>` only when it narrows the limit: a positive N (at most INT_MAX) below `maxagepolicy`, or such an N when there is no policy. `a0`, a larger N, a negative number or an argument without a numeric prefix keep the policy; no new reply is added, a request the policy excludes gets the existing 430. Test: `TestReaderAgeArgNarrowsPolicy`.

### D-231 A spool request requeued mid-article sends a second status line and article
`dreaderd/spool.c:892-897` calls `NNServerTerminate()` on the `T` timeout (counted from the queue time), and `dreaderd/server.c:1178-1195` requeues the request without touching `sr_Cache` or the bytes already sent to the client. The next server's 220 adds a new status line and the whole article. The same holds for the "." in header requeue (`dreaderd/spool.c:619-628`) and for a server removed from dserver.hosts (`server.c:98-106`). By reading, `sr_Cache` can commit a partial plus full copy, or leak the FILE. Who: nobody directly; a slow or removed spool server, and the client that fetches then. Crash: none. Effect: wrong reply, connection out of step. Conditions: the `T` flag or a dserver.hosts edit; a transfer that stalls mid-article. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproRequeueMidArticle` on gw-r2-repro-b (client half: status line, partial text, a second `220` line and the whole article; the cache half was not shown). Effort: L.
Plan: in the requeue paths abort and close `sr_Cache`; once bytes went to the client, end the article like the `len < 0` branch instead of requeuing.
Fixed (#484, network spool, client half): when the connection to a network spool server is terminated (the `T` timeout, a server removed from dserver.hosts, or EOF) after fast copy has put part of the reply into the client's transmit buffer (`sr_SpoolOutput`), `NNServerTerminate()` no longer requeues the request: `NNFinishInterruptedSpool()` ends it once, with the "(spool server died prior to completion of the article dump)" line and "." (ARTICLE, BODY, BODYWVF, BODYNOSTAT, HEAD) or nothing more (STAT), and the partial reader-cache copy is dropped. A request that has sent nothing to the client is still requeued on the `T` timeout, a removed server or EOF, with `FastCopyOpt` on or off; `NNSpoolResponse3()` no longer ends an EOF with a simulated "." and terminates the server instead. Test: `TestSpoolInterruptedFetch` (a C driver with stubs for the retry decision). Still open: the line "(spool server died ...)" is also written inside the 221 block of a HEAD reply before the "."; and the local spool (`dreaderd/dfa.c`, fast copy then `NNServerTerminate()`) still requeues after output went out (D-233 area).

### D-232 The reader feed leaks Supersedes and repeated headers
`dreaderd/group.c`, `NNFeedOverview()`: `supers` (`:442`) is never freed (`:661-667` frees six others), and each repeated Message-ID, Subject, Date, Xref, Control or Newsgroups line allocates without freeing the previous one. `co_MemPool` is freed only when the feed connection closes. Lines are at main 9dc51c18. Who: content via feeds: every superseding article leaks a little; repeated header lines leak much more. Crash: the feed fork runs out of memory in the end. Effect: lost service, shown. Conditions: default config. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproSupersedesLeak` on gw-r2-repro-b (2000 articles with 500 Supersedes lines: +28 MB, about 15.6 kB per article). Effort: S.
Plan: free `supers` with the others and `zfreeStr()` each field before it is assigned again.
Fixed (#421): `NNFeedOverview()` frees `supers` with the other values, and frees the earlier copy of a repeated Message-ID, Subject, Date, Xref, Control, Newsgroups or Supersedes line before it stores the new one (the last header still counts). Test: `TestReaderFeedRepeatedHeadersMemory`.

### D-233 localspool serves the raw spool record
`whereis` answers the offset and length of the record including its 24-byte `SpoolArtHdr` (`util/diablo.c:2546`, `:4066`); dreaderd's local path (`dreaderd/spool.c:240-300`, `dfa.c`) knows nothing of that header, of gzip or of the wire store. Every article starts with binary bytes before `Path:`, the Path/Xref rewrite is skipped, gzip records are sent compressed and wire records dot-stuffed twice. By reading, DFA state 0x05 (`dfa.c:518-545`) also loses the header end after a 5-character last header line. Who: nobody; every article fetched on a host that uses `localspool=`. Crash: none. Effect: wrong output. Conditions: `localspool=` against a diablo spool (an option). Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproLocalspoolDiabloSpool` on gw-r2-repro-b (the first defect; the DFA part was not shown). Effort: L (S to document or refuse localspool).
Plan: `whereis` returns offset + header length for plain text records and refuses gzip and wire records (the reader falls back to ARTICLE); do the Path/Xref rewrite on the local path; fix the 0x05 case. Or document localspool as unusable with a diablo spool.
Fixed (#501): `ArticleWhere()` (`util/diablo.c`) answers `whereis` with the offset and length of the article text after the `SpoolArtHdr` (a record without a header, from an old spool, as before); a gzip record, a wire-format one, a header-only one and one that cannot be read get `503 whereis <id>: <reason>, use ARTICLE`, which `NNGetLocal1()` (`dreaderd/spool.c`) already answered with ARTICLE. `NNGetLocal1()` also fetches over NNTP a range that starts with the `SpoolArtHdr` magic (0xff 0x99), as an older diablo names it. The DFA (`dreaderd/dfa.c`) had its own Path/Xref rewrite, which differed from the NNTP path (`NNSpoolResponse2()`): it put the name in front of a Path that began with it, and made up `Path: <name>!not-for-mail` and `XRef: <name> no-xref` for an article without them. It now prefixes the Path unless the name is first (`DFAPathDecide()`, the value's first characters are held back across reads) and makes no header up, and resets `dfa_Flag` and `dfa_Field` per article (they kept the last article's). The 0x05 state hands the end of a 5-character line to the generic state; WRDDEL no longer leaves its position one past the data at the end of the buffer (a stale byte went out); BODY doubles a "." at the start of the first body line. The Path value is decided on after the white space after the colon (not counted towards the characters that decide, up to 256 held back, more goes out first), for any white space and for "Path:" without any; a header line that starts with a dot is doubled. Tests: `TestLocalspoolDiabloPlain`, `TestLocalspoolDiabloFallback`, `TestLocalspoolDiabloWhereis`, `TestLocalspoolDfaFiveCharHeader`, `TestLocalspoolDfaBodyFirstDot`, `TestLocalspoolDfaBufferEnd`, `TestLocalspoolDfaPathWhitespace`, `TestLocalspoolDfaPathWhitespaceEdge`, `TestLocalspoolDfaPathBlankOverflow`, `TestLocalspoolDfaDotHeader`, `TestLocalspoolHeaderedRecordFallsBack`. Not changed: the local read has no reader-cache write (`sr_Cache` is only created for a 220 from the spool server), and a read error in the middle of the local read still ends in `NNServerTerminate()` and requeues (see D-231).

### D-234 Two fetches of one new article in one reader process corrupt its cache file
`dreaderd/cache.c:260` opens the same `.tmp`, `:264` gets the fcntl lock the process already holds (`lib/lock.c:26`), and `cache.c:279` truncates under the first writer. The committed cache file can hold a NUL run, a splice of two copies of different length, or be truncated by `AbortCache()` (`cache.c:305-307`), and it is served to every later reader until dexpirecache. Who: any two clients asking for one new article. Crash: none. Effect: wrong data, shown. Conditions: default config (readercache on), two spool connections, copies of different length or timing. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproCacheSameArticleTwice` on gw-r2-repro-b (a third client got the short copy followed by the tail of the long one). Effort: M.
Plan: `CreateCache()` checks the process's in-flight requests for the same Message-ID (or a unique tmp name per writer that never truncates a non-empty file); `AbortCache()` must not truncate a committed file.
Fixed (#428): `dreaderd/cache.c` keeps a list of the `.tmp` cache files this process is writing, and `CreateCache()` leaves an article alone that the process is already caching, so the second of two simultaneous fetches is served to its client without caching. Both requeue paths in `dreaderd/server.c` abort and close the request's cache file first (`requeueDropCache()`). Tests: `TestReaderCacheSameArticleTwice`, `TestReaderCacheRequeueMidHeader`.

### D-235 dexpireover -R empties trackonly indexes
In trackonly mode every entry has `oa_SeekPos` -1 (`dreaderd/group.c:1048`); `rewriteDataFile()`'s `-1` branch does nothing (`util/dexpireover.c:2519-2520`), so the new all-zero index is written. The next expire moves NB past NE: all articles of the group drop out of the active range. The shipped weekly.reader runs `-R`. Lines are at main 9dc51c18. Who: the local admin or the weekly cron. Crash: none. Effect: data loss. Conditions: `readerxover trackonly` (an option) and `-R`. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproTrackonlyRewrite` on gw-r2-repro-b (after `-R` and the next `dexpireover -a`: GROUP `211 1 20 20` instead of `211 20 1 20`). Effort: S.
Plan: copy the entry (`*ob = *oa`) in the `oa_SeekPos == -1` branch, with no data file.
Fixed (#423): `rewriteDataFile()` copies a record without a data offset (`oa_SeekPos` -1) unchanged into the new index. `dexpireover -z` leaves an empty data file alone, and `-R` takes a `.gz` that inflates to nothing as a range without data, only when no index record of that range has a data offset. Tests: `TestDexpireoverRewriteTrackonly`, `TestDexpireoverRewriteMixedRecords`, `TestDexpireoverRewriteTrackonlyGzip`.

### D-236 A DNS failure during the host cache rebuild locks out peers named by host name
`lib/hostauth.c:252-264`, `convHost()` writes the records of an inhost given by name only when `getaddrinfo()` succeeds; the new dhosts.cache replaces the old one (`:179-187`), and the next rebuild is up to an hour later (`:59-97`). A resolver failure at the rebuild gives every peer named by host name 502, also after DNS is back. Who: nobody; a resolver failure at the rebuild. Crash: none. Effect: lost service, shown. Conditions: peers listed by host name. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2HostCacheDNSBlip` on gw-r2-repro-c (a getaddrinfo shim returns EAI_AGAIN during a forced rebuild: the peer `localhost` gets 502 afterwards). Effort: M.
Plan: on a temporary failure keep that name's records from the old cache, or keep the old cache and retry the rebuild soon.
Fixed (#424): the rebuild opens the previous cache; when the lookup of a name gets no answer for now (EAI_AGAIN, EAI_MEMORY, EAI_SYSTEM), `keepOldHost()` (`lib/hostauth.c`) writes the addresses the previous cache had for the same label and entry, each once, and logs `host cache: <name>: <reason>, N previous address(es) kept`. Other lookup errors drop the name as before. Tests: `TestHostCacheDNSFailureKeepsPeers`, `TestHostCacheDNSFailureNoDuplicates`, `TestHostCacheDNSFailureAfterFreshLookup`.

### D-237 PAM authentication skips the account phase
`dreaderd/dns.c:1766-1791`, `PamAuthenticate()` calls `pam_start()`, `pam_authenticate()` and `pam_end()`, never `pam_acct_mgmt()`, and sets no `PAM_RHOST`. An account that PAM's account phase refuses (expired, disabled, denied by pam_access or pam_time) logs in. Who: a client with a valid password for such an account. Crash: none. Effect: an account the account phase refuses logs in, shown (with a PAM shim). Conditions: a `pam` authdef, dreaderd built with PAM. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2PAMAccountPhase` on gw-r2-repro-c (`pam_acct_mgmt()` returns PAM_ACCT_EXPIRED and is never called: 281). Effort: S.
Plan: call `pam_acct_mgmt()` after a successful `pam_authenticate()` and set `PAM_RHOST`. A PAM service with only auth lines falls back to `other` for account (often pam_deny), so working logins can change: a release note, or an opt-in.
Fixed (#487): `PamAuthenticate()` (`dreaderd/dns.c`) sets `PAM_RHOST` to the client's address before `pam_authenticate()` and, after a successful one, calls `pam_acct_mgmt()`; only `PAM_SUCCESS` from both logs in. `PAM_NEW_AUTHTOK_REQD` refuses too (an NNTP client cannot change its password). A refusal is logged at LOG_NOTICE: `PAM account check for <user> from <address>: <pam_strerror text>, login refused`. On by default, no option; a PAM service without an `account` line now needs one (release note). Test: `TestPAMAccountPhase`, adapted from go-while's `TestR2PAMAccountPhase`.

### D-238 drequeue keeps writing into a rotated queue file
`util/drequeue.c:188` opens `dqueue/<label>` once with `O_APPEND` and never looks at it again (`:273-292`). dspoolout renames the file and dnewslink removes it after sending, so everything drequeue writes after a rotation is lost, and drequeue exits 0. A `diloadfromspool -Q | drequeue` run longer than the 5-minute cron loses the rest. Who: the local admin or cron. Crash: none. Effect: data loss. Conditions: a drequeue run that spans a rotation. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2DrequeueRotatedQueue` on gw-r2-repro-c (rename and remove as dspoolout and dnewslink do: 330 of 600 lines lost, exit 0). Effort: S.
Plan: before each flush compare `fstat()` of the descriptor with `stat()` of the path and reopen when the file was replaced.
Fixed (#435): `flushFeedOut()` (`util/drequeue.c`) compares the descriptor with the name (fstat against stat) before each write and opens `dqueue/<label>` again when the name points at another file or at none. If the file cannot be opened, drequeue says so once per failure series, keeps the lines and tries again at the next flush; lines that cannot be queued are counted, and drequeue prints `drequeue: <label>: N lines were not queued` and exits 1. Tests: `TestDrequeueRotatedQueue`, `TestDrequeueReopenFails`, `TestDrequeueOpenFails`.

### D-239 A trailing slash in path_spool makes dexpire mark the spool expired in the history
`util/dexpire.c:405-411` keeps a spool path with its trailing slash, the prefix test at `:1207` needs `partnameLen > spoolHomeLen`, and `ArticleFileName()` adds its own slash or blanks spool 00 (`lib/spool.c:86-89`, `:152-159`). No name matches, so `dexpire -h1` marks every history entry of that spool expired, and the feeder answers 430 for articles that are still stored. A valid spelling, not a typo. Who: the local admin (config). Crash: none. Effect: data loss. Conditions: a trailing slash in `path_spool` or a spool `path`. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2DexpireSpoolPathSlash` on gw-r2-repro-c (nothing old enough to expire; both articles 430 after `dexpire -a -h1`). Effort: S.
Plan: strip trailing slashes (not "/" itself) from `path_spool` and spool paths when they are loaded.
Fixed (#433): `loadSpoolCtl()` (`lib/spool.c`) strips trailing slashes from spool paths (a path `/` stays), `GetSpoolPath()` and `ArticleFileName()` add no second slash, and `updateHistory()` and `DumpSpoolEntries()` (`util/dexpire.c`) compare with the spool home without trailing slashes. A `path <spool home>/` now means the spool home itself, on spool 00 too (it used to become `P.nn`). Test: `TestDexpireSpoolPathSlash`.

### D-240 An unstuffed first line desyncs the external filter
`filter/diab-filter.c:349` starts its copy with `eoln = 0`, and the copy starts at the first byte of the stored article (headers included, `:346`), so an article whose very first line is "." (sent as `..` and stored unstuffed) goes to the filter starting with ".\r\n"; a "." line later in the article, the body's first line included, follows a line end and is stuffed. A filter that answers per "." line answers twice; `DiabFilter()` takes the reply to each article from one read() (`:396`), so when the two answers come in separate reads the following articles get the verdict meant for the article before them. Who: content via feeds (one article from any peer). Crash: none. Effect: wrong filter verdicts (spam passes, ham is dropped from nospam feeds), shown. Conditions: `feederfilter` configured, a filter that answers per "." line. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestFilterFirstLineDotStuffed` on gw-r2-repro-a. Effort: S.
Plan: start the copy loop with `eoln = 1`.
Fixed (#432): `DiabFilter()` (`filter/diab-filter.c`) starts the copy at a line start in text mode (`eoln = wireformat ? 0 : 1`), so the first line is stuffed like the others; a `wireformat` article is stored stuffed and is copied as it is. Test: `TestFilterFirstLineDotStuffed`.

### D-241 Articles the external filter cannot read pass unjudged
`filter/diab-read.c:89-118` returns -1 for any NUL in the record (compressed records have them); `DiabFilter()` then returns -1 without sending (`filter/diab-filter.c:338-343`), and -1 matches neither skip rule, so the article goes to nospam and onlyspam feeds alike, with no log line. Who: content via feeds (one NUL byte in any article); with a compressed spool object every article. Crash: none. Effect: the filter verdict is not applied, shown. Conditions: `feederfilter` with nospam or onlyspam feeds; `rejectartswithnul` off (the default), or `compresslvl` on a spool object. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestFilterUnreadArticlesPass` on gw-r2-repro-a (subtests nul and compressed). Effort: M.
Plan: send the article by its length (no NUL scan) and inflate compressed records; log and count a skipped article; keep passing through as the default.
Fixed (#434): `diab_read()` (`filter/diab-read.c`) reads the record the way the feeder reads it to serve it (the spool header skipped, a gzip record inflated), and `DiabFilter()` sends the article by its length, so a NUL byte no longer fails the read. An article that cannot be read is logged at LOG_ERR (`cannot read article ..., not filtered`) and passes as before: the default stays pass-through. Tests: `TestFilterUnreadArticlesJudged`, `TestFilterBadRecordPassesLogged`. Not done: the count of skipped articles that the plan names; none is kept.

### D-242 The feeder forks and does reverse DNS before the access check
`util/diablo.c:1332` forks for every connection, and the child does `getnameinfo(NI_NAMEREQD)` and a forward lookup (`lib/hostauth.c:448-450`) with no time limit before it reads dhosts.cache. Whoever answers the PTR query of the connecting address sets how long a fork slot and a master pipe stay taken, and a peer listed by address waits for the same lookup. Who: anyone on the network who controls the PTR answers for their address (or a slow resolver). Crash: none. Effect: delayed greetings and fork slots taken, shown. Conditions: default config (`maxconnect` off). Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2AcceptDNSBeforeAccess` on gw-r2-repro-c (PTR answers delayed 8 s: a peer listed by address waited 8 s for its greeting; 81 to 117 stranger children alive at once). Effort: L.
Plan: match address, CIDR and wildcard-IP entries before any lookup; do the PTR and forward lookup only when name entries exist, with a time bound in the child.
Fixed (#497): `Authenticate()` (`lib/hostauth.c`) matches the connecting address first, with no lookup: IP entries, CIDR and wildcard-IP entries, and name entries by the addresses `dhosts.cache` has for them. Only when none matches and a wildcard name entry exists does `peerName()` do the reverse and forward lookup, in a child process the session waits for at most 20 s (`HOSTAUTH_DNS_SECS`; the two queries run one after the other, and with the first nameserver dead glibc's 5 s timeout each makes them take about 10 s); after that the child is killed, the address has no name and "DNS lookup of <address> took longer than 20 s, its name is not used" is logged. An address that matches no address entry gets no lookup unless a wildcard name entry exists (then every such connection gets it, bounded by the limit). The host name of the session (logs, the per-host statistics slot) is the entry's name for a name entry matched by its cached address, the confirmed name for a match by name, and the address for an IP, CIDR or wildcard-IP entry (before: the PTR name whenever it was confirmed). Test: `TestFeederDNSAfterIPMatch`, `TestFeederDNSByNameTimed`. Not changed: the fork per connection, which happens before `Authenticate()`.

### D-243 A queued request whose client went away crashes the reader process
`NNTerminate()` sets `sr_CConn` to NULL and leaves the request queued (`dreaderd/reader.c:764-773`); `cbQueueReqFindThread()` dereferences `sr_CConn` (`dreaderd/server.c:638-639`) when the server has `groupdef=`. The reader process dies with all its clients. Lines are at main 9dc51c18. Who: a client that disconnects or idles out while its request waits for a server. Crash: the reader process, all its clients (about 40). Effect: lost service, shown. Conditions: a spool server line with `groupdef=` (an option); every matching server unreachable for longer than the client stays. Found in go-while's deep review round 2 of main 1c80d48f; reproduced on a test host with `TestR2ReproQueuedRequestClientGone` on gw-r2-repro-b (`Lost reader process` when the spool server came back). Effort: S.
Plan: `QueueServerRequests()` drops requests whose client is gone, and the callback tests `sr_CConn`.
Fixed (#422): `QueueServerRequests()` (`dreaderd/server.c`) frees a waiting request whose client is gone (article list and post list), `NNFinishSReq()` does not requeue such a request, and `cbQueueReqFindThread()` matches no server for a request without a client. Test: `TestReaderQueuedRequestClientGone` (subtests `article` and `post`).

### D-244 A bare-LF blank line after the header breaks BODY and the reader cache in wire mode
`util/diablo.c:3478-3479` takes a bare-LF blank line as the end of the header, and `:3033-3036` then skips two bytes for the separator of a wire-format record, so BODY drops the first byte of the body; dreaderd (`dreaderd/spool.c:594`, `:619-628`) answers 430 for the stored article and leaks one cache FILE per fetch. Who: content via feeds (a bare-LF blank line after the header), through a peer. Crash: none. Effect: wrong data; open FILEs grow in the reader process. Conditions: `wireformat on`; reader cache; more than one spool server for the requeue. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: the feeder uses the stored separator length for wire records; dreaderd accepts LF at the header end; the FILE leak goes with D-231. Do not rewrite stored bytes.

### D-245 Commands buffered before the session byte limit still run
`dreaderd/reader.c:1217-1231` sends "400 Session byte limit" and calls `NNTerminate()`, but pipelined commands already in the buffer still run (`:1245`). Lines are at main 9dc51c18. Who: a reader client. Crash: none. Effect: commands run after the limit, at most one read buffer of them. Conditions: a byte limit configured. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: return after `NNTerminate()` in `NNCommand()`.
Fixed (#463): `NNCommand()` (`dreaderd/reader.c`) returns right after the 400 line and `NNTerminate()`, so the connection's next step stays `NNTerminate()` and a command the client had already sent gets no reply after the 400. `NNTerminate()` now sets the write bit only when no ratelimit timer holds the output; before, with a `ratelimit`, the select loop returned at once for the writable socket, pass after pass, until the queued reply had gone out. Tests: `TestReaderByteLimitStopsPipelined`, `TestReaderByteLimitRateLimitNoSpin`. Not changed (by reading): the limit counts only bytes already written to the socket, so a slow reader can still be answered further commands while up to 256K is queued.

### D-246 NNSpoolResponseScrap continues as the 220 path
`dreaderd/spool.c:908` (reached from `:430-432`) continues as `NNSpoolResponse3`: the client gets text and "." with no status line, and the article's tail is committed as its cache entry. Who: nobody directly; a race where the overview entry goes away between the request and the 220. Crash: none. Effect: wrong reply, bad cache entry. Conditions: HEAD or ARTICLE via the overview path; timing. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: set `co_Func` to `NNSpoolResponseScrap` and abort the cache instead of committing it.

### D-247 A requeue skips a same-priority spool server that has the article
`dreaderd/thread.c:147-171` and `dreaderd/server.c:729-737` pick the least-loaded server on a requeue, so a server of the same priority can be skipped: 430 or 423 for an article it has. Who: a reader client (normal fetch). Crash: none. Effect: wrong behaviour. Conditions: several spool servers of one priority, the article on a subset. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: on a requeue walk the same-priority servers in ring order from `sr_Rolodex`, or remember the servers tried.

### D-248 The NEWGROUPS cache is built from the first connection's listgroups
`dreaderd/list.c:240-243`, `:291-292` and `:177-181` build the NEWGROUPS cache filtered by the first connection's listgroups and serve it to all, so other readerdefs see too many or too few groups. Who: a reader client. Crash: none. Effect: a wrong group list (group names only). Conditions: several readerdefs with different listgroups. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: build the cache unfiltered and filter per connection on output.

### D-249 NEWNEWS runs the next pipelined command inside its list
`dreaderd/xover.c:600-606` calls `NNCommand()` at the end of each group, which reads the next pipelined command (`dreaderd/reader.c:1279`; the `co_FCounter` guard at `:1256-1266` only puts it off to a later pass), so that command runs, now or on the next pass, and answers inside the 230 block. Lines are at main 9dc51c18. Who: a reader client that pipelines after NEWNEWS. Crash: none. Effect: only its own session. Conditions: `allownewnews` (opt-in). Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: call `NNCommand()` only after the final ".".
Fixed (#461): `ListOverviewRangeRun()` (`dreaderd/xover.c`) only sets `co_LastActiveTime` at the end of a NEWNEWS group and returns to `NNListNewNews()`; the per-group call to `NNCommand()` was removed, so the normal byte-limit check and the next command no longer interrupt the list, and `NNCommand()` is called after the final ".". A client that goes away during a NEWNEWS scan that prints nothing can go unnoticed until the list ends (by reading, not run). Test: `TestReaderNewnewsPipelinedCommand`.

### D-250 NEWNEWS with a wide wildmat walks all groups in one turn
`dreaderd/xover.c:132-161` and `:580-583` walk every matching group in one pass; the reader process serves nobody else meanwhile, and before #460 (D-223) each group got an index file. Who: a reader client. Crash: none. Effect: one reader process serves nobody else meanwhile. Conditions: `allownewnews` (opt-in); many groups. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: bound the groups per pass and yield via WFds.

### D-251 An overview data file over 2 GB stops the reader process
`dreaderd/group.c:1051` stores the append position in the int `oa_SeekPos`; past 2 GB it is negative, and `:1462-1474` exits on "mmap() failed B" for each access to the group. Lines are at main 9dc51c18. Who: nobody in practice. Crash: every reader process that touches the group (exit 1). Effect: lost service. Conditions: an overview data file over 2 GB (a large `e`, non-default). Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: refuse appends past INT_MAX and take a negative `oa_SeekPos` as no data, without exit(1). A 64-bit field would change the on-disk format.

### D-252 dexpireover -O deletes a group that got an article during the run
`util/dexpireover.c:549-561` decides on the LMTS read at the start, also when the group got an article during the run. Who: the local admin or cron (race). Crash: none. Effect: data loss. Conditions: `dexpireover -O`; timing. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: re-read LMTS under `KP_LOCK` just before `KPDBDelete()`.
Fixed (#431): `deleteStaleGroup()` (`util/dexpireover.c`) reads the record again with `KP_LOCK` and deletes the group only if its LMTS is still older than the cutoff. Tests: `TestDexpireoverStaleGroupNewArticle`, `TestDexpireoverStaleGroupDryRun`.

### D-253 XZVER leaks its deflate state when a timeout closes the connection
`dreaderd/mbuf.c:291-318` (`MZInit()`), `:372-386` (`MZFinish()`, which frees the state) and `dreaderd/reader.c:679-744`: the deflate state (about 260 KB) of an XZVER list is not freed when a timeout closes the connection mid-list. Lines are at main 9dc51c18. Who: a reader client (XZVER, then let the session time out). Crash: none. Effect: memory growth, paced by the timeout. Conditions: XZVER enabled. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: finish and free `co_ZStream` in `DeleteConnection()`.

### D-254 Group creation can reset NE of a numbered group
Group creation is not create-if-absent (`dreaderd/group.c:708-721`, `dreaderd/control.c:967-969`, `:1729-1735`, `lib/active.c:231-243`, `util/dsyncgroups.c:704-750`): a second creator writes NE=0 over a group that already has numbers, so article numbers are used twice. The same-value test #413 added to `KPDBWrite()` does not change it (0 is a new value). Lines are at main 9dc51c18. Who: a control message (newgroup) and the local admin or cron at the same time. Crash: none. Effect: wrong data. Conditions: two creators at once; timing. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: L.
Plan: a KPDB create-if-absent helper (re-read under the lock, never write NB/NE over an existing record), used by every creator.
Fixed (#507): `KPDBCreate()` (`lib/kpdb.c`) makes a record with its first field only if there is none: it looks again under the append lock, which every record creation takes (letting go of it before it waits for the lock of a record that appeared), and returns 1, with nothing written, when the record exists. Every creator uses it for its first field (NB) and goes on with NE and the rest only when it made the record: `AssignArticleNo()` (`dreaderd/group.c`) and `GenerateXRef()` (`lib/active.c`) leave a group that exists by then as it is; `CtlNewGroup()` (`dreaderd/control.c`) and `GrpCtlNewGroup()` (`util/dgrpctl.c`) treat it as an existing group, as they do one that was there at the first read (moderator, description, times and status are set, NB and NE stay); `CgCreate()` and `GrpCtlCheckGroups()` leave it as it is and say so; `dsyncgroups` (`util/dsyncgroups.c`) leaves a new group that exists by the write-back as it is, and holds the record lock of a group it creates from the NB write to the last one (KP_LOCK, KP_LOCK_CONTINUE, then an unlock), as the other creators do, so a feeder or dreaderd reading the record meanwhile waits and never sees it with NB but no NE or NX. What `dsyncgroups -N...` writes on a group that was there at its scan is unchanged (it syncs the numbers on purpose). Tests: `TestGroupCreateDgrpctlNewgroup`, `TestGroupCreateDgrpctlCheckgroups`, `TestGroupCreateDsyncgroups`, `TestGroupCreateDsyncgroupsLock`, `TestGroupCreateControlNewgroup`, `TestGroupCreateControlCheckgroups`, `TestGroupCreateReaderAutoAdd`, `TestGroupCreateFeederAutoAdd` (an LD_PRELOAD shim holds the first creator after its look and before its write while a second one makes the group and numbers it).

### D-255 An opener waiting for dkp -t keeps the old dactive.kp
`lib/kpdb.c:307-336` and `util/dkp.c:139-199`: a process that waits for `dkp -t`'s lock keeps the unlinked old file and writes into it, so Xref numbers are given twice. Lines are at main 9dc51c18. Who: the local admin (`dkp -t`), race. Crash: none. Effect: wrong data. Conditions: a narrow window. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: after the shared lock compare `fstat()` with `stat()` (inode, nlink) and reopen a replaced file.

### D-256 A power loss can leave history chain heads past the end of the file
`lib/history.c:1089-1124` writes the head before the record is on disk; after a power loss on ext4 or XFS a head can point past EOF, and older entries of that chain are unreachable until dhisexpire (duplicates can be accepted again). Who: nobody (power loss). Crash: none. Effect: wrong data. Conditions: a crash of an ext4 or XFS host. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: at open take heads past the record count as chain end and log it at LOG_CRIT (recovers nothing; the real cure is write ordering with fsync).

### D-257 dnewslink removes a rotated queue file before its deferred entries are sent
`util/dnewslink.c:4091-4100`, `:1014-1034` and `:1568-1573`: in realtime mode the rotated file is removed before entries the peer deferred with 431 are sent again; a session end then loses them. Who: an outbound peer that answers 431, then a session end. Crash: none. Effect: lost queue entries. Conditions: dnewslink realtime. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: remove the rotated file only when the defer queue and the pending stream are empty.

### D-258 A look-ahead dnewslink takes a queue file before the master flushes it
`util/dspoolout.c:726-727`, `:817-840` and `util/dnewslink.c:833-845`, `:1560-1562`: a look-ahead batch can take the file dspoolout just renamed before `dicmd flush`, so the master's flushed lines go to a file that is gone. Who: nobody directly (dspoolout and dnewslink timing). Crash: none. Effect: lost queue lines. Conditions: a look-ahead batch; timing. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: rename to a name outside the sequence, flush, then rename to `label.S<k>`.

### D-259 One failed fork stops host cache rebuilds until a restart
`lib/hostauth.c:103-107` and `util/diablo.c:989-990`: a failed `fork()` leaves `HostCachePid` at -1, and the cache is never rebuilt again, so diablo.hosts and DNS changes are ignored. Who: nobody (one fork failure under resource pressure). Crash: none. Effect: changed peers are not picked up. Conditions: a fork failure. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: reset `HostCachePid` on a failed fork.

### D-260 A peer that repeats feedrset and feedcommit starves the dnewsfeeds reload
`util/diablo.c:5084`, `lib/newsfeed.c:1233-1241`, `:1270-1277` and `lib/hostauth.c:67-86`: each feedcommit touches dnewsfeeds, so its mtime stays younger than 10 s: no reload, and a host cache rebuild every 10 s. Who: a peer allowed those commands. Crash: none. Effect: mild: CPU and DNS load, and the reload waits. Conditions: a peer allowed feedrset/feedcommit. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: touch dnewsfeeds only on a real change, and do not rebuild the host cache on that touch.

### D-261 AltFeed clears the label's requiregroup list
`lib/newsfeed.c:2195` and `:2239`: when a peer feed table exists, AltFeed drops the label's requiregroup list, so articles go out that should be held back. Who: the local admin (config). Crash: none. Effect: feed rules not applied as written. Conditions: AltFeed with a peer feed table. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: walk `prNode` to the end of the list, as for `pgNode` and `psNode`.
Fixed (#477): `AltFeed()` walks `prNode` to the end of the list too, so a peer feed table adds its requiregroup lines to the label's instead of emptying (or replacing) them. Test: `TestDnewsfeedsPeerTableRequiregroup`, `TestDnewsfeedsRulesReload`.

### D-262 groupref inside IFILTER, ISPAM and ESPAM is ignored
`lib/newsfeed.c:2029-2034`, `:2108-2129` and `:2498-2499` never resolve a groupref in those feeds; it is silently ignored. Who: the local admin (config). Crash: none. Effect: feed rules not applied as written. Conditions: that construct in dnewsfeeds. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: resolve the group lists of the IFILTER, ISPAM and ESPAM feeds too (grouprefs there then take effect).
Fixed (#477): `LoadNewsFeed()` resolves the group lists of IFILTER, ISPAM and ESPAM too (`resolveFeedGroupLists()`); the simple defaults of a groupdef (`resolveDefaults()`) still do not apply to those labels. Test: `TestDnewsfeedsGrouprefIncomingFilter`, `TestDnewsfeedsGrouprefSpamLabels`.

### D-263 GLOBAL is applied again at every groupref
`lib/newsfeed.c:2487-2488` applies GLOBAL again at the start of the scan inside every groupref, so it overrides the rules the label set before that groupref. Who: the local admin (config). Crash: none. Effect: feed rules not applied as written. Conditions: GLOBAL together with grouprefs. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: apply GLOBAL only at the top level of the scan.
Fixed (#477): `recursiveScan()` applies GLOBAL at the top of the scan and `scanFeedList()` walks the label's explicit grouprefs without applying it again, so GLOBAL no longer overrides what the label set before a groupref (at the top GLOBAL's list still runs twice, once more through the implicit "GLOBAL" groupref at the head of a label's group, alias, spam, requiregroup, filter and offerfilter lists (not spamalias), both before the label's own lines). A groupref in GLOBAL to a groupdef no longer recurses to the limit ("Infinite recursion in dnewsfeeds file!"); a `groupref GLOBAL` inside GLOBAL refers to itself and still does. Test: `TestDnewsfeedsGlobalNotReappliedInGroupref`, `TestDnewsfeedsRulesReload`.

### D-264 The host cache rebuild child keeps the listening socket
`lib/hostauth.c:103-192` and `util/diablo.c:742-783`: the rebuild child inherits the listening socket, so a restart during a DNS outage fails to bind while the child waits for DNS. Who: nobody (a restart during a DNS outage). Crash: none. Effect: the restart fails. Conditions: a DNS outage at restart. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: close the listening sockets in the rebuild child.

### D-265 A NUL in Newsgroups hides the names after it
`util/diablo.c:3679` and `lib/subs.c:145` copy Newsgroups as a C string, so the names after a NUL are hidden from filters and feed rules while the stored header keeps them (the Path: and Distribution: copies at `:3626` and `:3658` cut at a NUL the same way). Who: content via feeds. Crash: none. Effect: filters and feed rules do not see those names. Conditions: `rejectartswithnul` off (the default). Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: take a NUL as a name separator in the internal group list (stored bytes unchanged); a reject would need an opt-in.

### D-266 nortflush and domismatch do the opposite on unsigned char platforms
`lib/newsfeed.c:82-83`, `:1507`, `:1527`, `:826` and `util/diablo.c:1572` keep the option values in plain `char` and test them for -1, so on arm64, ppc64le and s390x `nortflush` and `domismatch` do the opposite. Who: the local admin (config). Crash: none. Effect: wrong behaviour. Conditions: an unsigned char platform: arm64, ppc64le, s390x. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: `signed char` for `nf_PerLineFlushOpt` and `nf_NoMisMatch`; test by building with `-funsigned-char` on an x86 test host.
Fixed (#477): `nf_PerLineFlushOpt` and `nf_NoMisMatch` are `signed char`; verified with a `-funsigned-char` build on x86 (the test fails on main built that way). Test: `TestDnewsfeedsNortflushDomismatch`.

### D-267 dexpire keeps history entries of directories removed in a later pass
`util/dexpire.c:1415`, `:1459` and `:312-320`: `NodeAry` is not cleared between passes, so directories removed in pass 2 or later keep their history entries unexpired. Who: the local admin or cron. Crash: none. Effect: wrong data. Conditions: dexpire with more than one pass. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: `freeNodes()` at the start of `updateHistory()`.
Fixed (#438; needs the D-268 change, #437): `updateHistory()` (`util/dexpire.c`) frees the node table (`freeNodes()`) before it fills it for a pass, so a directory of an earlier pass that this pass removed no longer keeps its history records valid. Test: `TestDexpireSecondPassHistory`.

### D-268 A failed opendir or readdir in dexpire marks the whole spool expired
`util/dexpire.c:888-894` counts a failed `opendir()` or `readdir()` as an empty spool, so every history entry of that spool is marked expired (as D-157). Who: nobody (an I/O error). Crash: none. Effect: wrong data. Conditions: an opendir or readdir failure. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: mark the spool unsure on such a failure and update no history entries for it.
Fixed (#437): `scanDirectory()` (`util/dexpire.c`) records a directory that `opendir()` cannot open (other than ENOENT, ENOTDIR) or whose `readdir()` fails (`scanFailed()`), and `updateHistory()` leaves the records below it alone for that pass. After a `readdir()` error nothing more is removed from that directory, and `cleanupDirectories()` skips the `N.xx` directories below it. Tests: `TestDexpireUnreadableSpoolDir`, `TestDexpireReadErrorNoRemoval`.

### D-269 Record updates during the dhisexpire copy pass are lost
`util/dhisexpire.c:401-420` and `:448-507`: a record changed in place during the copy pass before the history is marked dead (for example by `dcancel -h`) is copied in its old state into the new history; updates after the dead mark are done again (`:479-484`). Who: the local admin (`dcancel -h` during dhisexpire). Crash: none. Effect: wrong data. Conditions: the two tools at the same time. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: L.
Plan: mark records dead before copying records that can change in place, or copy changed records again.

### D-270 An unreadable dexpire.ctl at its reload stops every reader process
`lib/expire.c:192-198` exits when dexpire.ctl cannot be opened at a reload. Reader processes call it every 30 s (`dreaderd/reader.c:307-310`), and it opens the file when the minute has changed (`lib/expire.c:180`), so every reader process exits at its next check, once a minute (as D-175 was for dspool.ctl). Lines are at main 9dc51c18. Who: the local admin (the file briefly missing, e.g. replaced during an edit). Crash: every reader process (exit 1). Effect: lost service. Conditions: the file not openable at reload time. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: on a reload failure log it and keep the loaded list; exit only on the first load.
Fixed (#458): `LoadExpireCtl()` (`lib/expire.c`) keeps the loaded list, the stored file time and the process when `fopen()` fails at a reload after a first successful load, logs "<path of dexpire.ctl> cannot be opened at reload - keeping the loaded list" and tries again at the next minute check. The first load at start, and calls with force set (reader start, `dexpireover`), still log and exit. The old "file not found" line named the log file, not dexpire.ctl, when logging to a file; both lines name dexpire.ctl now. Test: `TestReaderDexpireCtlMissingAtReload`.

### D-271 compresslvl 0 stores gzip records that the spool reads as plain
`util/diablo.c:4015` compresses for `compresslvl 0` (the sample's own line, `samples/dspool.ctl:32`), but `SpoolCompressed()` (`lib/spool.c:123-132`) says plain, so dspoolrefeed (`util/dspoolrefeed.c:226`, `:369`) and dreadart (`util/dreadart.c:233`) output is broken. Who: the local admin (config). Crash: none. Effect: wrong tool output. Conditions: `compresslvl 0`. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: compress only for levels 1 to 9.

### D-272 DirTime is one global for all metaspools
`lib/spool.c:57`, `:234-238` and `:296-299` keep one `DirTime`, so metaspools with different `reallocint` file articles by the last one's time slot. Who: the local admin (config). Crash: none. Effect: mild: wrong time slots, coarser expiry. Conditions: metaspools with different `reallocint`. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: keep the directory time per metaspool or spool object.

### D-273 minfreefiles on btrfs removes every directory but the newest
`util/dexpire.c:726` and `:1580-1581` read `f_ffree` 0 on btrfs as no free inodes, so every directory but the newest is removed at each run. Who: the local admin (config). Crash: none. Effect: data loss. Conditions: btrfs and `minfreefiles` (non-default). Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: take `f_files == 0` as unknown and skip minfreefiles.
Fixed (#481): `freeSpaceOn()` (`util/dexpire.c`) leaves the free files unknown (-1) when `statfs()`/`statvfs()` reports `f_files` 0; `cleanupDirectories()` then does not apply `minfreefiles` to that file system and says "reports no inode count: not expiring for minfreefiles there" once per file system and pass, while `minfree`, `maxsize` and `keeptime` still apply. Test: `TestDexpireMinFreeFilesNoInodeCount`. Not changed here: `dexpirecache -i` read `f_ffree` the same way (`util/dexpirecache.c`); that is D-299.

### D-274 The t size suffix in dspool.ctl multiplies only once
`lib/subs.c:782-803` stops after one step for `t`, so `maxsize 4t` is 4 MB and the spool is emptied every hour. Who: the local admin (a valid suffix). Crash: none. Effect: data loss. Conditions: a `t` size suffix in dspool.ctl. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: let `t` fall through the `g` and `m` cases.
Fixed (#462): the `t` and `T` case of `bsizektod()` (`lib/subs.c`) comes first and falls through the `g` and `m` steps (x1024 each); `k`, `m`, `g` and a plain number (kilobytes) give the same values as before. Test: `TestSpoolCtlSizeSuffix`. Upgrade effect: every value written with a `t` or `T` suffix in dspool.ctl becomes 1,048,576 times larger (spool `minfree 4t` goes from 4 MiB to 4 TiB of wanted free space, spool `maxsize 4t` from a 4 MiB to a 4 TiB allowance, and a metaspool `maxsize` of `1t` or more is above the `int` range of the article size, so it effectively stops filtering by size); an admin who wrote a `t` value meaning megabytes must change it. Also open in the size parsers: D-292.

### D-275 An unconfirmed PTR name becomes the reader's host name
`dreaderd/dns.c:2021-2022` (also `:1982`) stores the PTR name before the forward check, so with `useverifieddns` an unconfirmed name appears in logs and NNTP-Posting-Host. Access is not widened. Who: anyone on the network who controls the PTR of their address. Crash: none. Effect: wrong trace data only. Conditions: `useverifieddns`, an access line matched by IP. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: drop the unguarded `StoreDnsHost()` (the guarded copy at `:2051` stays).

### D-276 A vserver accessfile uses only the first pattern of a comma list
`dreaderd/dns.c:2740`, `:2806` and `:2808-2812` invert the `needread` logic, so only the first pattern of a comma list is used, and `|reader` is not parsed. Who: the local admin (config). Crash: none. Effect: wrong access decisions, too wide or too narrow. Conditions: a vserver accessfile. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: keep `needread` at 0 while a comma leaves patterns; parse `|reader`; ideally one parser for both access files.

### D-277 A 110 readerdef answer from one access line is lost when another line's backend runs
`dreaderd/dns.c:2197-2416`, `:2458-2476` and `:2507-2512` keep one backend answer, so a "110 readerdef" result is lost or taken from another line's backend, and the user gets the wrong readerdef. Who: an authenticated client. Crash: none. Effect: the wrong readerdef, wider or narrower. Conditions: several access lines with auth backends. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: keep the backend answer with the access line that matched.

### D-278 With -c N refused connections stay counted and switch listeners off
`dreaderd/main.c:3447-3461` and `:1477-1479` count parked refusals in `NumActive` for N seconds, so clients that get refused switch the listeners off. Lines are at main 9dc51c18. Who: anyone on the network who gets refused repeatedly. Crash: none. Effect: new connections are not accepted. Conditions: `-c N` (non-default). Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: do not count parked refusals in `NumActive`.

### D-279 addrealm of one access line rewrites the user name for later lines
`dreaderd/dns.c:2112-2124` and `:234-249` build user@realm in a shared buffer, so the name stays rewritten for later access lines and a valid user is refused. Who: an authenticated client. Crash: none. Effect: a valid user is refused. Conditions: `addrealm` on an earlier access line. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: build user@realm in the line's own buffer.

### D-280 With realm * one user counts as separate users for maxconnperuser
`dreaderd/dns.c:2136-2145` and `dreaderd/main.c:3266-3270`: with `realm *`, user@a and user@b are one account but get separate `maxconnperuser` counts. Lines are at main 9dc51c18. Who: an authenticated client. Crash: none. Effect: the connection limit is not applied as configured. Conditions: `realm *`. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: compare the user without the realm for `maxconnperuser`.

### D-281 A slow transfer resets a tripped spam filter slot
`util/diablo.c:3161`, `:3959` and `lib/spamfilter.c:501-511` pass the article's start time, so a transfer slower than 10 s resets a tripped slot and copies pass again. Who: a peer (a slow transfer). Crash: none. Effect: the rate filter lets copies through. Conditions: the internal spam filter on. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: pass `time(NULL)` at the call.
Fixed (#476): `LoadArticle()` passes `time(NULL)` to `SpamFilter()` instead of the time the article began to come in; the `dt < -10` reset in `spamFilterTable()` stays for a clock that steps back. Test: `TestSpamFilterSlowTransferKeepsTrip` (a copy that began 13 s before its slot was made and ends after it tripped gets 437, the copies after it are refused too).

### D-282 The spam filter decay keeps idle minutes as credit
`lib/spamfilter.c:519-523` and `:550-557` stop the decay at 0 but keep the idle minutes, so after a quiet hour about 59 plus the trip value copies pass. Who: content via feeds (copies sent after a quiet hour). Crash: none. Effect: the rate filter lets copies through. Conditions: the internal spam filter on. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: set `t0 = t` when the decay loop reaches 0.
Fixed (#476): when the decay brings the count to 0, `spamFilterTable()` sets `t0 = t`, so the slot counts like a new one. Test: `TestSpamFilterDecayRestartsFromNow` (the slot time moved back 3500 s in the cache file: 3 of 10 copies stored, as on a new slot; skipped on OpenBSD, D-171).

### D-283 A new spam filter cache size truncates the mapped cache
`lib/spamfilter.c:261-287`: `dicmd config internalfilter` with a new size (or dspaminfo with another size) truncates the cache file that feeder processes have mapped; by reading, they get SIGBUS. Who: the local admin. Crash: feeder processes, possibly the master (SIGBUS), by reading. Effect: lost service. Conditions: a size change at runtime. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: never truncate a mapped cache: write a new file and rename it.
Fixed (#476): `openFilterFile()` writes a cache of another size to `<file>.new.<pid>` (`newFilterFile()`) and renames it over the file (an empty file, as at the first start, is filled in place as before: no process can have a table in it); feeder processes forked before the change keep the old, unlinked file until they exit, the master and later children use the new one. Test: `TestSpamFilterResizeKeepsMappedCache` (`dicmd config internalfilter B4s1024` with a connection open: it still gets 235, the filter works at 1024 entries). On main the old process dies of SIGBUS (`BUS_ADRERR`, seen with strace on Debian 13; the test sees the connection close). The build maps the cache file: `lib/config.h` sets `USE_SPAM_SHM`, but `lib/spamfilter.c` undefines it. The new file gets the old one's owner and mode (`fchown()`, `fchmod()`). When they cannot be kept (`fchown()` fails and the new file's owner differs from the old one's, or `fchmod()` fails), the resize is refused: logged at LOG_ERR, the temp file removed, the old file left in place, and that process runs without the filter. Tests: `TestSpamFilterResizeKeepsMode` (the mode), `TestSpamFilterResizeRefusedWithoutOwner` (root only, skipped otherwise). A symlinked cache path is replaced by a regular file, and a directory diablo cannot write to now makes the resize fail (the filter is then off, logged). A `<file>.new.<pid>` is left behind if the process dies between create and rename.
Still open: `dspaminfo` takes the size from diablo.config, so after a size set only with `dicmd config internalfilter` (not in diablo.config), `dspaminfo -l` or `-c` replaces the cache file with an empty one of the configured size (seen on Debian 13: 49152 -> 3145728 bytes). Before #476 it truncated the live file; now running diablo processes keep theirs and only the file on disk (dspaminfo, the next start) is reset. Two processes that open the same freshly created (empty) cache file at the same moment both fill it with zeros in place, so the second can clear entries the first already wrote; this is the same window main had before #476, only at the first start, and is not changed.

### D-284 dcancel -a and -s rewrite a spool file a feeder still appends to
`util/dcancel.c:476-489`, `:560-579` and `:664` rewrite a spool file that a feeder child still appends to, so other articles in it are lost or misaddressed. The man page warns about it. Who: the local admin (`dcancel -a/-s`). Crash: none. Effect: wrong data. Conditions: a spool file still being written. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: take the feeder's exclusive lock without blocking and skip busy files.

### D-285 doverctl clean deletes the overview of a group created after its scan
`util/doverctl.c:212-248` and `:620-630` delete by the scan made at the start, so the overview of a group created meanwhile is deleted. Who: the local admin (`doverctl clean`), race. Crash: none. Effect: wrong data. Conditions: timing. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: check the group against the live active file before deleting.
Fixed (#506): `ProcessFile()` (`util/doverctl.c`) no longer deletes a file whose group the start scan did not know before `RescanActive()` has looked again: the file name holds only a hash of the group name, so it reads the active file again and enters the groups added since (`ScanActive()` with `newOnly`; known groups keep their range) and looks the hash up once more. The rescan runs only when the active file's append count (`KPDBAppendCount()`, bumped by every append) differs from the one at the last scan, so a run with many stale files scans once per change, not once per file. Test: `TestDoverctlCleanGroupCreatedDuringClean` (a preloaded `opendir()` holds clean at its first directory scan, after the active file was read, until the test has created the group). A known group is not entered again, so its iteration (part of the file names with the crc and md5 hashes: `<hash>.<iter>`) stays the one of the start scan, and dreaderd, dcancel and dgrpctl create the files of the new iteration before they write `ITER` into dactive.kp: `IterInProgress()` keeps a file of an unknown (hash, iteration) when a known group with that hash has, in the live record, an `ITER` equal to or below the file's, and the files of a lower iteration (a group removed and created again) are still removed as before. Test: `TestDoverctlCleanIterChangeDuringClean` (default crc hash; the group's `ITER` goes from 0 to 1 while clean is held, its iteration-1 files stay, a stale iteration-0 file of the recreated group goes). Also kept (the safe side): a stale file of a higher iteration than a live group with the same hash; dexpireover's directory walk removes it.

### D-286 dcancel -r leaks two descriptors and a map per line
`util/dcancel.c:277-301` and `:718-905` open the active file and an OverInfo per input line and never close them, so the tool stops after about 500 lines (with 1024 descriptors). Who: the local admin (`dcancel -r`). Crash: none. Effect: the tool fails. Conditions: long input. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: open the active file once; close and unmap the OverInfo per line.
Fixed (#436): `GetOverInfo()` (`util/dcancel.c`) opens the active file once per run, and the new `dcancelFreeOverInfo()` unmaps the overview index, drops its lock and closes it after each line, also on the error paths. Test: `TestDcancelReaderManyLines`.

### D-287 didump prints the end mark and exits 0 after a read error
`util/didump.c:545-572` still prints "." and exits 0 after a read error, so diload installs a cut history (as D-85). Who: the local admin, with an I/O error. Crash: none. Effect: wrong data (the whole history). Conditions: a read error during didump. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: no "." and exit 1 after a read error.
Fixed (#441): after a read error `DumpQuick()` (`util/didump.c`) prints the statistics and `dump incomplete: read error`, leaves out the "." line and exits 1; with `-f` a read error ends the run the same way. `diload` is unchanged: it accepts a dump without the final ".", so a `didump | diload` pipeline must check didump's own status. Tests: `TestDidumpReadError`, `TestDidumpFollowReadError`.

### D-288 A shim compile error makes 23 DNS and IPC tests skip
`tests/e2e/dnscache_test.go:53-58`, `dnsoverload_test.go:558-563` and `streamipc_test.go:38-43` skip when the shim does not compile or the working directory is wrong, so 23 tests skip and the job stays green. Who: nobody (CI). Crash: none. Effect: test-only. Conditions: a shim compile error or another working directory. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: fail on a shim compile error; find the test data from the source path.
Fixed (#443): the three shim sources are embedded in the test binary (`go:embed`) and built by one helper, `buildTestdataShim`: a compile error fails the test, and the working directory no longer matters. A missing C compiler fails these tests and the `preloadShim` tests too; `E2E_NO_CC=1` skips them on a host that has none on purpose.

### D-289 The writers-waited check in TestKPDBConcurrentUpdate can never fail
`tests/e2e/kpdb_test.go:105-110` reads `ProcessState` before `Wait()`, so the check that the writers waited can never fail. Who: nobody (CI). Crash: none. Effect: test-only. Conditions: always. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: S.
Plan: use the done channel and `waitLockWaiter` pattern of `kpdb_relock_test.go`.
Fixed (#447): each writer's `Wait()` runs in a goroutine with a done channel, the test unlocks only after its wait check, and a writer that exits first fails the test. On Linux `waitRecordWaiter` reads `/proc/locks` and accepts only a wait for the 4 bytes at the record's offset in the database's inode; elsewhere `waitLockWaiter` of `kpdb_relock_test.go` is used.

### D-290 The 30 s wait in the dnewsfeeds no-argument test misses the reload
`tests/e2e/dnewsfeeds_noarg_test.go:42-55` waits 30 s, which catches the master's reload in about one run of three. Who: nobody (CI). Crash: none. Effect: test-only, flaky. Conditions: always. Found in go-while's deep review round 2 of main 1c80d48f; shown from the code, not run. Effort: M.
Plan: compute the minute boundary like `dnewsfeeds_longconn_reload_test.go`; check with repeated runs, without loosening the test.
Fixed (#449): the test takes the first minute boundary 15 s or more after the file's mtime and waits until the master has logged the line without an argument; it fails at once if the master exits, and if no reload is logged within 20 s after the boundary. It then watches the master 6 s more and makes a second connection. It shows that the master reloaded the file, logged the line without an argument and was still running afterwards; it does not check the settings the reload produced.

### D-291 dclient -f: a NUL in a line, a read error and the unread replies
`util/dclient.c`, `dodumpfile()` (`-f`), found in the review of #459 and not changed there: (a) `fgets()` reads past a NUL byte but `strlen()` stops at it, so "abc\0def\n" goes out as "abc", and a NUL inside a full piece makes the piece look short (CR LF is added early); (b) `ferror()` is not checked after the read loop, so a read error (also a directory given as the file) is not reported as a file error and the tool goes on to QUIT; (c) the replies to the dumped commands are not read (the "dumped" note in `newsquit()`): the first reply is taken for the QUIT reply, and a large dump could fill both socket buffers and stall (a possibility by reading). Who: whoever runs `dclient -f` with such a file; local tool, local input. Crash: none. Effect: a changed or shortened command, a cut dump taken as complete. From review, not run. Whether the replies should be read per line is a question for the maintainer.
Plan: read with a length-aware call (`getline()` or `fread()`) or refuse a NUL; check `ferror()` and exit 1; read the reply after each line, or document that it is not read.

### D-292 The size parsers in lib/subs.c mis-convert large values and their own output
`lib/subs.c`, found in the review of #462, in code #462 did not change: (a) the `T` branch of `ftos()` converts `d/ONE_M` to `int`, which is out of range from 2048 t up (`d/ONE_M` reaches 2^31), and its fraction expression `(int)(d/ONE_G) % (int)(d/ONE_M) % ...` is odd; round values such as 4t are fine; (b) `bsizektod()` cannot read back `ftos()` output: "4.000T" is read as 4 KiB because `strtol()` stops at the dot, so a dumped config cannot be pasted back, and "4.5g" is read as 4 KiB too; (c) `bsizetol()` (`lib/subs.c:756`) returns `long`, and the value is narrowed where it is assigned to the `int` `nf_MaxArtSize` (`lib/newsfeed.c:73`, `:1591`, `feedQueryPaths` `:439`), so `maxsize 2g` in a dnewsfeeds label gives 2^31, which narrows to INT_MIN in the `int` `nf_MaxArtSize` and `feedQueryPaths()` (`lib/newsfeed.c:439`) then refuses every article for that feed; `3g` also goes negative, and `4g` wraps to 0 (no limit); by reading, two's-complement conversion assumed; `feedermaxartsize` and `readermaxartsize` in diablo.config are `int` too (`lib/defs.h:274-275`); with a 32-bit `long` the multiplication itself overflows. Who: the local admin, with a large or fractional value. Crash: none. Effect: every article refused for the feed, a limit that is silently off, or a printed value that does not read back. From review, not run.
Plan: use a 64-bit type in `ftos()` and for `nf_MaxArtSize`, or clamp; read the fraction in `bsizektod()` or refuse the value. Test each of the three.

### D-293 Repeated overview index misses repeat the lookup work, and a silent ENOENT hides a missing spool
`dreaderd/group.c`, a follow-up to #460 (D-223): (a) a group without an index is not cached as missing, so each look that reaches the miss (`GROUP`, `LISTGROUP`, `ARTICLE n`, `XOVER`, each NEWNEWS group) repeats `GetOverExpire()` (the dexpire.ctl pattern walk), the active-file (KPDB) lookup and an `open()` that fails, and a miss can evict an unheld cached group before the open fails (`dreaderd/group.c:1184-1239`); (b) #460 made ENOENT on the read path silent (the group is taken as having no index), so a missing group spool directory makes readers find no index for its groups without a log line (a missing mount does not always give ENOENT); the `GROUP` and `LISTGROUP` status line still reports an estimate from the active file (low number and count from `max(NB, NE-1024)`, with the usual adjustments) while nothing can be listed. The feed logs only when creating the index fails, and it can create a missing directory. Who: (a) any reader client, bounded CPU, it avoids the initial create cost of the old path, but repeated misses are not necessarily cheaper than the old create-and-cache path; (b) the local admin (a misconfiguration or an unmounted spool). Crash: none. Effect: (a) CPU; (b) wrong empty replies. From review, not run.
Plan: (a) open first and evict and call `GetOverExpire()` only when the open succeeds, or keep a short negative cache; (b) log once when the spool directory of the groups is missing, or document it.

### D-294 dnewslink miscounts offers during replay and bytes on pipelined -H feeds; a retried fresh article can be marked deferred
`util/dnewslink.c`, found in round 0 of the D-214 and D-216 fixes, reading only, lines at the reviewed head: (a) `main()` (~`:1377-1385`): the offer accounting follows the replay flag at reply time, so a fresh article answered during a replay adds nothing to `tot`, and a replayed one answered after the flag has reset can count again; the offer totals depend on timing under pipelining (statistics only); (b) `DumpArticle()` and `main()` (~`:2667`, `:1237-1253`): on a pipelined `-H` feed a reject charges the global `HeaderSize` instead of the slot's own size (also for 439 and 437; statistics only); (c) a fresh article retried after a lost connection during an active replay is sent with `defers = (deferbegin > 0)`, so it is marked deferred, and a 431 to its TAKETHIS is then handled as a reject of a deferred article: the reject removes the pending entry without requeueing it (`util/dnewslink.c:2979-2996`, `:3138-3148`, `:1232-1257`), so delivery of that article can be lost. `RefilePendingStreams()` marks the slot for retry and `readretry()` serializes and clears it. D-215 is the reverse case (a deferred article deferred again) and D-214 the 431-after-TAKETHIS drop; the fix of (c) belongs with them, as the same per-article mark is missing. Separate cleanup, no visible effect: the slip at ~`:1598`, `Stats->SentStats.RejectedBytes = Stats->SentStats.AcceptedBytesTotal` (should read `RejectedBytesTotal`); `logStats()` does not print that field. Who: a peer (its replies). Crash: none. From review, not run.
Plan: take the accounting from the entry's own mark and size, not from the flags at reply time; fix (c) with D-214 and D-215.

### D-295 A CRLF separator can be taken for a folded header in a fed control article
`dreaderd/control.c`, `ExecuteControl()` (~`:118-133`), found in the review of #465, by reading only: a CRLF separator can be taken for a folded header when the first body line of a fed control article starts with a space or a tab, so a later body line that begins with `From:` can be counted as a second header and suppress an otherwise valid control action (the duplicate-header refusal). Who: an authorized feed (IHAVE, TAKETHIS). Crash: none. Effect: a valid control message is not executed. From review, not run. Tests: `TestControlDuplicateHeadersRefused` (`tests/e2e/controldup_test.go`) covers two `Control:` and two `Sender:` headers only at the daemon level; no daemon-level test found for the control scan's `From:` or `Newsgroups:` duplicate refusal (`TestPgpverify` checks the duplicate signed `From:` refusal of the signature check, a different place), and the test added by #465 (D-298) is about the release of replaced copies, not the refusal.
Plan: a test first: daemon-level tests for the duplicate `From:` and `Newsgroups:` and for a body line that starts with `From:` after an indented first line; then decide the fix.

### D-296 Reported five-second startup-log timeout in TestAccessErrorLoggedOncePerChange
`tests/e2e/access_empty_test.go:61-79` starts the reader and waits 5 s for the log line about the bad value, which the startup parse of dreader.access writes. A CI report on #463 (ubuntu-26.04 container) names a failure "timed out after 5s waiting for the bad-value line in the log" that passed on a rerun and on main; the raw failure and the cause were not examined, and a loaded host is only a guess. Who: nobody (CI). Crash: none. Effect: test-only. Reported timeout, not reproduced; the wait is in the test, the failure is from the report only.
Plan: find out how long the startup parse takes on such a host; then a longer bound for this wait, without loosening what the test checks.

### D-297 postbytes= counted an accepted POST twice
`dreaderd/feed.c` `NNAcceptArticle()` adds the length of each line of a POST as it reads it, and `dreaderd/post.c` `NNPostBuffer()` added the stored article's size again once dreaderd's checks passed. `postbytes=` of the "closed from" log line, `PostBytes` of `drcmd stats` and the post bytes a NetRemote server gets at the end of a session (builds with `NETREMOTE_ENABLED`) showed the lines of the post plus the stored article, which also holds the headers dreaderd adds: before #457 a post of 204 bytes showed `postbytes=582` (204 + 378), as measured in the text of PR #457. A post that passed the checks and was then refused by the outgoing server, or mailed to a moderator, had both adds too. Who: any client allowed to POST, with an article that passes the checks. Crash: none. Effect: statistics only.
Fixed (#457): the add of `co_ArtBuf.mh_Bytes` to `dr_PostBytes` is removed from `NNPostBuffer()`; the per-line add stays, so `postbytes=` is the bytes the client sent after the POST command up to, not including, the "." line, with the line ends and dot-stuffing as sent (a `Bytes:` or `Xref:` header line and a line over the line limit are not counted, as before). A post refused by dreaderd's checks and a fed article (IHAVE, TAKETHIS) keep their numbers; `posts=` is unchanged. The size in the "post ok <id> <size>" line of the post log is the size of the prepared article, with the headers dreaderd adds. Test: `TestReaderPostBytesCountedOnce`.

### D-298 Header copies of From: and Newsgroups: are not released in the control scan
`dreaderd/control.c`: when the scan of a control article meets a second `From:` or `Newsgroups:` field, it replaces the pointer to the first copy without freeing it. The final cleanup can free the last copy, but the earlier one stays in the feed connection's memory pool until the connection closes. Who: an authorized feed, through IHAVE or TAKETHIS with a control article; a post with `Supersedes:` also reaches the scan when delivered on the feed (`ExecuteSupersedes()` calls `ExecuteControl()`); the reader's POST takes a separate path. Crash: none seen; retained copies, more on a longer-lived connection. Effect: memory held until the connection closes.
Fixed (#465): the change releases the previous string before assigning its replacement, as the `Sender:` case does, and leaves the duplicate-header refusal as it is; it adds `TestControlHeaderOwnership`. See D-295 for the refusal tests.

### D-299 dexpirecache -i on btrfs removes the oldest empty cache files at every run
`util/dexpirecache.c:437-441` (`main()`, at 781e1349) takes `f_ffree` from `statfs()` as the free inodes. btrfs has no fixed number of inodes and reports `f_files` and `f_ffree` as 0, so with `-i N` all N inodes counted as needed at every run, and `addSLink()` and `removeLinks()` removed empty cache files (articles kept as uncacheable) and symbolic links, oldest first, as if the inodes had run out. Who: the local admin (an `-i` option, non-default). Crash: none. Effect: reader cache entries only, no spool data. Conditions: btrfs, or another file system that reports `f_files` 0, and `-i`. The same reading as D-273, left open there. Effort: S.
Fixed (#490): when `statfs()`/`statvfs()` reports `f_files` 0, `main()` (`util/dexpirecache.c`) leaves the free inodes unknown: `-i` is not applied and the program says "dexpirecache: <dir> reports no inode count: not expiring for -i there" (once per run, on stderr), while `-f` still applies. Test: `TestDexpirecacheNoInodeCount`. Not changed here: which empty files and links `-i` removes where the count is known (`addSLink()`); that is D-302.

### D-300 A GLOBAL label without end is linked as a feed
`lib/newsfeed.c` `LoadNewsFeed()`: only `end` (`:2027-2031` on main 61030c9b) links the GLOBAL label into GRBase. A label left open, with no `end` before the next `label` (`:1402-1417`), the next `groupdef` (`:1645-1659`) or the end of the file (`:2083-2091`), is linked like any other label, so a GLOBAL left open lands in NFBase: it is a feed of its own (`FeedAdd()` matches it by its own group lines and diablo writes a `dqueue/GLOBAL` queue file for it), and as it is not in GRBase every label logs "Error: grouplist GLOBAL does not exist (from <label>)" at LOG_CRIT, once for each of its lists (six lines per label and load). GLOBAL's rules still reach the other labels (`NFGlob` is set, `recursiveScan()` applies it at the top). At the end of the file the open label's peer feed table (`feeds/<label>`, `AltFeed()`) was also never read, and nothing was logged. Who: the local admin (config). Crash: none. Effect: a queue for a feed named GLOBAL, log noise, a peer feed table ignored. Conditions: a label without `end`. Found as a follow-up of #477 (D-263); shown with `TestDnewsfeedsLabelWithoutEnd` on main.
Fixed (#483): `closeFeed()` links a finished label or groupdef the way `end` does at all four places (GLOBAL to GRBase, IFILTER, ISPAM and ESPAM in neither list) and reads its `feeds/<label>`; the missing `end` at the end of the file is logged ("no end before the end of the file!", LOG_CRIT), as before the next label or groupdef already was. The open label is closed, not dropped, as the parser already did before a new label or groupdef. Test: `TestDnewsfeedsLabelWithoutEnd` (dfeedtest, and a running diablo with no `dqueue/GLOBAL`).

### D-301 A groupref to its own label recurses to the limit
`lib/newsfeed.c` `resolveGroupList()` (`:2182-2199` on main 61030c9b) resolves `groupref GLOBAL` inside GLOBAL, or `groupref X` inside groupdef X, to the label itself, so `scanFeedList()` (`:2533-2543`) walks the same list again at each level down to MAXRECURSION (16) and logs "Infinite recursion in dnewsfeeds file!" at LOG_EMERG (once per process). The result of the scan is the same as without the line (the callbacks keep the last match, and the lines after the groupref run again at every level), but each such line multiplies the work of every scan of that list: with three `groupref GLOBAL` lines in GLOBAL, one dfeedtest of one group against one label took 1.8 s on main (4 ms with one line, 17 ms with two), 3 ms with the fix, measured on our Debian test VM. Who: the local admin (config). Crash: none. Effect: a false LOG_EMERG and CPU per article. Conditions: a groupref to its own label. Found as a follow-up of #477 (D-263).
Fixed (#483): `resolveGroupList()` leaves a groupref to its own label or groupdef unresolved, which the scan already skips, and logs "Newsfeed config: groupref X in X refers to itself, ignored" at LOG_WARNING, once per line and load. A longer cycle (groupdef X refers to Y, Y to X) still stops at the limit and logs the recursion, as before. Test: `TestDnewsfeedsGrouprefSelf`.

### D-302 dexpirecache -i N removes the first empty file met and older ones, not the N oldest
`util/dexpirecache.c` `addSLink()` (`:274-331` at 0de79b57) keeps the candidates for `-i` (empty cache files, which are articles kept as uncacheable, and symbolic links) in a list sorted by ctime, from `inodeBeg` to `inodeEnd`, and `removeLinks()` removes the whole list. `inodeEnd` started as the first entry `parseDir()` met in directory order, and an entry with a newer ctime than `inodeEnd` was left out (`:283`) also while the list held fewer than N; an entry with the same ctime as the last one was never linked in at all (its copy is leaked); and the cut-off kept N+1 entries (`:315-323`). So `-i N` (N = the inodes still needed after `f_ffree`) removed only the first entry met and the older ones, at most N+1, ties in directory order. Measured on our Debian test VM with seven entries made in three seconds (2, 3, 2): `-i 4` removed 5, `-i 1` 2, `-i 10` 6 of the 7 (tmpfs and ext4); with five entries in one second, `-i 3` removed 1 (tmpfs; from the code: 1 whatever the order). Who: the local admin (an `-i` option, non-default). Crash: none. Effect: reader cache entries only: fewer inodes freed than needed (down to one entry per run) or one more than needed. Found while fixing D-299. Effort: S.
Fixed (#492): `addSLink()` takes every entry until the list holds N, then only one older than the newest in it; the list is sorted by ctime, ties by name (`inodeOlder()`), and cut after the N-th entry, so `-i N` removes the N oldest, oldest first. Test: `TestDexpirecacheInodeOrder`. Not changed: `-f`'s tree (`addFile()`, `cleanTree()`) has no such early bound (its `fileEnd` starts at the start time and is lowered only once the tree holds more than the space needed); `addSLink()` still unlinks an entry older than two days by ctime at once, also with `-f` alone, and that entry still counts toward N; the statistics lines do not count `-i` removals (that is D-303).

### D-303 dexpirecache's statistics miss the empty files and links removed and give removed-file ages when none was removed
`util/dexpirecache.c` `main()` (`:456-458` at f3056fd3) prints the statistics at the end of a run that scans the cache. (1) `removeLinks()` (`:95-104`, the `-i` list) and the two-day rule in `addSLink()` (`:295-296`) unlink empty cache files and symbolic links without counting them anywhere: `-v -i 3` (no free inodes) on a cache with three such entries and two cache files printed `Removing ./00/e1`, `./00/e2`, `./01/l1` and then `*       0 files have been cleaned` and no other count of them. (2) `oldestremoved` starts at the start time and `newestremoved` at 0 (`:41`, `:380`), and only `removeFiles()` sets them, for a removed cache file with content (`:84-85`), so a run that removed none (`-i` alone, or `-f` with nothing to remove) printed `Oldest removed file was created 0 days 0 hrs 0 min 0 sec ago` and `Newest removed file was created 20783 days 0 hrs 13 min 28 sec ago` (the age of the epoch on 2026-11-26). Measured on our Debian test VM (tmpfs). Who: the local admin reading the cron output. Crash: none. Effect: misleading statistics only. Found while fixing D-302. Effort: S.
Fixed (#493): a new line `*       N empty files and links removed`, after the "files have been cleaned" block, counts each successful unlink of an empty file or link once (`linksremoved`; an entry the two-day rule already removed fails in `removeLinks()` and is not counted again). The "files have been cleaned" count and its three sub-lines are unchanged, so they still add up. The two "removed file was created" lines are printed only when a cache file with content was removed (`readremoved + unreadremoved`). The two-day rule (from the original 2017 source), by which `addSLink()` unlinks each empty regular file not named `*.tmp` and each symbolic link whose ctime is more than two days old, still runs at every run that scans the cache, also with `-f` alone: it is how the empty files `-t` leaves and dreaderd's uncacheable markers expire (dreaderd does not cache an article again while its empty file exists, `dreaderd/cache.c` `OpenCache()`); `man/dexpirecache.8` now describes it (Jesse's decision, after go-while's #493 review: an earlier head limited the rule to `-i`). Test: `TestDexpirecacheReport`. Not changed: the two-day rule prints no `-v` line of its own, and such an entry still takes a place in the `-i` list, so `-i` frees the larger of N and the number of such entries. `Oldest file found was created 0 days 0 hrs 0 min 0 sec ago` when no cache file with content was found (`oldest` starts at the start time). `dtlenstr()` (`lib/subs.c`) prints exactly one hour as `0 hrs 60 min` (its comparisons are `>`).

### D-304 On NetBSD a reader connection right after another one's QUIT can be reset or closed
`dreaderd` and the feeder `diablo` on NetBSD: a test's readiness probe connects, gets `200`, sends `QUIT` and gets `205`; a second connection to the same site within about 1 ms is then either reset during `connect()` (`connection reset by peer`, the daemon logs nothing for it) or greeted with `200` and closed at once (the reader logs `$ exit` without reading a command, the client gets EOF). In CI: about 1 NetBSD job in 25 since 2026-11-13 (0 in 122 jobs before), in `TestSitesOwnTheirPorts`, `TestReaderNewsgroupsSpace`, `TestReaderAuthRejectKeepsMaster` and twice in feeder tests, each time one test with the others passing; not seen on Linux, FreeBSD or illumos. Reproduced on a NetBSD 11 VM (go-while): about 1 failure per 100 runs of `TestSitesOwnTheirPorts`, on main and on #484 alike (4 of 317, 2 of 317; too few to compare rates); still fails with a plain bind instead of the listen hand-off (4 of 200) and with a probe that reads the `205` before closing (3 of 200), so neither the hand-off nor the probe's reset is shown to be the cause; load beside it made no clear difference. The kernel counter `aborted (no space to build PCB)` rose with the connect-reset cases. Who: a client that connects right after another one leaves. Crash: none. Effect: a refused or dropped connection, the client retries. Cause not known. Next: the diagnostic logging (read errno, `EOF before first command`, TCP counters in the netbsd job) on branch `dreaderd-read-errno`.

### D-305 dexpirecache -i with a large N walks the list for every entry kept
`util/dexpirecache.c` `addSLink()` (`:290`, main fba31ab8, since D-302 #492): `main()` sets N = the `-i` value minus `f_ffree` (0 when `f_ffree` is larger; `-i` is ignored when `f_files` is 0), and the scan offers every symbolic link and every empty regular file not named `*.tmp`. `addSLink()` kept them in a list sorted by ctime, then name, of at most N entries (`inodeBeg`, `inodeEnd`). A full list refused an entry that was not older than `inodeEnd` in O(1), but an entry that was older was placed by a walk from `inodeBeg` (`:313`) and the cut that follows walked on to the N-th entry (`:319`) to free the tail, so a run cost about N squared (up to n*N for n entries). Measured on the Debian test VM (`/data`, ext4, 16 directories, names in no order, files made just before): 100000 empty files: N = 10000 2.1 s, N = 100000 103.7 s (user 102.1 s); 400000 empty files: N = 10000 4.6 s, N = 100000 422.1 s (user 419.3 s); a scan without `-i` takes under 1 s. The man page example uses `-i 100000`; a cache with millions of empty files and N near that would make the hourly cron run take far longer. Who: the local admin (an `-i` option, non-default). Crash: none. Effect: CPU and run time only. Found by go-while in the #492 review (Low 1). Effort: S.
Fixed (#502): `addSLink()` keeps the N oldest entries in a binary heap (`inodeHeap`, the newest kept entry at the root): while there is room an entry is added, then an entry older than the root replaces it, so each entry costs O(log N) instead of O(N); `removeLinks()` sorts the heap by ctime, then name (`inodeCmp()`, the order of `inodeOlder()`) and removes oldest first as before. The entries removed, their order and the `-v` output are the same as before for every input (checked against an order made with `find -printf %C@` on the 100000 and 400000 file caches below, ties by name). The heap grows by doubling up to N, so a large N costs no memory until entries come. Same caches after the fix (wall, user): 100000 files: N = 10000 0.5 s, 0.06 s; N = 100000 2.0 s, 0.19 s; 400000 files: N = 10000 1.2 s, 0.27 s; N = 100000 3.2 s, 0.42 s (the rest is system time for the unlinks). Tests: `TestDexpirecacheInodeSet` (the set and order for N = 1 to 1000 on 90 entries with ties) and `TestDexpirecacheInodeTwoDays`; both pass before and after the fix (it must not change the set). Looked at and not changed: the side observation that an entry older than two days, unlinked in the scan, is also queued and then fails to unlink in `removeLinks()` is not a bug in what is removed. Those entries are the oldest of all, so they take the first places among the N, and their inodes are already free: `-i` frees the larger of N and their number, not N more (the "still takes a place" note under D-303). Not queuing them would free N more on top. The only effect is a `Removing <name>` line in `-v` output for a file that is already gone.

### D-306 dexpirecache.8 says -i removes cache files by access time
`man/dexpirecache.8:29-39` (main fba31ab8): "When the file system ... has less free space than -f or fewer free inodes than -i, it removes cache files, least recently used first (by access time)", and the noatime note follows from it. That holds for `-f` only. With `-i` alone `freeblocks` is 0, `addFile()` keeps no tree (`util/dexpirecache.c:207`) and `removeFiles()` removes nothing (`:454`); `removeLinks()` (`:455`) removes only empty cache files and symbolic links, the N oldest by ctime (D-302), plus those more than two days old (`addSLink()`). The `-i` option's own text (`:57-70`) is right. Who: the local admin reading the page. Crash: none. Effect: docs only. Found by go-while in the #492 review (Low 2). Effort: S (docs; needs Jesse's read).
Fixed (#494): the DESCRIPTION says `-f` removes cache files, least recently used first (by access time), and `-i` removes empty cache files (other than *.tmp) and symbolic links, oldest first (by ctime), as many as inodes are missing; the noatime note names `-f`. Docs only.

### D-307 dclient waits forever for a server that sends nothing
`util/dclient.c` (main 336b0d99) reads the greeting (`connectnews()`, `:131`), each reply (`newscommand()`) and the reply to QUIT (`newsquit()`, `:274`) with blocking `fgetc()` (`ReadReplyLine()`, `:74`) and sets no timeout, so a server that keeps the connection open and sends nothing, or a connection whose reset the system never learns of, leaves dclient waiting for good. Seen in CI: `TestDclientReplyLineBoundaries/quit-read-error` on FreeBSD, where the test server's reset was apparently lost and dclient waited until the test killed it after 15 s (go-while, fixed on the test side in #491). Who: a user or a cron/monitoring script running dclient. Crash: none. Effect: a hung dclient. Effort: S.
Fixed (#495): `-t seconds` (0 to 86400, default 0 = no limit, as before) sets `SO_RCVTIMEO` on the socket after the connect; a read that times out (`EAGAIN`/`EWOULDBLOCK` with `-t`) prints `No data from server for N seconds` and exits 2, for the greeting, a reply and the reply to QUIT. The limit is per read, not for the whole session. A bad value is a usage error (exit 1). `man/dclient.8` and the usage text describe it. Test: `TestDclientReadTimeout`. Not changed: writes have no timeout (a `-f` dump to a server that stops reading still blocks when the socket buffer is full); `connect()` keeps the system's timeout; no default limit.
Also (#496), from go-while's #495 review: `ReadReplyLine()` keeps the read's errno before it prints the rest of a line, so a failed write to stdout cannot turn a timeout into "Error reading response from server"; after part of a line the timeout message starts a line of its own; `man/dclient.8` says `-t` limits only the waits for data, not the connect or the sending. Test: `TestDclientReadTimeout` (`partial`, `partial-greeting`).

### D-308 MBRead() adds a failed read's -1 to its pointer and count
`dreaderd/mbuf.c` `MBRead()` (`:518`, main 5beb9477) handles a `read()` of -1 only for EINTR, EWOULDBLOCK/EAGAIN, EINPROGRESS and ENOTCONN. For any other error (ECONNRESET, EIO, EBADF ...) it falls through to `data += n; len -= n; r += n` with n = -1: the pointer moves one byte before the buffer, the count grows, and the loop reads again; a persistent error loops for good, and a later successful read would write one byte before the buffer. `MBReadLine()` takes such an error as EOF (and keeps its errno, D-304). Not reachable today: the only caller, `NNSpoolHdrResponse()` (`dreaderd/spool.c:536`), reads `co_ArtBuf`, which has no descriptor (`MBInit(&conn->co_ArtBuf, -1, ...)`, `dreaderd/reader.c:650`), so `MBRead()` never calls `read()`. Who: none today; a future caller with a descriptor. Crash: possible then. Effect: none today. Found while checking a note from the D-304 work. Effort: S (take the error as EOF, as `MBReadLine()` does), with a test once a caller reads a descriptor.

### D-309 localspool and the NNTP path still differ in the Path and Xref rewrite and in line ends
What is left of the differences between the `localspool=` DFA (`NNCopyLocalArticle()`, `dreaderd/dfa.c`) and an article fetched over NNTP (`NNSpoolResponse2()`, `dreaderd/spool.c:610`, with the spool server's `SendArticle()`, `util/diablo.c:3022`), at main 96af33fe (after #501). All found by reading both paths; none was run. (1) Header name case: `NNSpoolResponse2()` matches `Xref:` and `Path:` with `strncasecmp()` (`spool.c:693`, `:738`) and writes a rewritten line with a fixed `Xref: <name> ` / `Path: <name>!` (`:718`, `:762`), so `xref:` and `path:` come out in canonical case; the DFA matches them with `strncasecmp()` as well (`dfa.c:594`, `:610`) but sends the name as it was read (`xref: <name> ...`, `path: <name>!...`). A line that is left alone keeps its case on both paths. (2) `Xref:` followed by a TAB: the NNTP path skips any white space after the colon (`spool.c:705`) and rewrites; the DFA's 6th-character state only looks for a space (`dfa.c:591`), so `Xref:<TAB>host a:1` goes on to HDRCPY and keeps the old host. The same state also differs for `Xref:` with no space (NNTP rewrites, the DFA copies), with two or more spaces (NNTP gives `Xref: <name> a:1`; by the code the DFA drops the word only after the first space, an empty one, and keeps the old host as a second word) and with a host and nothing after it (NNTP leaves the line: `if (*ptr)`, `spool.c:711`; the DFA writes `Xref: <name>`). (3) CR in a line: `SendArticle()` splits the stored text on LF only and writes every line followed by CRLF, so a CR is data on the NNTP path: `a<CR><LF>` in the spool goes out as `a<CR><CR><LF>`, and a blank line of `<CR><LF>` is not taken for the end of the headers there (`i - b == 0` in `SendArticle()`, `"\r"` at `spool.c:639`). The DFA takes a CR before an LF as part of the line end (`dfa.c:465-476`, `:629`; no second CR) and sends `a<CR><LF>`, and at the start of a header line it drops a lone CR when Path and Xref were both seen, in HEAD mode or at the end of the read buffer (`dfa.c:543-550`, "we simply forget this '\r'"). A CR inside a line goes out as it is on both. Which output is right for an article stored with CRLF is not decided here (diablo's own spool stores LF only). (4) Line length: for a Path or Xref line it rewrites, `NNSpoolResponse2()` builds the line in `char line[8192]` and cuts the value after the name to 8092 bytes (`spool.c:721-722`, `:765-766`, "as cache.c does"), so a longer value loses its end; the DFA has no such cut and sends the whole line. A line over 1 MiB (`SERVER_MAXLINE`, `dreaderd/server.c:31`; `mh_SkipLong` is 0 on a server connection) makes `MBReadLine()` report EOF and the request is terminated (`mbuf.c:793-803`); the DFA reads blocks of 64 KiB and has no line limit. Who: a reader fetching such an article with `localspool=` against a diablo spool (the same article over NNTP looks different). Crash: none. Effect: header case, an Xref host or a CR differs by path; nothing is lost on the local path. Conditions: unusual articles (lower-case `path:`/`xref:` and an Xref with a TAB come from some posting software; a CR before the LF in the spool is not in diablo's own articles; a Path or Xref line over 8 KiB is rare). Found by go-while in the #501 review (rounds 1 and 2), checked by reading both paths. Effort: S for (1) and (2) (do what `NNSpoolResponse2()` does, or change the NNTP path to keep the case), M for (3) and (4), which need a decision about which side is right; tests in `tests/e2e` with a diablo spool, as `TestLocalspoolDfaPathWhitespace`. The local half of D-231 (a read error in the middle of the article, the file shrinking during the read) stays in D-231.

### D-310 doverctl clean removes the over file of a live group while dreaderd is setting it up
`util/doverctl.c`, `ProcessFile()`, the over file branch ("bad file header - removing"): an over file that is empty, shorter than the header or zero-filled is removed as a bad header. dreaderd creates the file empty and writes the header only after `ftruncate()`, `FileAllocSpace()` and `fsync()` (`dreaderd/group.c`, `OpenOverInfo()` about `:1232-1339`), so a file of a live group that is being set up is removed under it. Who: the local admin (`doverctl clean`) while dreaderd runs ("can be used at any time" in the man page), race. Crash: none. Effect: the over file of a group is lost (the group's overview is rebuilt by new articles only). Conditions: clean reads the file in the set-up window. Found by go-while in the review of #506, with a stress run: 4 files lost in 862 rounds with a 2 ms set-up window; not run here.
Plan: leave an over file alone that is shorter than the header or has no header yet while its group is in the active file (or hold the file's lock at offset 0 as the writer does), and remove it only when it stays bad; test with a preloaded shim that holds the writer between create and header write.

### D-311 doverctl: a signal during the active scan still lets clean delete files
`util/doverctl.c`, `ScanActive()`: SIGINT, SIGHUP or SIGTERM sets `MustExit`, the scan loop breaks out on it, and `ActiveSeq` is still set afterwards, so after a rescan that was cut short `RescanActive()` takes the table as complete and the file in hand is removed as "group not found" before `scanDirectory()` reaches its `MustExit` check and calls `exit(1)`. The start scan has the same problem: it stops on `MustExit` and `CleanSpool()` still runs and deletes the files of the groups it did not read. Who: the local admin who interrupts `doverctl clean`. Crash: none. Effect: wrong data (at most one file, the one in hand: `scanDirectory()` exits on `MustExit` after it, `util/doverctl.c:648`). Conditions: a signal while the active file is read. Found in the review of #506, by reading, not run.
Plan: exit (status 1) from `ScanActive()` when `MustExit` is set, before `ActiveSeq` is set and before any clean; test by a signal during the scan with a preloaded shim that holds the process.

### D-312 checkgroups skips the flag and description of a group made meanwhile
`dreaderd/control.c:2026-2031` (`CgCreate()` and its caller in `CgCheck()`) and `util/dgrpctl.c:846-850` (`GrpCtlCheckGroups()`): since D-254 (#507) a group that did not exist at the first look but was made before the write is left as it is. When an auto-add made it as a plain `S=y` group, a moderated group of the list (`(Moderated)`) stays unmoderated, and the description stays empty, until the next checkgroups. `newgroup` goes on as for an existing group and sets both. Found by go-while in the review of #507. Who: a checkgroups control message or `dgrpctl checkgroups exec` at the same time as an auto-add. Crash: none. Effect: wrong status or description until the next checkgroups. Conditions: two creators at once; timing. Effort: S.
Plan: when `KPDBCreate()` finds the record, go on to the existing-record path (compare S and GD with the list and write the differences), as for a group that was there at the first look.
Fixed (#509): when `KPDBCreate()` finds the record, `CgCheck()` (`dreaderd/control.c`) and `GrpCtlCheckGroups()` (`util/dgrpctl.c`) log "was made meanwhile, checked as an existing group", read the record again under its lock and go on in the path of a group that was there at the first look: status (`S`, only where a change of status is allowed: `doit` and the line is `ce_Ok`) and description are compared with the list and written when they differ, `LMTS` set, one "Changed group" (control) or "Modified group" (`dgrpctl`) line; the numbers (D-254) are never touched, and the group counts once in the limits (as an addition, as it was counted at the first look). A record that is gone again at the second read is skipped. Tests: `TestGroupCreateCheckgroupsMeanwhileDgrpctl` and `TestGroupCreateCheckgroupsMeanwhileControl` (a shim holds the checkgroups at its first append lock, a second creator makes the group as plain `y` with NB=5 NE=42, the list says `(Moderated)`): fail on 257981c2 (S=y, no description), pass with the fix (S=m, description, NB=5 NE=42, one change line, no "Added").

### D-313 doverctl clean takes a populated group's files for an empty group with the same crc name
`util/doverctl.c`, `FindGroupByHash()` (`:536` at #506 head 25e46a0b): with the default crc (or md5) group hash two groups can have the same file name hash; the iteration keeps their files apart. A group with no overview yet (ITER 0) whose hash equals that of a populated group, and whose record comes first in dactive.kp, is found first for the populated group's files: its over file is removed as "bad file header" and its data files as "stale overview data". No race: it happens on every clean run. With the populated group's record first the files are kept. Who: the admin running `doverctl clean`. Crash: none. Effect: loss of overview data of a live group. Conditions: default crc hash and a hash collision with an empty group (collisions are common: `hhash()` folds). Found by go-while in the #506 review (round 2), shown on c2e24ba9 and 25e46a0b. Effort: S.
Plan: match a file to the group whose ITER is the file's iteration (or check every group with that hash), not the first group with the hash.
Fixed (#508): `FindGroupByHash()` (`util/doverctl.c`) still matches the hash and the file's iteration, and when more than one group fits (an empty group has ITER 0 like the group that holds the name) it takes the one whose over file of that iteration (`OverFileIsGroups()`) has the group's name in its header; the first one only when none does, as before. The over file is opened only when two groups fit. Test: `TestDoverctlCleanCrcHashCollision` (both orders of the two records in dactive.kp). `dexpireover` had the same lookup (`FindGroupByHash()` in `util/dexpireover.c`, the daily `dexpireover -a`): it now has the same owner pick (its own `OverFileIsGroups()`, the over file read in the directory of the file being processed), so the populated group's data file is no longer removed as "stale overview data" and its over file gets its expire instead of "bad file header". Test: `TestDexpireoverCrcHashCollision`. With `dexpireover -w <wild>` only the matching groups are loaded, so for the files of a populated group that does not match, the empty group that shares its hash was the single candidate and was taken without a look at the over file (go-while's #508 r2 review, F1): `-w <empty group>` removed the populated group's data file as "stale overview data" and gave its over file "bad file header". `FindGroupByHash()` now takes a single candidate under `-w` only when the over file names it (a header of version 1 has no name and is taken as before), else the file is left alone (`ProcessOverviewFile()` removes nothing without a group under `-w`); hierarchy names are made of the group name, so they have no twin and no header check. `doverctl clean` is not affected: `-w` there only limits `convert`, the group table holds every group. Test: `TestDexpireoverWildEmptyGroup` (`-w` the empty group and `-w` the populated one; fails on bc07b1fa). Under `-w`, a selected group whose own over file is missing, short, of the wrong byte order or of a bad version is skipped and its out-of-range data files stay until the next `-a`. In #508 the skip printed nothing (no "bad file header", not even with `-v`); since #510 `-v` prints one "left alone" line per file (test: `TestDexpireoverWildOverFileUnusable`).

### D-314 doverctl convert crc hierarchy clean deletes the overview of every group with ITER above 0
`util/doverctl.c`: after `doverctl convert crc hierarchy`, the hierarchy file names carry no iteration, but ITER stays above 0 in dactive.kp; `clean` in the same command then looks the files up with the group's old ITER, reports "Group for over file not found" and removes them. Who: the admin converting the hash layout. Crash: none. Effect: loss of overview data of every converted group with ITER above 0. Conditions: convert and clean in one command (by reading also likely when clean runs later with the old ITER). Found by go-while in the #506 review (round 2), shown on c2e24ba9 and 25e46a0b. Effort: S.
Plan: convert sets ITER to 0 for the hierarchy layout (or clean ignores ITER for hierarchy names), with a test of convert followed by clean.
Fixed (#508): `clean` does not compare ITER for a hierarchy file name (`ProcessFile()`, the `hier` argument of `FindGroupByHash()`): the name has no iteration and the group's name is unique. ITER is not reset by `convert`: dreaderd ignores it for the hierarchy layout, and a conversion back to crc or md5 needs it (two groups with the same crc hash keep their files apart only by it). Test: `TestDoverctlConvertHierarchyClean` (`convert crc hierarchy clean`; `convert` and a later `clean`; and back with `convert hierarchy crc clean`, where the files get their `.1` names again). `dexpireover` had the same lookup and removed `o.0.<group>` of every group with ITER above 0 in its next `-a` run ("Group for over file not found"); it now does not compare ITER for a hierarchy file name either (`ProcessOverviewFile()`, the `hier` argument of its `FindGroupByHash()`). Test: `TestDexpireoverHierarchyIter` (`convert crc hierarchy`, then `dexpireover -a`). Not changed: `dexpireover` scans a hierarchy data name (`d.<n>.<group>`) only when the group name contains `.d.` (`strstr(name, ".d.")`, `util/dexpireover.c:980`); the data files of the other groups stay either way. For a group with `.d.` in its name the base removed the data file too (`d.256.e2e.d.five`), and the head keeps it.

### D-315 dsyncgroups -v prints while it holds a new group's record lock
`util/dsyncgroups.c` write-back (about `:713-777` after #507): since #507 a new group's record stays locked from `KPDBCreate()` to the unlock after the last field, and with `-v` the per-field `printf()` calls run inside that window; stdio can flush there. A stdout that does not drain (`dsyncgroups -v ... | less` left unscrolled, a paused terminal, a slow pipe) keeps the record locked, and every feeder (`GenerateXRef()`) and dreaderd (`AssignArticleNo()`) touching that group waits until it drains. Who: the local admin running `-v`. Crash: none. Effect: writers of that group stall. Conditions: `-v` and a blocked stdout. Found by go-while in the #507 review (round 2), by reading. Effort: S, but `TestGroupCreateDsyncgroupsLock` pauses on that very `printf`, so the test needs another pause point.
Plan: collect the `-v` text of a group and print it after the unlock.
Fixed (#509): `dsyncgroups` makes a new group with one `KPDBCreateRecord()` (see D-316), which needs no record lock afterwards, and prints the `-v` text of the group only after the record is made (`CreateNewGroup()` collects it); no record lock is held while it prints. The groups it updates keep their field-by-field writes (no lock, as before #507). Test: `TestGroupCreateDsyncgroupsLock` now stops `dsyncgroups -v` where it prints " NE=" of the new group (a shim on `printf()` and `__printf_chk()`, matching the formatted text, so it does not matter how the text is printed) and requires a complete record there and a `dgrpctl adjust NX +5` that is done within 2 s; it fails on 257981c2 (record locked, with NB, GD and NE only; adjust waits) and on ed4f916e (#507 before the lock: record with NB, GD, NE only, NX=0 written over the adjust), and passes with the fix. No code for the test: the pause is a shim.

### D-316 A killed dsyncgroups leaves a new group with NB but no NE or NX
`util/dsyncgroups.c` write-back: a new group is written field by field (under its record lock since #507); a `dsyncgroups` killed (SIGKILL) between the NB write and the last one leaves the record with NB and without NE or NX. No lock is left behind (the process's locks go with it). Who: the admin (a killed run). Crash: none. Effect: a group with a partial record until it is written again. Conditions: SIGKILL during the write-back. Found by go-while in the #507 review (round 2), 2 of 5 runs. Effort: M (one append of the whole record would avoid it).
Fixed (#509): a new narrow `KPDBCreateRecord(kpdb, key, fields, lockMe)` (`lib/kpdb.c`; `KPDBEncode()` is the encoding of `KPDBWriteEncode()` made callable) creates a record with all its fields in one `pwrite()` of one line, with the same look under the append lock as `KPDBCreate()` (returns 1 and writes nothing when the record exists). `dsyncgroups` uses it for a new group: NB, GD, NE, NX, CTS, LMTS and S in one append, so a process killed at any point leaves no record or the whole one, except for a record longer than a page: a kill inside that `pwrite()` can stop it between two pages and leave an unterminated last line (go-while's #509 review: 1 of 188 kills with records of about 11 KB; `KPDBLookup()` ignores it, `KPDBScanNext()` may return it until the next append marks it deleted). Test: `TestGroupCreateDsyncgroupsKilled` kills `dsyncgroups` (SIGKILL) with a shim at two points: right after the line of the new record is written (not yet published) and right after the append counter is (published); the group must be absent or have every field, and a second run must complete it; fails on 257981c2 at both points (the group has NB only, and a second run leaves it so, as the group exists), passes with the fix. Only a kill of the process is tested, not a power loss or a torn write by the file system. Not changed: the other creators (`CgCreate()`, `GrpCtlNewGroup()`, `GrpCtlCheckGroups()`, `CtlNewGroup()`) still write the fields after NB one by one under the record lock, so a kill of one of them leaves the same partial record.

### D-317 doverctl convert back to crc or md5 moves two colliding groups with ITER 0 onto the same file name
`util/doverctl.c`, `ConvertHash()`: two groups whose crc (or md5) file name hash is the same keep their files apart only by ITER (the suffix `.<iter>`; none for 0). In the hierarchy layout the names are unique, so after `convert crc hierarchy` followed by `convert hierarchy crc` both groups can have ITER 0 and the same crc name: the `rename()` of the second group's files overwrites the first group's. Who: the admin converting the hash layout back. Crash: none. Effect: loss of the overview data of one of two colliding groups (pre-existing, not made worse by #508). Conditions: two groups with the same crc hash (collisions are common: `hhash()` folds), at least one populated, ITER 0 for both, conversion to crc or md5. Found by reading in #508 by jesse/cc-1's subagent, confirmed by reading only (not run) in go-while's #508 r1 review (F2). Effort: M.
Plan: assign iterations on conversion for colliding groups (the same as dreaderd does when it creates a group whose name is taken), or refuse to move such a group with a message; with a test of two colliding populated groups through `convert crc hierarchy` and back.

### D-318 Hierarchy layout: clean removes every compressed data file (the .gz suffix is taken into the group name)
`lib/hash.c`, `ExtractGroupHashInfo()` (`:799` at 257981c2): for a compressed data file `d.<n>.<group>.gz` the pattern `d.%lld.%[^/].gz` lets `%[^/]` consume `.gz` as part of the group name, and `sscanf()` still returns 2 (the literal `.gz` after it never matches, which is not counted). The group looked up is `<group>.gz`, which does not exist, so `doverctl clean` removes every compressed hierarchy data file as of no group; `dexpireover` is likely affected too where it scans such names (it does not scan `d.<n>.<group>` names today, only names with `.d.` in them). It also means "clean keeps the files after `convert crc hierarchy`" is not true for compressed data. Who: the admin running `doverctl clean` with the hierarchy layout and compressed overview data. Crash: none. Effect: loss of the compressed overview data of every group. Conditions: `readergrouphash hierarchy` and `.gz` data files (`dexpireover -z`). Found by jesse/chatgpt-1 and go-while in the #508 r1 reviews, shown on 257981c2 and #508 (not caused by it). Effort: S.
Plan: take the `.gz` suffix off the name before the lookup (the same for the other patterns that end in `%[^/]`), with a test of `clean` on a compressed hierarchy data file. Not fixed in #508.

Fixed (#511): `ExtractGroupHashInfoGz()` (`lib/hash.c`) reads a hierarchy data name with one `d.%lld.%[^/]` and returns two readings when it ends in `.gz`: `Hash` as it stands in the name (`<group>.gz`, the plain data file of a group that ends in `.gz`) and `HashZ` without the suffix (the compressed data of `<group>`). The name cannot tell which: a group name may end in `.gz`, and `d.<n>.X.gz` is the compressed data of `X` or the plain data of `X.gz` (for `e2e.foo` and `e2e.foo.gz` they lie in different directories, `e2e/` and `e2e/foo/`, but the name is the same; a group whose last component is one character, like `e2e.x`, has no directory of its own for it, see D-325). `doverctl clean` (`ProcessFile()`) looks up both groups and keeps the file unless it is stale (outside the range, misaligned: `DataFileStale()`) for every group that exists, so a live group's file is never removed because of the other reading; with neither group the file goes as before. `ExtractGroupHashInfo()` is a wrapper that gives the old single reading, so `dexpireover` is unchanged. The other patterns are right: the crc ones (`%17[0-9a-f.]` stops at `g` and at 17 characters, the width of `%08x.%08x`; shown on the base with `data.256.<hash>.gz` and `data.256.<hash>.1.gz`, both kept) and the md5 ones, by reading only (`%[^.]` stops at the first dot, `%lld` at the dot). The active file is read again (`RescanActive()`, D-285) when either reading is missing, not only when both are, so a group created during `clean` is seen while the other one is known. Tests: `TestDoverctlCleanHierarchyGz`, `TestDoverctlCleanHierarchyGzGroupName`, `TestDoverctlCleanHierarchyGzCreatedDuringClean`.
Fixed (#513): `dexpireover -a` took a file for an over or a data file by a substring of its name (`over.`, `o.` and `.o.` for over files; `data.` and `.d.` for data files), and a hierarchy name holds the group name. Shown on 720e1c4f: (1) the compressed data `d.256.e2e.d.q.gz` of the live group `e2e.d.q` was scanned only because of the `.d.`, looked up as the group `e2e.d.q.gz` and removed as "Group for over file not found"; (2) the over file `o.0.e2e.d.q` of such a group was scanned as a data file as well (second loop), and removed as "stale overview data" once the group's first article was 256 or more (article base 0 is outside the range): loss of a live group's over file; (3) the data files of a group like `e2e.o.x` were taken for over files (the `.o.`), opened read-write and reported as "bad file header", and never range-checked; (4) every other hierarchy data file `d.<n>.<group>` was not scanned at all, so stale files and files of no group stayed (disk not reclaimed). Now `scanDirectory()` classifies a name by what `ExtractGroupHashInfo()` makes of it (`overFileType()`; the crc and md5 names have a fixed form, for which the parse and the substrings agree), and does not process a directory whose name reads as a data file (a real hierarchy directory never parses as `d.<n>.<group>`, so this only covers a directory an administrator made). Behaviour change: with the hierarchy layout `dexpireover` now checks every `d.<n>.<group>` data file for staleness (outside the range, misaligned, or of no group), as it does for the crc `data.` files and `doverctl clean` does; the same branch also applies `-z` to the plain ones, as for the crc files. `ProcessOverviewFile()` reads the name with `ExtractGroupHashInfoGz()` and for `d.<n>.X.gz` looks up both groups (`X.gz`, the plain data, and `X`, the compressed data) with `FindGroupByHash()` (the hierarchy case has one candidate and no header check), looks the active file up again (`rescanActive()`, not under `-w`) when either is missing, and removes the file only when `dataFileStale()` says stale for every group that exists; a group that left the active file skips the file. With neither group the old rules stay (`currentIter()`, "Group for over file not found", nothing removed under `-w`). Under `-w` only the selected groups are loaded, so a `.gz` name with only one reading loaded is left alone (the other group may exist without being selected), and `-v` says so. Compression (`-z`) of a plain hierarchy data file `d.<n>.X` goes to `d.<n>.X.gz`, which is also the plain data file name of the group `X.gz`. `gzNameTaken()` never compresses the file when that name is the same path for both groups (`GFName()` of `X.gz` equals the compressed name, so only a last component of one character, D-325), whether the group `X.gz` exists, is loaded under `-w` or the compressed file exists: a gzip file on that name would be taken by `X.gz` for its own plain data, also when the group is made later. `-v` says "not compressed: ... is also the data file name of the group X.gz (D-325)". `e2e.foo` and `e2e.foo.gz` have different directories (`e2e/` and `e2e/foo/`) and are compressed. `diablo_minigzip()` itself does not replace an existing destination (it stats it first and fails, `Unable to compress`, exit 1, as shown on 2b1bf382), so the data of `X.gz` was not overwritten there. The crc and md5 names cannot collide (a plain name never ends in `.gz`). `dataFileStale()` reads the data entries per file from the group's over file, which it now opens by the path from the group home (`PatExpand(GroupHomePat)` plus `GFName()` with `wantdir` 1, right for crc, md5 and hierarchy and for either reading of a `.gz` name, whose groups have their files in different directories). It used `GFName()` with `wantdir` 2, which for the hierarchy layout is relative to the group home, while `scanDirectory()` has changed into the file's directory: the open failed (ENOENT) and the count fell back to 256 (found by go-while in the r2 review, F1; on main only the group names with `.d.` were scanned, now every hierarchy data file is). With a dexpire.ctl `e` other than 256 the live data files were then judged by 256: `e4` or `e64` removed the files on no multiple of 256 (go-while: `OVER 1-12` returned 3 lines instead of 12 after one `dexpireover -a` with `e4`), `e1024` the file with the oldest live articles, and `-z` could compress the file still written to. The 256 is only a guess for an over file that cannot be read, and it can err either way (it keeps a file that is stale for the real count, and judges a live file stale when the real count is larger or not a multiple), so it is no safeguard. The crc and md5 paths were absolute (`wantdir` 2) and worked before. Tests: `TestDexpireoverHierarchyDataEntries` (e4, e64, e1024 with the first article 1500, and a `.gz` file whose plain reading has its over file in another directory), `TestDexpireoverCrcDataEntries` (guard); the hierarchy ones fail on 8a146aa8 and pass with the fix. Tests: `TestDexpireoverHierarchyGzDotD`, `TestDexpireoverHierarchyDataStale`, `TestDexpireoverHierarchyGzGroupName`, `TestDexpireoverHierarchyGzOneReadingCovers`, `TestDexpireoverHierarchyGzWild`, `TestDexpireoverHierarchyGroupNameO`, `TestDexpireoverHierarchyCompressGzName`; they fail on 720e1c4f and fc695de7 (the same dexpireover code; cases (1) to (4) above, and the file judged by one reading alone) and pass with the fix; `TestDexpireoverHierarchyCompressGzName` fails on 2b1bf382 (the file is compressed onto a name of `e2e.x.gz`, or the run fails with `Unable to compress`) and passes with the fix.

### D-319 dexpireover removes the files of a new iteration while the ITER write has not landed
`util/dexpireover.c`, `currentIter()` (the fallback of `ProcessOverviewFile()` when no group has the file's hash and iteration): it keeps a file only if the live ITER equals the file's iteration, while `doverctl`'s `IterInProgress()` keeps it when the live ITER is at most the file's iteration. A group that is about to change its iteration (dreaderd makes the new files first, then writes ITER) has files of iteration N+1 while ITER is still N: `dexpireover -a` takes them as "Group for over file not found" and removes them. The same when the ITER write was lost (dreaderd between the file and the write, a crash). Who: the daily `dexpireover -a`. Crash: none. Effect: loss of the new iteration's overview files of one group. Conditions: `dexpireover` reads the group's record in the window between the creation of the files and the ITER write, or after a lost write. Found by go-while in the #508 r2 review (F2), shown for the lost-write state. Effort: S.
Plan: use the same rule as `doverctl` (keep the file when the live ITER is at most the file's iteration), with a test that holds ITER below the files' iteration.
Fixed (#510): `currentIter()` (`util/dexpireover.c`) keeps the file when the live ITER of any group with the file's hash is at most the file's iteration, as `IterInProgress()` in `doverctl` does (the live record is read, the table's iteration is not compared: a group with the file's iteration in the table is found before this fallback). The `-v` line reads "file of a group recreated or changing its iteration, left alone". Files of an iteration below the live ITER are still removed. Same cost as in `doverctl`: a leftover file of a removed group whose iteration is at or above the ITER of a group that still has its hash is kept as well, now by `dexpireover -a` too (`doverctl clean` keeps it already); it would go only once every group that still has the hash is past the file's iteration, and in the usual collision layout the group holding the name stays at ITER 0, so a removed twin's `.1` files stay for good until they are removed by hand (disk not reclaimed, no data loss; D-323). Test: `TestDexpireoverNewIterBeforeIterWrite` (ITER 0 with files of iteration 1, ITER 1 with files of iteration 2: kept; ITER 2 with files of iteration 1: removed, as the control); fails on a99be78c (the new iteration's over and data files removed as "Group for over file not found"), passes with the fix.

### D-320 dsyncgroups splits a LIST NEWSGROUPS line over 4095 bytes and makes a group of its tail
`util/dsyncgroups.c` (about `:655`, the `LIST NEWSGROUPS` loop): `fgets()` into a 4096-byte buffer splits a longer line, and the tail is read as a line of its own: its first word becomes a group name, and with `-g -i` the group is created (a junk group `%=` appeared in go-while's runs, with #509 and with main alike). Any group named only in `LIST NEWSGROUPS`, not in `LIST ACTIVE`, is created too. Who: the remote server being synced from (a long description, or a malicious one). Crash: none. Effect: junk groups in dactive.kp. Conditions: `-g` (and `-i`) and a description line over 4095 bytes. Found by go-while in the #509 review (F3), shown with main and #509. Effort: S.
Plan: read whole lines (skip the rest of a line that does not fit, and skip that entry), and create groups only from `LIST ACTIVE`; a test with a long description line.
Fixed (#512): `dsyncgroups` reads the lists with `ReadListLine()` (`util/dsyncgroups.c`) instead of `fgets()`: a line that does not fit in the 4096-byte buffer is read to its end and skipped whole, with one note "Line over 4095 bytes - skipping : <first word>" (the word is sanitized), and the lines after it, the counts and the `.` are as before. The split line was also read wrongly in `LIST ACTIVE` (the tail was taken for a group with its numbers: `junk.act 10 1 y` made a group, and the first part made one with a flags field of about 4000 bytes) and in the `-G` file loop; all three loops use it now. A line of exactly 4095 bytes before its line end (LF or CR LF) or at the end of the input still fits, as with `fgets()`; one byte more is dropped. A NUL byte does not end a line, as with `fgets()`: the 4095-byte count goes on after it, so a NUL cannot split a line, and the C-string parsers see only what is before it. `LIST NEWSGROUPS` no longer makes groups: `EnterGroup()` takes pass 3 for it, which sets the description of a group that is already there (from `dactive.kp` or `LIST ACTIVE`) and returns NULL for any other, so `-g -i` makes a group only from `LIST ACTIVE`. Not changed: a group of `dactive.kp` that only `LIST NEWSGROUPS` names still counts as listed by the remote (`-D` does not delete it), and a `LIST NEWSGROUPS` line for an unknown group still counts in "N groups returned by list newsgroups". A dropped line may have named a local group, so the lists are then incomplete: with `-D` nothing is deleted and one note "N list lines over 4095 bytes were skipped: not deleting groups (-D)" is printed (the same reasoning as the exit when the `.` terminator is missing); this counts the dropped lines of `LIST NEWSGROUPS` too, as an entry there marks a known group as listed. Test: `TestDsyncgroupsLongListLine` (`LIST NEWSGROUPS`, `LIST ACTIVE` and `-G` file; the group named only in `LIST NEWSGROUPS` is not made); fails on a9d79551 (`junk.tail`, `junk.act`, `e2e.only` and `e2e.longact` made), passes with the fix.

### D-321 dgrpctl checkgroups without exec and masscancel print while they hold a record lock
`util/dgrpctl.c`: `GrpCtlCheckGroups()` without `exec` reads a group's record with `KP_LOCK` (about `:839`) and prints its `dgrpctl modgroup` line (about `:910`) before the unlock; `masscancel` prints while holding a record lock (about `:229` and `:237`). The same pattern as D-315: a stdout that does not drain (a pipe to a pager left unscrolled) keeps the record locked, and every writer of that group (the feeder's `GenerateXRef()`, dreaderd's `AssignArticleNo()`) waits. Who: the local admin. Crash: none. Effect: writers of that group stall. Conditions: a blocked stdout. Found by go-while in the #509 review (F4), by reading. Effort: S.
Plan: as for D-315, collect the text and print it after the unlock, or unlock before printing.
Fixed (#512): `GrpCtlMassCancel()` collects its lines (`>> cancelled <group>:<n>`, `>> masscancel: overview lock failed`, `>> masscancel: overview write failed`) with `OutAppend()` and prints them after `KPDBUnlock()`, in the same order. `GrpCtlCheckGroups()` without `exec` printed the `dgrpctl modgroup` line before the unlock; it prints it after. Not changed, and not under the lock on a9d79551 either (shown by controls in the test): in `checkgroups exec` the messages `Added group`, `Modified group` and `was made meanwhile` follow the `KPDBWrite(..., KP_UNLOCK)` that releases the record, and the `dgrpctl newgroup` line is for a group with no record (nothing locked), so only the `modgroup` line (about `:910`) was inside the window. If the buffer of `masscancel` cannot grow it exits 1 with a message on stderr (the exit releases the lock; the lines collected so far are lost). The `OA ...` line of `GetOverArt()` (`DebugOpt > 2`) is not reached from a command line (`dgrpctl` has no option for it). Tests: `TestDgrpctlCheckgroupsPrintsAfterUnlock` and `TestDgrpctlMasscancelPrintsAfterUnlock` (a shim stops dgrpctl in the `printf()` of the line, as a stdout that does not drain would, and `dgrpctl adjust NX +5` must be done within 2 s); the `modgroup` subtest and the masscancel test fail on a9d79551 (adjust waits), the `exec-modified` and `exec-added` subtests are controls and pass on both.

### D-322 dexpireover -w with two selected empty colliders still takes an unselected group's files
`util/dexpireover.c`, `FindGroupByHash()` (about `:1660` at #508 head 1c35db7b): with `-w` only the matching groups are loaded. When two or more loaded groups fit a file's hash and iteration and none of them is named in the over file's header, it falls back to the first, as without `-w`; but under `-w` the owner may simply not be loaded. Then an empty selected group's range is applied to the unselected owner's files: its over file is reported "bad file header" and its data files are removed as "stale overview data". #508 fixed the case of one selected candidate. Who: the local admin running `dexpireover -w`. Crash: none. Effect: loss of overview data of a live group. Conditions: `-w` that selects two or more empty groups sharing a hash with a populated group it does not select; shown with the shortened `md5-32:2/256` hash (groups `e2e.m0015` populated, `e2e.m0021` and `e2e.m0031` empty, `-w '????????1'`), where collisions are easy; with the full crc hash it needs a real collision of three names. Found by jesse/chatgpt-1 in the #508 round 3 review, shown on bc07b1fa and 1c35db7b. Effort: S.
Plan: under `-w`, when no candidate is named in the header (and the header is not version 1), return no group, so the file is left alone, as for a single candidate; with a test of the case above.
Fixed (#510): `FindGroupByHash()` (`util/dexpireover.c`) treats several candidates under `-w` as it treats one: when none of them is named in the over file's header it returns no group, so the file is left alone (`ProcessOverviewFile()` removes nothing under `-w` without a group), with a `-v` line "files of hash ... left alone: with -w the over file does not name a selected group" (also for the single candidate). A header of version 1 has no name and cannot show that the owner is not loaded, so the first candidate is taken as before, the same rule as for a single candidate. Without `-w` and for hierarchy names nothing changes. Test: `TestDexpireoverWildTwoEmptyColliders` (`readergrouphash md5-32:2/256`; `e2e.m0015` populated, `e2e.m0021` and `e2e.m0031` empty, `-w '????????1'`; `-w e2e.m0015` and no `-w` as controls); fails on a99be78c ("bad file header" for the over file, "Deleting stale overview data" and removal of the data file), passes with the fix. Test of the skip and its line: `TestDexpireoverWildOverFileUnusable`.

### D-323 doverctl clean and dexpireover keep a removed group's files of an iteration at or above a same-hash group's ITER
`util/doverctl.c` `IterInProgress()` and, since #510, `util/dexpireover.c` `currentIter()`: a file of no loaded group is kept when the live ITER of any group with its hash is at most the file's iteration, so that the files of a group changing its iteration are not removed (D-285, D-319). The same rule keeps the files of a removed group whose iteration is at or above the ITER of a group that still has the hash. In the usual collision layout (the group holding the name at ITER 0, its twin at ITER 1 from dreaderd's loop, `dreaderd/group.c:1345-1373`) a removed twin's `.1` files stay for good, as the holder's ITER never rises; before #510 the daily `dexpireover -a` removed them. Who: the admin (rmgroup) or rmgroup/checkgroups control messages, then the cron run. Crash: none. Effect: disk not reclaimed, bounded per removed group, grows with group churn; no data loss. Found by go-while in the #510 review, shown by runs. Effort: S.
Plan (both tools): keep a file of a higher iteration only when its over file is missing or short (a group still being set up), or its header names a live group with that hash; else remove it as before.

### D-324 doverctl clean keeps a hierarchy data file that is stale for its own group while the other .gz reading covers it
`util/doverctl.c`, `ProcessFile()` (`:815-823`, `:881-882`): for a hierarchy data name `d.<n>.X.gz` (the plain data of group `X.gz`, or the compressed data of group `X`; D-318, #511) it looks up both groups and removes the file only when it is stale for every group that exists. It does not use the directory, which tells the reading: `e2e/foo/d.256.e2e.foo.gz` can only be the plain data of `e2e.foo.gz` (the compressed data of `e2e.foo` is `e2e/d.256.e2e.foo.gz`). So a file that is stale for its real owner is kept while the other group's range covers its first article. Two cases, both shown by go-while's runs on the #511 head (a live file is never removed): an orphan `e2e/foo/d.256.e2e.foo.gz` after `rmgroup e2e.foo.gz`, while `e2e.foo` exists with 256 in its range; and a stale `e2e/foo/d.1024.e2e.foo.gz` (outside the range of `e2e.foo.gz`) while both groups exist and `e2e.foo` covers 1024. Both shown on the fix tree here too (the file is kept, `clean` prints nothing about it). Before #511 the same files were removed, together with every live compressed file (D-318). Who: the admin. Crash: none. Effect: disk not reclaimed, no data loss. Conditions: `readergrouphash hierarchy` and both a group `X` and a group `X.gz` (rare). Found by go-while in the #511 review (F3). Effort: S.
Plan: use the file's directory to pick the reading (a file in X's directory is X's compressed data, one in the directory of `X.gz` is `X.gz`'s plain data), and fall back to both readings only where the two directories are the same (a one-character last component, D-325); with a test of the two cases above.

### D-325 Hierarchy layout: a group with a one-character last component and the same name plus .gz share one data file path
`lib/hash.c`, `FindGroupDir()` (`:710`) and `GFName()` (`HASHGRP_HIER`): the directory of a group is built from its components but not the last, and a component of one character is skipped (joined to the next one). For a group `e2e.x` the directory is `e2e/`, and for `e2e.x.gz` too (`dgrpctl listgrouphashfile` gives `e2e/o.0.e2e.x` and `e2e/o.0.e2e.x.gz`, shown). The compressed data file of `e2e.x` is `e2e/d.<n>.e2e.x.gz`, and the plain data file of `e2e.x.gz` is the same path. The over files differ, the data files are one file. By reading (not run): `dexpireover -z` of one group, a rewrite, or `doverctl convert` of the other group could overwrite or move the data of the other, and dreaderd would read the one's data through the other's over file. Not caused by #511 (its test for `.gz` group names uses `e2e.foo` and `e2e.foo.gz`, which lie in different directories). Partly addressed in #513: `dexpireover -z` never compresses a plain data file whose compressed name is the data file name of the group `<G>.gz` (D-318); `doverctl convert` and a rewrite are unchanged. Who: the admin running `dexpireover -z` or `doverctl convert`. Crash: none. Effect: corruption of an overview data file. Conditions: `readergrouphash hierarchy`, a group whose last component is one character, and a group of the same name plus `.gz` (rare). Found by go-while in the #511 review (F4). Effort: M.
Plan: needs a design decision: do not skip the one-character last component in the directory (changes the path of existing files, so only for new layouts or with a conversion), or refuse to create the second group of such a pair (`dgrpctl newgroup`, `dsyncgroups`, dreaderd) and report existing pairs.

### D-326 doverctl clean skips group directories whose name does not start with two alphanumeric characters
`util/doverctl.c`, `CleanSpool()` (`:615-616`): only directories of the group home whose first two characters are alphanumeric are scanned. Shown by go-while's runs on the #511 head (jobs 4823, 4824): with `readergrouphash md5-32\1` (one-character directories) `doverctl clean` scans nothing. Shown here on the fix tree with the hierarchy layout: for a group whose first component is one character (`a.bcd.efg`, `dgrpctl listgrouphashfile` gives `a.bcd/o.0.a.bcd.efg`, the one-character component is joined to the next) a data file of no group in `a.bcd/` is not removed, while the same file in `e2e/` is ("Group for over file not found, removing"). By reading, not run: crc and numbered md5 layouts with a directory name that does not begin with two alphanumeric characters (a `/` directory level that gives one character, a `.` in the name) are skipped the same way. Who: the admin. Crash: none. Effect: stale files of such groups are never cleaned (disk, no data loss). Conditions: `md5-32\1`, or the hierarchy layout with a group whose first component is one character. Found by go-while in the #511 review (outside the diff). Effort: S.
Plan: scan every directory entry that is a directory (skip `.` and `..` only), or test the layout's directory names; with a test of `md5-32\1` and of a hierarchy group with a one-character first component.
Fixed (#515): `CleanSpool()` (`util/doverctl.c`) enters a top-level directory of the group home when `LayoutDirName()` says the configured `readergrouphash` can have made it, not by its first two characters, matching what `GFName()` (`lib/hash.c`) prints: crc `%02x`, two lowercase hex digits; md5 `/N` `%0Wx` of `hashNum() % N`, so W lowercase hex digits (W the decimal digits of N-1, at least 1) with a value below N (`md5-32/1` makes only `0`, so `f` is not entered); md5-32 `\N` and md5-64 `\N` exactly N characters (at most `:B` minus 1) of the hash alphabet (`[a-z0-5]`, and `[a-zA-Z0-9+-]` for md5-64); hierarchy a name that starts with a letter or digit, so `.hidden` and dexpireover -R's `.d.` junk names are not entered, and `lost+found` is left alone (hierarchy directories are entered only when the name starts with a letter or digit; a group name component may also start with `+`, `-` or `_`, which `ValidGroupName()` accepts, so groups whose first component starts with one of them are not cleaned or expired, as on main: D-330). The rule is not simply "every directory" because `scanDirectory()` and `ProcessFile()` delete every entry of a directory they enter whose name does not parse ("Deleting unknown/stale file", "Removing unknown file"): on main `lost+found` (first two characters alphanumeric) was entered and its files removed. Symlinks (#515 r1, chatgpt-1): a top-level symlink is followed, as on main, only when its name starts with two alphanumerics (main's rule; an admin can put layout directories on other disks that way); a name that only the new rule accepts (`md5-32\1`, hierarchy `a.bcd`) must be a real directory (`lstat()`), and a symlink of such a name is skipped with a `-v` line `Skipping symlink <path>: ...`, so a link out of the group home is not scanned. `scanDirectory()` now returns with `chdir()` to the directory it came from (`getcwd()` before entering, as dexpireover does) instead of `chdir("..")`: on main, after a followed symlink the scan went on in the parent of the link's target, and the empty-directory `remove()` ran there and removed the target itself once it was empty. A symlink is no longer passed to the empty-directory `remove()` (it would unlink the link), at the top level and below. Otherwise empty-directory removal and the rest are unchanged. Also found while reading the layout: with md5-64 `\N` the hash alphabet has `+` and `-`, so 2 of 64 groups have a directory starting with one of them, and `dexpireover` (`ScanDirectories()`, `isalnum(d_name[0])`) skipped those directories, so their groups were never expired; it now accepts `+` and `-` as the first character for md5-64. Such a `+` or `-` entry must be a real directory (#515 r2, chatgpt-1): `ScanDirectories()` checks it with `lstat()` and skips a symlink with a `-v` line `Skipping symlink <path>: ...`, since it would otherwise remove the over and data files of groups not in the active file in the link's target, while a name starting with an alphanumeric is followed with `stat()`, as on main (`TestDexpireoverMd564SignSymlink`). Below a top-level directory entered only under the new rule (doverctl: a name `LayoutDirName()` accepts and main's two-alphanumeric rule does not; dexpireover: an md5-64 `+` or `-` name), both tools do not follow a child symlink to a directory (#515 r3, chatgpt-1): `scanDirectory()` gets a `noLinks` flag, skips such a link with a `-v` line `Skipping symlink <path>/<name>: ...` and does not pass it to the empty-directory `remove()`, since main never scanned those trees and a link out of them would remove the files of groups not in the active file in its target; the trees main entered still follow child links as before. In those trees every child symlink is skipped, not only one to a directory (#515 r4, chatgpt-1): a link to a regular file or a dangling one is not opened, removed or renamed either, with the same `-v` line (one `lstat()` per entry; dexpireover's second pass, junk files and data files, skips links too), because a link named as a live group's over file was opened `O_RDWR` through the link, and dexpireover expired the over file it points to, wherever that is, while a link named as a stale data file was removed (`TestOverToolsMd564SignDirFileSymlink`; the control `TestOverToolsOldNameDirFileSymlink` shows that links in the trees main entered are handled as on main). Directory loops (go-while's #515 review, F3): a symlink to a directory already on the path from the group home (`cd -> .` at the top level, `loop -> .` or `up -> ..` below) made `scanDirectory()` recurse until the path no longer fitted `PATH_MAX`, as its `*level` limit is never incremented; with about 8 KB of path buffers per frame that is a stack overflow first. On 20427179 both tools died of SIGSEGV; on main dexpireover did too (it always returned with `chdir()` to where it came from), and doverctl ended only because its `chdir("..")` drifted out of the group home (exit 1, 1527 failed file operations), and each pass through the loop scanned every directory of the group home with `scanDirectory()`, which does not apply the layout rule, so `lost+found` was entered (go-while saw its files removed on the head). Both tools now keep the `(st_dev, st_ino)` of each directory on the path, from the group home (its `stat(".")`) down, in a `DirFrame` list passed down the recursion, check each child directory (its `stat()`, so a link's target) and each top-level entry against it, and skip one that is on the path with a `-v` line `Skipping directory loop <path>/<name>: ...`; it is not scanned and not passed to the empty-directory `remove()` (`TestOverToolsDirLoopSymlink`; ASan run of both tools clean). `dexpireover` has no problem with `a.bcd` (its first character is alphanumeric) or `md5-32\1`, and it deletes only names it parses. Tests: `TestDoverctlCleanHierarchyOneCharComponent`, `TestDoverctlCleanMd5OneCharDirs`, `TestDoverctlCleanLeavesForeignDirs` (control), `TestOverToolsMd564PlusMinusDir` (doverctl and dexpireover), `TestDoverctlCleanSkipsNewNameSymlink`, `TestDoverctlCleanFollowsOldNameSymlink` (control and the way back), `TestDoverctlCleanMd5NumBuckets`, `TestOverToolsMd564SignDirChildSymlink` (doverctl and dexpireover), `TestDoverctlCleanNewNameDirChildSymlink`, `TestOverToolsOldNameDirChildSymlink` (control, doverctl and dexpireover), `TestOverToolsMd564SignDirFileSymlink` (doverctl and dexpireover), `TestOverToolsOldNameDirFileSymlink` (control, doverctl and dexpireover), `TestOverToolsDirLoopSymlink` (doverctl and dexpireover). Not changed: below a top-level directory that main already entered, `scanDirectory()` still follows symlinks (`stat()`), as on main, and its `*level` depth limit is never incremented. Hierarchy groups whose first component starts with `+`, `-` or `_` are still not cleaned or expired (D-330), and the docs of `doverctl convert` followed by `clean` are D-331.

### D-327 doverctl clean re-reads the whole active file for every file that triggers a rescan
`util/doverctl.c`, `RescanActive()` -> `ScanActive(0,1)`: `clean` reads the whole active file again for every file that triggers a rescan while the append count keeps changing, so under a steady group-creation rate `clean` costs files x active size. #511 added the plain data of a `.gz` group whose `X` is absent to the file classes that trigger it. Shown by go-while on Ubuntu 22.04 (the #511 r3 review, F1): 3000 files with about 200 groups created per second, main 0.10 s / 1.53 s, the #511 head killed at 480 s with 1340 of 3000 files done. Who: the admin's cron. Crash: none. Effect: `clean` slow or stuck. Conditions: hierarchy layout, many `.gz` files whose plain group is absent, hundreds of group creations per second. Found by go-while in the #511 r3 review (F1). Effort: M.
Plan: an incremental rescan in `ScanActive()` from the last scanned offset; narrowing the rescan gate would bring back the #511 r2 High.

### D-328 dsyncgroups -D with a -G file that ends early deletes every local group it does not name
`util/dsyncgroups.c`, the `-G` read loop (`:698-707`, the `else` of `if (host)`): `while ((r = ReadListLine(...)) != 0)` with `processLine(cmd, wild, &count, NULL)` at `:705`. The network lists pass `&ok` and the run ends with "Did not receive . terminator" (`exit(1)`, `:693-697`, `:782-786`) when the "." line is missing, before `-D` deletes anything; the `-G` loop passes NULL, never looks for the terminator and ends at the end of the file, so a truncated `-G` file (a full disk, a bad copy) ends like a complete one and `-D` (the "Deleting" branch of the write-back, `:856-862`) deletes every local group the file does not name. Checked by reading the code, not run. `main`, #512 r1 and r2 behave the same. Found by go-while in the #512 r3 review (P1). Who: the admin with a truncated -G file. Crash: none. Effect: loss of live local groups. Conditions: `-D` with `-G`. Effort: S.
Plan: require the "." terminator in `-G` files when `-D` is given, as the network lists do (note: `processLine()` accepts only ".\r\n", and a `-G` file in active-file form usually has no terminator, so this is a change for existing `-G -D` users: say so in the release note), or refuse `-D` with `-G`; a test with a truncated file.

### D-329 dexpireover -w never cleans compressed hierarchy data files
`util/dexpireover.c`, `ProcessOverviewFile()`: under `-w`, compressed hierarchy data files are never cleaned, even when the missing `X.gz` group would match the wildmat (the `-w` rule leaves a `.gz` name alone when only one reading is loaded). Found by go-while in the #513 r2 review (F3); main behaves the same. Low, open. Effect: disk only.
Plan: under `-w`, check the other reading against the wildmat and load or look it up when it matches.

### D-330 Hierarchy groups whose first character is +, - or _ are never cleaned or expired
`util/doverctl.c`, `CleanSpool()` -> `LayoutDirName()` (hierarchy: the first character must be a letter or digit) and `util/dexpireover.c`, `ScanDirectories()` (`isalnum(d_name[0])`, `+`/`-` only for md5-64): with `readergrouphash hierarchy` a group whose first component starts with `+`, `-` or `_` has a top-level directory of that first character, which servers must accept (lib `ValidGroupName()` does) but neither tool enters, so `doverctl clean` never removes its stale files and `dexpireover` never expires it. Main behaves the same (its rules were two alphanumerics and one alphanumeric); #515 kept the rule and only corrected a comment that claimed RFC 5536 section 3.1.4 requires a letter or digit. Shown by go-while with a `GFName()` driver over 200000 names: 5909 `+`, 6057 `-` and 5947 `_` directories refused, nothing else (jobs 5006, 5011, 5015, 5016). Accepting them in dexpireover also needs a guard in `GFIndex()` for `_` under `-P`. Who: the admin. Crash: none. Effect: disk growth and no expiry for those groups; no data loss. Conditions: hierarchy layout and such group names. Found by go-while in the #515 review (F1). Effort: S-M.
Plan: accept a first character of `+`, `-` or `_` for the hierarchy layout in both tools (a real directory, `lstat()`, with the `noLinks` rules of D-326), with the `GFIndex()` guard for `-P`; a test with a group of each.

### D-331 doverctl docs do not say which old-layout directories clean enters after a convert
`man/doverctl.8` (`:28-30`) and `changes/doverctl-clean-dirs.md`: they do not say that after a `doverctl convert`, `clean` enters old-layout top-level directories only when their names fit the new layout (crc to `md5-32\1` leaves 9 old directories unscanned; hierarchy to crc leaves `e2e/` and `a.bcd/`), and that symlinks with names that only the new rule accepts are skipped. Docs only. Who: the admin. Crash: none. Effect: old directories left behind after a convert. Found by go-while in the #515 review (F2). Effort: S.
Plan: one paragraph in the man page and the release note; no code change.

Not added from deep review round 2: H4 (with `_FORTIFY_SOURCE`, glibc aborts on FD_SET of a descriptor of 1024 or more) did not reproduce: on glibc FD_SETSIZE stays 1024 despite the `__FD_SETSIZE` override in `lib/defs.h`, and MAXFDS is 1024 too (a probe on two test hosts), and the feeder refuses a connection above that and stays up (`TestR2FdSetFortify` on gw-r2-repro-c). M8 (`dexpireover -R` exit(1) on a full group being written) did not reproduce: every run found the group in use (`TestR2ReproRewriteFullGroupBeingWritten` on gw-r2-repro-b).

## Feature requests

### F-01 Cancel-Lock (RFC 8315)
Generate `Cancel-Lock` for local posts in dreaderd and verify `Cancel-Key` before executing a
cancel or supersede. The maintainer's main reason not to replace INN with diablo plus
dreaderd yet. **Status: done** (#19, #136, #137, #138), modelled on INN.

**Verification: done in #19, on libcanlock since #137.** `CtlCancel` (`dreaderd/control.c`) checks
the Cancel-Key of a cancel or supersede against the target's Cancel-Lock (`lib/canlock.c`; sha256,
sha512 and sha1). `readercanlock` in diablo.config (the values of INN's `docancels`):

- `require-auth` (default): executed only when the target has a Cancel-Lock and the Cancel-Key
  matches it; a target without a lock is not cancelled.
- `auth`: a target with a Cancel-Lock needs a matching key; a target without one is cancelled.
- `none`: no cancel or supersede is executed.
- `all`: no checks (the behaviour before #19).

The locks and keys are computed and checked by libcanlock 3.3.0 or later, the library INN uses
(nothing is re-implemented), behind the optional `CANLOCK_ENABLED` switch in `lib/vendor.h`. A
build without the library keeps the default `require-auth`, so nothing locked is cancelled (dreaderd
warns at start), and accepts every mode. Trackonly mode is refused with a Cancel-Lock check (except
`all` and `none`). Tests: `TestCanLockVectors`, `TestCancelLock*`.
Upgrades: a reader that relied on unchecked cancels must set `readercanlock all`.

**Generation: done in #136.** A `canlockadmin` and/or `canlockuser` secret in the vserverdef (up to
4 of each, every one makes locks, which lets a secret be replaced while old articles stay
cancellable) adds a `Cancel-Lock` (sha256 and sha1; `canlocksha1 no` leaves sha1 out) to posts,
appended to the poster's own header as INN's nnrpd does; `addcanlockuser` (readerdef) says whether
the user lock is made for the AUTHINFO user, the address or nobody. The poster's own cancel or
supersede gets the matching `Cancel-Key`. A Cancel-Lock or Cancel-Key of the poster that cannot be
appended to (1023 bytes or more, a second header) is refused with 441.

**dcanlock: done in #138.** `dcanlock key|lock|verify|cancel`: keys and locks from the secrets of
`dreader.access`, and a whole cancel article (like INN's `gencancel`) for the administrator who
withdraws any article that came from the site; the output is POSTed as it is.

**Early cancels: not done, by decision.** A cancel that arrives before its article is dropped under
the checking modes, as in INN; keeping pending Cancel-Key operations and their interplay with the
history file is more work than it is worth. Delayed feeds against spam are meant for NoCeM notices
(F-04), not for authenticated cancels.

### F-02 configure script for the build options
Every option in `lib/vendor.h` (RADIUS, CDB, zlib, DB and so on) is meant to be chosen by the admin
before building. Today the defaults must suit every platform at once, which is why `RADIUS_ENABLED`
(needs a BSD `libradius` with `radlib.h`, not on Linux) cannot simply be on or off for everyone
(see D-16, which stays undecided until this exists). A `./configure` step would probe the build
host and turn an option on only where its header and library are there, and the admin could still
force any option either way.

First step, small: a hand-written `./configure` in POSIX sh. The tree already has most of the parts:
`compat/detect-vendor-option.awk` reads `vendor.h`, and `tests/e2e/ci/build-on-host` already probes
for `db_185.h` and `radlib.h`. The script would probe the same headers (and zlib and OpenSSL) and
write a local override file that `vendor.h` includes, leaving the admin's own settings in charge.
It covers Linux, FreeBSD and SmartOS with little churn. A full autoconf setup (`configure.ac`, a
generated `configure`, a `config.h.in`) can follow if it is wanted. Suggested by the maintainer and
go-while on 2026-10-27. **Status: done in #167** (merged 2026-10-31): a hand-written POSIX-sh `./configure` probes zlib, libcanlock, Berkeley DB, cdb, PAM, LDAP, Perl and RADIUS (radcli on Linux), writes `config.mk` and sets the `vendor.h` options. This also settled D-16.

### F-03 Refeed a whole spool to another server
There is no command that feeds every article on a diablo spool to another server. To another diablo
the spool files and a few other files can be copied, but to INN (or any other server) the articles
have to be offered one by one. A recipe can be built from tools diablo has, and it should either
become a tool (for example `dspoolrefeed`) or be documented extensively (README and a man page):

1. `didump -v dhistory` lists the history entries with `boff=`, `bsize=` and `file=`.
2. The history stores only a hash of the Message-ID, so the Message-ID header is read from the
   spool file at that offset (not possible for compressed spool objects: use `dreadart -h <hash>`).
3. The result is a `dnewslink` batch file, lines of `D.xxxxxxxx/B.xxxx <message-id> offset,size`,
   sent with `dnewslink -h host -b batchfile` (CHECK/TAKETHIS or IHAVE; articles the peer has are
   refused, deferred ones stay in the batch file, so a rerun retries).

Points to cover: the receiving server must accept old articles (INN `artcutoff`, default 10 days)
and have the sender as a peer; run as the news user from the queue directory; large spools need
the batch split over several `dnewslink` processes; header-only entries (`flags=H`) are left out.

Checked on 2026-10-27 on a scratch instance (batch generation only, the feeding step could not be
run because the spool there was full): `didump -x` does **not** leave out expired entries (it dumped
all 4775 records, 1843 of them still on the spool), so entries whose file is gone have to be
skipped, and `file=` is an absolute path, so the spool directory has to be stripped to get the
`D.xxxxxxxx/B.xxxx` form. Raised by the maintainer.

**Status: done (#310, merged 2026-11-05)** (maintainer, 2026-11-04: "Take F-03"): a new tool, `dspoolrefeed`, writes the queue files the recipe above builds by hand. It walks the history, reads each Message-ID from the article on the spool (compressed spool objects too) and checks it against the history hash, and leaves out expired, header-only and vanished entries. `-a days` and `-g groups` select; `-n` splits the output for parallel dnewslinks. The man page dspoolrefeed(8) covers the receiving side.

### F-04 NoCeM notices
INN can act on NoCeM notices (signed notices from third parties that list the Message-IDs of
spam and other unwanted articles to hide), and such notices are still issued today. Diablo has
nothing for them. A first design would have to settle: where a notice is applied (hide the
articles from the readers' overview, HEAD, BODY and ARTICLE, or stop the feeder from taking
them, or cancel them), whom to trust (the issuers' PGP keys, checked with the existing
`pgpverify`), where the notices come from (a newsgroup the reader or feeder already gets, or a
feed of its own), and how to give a notice time to arrive before the spam is delivered (a delayed
feed). Status: open, not started. Requested by the maintainer on 2026-10-28.

### F-05 Init scripts and adm script replacements
The xs4all-diablo-patches collection has init scripts and replacements for the adm scripts.
That repository carries no license, so nothing was copied; the same was built on go-while's
`diablo-ctl` instead. **Status: done in #140** (with #139's file modes for the installer):
`samples/init/` has a systemd unit, FreeBSD rc.d scripts, an illumos/SmartOS SMF manifest and
SysV scripts, all only calling `adm/diablo-ctl`; `diablo-ctl` got `check` and checks before a start
(mounts, an active file that exists and is not stale, an unclean-shutdown guard, an optional demand
for the history file; a missing history is still created by diablo), `-F`, and an optional `dkp`
trim and sort at start; `adm/actsync`, `adm/dumphist` and `adm/histbackup` are new (the dexpireover
and rewriteover jobs were already `daily.reader` and `weekly.reader`). Tried for real on systemd,
SMF and FreeBSD rc.d. Follow-ups: #145.

### F-06 Perl authentication for dreaderd
dreaderd can call an external `perl` auth hook, but the path was thin and effectively single-use. This
feature tracks bringing dreaderd authentication up to standard as part of the auth-backend
modernization (alongside F-02's configure step, which also wires up RADIUS/radcli, cdb and PAM). Merged
on 2026-10-31: **#167** adds `./configure` feature-detection and RADIUS via radcli;
**#168** adds the tracking rows D-167 to D-169 (Perl authentication, file and cdb deny lists); **#169**
adds `contrib/htpasswd-auth.pl` (auth against an Apache htpasswd file) and reworks the dreaderd Perl
hook so each distinct `perl` line gets its own interpreter (fixes D-167) and a failed load refuses
logins instead of killing the worker.

`PERL_ENABLED` is treated as a feature, not a quick merge: it goes live only after (1) the #167/#169
review findings are fixed — notably the htpasswd no-cache DoS and the `call_checkuser` `G_EVAL` guard
(patch sent), the configure missing-`config.mk` hard error, and the radcli Filter-Id fail-open — and
(2) a second deep code review of the fixed auth path (`dreaderd/dns.c` `PerlAuthenticate`/`perlWords`,
`contrib/htpasswd-auth.pl`, the radcli adapter). Agreed with the maintainer on 2026-10-30.

### F-07 dreaderd POST as an RFC 5536/5537 injecting agent
The maintainer's gold standard for Netnews behavior is the USEFOR index
(https://www.eyrie.org/~eagle/usefor/): RFC 5536 (article format, on RFC 5322) and RFC 5537
(architecture; section 3.5 lists the duties of an injecting agent, which dreaderd's POST is).
Review of `dreaderd/post.c` (`NNPostBuffer()`) at main e92a51f, from reading, not run. Each
item gives the requirement and what dreaderd does today.

MUST, not met:
- **Injection-Date** (RFC 5536 3.2.7, RFC 5537 3.5 step 11): it must be added at injection
  unless the proto-article has both Message-ID and Date, and an existing one must never be
  changed. dreaderd never adds it, and passes a client's own through unchecked.
- **Reject proto-articles with Injection-Info or Xref** (RFC 5537 3.5 step 2). dreaderd keeps
  a client's Injection-Info and silently deletes Xref.
- **Reject a Path containing the "POSTED" diag-keyword** (RFC 5537 3.5 step 2). dreaderd deletes
  the client's Path without looking for `.POSTED`; it only counts `!` elements (`pathcomponents`)
  and, with `POST_CKPATHLOOP`, a fixed string.
- **Do not alter the body** (RFC 5537 3.5 step 6). The vserverdef `posttrailer` option appends
  lines to the body.
- **Do not generate obs-zone dates** (RFC 5322 3.3, restated in RFC 5536 3.1.1): the generated
  `Date:` is `%d %b %Y %H:%M:%S GMT`; it should be `+0000` (an optional day name is fine). The
  same applies to `NNTP-Posting-Date` with `POST_NNTP_POSTING_DATE_GMT`.
- **Syntactically valid as defined by RFC 5536** (RFC 5537 3.5 step 2). dreaderd checks
  little syntax: Message-ID only through `MsgId()` and an `@`; Date not at all; header lines
  over 998 octets (RFC 5322 2.1.1) are not refused; the From check (`ckaddress()`) is
  compile-time only (`POST_CKADDRESS`).

SHOULD, not met:
- **Injection-Info** (RFC 5536 3.2.8, RFC 5537 3.5 step 10) instead of the older trace fields:
  dreaderd adds `NNTP-Posting-Host`, `X-Trace`, `X-Complaints-To` and (optionally)
  `NNTP-Posting-Date`. These could move into Injection-Info (`posting-host`, `logging-data`,
  `mail-complaints-to`), as INN does, keeping the old fields behind an option during a
  transition.
- **`.POSTED` in Path** (RFC 5537 3.2.1 step 2): the injecting agent should prepend
  `!.POSTED[.<host>]`. dreaderd writes `Path: <postpath>!not-for-mail`.
- **Date window** (RFC 5537 3.5 step 3): refuse a Date (or Injection-Date) more than 24 hours
  in the future, or older than the relaying cutoff (at least 72 hours). Not checked.
- **Reserved newsgroup names** (RFC 5536 3.1.4, RFC 5537 3.5 step 4): `example.*`, `poster`,
  `to.*`, `control.*`, `junk`, `all` should be refused unless a specific purpose applies.
  `checkpostgroups` only refuses groups that do not exist.
- **Lines** (RFC 5536 3.3.1): obsolescent; agents should not generate it. dreaderd deletes the
  client's and adds its own.
- **Do not alter, delete or reorder existing header fields except Path** (RFC 5537 3.5 step 6):
  dreaderd puts Path first, deletes Lines and an empty Distribution, can rewrite Newsgroups
  (`POST_CLEANUPNEWSGROUPSLINE`), and with `POST_BOFHCLEANUP` refuses To and Cc, which are
  valid RFC 5322 fields in news.
- **Moderated groups** (RFC 5537 3.5 step 7, 3.5.1): forwarding must happen after Message-ID and
  Date are added and before the injection fields; dreaderd builds its trace fields before
  `MailIfModerated()`, so they go to the moderator too. To be checked when Injection-Info is
  added.

Met, for reference: the required From, Newsgroups and Subject, Message-ID and Date added when
missing, a client Message-ID kept unchanged (without `POST_FORCEMSGID`), Approved only with
the readerdef's permission, the moderator forwarding, group status `n`/`x` refusals, Cancel-Lock
(RFC 8315, F-01).

Maintainer decisions (2026-11-04):
- **Lines: no longer generated and not deleted** (RFC 5536 3.3.1 "SHOULD NOT generate"; RFC 5537
  3.5 step 6). A poster's own Lines header is kept as it is. Checked: dreaderd's overview "Lines"
  column is the stored `Lines:` header and stays empty without one (D-174), and dreaderd gets only
  headers, so it cannot count lines itself. The count comes from the feeder with the header feed
  as overview data (`:lines`, D-174), not as an article header, so local posts keep their Lines
  column after this change.
- **posttrailer: removed** (RFC 5537 3.5 step 6, MUST NOT alter the body). Since #283 an unknown
  vserverdef option makes dreader.access invalid, so the keyword stays known: it is logged once
  as no longer supported and ignored, and an old dreader.access still loads.
- **X-Trace: dropped; its information moves into Injection-Info** (RFC 5536 3.2.8), with the
  same encryption that `cryptpw` gives X-Trace today (AES, `C=`, with the optional key
  generation `G=`). Mapping:
  - `<path-identity>`: the vserverdef's path identity (as used for Path).
  - `posting-host`: the client's host or address. With `cryptpw`, the same mangled form as
    today's NNTP-Posting-Host, or left out. The readerdef's NNTP-Posting-Host switch
    (`rd_TurnOffNNPH`) turns it off.
  - `posting-account`: the authenticated user (and ident user). With `cryptpw`, encrypted.
  - `logging-data`: what X-Trace carried (time, server, process, user, address). With
    `cryptpw`, encrypted as `G=...,C=...` as in X-Trace today.
  - `mail-complaints-to`: the vserverdef's abuse address, replacing X-Complaints-To.
  - NNTP-Posting-Date gives way to Injection-Date.
  NNTP-Posting-Host and X-Complaints-To are not generated any more either, since Injection-Info
  carries them. A poster's own X-Trace, NNTP-Posting-Host or X-Complaints-To is still refused
  (RFC 5537 3.5 step 2 allows refusing trace fields of an earlier injection).

Compatibility (maintainer, 2026-11-04): the default follows the standards; where the code
allows it cheaply, an opt-in option keeps today's behavior for sites that depend on it, and each
PR says whether it has one. For the trace fields the option is either-or, never both: by default
Injection-Info and Injection-Date; with the option the old X-Trace, NNTP-Posting-Host and
X-Complaints-To (and NNTP-Posting-Date where built in) instead, with no Injection-Info or
Injection-Date. An option to keep generating Lines is possible the same way. posttrailer stays
removed with no option: it should never have existed.

Suggested order, one PR each: (1) the step-2 refusals (Injection-Info, Xref, `.POSTED`);
(2) Path `.POSTED`, Injection-Info and Injection-Date in place of X-Trace, NNTP-Posting-Host and
X-Complaints-To (with `cryptpw`, and the either-or option; Injection-Date moved here from (1)
because the legacy mode adds none); (3) the Date format and window; (4) syntax
checks (Message-ID, Date, line length); (5) reserved group names; (6) the header-preservation
items, Lines among them; (7) removing posttrailer.

Status: (1) fixed (#304). (2) fixed (#313). (3) fixed (#319). (4) the Message-ID syntax: fixed (#322), together with the vserverdef option `rfc5537` (default yes) that now turns (2) to (4) on together; `legacytrace` is its older spelling. The refusals of (1) and the +0000 Date stay on either way. The line length of (4) is fixed (#366), and (5) is fixed (#367); (6)'s Lines part and (7) are fixed (#365), using D-174 (#312) for overview line counts. The remaining header-preservation items of (6), including supplied-Lines syntax and empty Distribution, are fixed (#369).

### F-08 Smaller overview: overview fields only, one copy per article
dreaderd stores the whole header block of every article in the overview data files (`data.*`), once
for each group the article is in (`dreaderd/group.c`, `WriteOverview()`: the header block, plus the
Xref it adds, is appended to each group's data file). Only control messages also keep their body.
INN stores one overview line per group (the overview fields, a few hundred bytes). This is why
HDR and XPAT work on any header in Diablo, but it makes the overview large: on Jesse's archive
test zone (2026-11-07) the overview is 122 GB on ZFS at a compression ratio of 2x, so about 244 GB
of header data, for a 276 GB spool; INN's overview for the whole 5+ TB spool is 202 GB. Old text
articles have short bodies and long headers (Path, References), and a crosspost multiplies them.
A design would have to settle:
- Which headers to keep: the overview fields (Subject, From, Date, Message-ID, References, Bytes,
  Lines, Xref) plus a configured list of extra headers, as INN's `extraoverviewadvertised` and
  `extraoverviewhidden`. HDR and XPAT on any other header then read the article from the spool,
  or answer as INN does. LIST OVERVIEW.FMT follows the list.
- One copy per article: store the header record once and point every group's index slot at it
  (the slot already has the Message-ID hash, `oa_MsgHash`), so a crosspost costs one index
  slot per group instead of one header copy per group. Expire then has to count references.
- The on-disk change: a new data format next to the old one, `dexpireover -R` to convert, and
  what a reader of an older version does with converted groups.
Until then, `dexpireover -z` (dexpire.ctl `Z`) gzips old data files whole, which compresses header
text much better than file system compression in small records; dreaderd reads them through a
temporary file (tested by `TestReaderReadsGzipData`). Status: open, after v7. Requested by
go-while on 2026-11-08, from the archive zone's numbers.

### F-09 Overview writes scattered over thousands of files
For every article and every group it is in, dreaderd writes the overview twice
(`dreaderd/group.c`, `WriteOverview()`):
- it appends the header block to the group's `data.*` file;
- it overwrites one 32-byte `OverArt` slot in the group's circular index with `pwrite`. The
  index is `over.*`, and the slot is the article number modulo the index size.

Diablo reads nothing to do this (`GetOverArt()` only computes the slot), but the writes are
tiny and land in thousands of different files. A copy-on-write file system such as ZFS
rewrites the whole record for each change, plus the record's indirect blocks and the file's
dnode, so a ~1.2 KB overview entry costs far more on disk. Measured on 2026-11-09 on an archive
test box (illumos, ZFS, about 30,000 groups, history and overview on an SSD pool), in 30-second
samples:
- the pool wrote about 46 times what the daemons wrote (58 KB per overview entry); indirect
  blocks were about 14 of 18 GB logical;
- `zfs_dirty_data_sync_pct` 50 (fewer syncs, so more rewrites of the same block merge) brought
  it to about 25x, and `redundant_metadata=most` to about 18x (23 KB per entry);
- the spool pool, written sequentially, was about 1.1x.

Two overview stores by go-while append in order instead, with few files touched per batch:
- [nntp-overview](https://github.com/go-while/nntp-overview) keeps one append-only file per
  group with one line per article (the overview fields). The article numbers are consecutive,
  so the line number is the article number and no slot table is needed. A sparse `.Index`
  every 100 articles serves seeks, and a footer written after the line commits it, which
  makes the file crash-safe.
- [go-pugleaf](https://github.com/go-while/go-pugleaf) keeps one SQLite database per group in
  WAL mode. The article number is the `INTEGER PRIMARY KEY`, so new rows go to the right edge
  of the B-tree, and inserts are batched per group.

A design would have to settle:
- An append-only index instead of the circular one. The position of an article comes from its
  number and a sparse index, or the index is appended to and searched from the end.
- Expire by dropping whole files from the front (rotated index and data files) instead of
  reusing slots. Cancels mark the record in place (rare) or go to a small tombstone list.
- Writes batched per group and flushed together, not one `pwrite` per article.
- Crash consistency: a commit marker after each batch, as nntp-overview's footer.
- One new on-disk format together with F-08 (overview fields only, one copy per article), not
  two. The conversion is the same `dexpireover -R` step, and the same question applies of what
  an older reader does with converted groups.

Until then, on ZFS (from the 2026-11-08 and 2026-11-09 measurements):
- fewer transaction-group syncs (`zfs_dirty_data_sync_pct` 50, below the 60% write throttle)
  and, where the data can be rebuilt, `redundant_metadata=most`;
- a smaller `recordsize` is not free: it means more indirect blocks. On raidz, overview at 128K
  wrote faster than at 16K;
- history and overview on SSDs with power-loss protection, the spool on rotating disks;
- `primarycache=all` on the spool when a reader header feed reads new articles right after
  they are written.

Status: open, for v7. Requested by go-while on 2026-11-08; numbers from the archive test box
on 2026-11-09.
