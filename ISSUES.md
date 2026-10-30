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

Last updated 2026-10-30: D-167 to D-169 added (Perl authentication, the file and cdb deny lists); F-06 (Perl authentication) added; F-02 noted in progress (#167). Before that, 2026-10-29: D-16 marked fixed (#127). D-135 to D-146 and D-156, D-157 marked fixed (#145, #151 to #154); D-158 won't fix; D-150 to D-155 marked fixed (#158); D-147 to D-149 marked fixed (#159); D-159 to D-162 marked fixed (#160); D-164 to D-166 added from go-while's review of #160. Before that, 2026-10-28: D-91, D-96, D-98 and D-131 to D-133 marked fixed (#146 to #149). F-01 marked done, F-04 (NoCeM) and F-05 (init scripts, done) added, D-163 (file modes, fixed in #139) added; D-135 to D-162 added from go-while's batch of `main` at `0401451`, and a note on D-94. Before that, D-110 to D-134 added from go-while's third review pass of `main` at `6a4b97d`. Before that, 2026-10-27: D-66 to D-109 added from the second review pass and the reviews of the fixes
(D-77 is documented behaviour and only needs a note); the fixed ones are marked with their PR. Before that, D-53 to D-65 added from the review of `main` at `ca648d4`. Before that, 2026-10-26, after batch 1 (#30, #31, #37 to #44, #47 to #49) and the first part
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
| [D-16](#d-16-linux-build-fails-with-the-default-libvendorh) | Medium | Linux build fails with the default `lib/vendor.h` (RADIUS) | fixed (#127) |
| [D-17](#d-17-unchecked-results-under-_fortify_source) | Low | 80 `-Wunused-result` warnings under `_FORTIFY_SOURCE` | fixed (#71) |
| [D-18](#d-18-debian-gnuhurd-build) | Low | Debian GNU/Hurd build: `PATH_MAX`, `setpgrp`, `db_185.h` | open |
| [D-19](#d-19-changelog-links-a-missing-document) | Low | CHANGELOG links the missing `BUILD-PORTABLE-MAKE.md` | fixed (#61) |
| [D-20](#d-20-strncpy-truncation-warning-in-dreaderddnsc) | Low | `strncpy` truncation warning in `dreaderd/dns.c` (GCC 16) | fixed (#65) |
| [D-21](#d-21-dactivekp-write-errors-now-exit-the-process) | — | dactive.kp write errors now exit the process (#7) | won't fix (kept on purpose) |
| [D-22](#d-22-gnu-make-install-ignores-prefix-and-destdir) | High | GNU Make `install`/`uninstall` ignore `PREFIX` and `DESTDIR` (live `/news` used) | fixed (#16) |
| [D-23](#d-23-feederreader-sample-config-is-ignored) | Medium | `samples/feeder+reader/diablo.config` has no `version 2.0`: every directive ignored | fixed (#26) |
| [D-24](#d-24-sample-dreaderaccess-allows-1008-not-localhost) | Medium | Sample `dreader.access` allows 1.0.0.0/8, not localhost | fixed (#55) |
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
| [D-55](#d-55-dreaderd-small-items) | Low | dreaderd small items: descriptor leaks on `!` deny files, OVER range clamp | open |
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
| [D-70](#d-70-dgrpctl-checkgroups-reads-long-lines-as-two) | Medium | `dgrpctl checkgroups`: lines of 128 bytes or more become two lines; an empty list crashes | open |
| [D-72](#d-72-dnewslink-deletes-the-queue-after-an-overlong-line) | Low | `dnewslink` removes the whole queue file after a queue line of 4 to 8 KB without a newline | fixed (#113) |
| [D-73](#d-73-dnewslink-queue-fields-leak-into-the-next-line) | Low | `dnewslink`: `extractFeedLine` keeps fields of the previous line; `readretry` drops H and C flags | fixed (#114) |
| [D-74](#d-74-dnewslink-queue-rewrite-write-errors-are-unchecked) | Low | `dnewslink` does not check the writes of the rewritten queue file before the rename | fixed (#113) |
| [D-76](#d-76-dexpire-removes-a-directory-younger-than-keeptime) | Medium | `dexpire` removes a spool directory younger than its keep time | fixed (#109) |
| [D-77](#d-77-dhisexpire-without--a-leaves-the-history-dead) | Low | `dhisexpire` without `-a` leaves the history marked dead without saying so | open |
| [D-78](#d-78-dicmd-config-internalfilter-disables-the-spam-filter) | Medium | `dicmd config internalfilter` silently disables the internal spam filter | fixed (#124) |
| [D-79](#d-79-kpdb-blank-or-short-line) | Low | A blank or very short line in a KP database corrupts the sort and overflows the lookup stack | fixed (#123) |
| [D-80](#d-80-kpdb-torn-tail-loses-appends) | Medium | A KP database with a torn or NUL tail loses the first append | fixed (#110) |
| [D-81](#d-81-the-sample-internalfilter-line-turns-the-filter-off) | Low | `samples/diablo.config` shows an `internalfilter` option (`t`) that does not exist | fixed (#121) |
| [D-82](#d-82-long-config-lines-are-split) | Low | A long line in `diablo.config`, `dspool.ctl` or `dexpire.ctl` is read as two lines | fixed (#125) |
| [D-83](#d-83-indented-dexpirectl-line-is-ignored) | Low | An indented line in `dexpire.ctl` matches nothing | fixed (#119) |
| [D-85](#d-85-dhisexpire-treats-a-read-error-as-end-of-file) | Low | `dhisexpire` treats a read error as end of file | fixed (#122) |
| [D-86](#d-86-spool-number-off-by-one) | Low | Off-by-one in the spool number checks reads `SpoolObjectMap[100]` | fixed (#120) |
| [D-87](#d-87-numeric-ipv6-host-in-diablohosts-dropped-on-a-machine-without-ipv6) | Low | `getaddrinfo()` with NULL hints in `lib/hostauth.c` drops a numeric IPv6 `diablo.hosts` entry on glibc 2.39 without a global IPv6 address | fixed (#115) |
| [D-88](#d-88-dnewslink--p-overwrites-leftover-lines-of-an-earlier-run) | Low | `dnewslink -p`: a later pipe run overwrites the leftover lines an earlier run wrote to the batch file | open |
| [D-89](#d-89-ai_addrconfig-still-passed-for-listen-bind-and-connect-addresses) | Low | `dreaderd` listen, bind and connect and `dsyncgroups` still pass `AI_ADDRCONFIG` | open |
| [D-90](#d-90-kpdb-sort-writes-the-offsets-one-record-at-a-time) | Low | `KPDBSort()` does one 8-byte write per record; on a full ZFS dataset a sort of 259 records took about 51 s | open |
| [D-91](#d-91-takethis-without-an-argument-answers-439-instead-of-501) | Low | TAKETHIS without an argument answers 439 instead of 501 | fixed (#146) |
| [D-92](#d-92-header-only-feed-reports-unknownreject-for-laerrnobytes) | Low | Header-only feed reports UnknownReject for LAERR_NOBYTES | open |
| [D-93](#d-93-kpdbgetfield-key-scan-is-not-bounded-by-the-record-length) | Low | KPDBGetField key scan is not bounded by the record length | open |
| [D-94](#d-94-dcontrolctl-dnewsfeeds-and-dserverhosts-split-long-lines) | Low | dcontrol.ctl, dnewsfeeds and dserver.hosts split long lines | open |
| [D-95](#d-95-maxinboundrate-value-is-read-past-its-end) | Low | MaxInboundRate value ending in ';' or in an hour number is read past its end | open |
| [D-96](#d-96-spamfilter-s0-turns-the-filter-off-and-usespamrwmap-0-does-not-compile) | Low | spamfilter: s0 turns the filter off, and USE_SPAM_RW_MAP 0 does not compile | fixed (#147) |
| [D-97](#d-97-diab-filter-writes-one-byte-past-its-reply-buffer-writeall-loops-on-a-zero-write) | Low | diab-filter writes one byte past its reply buffer; writeAll loops on a zero write | open |
| [D-98](#d-98-ctype-calls-on-plain-char) | Low | ctype calls on plain char | fixed (#148) |
| [D-100](#d-100-cachefile-builds-the-path-without-bounds) | Low | cacheFile builds the path without bounds | open |
| [D-101](#d-101-spool-server-replies-have-no-line-cap-other-small-items-in-dreaderdserverc) | Low | Spool server replies have no line cap; other small items in dreaderd/server.c | open |
| [D-102](#d-102-readaccesscache-leaks-descriptors-when-a-cache-open-fails) | Low | ReadAccessCache leaks descriptors when a cache open fails | open |
| [D-103](#d-103-access-cache-reload-leaks-the-old-groupdef-and-grouplist-nodes) | Low | Access cache reload leaks the old GroupDef and GroupList nodes | open |
| [D-104](#d-104-dhisexpire-uses-the-history-record-size-without-a-check) | Low | dhisexpire uses the history record size without a check | open |
| [D-105](#d-105-dexpire--o-uses-a-null-file-after-a-failed-fopen) | Low | dexpire -o uses a NULL FILE after a failed fopen | open |
| [D-106](#d-106-dexpireover-getfirstartage-trusts-the-over-file-header) | Low | dexpireover getFirstArtAge trusts the over file header | open |
| [D-107](#d-107-metaspool-with-more-than-100-spool-lines-or-dontstore-with-no-spools) | Low | metaspool with more than 100 spool lines, or dontstore with no spools | open |
| [D-108](#d-108-dcancel-stores-the-history-offsets-before-the-rewritten-spool-file-is-in-place) | Medium | dcancel stores the history offsets before the rewritten spool file is in place | open |
| [D-109](#d-109-dsyncgroups-use-after-free-of-the-address-lists-unchecked-strtok-results) | Medium | dsyncgroups: use after free of the address lists, unchecked strtok results | open |
| [D-110](#d-110-dcontrolctl-ct_verify-leaks-or-keeps-a-stale-value-when-two-rules-match) | Low | dcontrol.ctl: ct_Verify leaks or keeps a stale value when two rules match | open |
| [D-111](#d-111-pgpverify-does-not-check-fdopen-and-realloc-results) | Low | pgpverify does not check fdopen() and realloc() results | open |
| [D-112](#d-112-strcpy-into-sun_path-in-the-filter-socket-code) | Low | strcpy() into sun_path in the filter socket code | open |
| [D-113](#d-113-cached-head-and-body-of-an-article-with-an-lf-only-headerbody-separator) | Medium | Cached HEAD and BODY of an article with an LF-only header/body separator | open |
| [D-114](#d-114-egets-loses-the-prefix-of-an-overlong-line) | Medium | egets() loses the prefix of an overlong line | open |
| [D-115](#d-115-dnewslink-flushcommandbuffer-can-spin-on-a-zero-write) | Low | dnewslink flushCommandBuffer() can spin on a zero write | open |
| [D-116](#d-116-dnewslink-writelarge-reports-a-write-timeout-as-success) | Low | dnewslink writeLarge() reports a write timeout as success | open |
| [D-117](#d-117-kpdbsort-is-not-synchronized-with-readers) | Medium | KPDBSort() is not synchronized with readers | open |
| [D-118](#d-118-kpdbwrite-with-a-null-token-the-documented-behaviour-does-not-exist) | Low | KPDBWrite() with a NULL token: the documented behaviour does not exist | open |
| [D-119](#d-119-articlefilename-builds-a-relative-path-for-an-unconfigured-spool) | Low | ArticleFileName builds a relative path for an unconfigured spool | open |
| [D-120](#d-120-unknown-options-inside-spool-and-metaspool-blocks-are-only-logged) | Low | Unknown options inside spool and metaspool blocks are only logged | open |
| [D-121](#d-121-getspoolfrompath-is-dead-code-with-a-stray-printf-and-a-prefix-match) | Low | GetSpoolFromPath is dead code with a stray printf and a prefix match | open |
| [D-122](#d-122-kp-files-above-2-gb-wrap-the-int-offsets) | Low | KP files above 2 GB wrap the int offsets | open |
| [D-123](#d-123-authinfo-user-accepted-again-after-a-passwordless-authentication) | Low | AUTHINFO USER accepted again after a passwordless authentication | open |
| [D-124](#d-124-capabilities-with-an-argument-answers-101) | Low | CAPABILITIES with an argument answers 101 | open |
| [D-125](#d-125-reader-only-command-on-a-transit-connection-answers-500-not-502) | Low | Reader-only command on a transit connection answers 500, not 502 | open |
| [D-126](#d-126-strtoll-overflow-into-artno_input) | Low | strtoll() overflow into artno_input() | open |
| [D-127](#d-127-dicmd-splits-lines-longer-than-255-bytes) | Low | dicmd splits lines longer than 255 bytes | open |
| [D-128](#d-128-dkp-parses-the-second-piece-of-a-long-input-line-as-a-new-record) | Low | dkp parses the second piece of a long input line as a new record | open |
| [D-129](#d-129-dclient-is-ipv4-only-gethostbyname) | Low | dclient is IPv4-only (gethostbyname) | open |
| [D-130](#d-130-newnews-and-newgroups-ignore-the-distributions-argument) | Low | NEWNEWS and NEWGROUPS ignore the distributions argument | open |
| [D-131](#d-131-dclient-does-not-know-the-202-reply-of-list-extensions) | Low | dclient does not know the 202 reply of LIST EXTENSIONS | fixed (#149) |
| [D-132](#d-132-config-dump-prints-the-bare-cr-option-under-the-nul-name) | Low | Config dump prints the bare-CR option under the NUL name | fixed (#149) |
| [D-133](#d-133-list-moderators-adds-an-extra-cr-to-a-crlf-line) | Low | LIST MODERATORS adds an extra CR to a CRLF line | fixed (#149) |
| [D-134](#d-134-dkp-trim-mode-keeps-the-tail-of-a-long-deleted-line) | Low | dkp trim mode keeps the tail of a long deleted line | open |
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
| [D-164](#d-164-a-misspelled-group-list-name-in-a-readerdef-falls-back-to-the-first-list) | Low | A misspelled `groups`/`listgroups`/`postgroups` name in a readerdef falls back to the first group list | open |
| [D-165](#d-165-dreaderd-exits-when-one-access-cache-file-cannot-be-opened) | Low | dreaderd exits when one access-cache file cannot be opened | open |
| [D-166](#d-166-a-misspelled-readerdef-option-is-logged-but-the-file-is-still-installed) | Low | A misspelled readerdef option is logged but the file is still installed | open |
| [D-167](#d-167-perl-authentication-uses-only-the-first-script-a-worker-loads) | Low | Perl authentication uses only the first script a worker loads | open |
| [D-168](#d-168-a-file-deny-list-leaks-a-file-handle-for-every-listed-user-that-logs-in) | Low | A `file !` deny list leaks a file handle for every listed user that logs in | open |
| [D-169](#d-169-the-cdb-deny-list-does-not-work-and-rejects-every-user) | Low | The `cdb !` deny list does not work and rejects every user | open |

Feature requests are at the end: [F-01 Cancel-Lock](#f-01-cancel-lock-rfc-8315), [F-02 configure script](#f-02-configure-script-for-the-build-options), [F-03 refeed a whole spool](#f-03-refeed-a-whole-spool-to-another-server), [F-04 NoCeM notices](#f-04-nocem-notices), [F-05 init scripts](#f-05-init-scripts-and-adm-script-replacements), [F-06 Perl authentication](#f-06-perl-authentication-for-dreaderd).

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

### D-87 Numeric IPv6 host in diablo.hosts dropped on a machine without IPv6
`lib/hostauth.c:240` called `getaddrinfo(host, NULL, NULL, ...)`. With NULL hints glibc uses
`AI_ADDRCONFIG|AI_V4MAPPED`, and on glibc 2.39 (EL10) `AI_ADDRCONFIG` also drops a numeric IPv6
address when the machine has no non-loopback IPv6 address: `getaddrinfo("2001:db8::25")` fails with
`EAI_ADDRFAMILY`, so an IPv6 `diablo.hosts` entry is silently ignored. Found by go-while's matrix
run (`TestHostCacheDiabloHostsForms` failed on AlmaLinux 10, CentOS Stream 10 and Rocky 10). Fixed
with explicit hints (#115).

### D-89 AI_ADDRCONFIG still passed for listen, bind and connect addresses
`dreaderd/main.c:485` (listen, `AI_PASSIVE|AI_ADDRCONFIG`), `dreaderd/server.c:401` (connect and
bindhost) and `util/dsyncgroups.c:906` still pass `AI_ADDRCONFIG`. On a glibc 2.39 host without a
global IPv6 address a numeric IPv6 listen, bind or connect address (for example `::1`) fails with
`EAI_ADDRFAMILY`. Same cause as D-87. From reading (found in the reviews of D-87).

### D-90 KPDB sort writes the offsets one record at a time
`KPDBSort()` (`lib/kpdb.c`) writes the sorted offset of every record with its own 8-byte `pwrite`.
On a ZFS dataset at its quota each such write took about 0.2 s: `dkp -r` and `dgrpctl newgroup` on a
full 2 MB test dataset with 259 records spent 40 to 50 s in the sort, with the file locked, while the
same sort takes well under a second with free space. A real active file has thousands of groups. An
interrupted sort is safe (the next open sorts again), but a sort that is interrupted by a timeout
starts over each time. Found while testing a full file system on the scratch instance. Fix idea:
build the offsets in memory and write them in one or a few large writes.

### D-91 TAKETHIS without an argument answers 439 instead of 501
`util/diablo.c:2076`: `TAKETHIS` with no message-id answers 439 (MsgIdMismatch); the other commands answer 501. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-92 Header-only feed reports UnknownReject for LAERR_NOBYTES
`util/diablo.c:3629-3639`: `LAERR_NOBYTES` on a header-only feed is reported as `UnknownReject`; the `HdrOnlyNoBytes` branch cannot be reached. Cosmetic. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-93 KPDBGetField key scan is not bounded by the record length
`lib/kpdb.c:403-409`: the scan for a key (`tok == NULL`) does not stop at `recLen`. Needs a damaged file. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-94 dcontrol.ctl, dnewsfeeds and dserver.hosts split long lines
`dreaderd/control.c:354`, `:373`: `dcontrol.ctl` is read with `fgets(buf[256])`; the same class (see D-82) in `dnewsfeeds` through `lib/include.c` `igets` (no long-line signal) and in `dserver.hosts` (`dreaderd/server.c:109`, `:116`). From reading, not run (deep review of main 76578ae by go-while's reviewers). The same long-line splitting occurs when serving LIST DISTRIBUTIONS, LIST DISTRIB.PATS and LIST MODERATORS (`dreaderd/list.c:446-459`, `:474-488`, `:517-529`): each 255-byte `fgets` chunk becomes a separate protocol entry instead of preserving or rejecting a complete source line (malformed list output for long local lines; independent of D-133). Added from go-while's third batch, not run.

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

### D-101 Spool server replies have no line cap; other small items in dreaderd/server.c
`dreaderd/server.c`: spool replies have no line-length cap; the `ctl[1]` test on a one-character reply (`:821`). Found by go-while's reviewers from reading. The `ctl[1]` read is confirmed with AddressSanitizer on a Debian build: `NNFinishSReq(conn, "", 0)` (the `COM_STAT` case in `dreaderd/spool.c`) makes it read one byte past the empty string (global-buffer-overflow, reached by every STAT answered from the spool server). A normal build shows nothing, but an ASan build of dreaderd exits there, so e2e runs against one fail until it is fixed.

### D-102 ReadAccessCache leaks descriptors when a cache open fails
`dreaderd/dns.c:3335-3373`: one failed open of the five cache files leaves the others open, and every retry leaks more. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-103 Access cache reload leaks the old GroupDef and GroupList nodes
`dreaderd/dns.c:3420-3444`, `:3477-3480`: each reload leaks the old nodes and names in `DnsMemPool`. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-104 dhisexpire uses the history record size without a check
`util/dhisexpire.c:196-198`: `hh.henSize` is used as the record size with no zero or minimum check (SIGFPE, or a read past the buffer). Needs a damaged history. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-105 dexpire -o uses a NULL FILE after a failed fopen
`util/dexpire.c:973-976`, `:1170-1182`: with `-o`, a failed `fopen` is logged, and the later `fwrite` and `fclose` use NULL. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-106 dexpireover getFirstArtAge trusts the over file header
`util/dexpireover.c:1026-1044`: `oh_MaxArts` is not validated (SIGFPE on 0) and `oaBase[i]` is read before the `i <= n` check. Needs a damaged over.* file. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-107 metaspool with more than 100 spool lines, or dontstore with no spools
`lib/spool.c:840-846`: more than 100 `spool` lines in one metaspool write past `ms_SpoolObjects[]`. `lib/spool.c:302-352` with `addMeta` (`:505-510`): a `dontstore yes` metaspool with no spools reaches a division by zero (allocstrat single) or index -1. Config. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-108 dcancel stores the history offsets before the rewritten spool file is in place
`util/dcancel.c:589-608`: the history offsets are stored before the rewritten file is installed; a later failure or a failed rename leaves the history pointing into a file that was not installed. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-109 dsyncgroups: use after free of the address lists, unchecked strtok results
`util/dsyncgroups.c:918-955`: `freeaddrinfo(dst)` at `:919` and `dst` is used at `:943`, `:949` on every connection; `:929` frees an unset `src`. `:467-469`, `:678-728`: the `strtok` results on a short `LIST ACTIVE` line from the peer are not checked; `CommandResponse` returns on EOF without setting `*pres` (`:965-989`). Checked in the code. From reading, not run (deep review of main 76578ae by go-while's reviewers).

### D-110 dcontrol.ctl: ct_Verify leaks or keeps a stale value when two rules match
`dreaderd/control.c:420-424` clears the flags of a reused `Control` but not `ct_Verify`; `:456` assigns a new `zallocStr()` over it. With two matching lines that both say `verify-`, the first string leaks (connection mempool, so bounded) and only the last line's flags and verifier are used. Cosmetic: free before assigning and clear it in the reuse branch. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-111 pgpverify does not check fdopen() and realloc() results
`util/pgpverify.c:218` (`Body = realloc(...)` then `memcpy`), `:391`, `:405`, `:533`, `:572` (`fdopen()` then `fprintf`/`fgets` on the result). On out-of-memory or descriptor exhaustion pgpverify dereferences NULL and crashes (dreaderd treats that as a failed verification, so it fails closed). One-line guards. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-112 strcpy() into sun_path in the filter socket code
`filter/socket.c:76`: `strcpy(sun.sun_path, pathname)` with no length check; a configured unix-socket path longer than `sizeof(sun_path)` overflows the `struct sockaddr_un`. Config only (same class as D-100). Use `snprintf` with a length check. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-113 Cached HEAD and BODY of an article with an LF-only header/body separator
`dreaderd/cache.c:480` ends the header only on a CRLF blank line (`i == b + 2 && map[b] == '\r' && map[b+1] == '\n'`). A blank line that is a bare LF (`i == b + 1`) never clears `inHeader`. HEAD (`cache.c:371-376`) then prints the whole article, and BODY (`cache.c:398-399`, prints only when `inHeader == 0`) prints nothing. Affects articles stored with LF-only line ends. Wrong reply from the cache for such articles, no crash. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-114 egets() loses the prefix of an overlong line
`lib/buffer.c:372-399`. When `bgets()` reports a full buffer without a newline it has set `bu_Beg = bu_NLScan = bu_End` (`buffer.c:198-204`). `egets()` copies `bu_End` bytes into a larger block but leaves `bu_Beg == bu_End`, and the next `bgets()` sees that and resets all offsets to 0 (`buffer.c:140-142`), so the copied prefix is dropped and the returned line is only the tail. The caller is `util/diablo.c:1461-1463` (the control pipe, one SOUT line per call). If a SOUT line can exceed the page-sized buffer (a long Newsgroups list), the master sees a tail that does not start with `SOUT` and the article is silently not fed. The mechanism is exact; whether a real SOUT line can reach that length was not checked (Newsgroups length cap). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-115 dnewslink flushCommandBuffer() can spin on a zero write
`util/dnewslink.c:2974-3019`. `r = write(cfd, Cmd + b, Cmde - b)`; a return of 0 is not treated as special (`b += r` at `:2989`), so `b` does not move. The `poll()` at `:3004` only leaves the loop on a timeout, so a socket that keeps returning 0 would loop without a timeout. Same pattern as D-97 (`writeAll`). A blocking Linux socket does not normally return 0, so practical reach is low. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-116 dnewslink writeLarge() reports a write timeout as success
`util/dnewslink.c:3067-3104`. `NDEBUG` is defined at `:50` (before `<assert.h>` at `:51`), so `assert(bytes == 0)` at `:3102` is compiled out. On a poll timeout the code sets `WritesLosing` and breaks at `:3084` with bytes left, then returns the last `writev()` status, which is 0 after an EAGAIN (`:3061`) or the last partial count. The caller at `:2260` (`if (writeLarge(...) < 0)`) treats any non-negative value as success, so the article's result is wrong for that record. The connection is marked losing, so the next command fails. What the code does after success (whether the record is refiled) was not checked. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-117 KPDBSort() is not synchronized with readers
`lib/kpdb.c:1286-1361`. `KPDBSort` takes no lock of its own and rewrites each record's offset field with one 8-byte write per record (`:1341-1361`). `KPDBLookup` locks only on a miss with `KP_LOCK` (`:1120-1122`), and the offset check at `:1163` accepts stale offsets that are in range. A reader that looks up a key during a resort can take a wrong branch and report not found for an existing key until the sort ends (a false hit is not possible, the final key compare is exact). The sort runs from `KPDBWrite` (every 128 appends), and D-90 measured it at about 51 s on a slow disk. Transient "no such group" style errors, no corruption. The caller's append lock (`kpdb.c:804-805`) and the read lock (`:261`) were not re-traced. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-118 KPDBWrite() with a NULL token: the documented behaviour does not exist
`lib/kpdb.c:636-640` says a NULL token unlocks a record without writing and creates a record if the key is missing. The code does neither: `strlen(tok)` at `:870` (copy-old path) and `sprintf("%s", tok)` at `:912` (new record) use the NULL, and a same-length replacement at `:744-766` can write over the key if `KPDBGetField(NULL)` returns it (that last point was taken from the report, not re-traced). Every current caller passes a token, so this is latent. Fix the comment or handle NULL. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-119 ArticleFileName builds a relative path for an unconfigured spool
`lib/spool.c:179-181`: `GetSpoolPath()` returns `""` when the spool object is missing (spool number below `MAX_SPOOL_OBJECTS` but not configured); only numbers at or above `MAX_SPOOL_OBJECTS` give `/dev/null` (`spool.c:150-151`). `ArtFileName` (`spool.c:94-108`) then builds `D.%08x/...` with no leading directory, so dexpire, dcancel and dreadart open or unlink that name in the current directory. Trigger: a history record that refers to a spool number the admin has removed from `dspool.ctl`. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-120 Unknown options inside spool and metaspool blocks are only logged
`lib/spool.c:764-767` (spool block) and `:869-872` (metaspool block) log "Unknown ... option" and go on; an unknown top-level definition exits (`spool.c:707-710`). A typo in `minfree`, `maxsize`, `keeptime`, `spooldirs` and so on is ignored and the spool runs with the default (for example no free-space floor). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-121 GetSpoolFromPath is dead code with a stray printf and a prefix match
`lib/spool.c:198` prints `XX:%s:%s:` to stdout on every iteration; `:199` compares `strlen(so_Path)` bytes, so "P.01" also matches "P.011". No caller in the tree (only the prototype at `spool.c:43`). Remove it or fix it. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-122 KP files above 2 GB wrap the int offsets
`lib/kpdb.c:800` (`int nlockoff` from `lseek`) and `:289` (`kp_MapSize = (int)st.st_size`). Between 2 and 4 GB the append offset goes negative (lock failure, then a controlled `KPDBFatalIO` exit); past 4 GB it could wrap to a wrong positive offset. KP files are small control databases and the format itself holds 8 hex digits, so this is a design-limit note. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-123 AUTHINFO USER accepted again after a passwordless authentication
`dreaderd/nntp.c:92-94` answers 502 "Already authenticated" only when `dr_AuthPass[0]` is non-empty. A connection authorized by host or address in `dreader.access` with no password has an empty `dr_AuthPass`, so `AUTHINFO USER x` falls through to `:104-111`, answers `381 PASS required` and replaces `dr_AuthUser`. The following PASS re-verifies the new identity, so this is not an access bypass; it deviates from RFC 4643 (502 expected) and is a leftover of D-46. What state a passwordless connection starts with was not traced (that part is the reviewer's claim). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-124 CAPABILITIES with an argument answers 101
`dreaderd/reader.c:1393-1398` (`NNTPCapabilities`) never uses `pptr`, so `CAPABILITIES foo` gets the capability list; RFC 3977 section 5.2 wants 501. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-125 Reader-only command on a transit connection answers 500, not 502
`dreaderd/reader.c:1289-1316`: the dispatch loop skips entries whose flags do not match the connection type and falls through to `NNUnknownCommand` (500 "What?"). RFC 3977 section 5.3 wants 502 for a command that is known but not available. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-126 strtoll() overflow into artno_input()
`dreaderd/nntp.c:422` passes `strtoll(artid, NULL, 10)` to `artno_input` (`dreaderd/subs.c:103-119`) with no `errno` check. `subs.c` rejects values above 0x7FFFFFFF only for RFC 3977 numbering; for window numbering it adds a non-zero delta, so `LLONG_MAX + delta` is signed overflow (undefined behaviour). The delta is non-zero only with window numbering and a group whose high article number is at least 1.5e9 (`subs.c:27-49`); in smaller groups it is 0 and the client simply gets 423. Theoretical. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-127 dicmd splits lines longer than 255 bytes
`util/dicmd.c:19` (`char buf[256]`), `:51-64` and `:84-91`: `fgets` splits a longer line, and each piece is sent to the server or printed as its own line. Not a hang: the `".\n"` terminator test cannot match a split piece. Cosmetic for very long lines. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-128 dkp parses the second piece of a long input line as a new record
`util/dkp.c:148-149`: `fgets` into a 1023-byte buffer and `strtok` on it. A longer line is split and the second piece is parsed as a new record, so a long key or value is written as truncated records. Data-quality issue for admin input. The trim mode has a related problem, see D-134. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-129 dclient is IPv4-only (gethostbyname)
`util/dclient.c:61-73`: `connectnews()` uses `gethostbyname()` and a `struct sockaddr_in`, so it cannot reach an IPv6-only host. The `memcpy` of `h_length` bytes into the 4-byte `sin_addr` is safe for AF_INET results. Modernization: `getaddrinfo()`. Limitation of a test client. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-130 NEWNEWS and NEWGROUPS ignore the distributions argument
`dreaderd/xover.c:53-54` and `:76` store the distribution pattern in `co_ListPat`, but the NEWNEWS output path (`xover.c:445-449`, `:591-595`) and `dreaderd/list.c:663-674` (NEWGROUPS) never apply it; `co_ListPat` is read in the output path only at `xover.c:583` (the XPAT case). Without the `GMT` token the fourth argument is taken as the GMT field (`xover.c:53`, `list.c:666`). The 501 text advertises `[<distributions>]`. RFC 3977 lets the server ignore it; the behaviour does not match the usage text. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-131 dclient does not know the 202 reply of LIST EXTENSIONS
`util/dclient.c:117-124` (`MultiLineReply`) lists no 202, while `dreaderd/list.c:138-141` answers 202 followed by HDR, OVER and the dot. dclient prints "Unexpected response from server" and exits 2. A fix with a test is ready on go-while's side (one-line `case 202`, test `TestDclientListExtensions`). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-132 Config dump prints the bare-CR option under the NUL name
`lib/config.c:892` prints `"rejectartswithnul: %d"` for `DOpts.RejectArtsWithBareCR`, so `dicmd config rejectartswithbarecr` shows a wrong label and the NUL option appears twice in a full dump. A fix with a test is ready on go-while's side (`TestDicmdConfigRejectArtsWithBareCR`). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-133 LIST MODERATORS adds an extra CR to a CRLF line
`dreaderd/list.c:523-528`: only the trailing LF is stripped, so a CRLF line from the moderators file keeps its CR and then gets `"\r\n"` appended (`\r\r\n` on the wire; the e2e reader flags it as a bare CR in the line). A fix with a test is ready on go-while's side (`TestReaderListModeratorsCRLF`). Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

### D-134 dkp trim mode keeps the tail of a long deleted line
`util/dkp.c:106-115`: the trim loop reads with `fgets` into `tmp[1024]` and drops lines that start with `-`. For such a line longer than the buffer, the first chunk is skipped by the `continue` at `:108-109` before `lnl` is updated, so `lnl` stays 1 and the next chunk (the rest of the same line) is taken as a new line start and written out unless it happens to start with `-`. So part of a line meant to be deleted stays in the KP file. Data quality only (the admin's own KP file). Fix: update `lnl` before the `continue`, or skip until the newline. Found by go-while's reviewers from reading, checked in the code at main 6a4b97d, not run.

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

### D-168 A `file !` deny list leaks a file handle for every listed user that logs in
`dreaderd/dns.c`, `fileAuthenticate()`: with a path that starts with `!` (an authdef `file !/path` is a deny list), the function returns `NULL` as soon as it finds the user in the file, without `fclose()`. Every login of a listed user leaves one open file in the DNS/auth worker. Verified on Debian with a deny file that lists a user: after 400 logins of that user one worker has 407 open descriptors, up from 7, and an unlisted user does not leak. With a limit of 1024 descriptors the worker runs out of descriptors after about a thousand refused logins and fails every later authentication until it is restarted, so a listed user (or a script) that keeps trying can lock everyone out. Low to medium. The fix is one `fclose(fp)` before that return.

### D-169 The `cdb !` deny list does not work and rejects every user
`dreaderd/dns.c`, `cdbAuthenticate()`: the `!` in front of the path marks a deny list, but unlike `fileAuthenticate()` the function does not step over it, so `open("!/path/users.cdb")` fails, the error "unable to open auth cdb" is logged, and the caller treats the `NULL` as "listed" and rejects. Verified on a build with cdb (tinycdb) and an authdef with `cdb !<file>` plus `user`/`pass`: a user who is not in the cdb file is refused (481), and so is a listed one, so the deny list refuses everybody, and the user/pass of the same authdef can never log in. Low (it fails closed, and `cdb !` is probably rarely used). Once the `!` is stepped over, the early `return(NULL)` for a listed user leaves the file descriptor open, the same leak as D-168. The fix is `conf++` after the check and a `close(fd)` before that return.

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
go-while on 2026-10-27. **Status: in progress in #167** (draft): a hand-written POSIX-sh `./configure` probes zlib, libcanlock, Berkeley DB, cdb, PAM, LDAP, Perl and RADIUS (radcli on Linux), writes `config.mk` and sets the `vendor.h` options; review findings are being worked before it lands. This also settles D-16.

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
modernization (alongside F-02's configure step, which also wires up RADIUS/radcli, cdb and PAM). Work
is on the maintainer's branches: **#167** adds `./configure` feature-detection and RADIUS via radcli;
**#168** adds the tracking rows D-167 to D-169 (Perl authentication, file and cdb deny lists); **#169**
adds `contrib/htpasswd-auth.pl` (auth against an Apache htpasswd file) and reworks the dreaderd Perl
hook so each distinct `perl` line gets its own interpreter (fixes D-167) and a failed load refuses
logins instead of killing the worker.

`PERL_ENABLED` is treated as a feature, not a quick merge: it goes live only after (1) the #167/#169
review findings are fixed — notably the htpasswd no-cache DoS and the `call_checkuser` `G_EVAL` guard
(patch sent), the configure missing-`config.mk` hard error, and the radcli Filter-Id fail-open — and
(2) a second deep code review of the fixed auth path (`dreaderd/dns.c` `PerlAuthenticate`/`perlWords`,
`contrib/htpasswd-auth.pl`, the radcli adapter). Agreed with the maintainer on 2026-10-30.
