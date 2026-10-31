# End-to-end tests

These tests run Diablo's programs as separate processes and talk to them over
real TCP connections, the way a peer or an operator would.

Many tests build a throwaway feeder site in a temporary directory: a
`diablo.config` whose `path_home` points there, `dnewsfeeds`, `dspool.ctl`,
and `diablo -d` (in the foreground) listening on a free port of 127.0.0.1.
Tests also start other programs and use host resources such as network ports.
The root-only full-disk test mounts a temporary filesystem; the optional
`ci/build-on-host` setup helper changes the host by installing packages and
creating a user. Run on a throwaway machine, not a live news server.

The tests are written in Go and use the
[nntp-testkit](https://github.com/go-while/nntp-testkit) library (standard
library only): a strict NNTP client that fails on bare LF line ends, bad
dot-stuffing, a missing `.` terminator or stray bytes after a reply, fake NNTP
peers, and `proc`, which starts and stops `diablo` and prints its output when a
test fails.

## What is covered

| test | programs | what it checks |
|---|---|---|
| `TestPeerIHAVE` | diablo | IHAVE from the peer (127.0.0.2) is accepted (335, 235); a second offer gets 435 |
| `TestStrangerRefused` | diablo | a host that no `dnewsfeeds` label lists gets `502 ... Transfer permission denied` |
| `TestDicmdConfig` | diablo, dicmd | `dicmd config maxconnect 17` is applied and reported; `path_dumphist` is rejected (startup only) |
| `TestGeneralLogNotWritable` | diablo | with an unwritable `path_generallog`, diablo reports it and keeps accepting articles instead of crashing |
| `TestHostCacheLastPeerRemoved` | diablo, dprimehostcache | removing the last `inhost` empties `dhosts.cache`, and the former peer gets a permission denial, not "label missing" |
| `TestHostCacheRebuildAfterFreshStart` | diablo | after a start without `dhosts.cache`, a peer added to `dnewsfeeds` is let in once the cache is rebuilt |
| `TestDnewslinkStreaming` | diablo, dicmd, dnewslink | queued articles reach a streaming peer (fake) in order, with TAKETHIS and no IHAVE |
| `TestDnewslinkIHAVEFallback` | diablo, dicmd, dnewslink | a peer that refuses MODE STREAM gets the articles with IHAVE |
| `TestDnewslinkPeerHas` | diablo, dicmd, dnewslink | an article the peer already has is not sent (CHECK gets 438); the others are |
| `TestDnewslinkLiteralPercentLabel` | diablo, dicmd, dnewslink | a `%` in a feed label remains literal in its queue path when `-S` is omitted |
| `TestDnewsfeedsHashfeedFiltersByMessageID` | diablo, dicmd | outgoing feed filtering splits Message-IDs correctly with all four MD5 word offsets (0, 4, 8, 12) and legacy quickhash (`@`) |
| `TestDspoolHashfeedSelectsSpoolByMessageID` | diablo, dicmd | complementary metaspool filters route each Message-ID to the matching spool with all four MD5 word offsets (0, 4, 8, 12) and legacy quickhash (`@`) |
| `TestArticleTypes` | dfeedtest | the ArtType (`lib/arttype.c`) of 34 sample articles: plain, MIME, html, control, cancel, postscript, partial, uuencode, base64, yEnc, PGP in every armor form, and others. Where the result is a misdetection (PGP signatures and keys taken for base64 binary, other PGP forms unrecognised, yEnc keyword orders, upper-case and unspaced headers, base64 text), the case pins today's result and says what it should be |
| `TestArticleTypesRoutedByArttypes` | diablo | an `arttypes binary` label gets a yEnc article and an `arttypes !binary` label a text one; a long PGP signature goes to the binary label today (pinned misdetection) |
| `TestDnewsfeedsLimitsDfeedtest` | dfeedtest | dnewsfeeds `maxsize`, `minsize` (with `k`), `maxcross`, `mincross`, `maxpath` and `minpath`: dfeedtest feeds an article only inside each range, with the edge values on both sides. `minsize` is exclusive and `maxsize` inclusive, and both are pinned |
| `TestDnewsfeedsLimitsLive` | diablo | the same six limits on a running diablo: an article outside the range does not reach the label's queue, one inside does |
| `TestStatWithLongSpoolPath` | diablo | STAT of a stored article answers 223 with a spool path of about 125 characters (D-62). **Fails** on main: 430 Article expired |
| `TestDnewsfeedsMaxconnect` | diablo | `maxconnect 2` in a dnewsfeeds label lets two parallel connections in and refuses the third with 502 (D-59). **Fails** on main: the third was accepted |
| `TestDhisexpireFailureKeepsHistoryAlive` | diablo, dhisexpire | a dhisexpire that fails after marking the history dead (backup directory missing) marks it alive again: dhistory is not dead and diablo still greets new connections (D-57). **Fails** on main |
| `TestDhisexpireKeepsBackup` | diablo, dhisexpire | a normal `dhisexpire -a -o file` installs the new history, replaces an earlier backup with the old file, leaves no temporary name, and diablo still knows the article |
| `TestDhisexpireNoteWithoutA` | diablo, dhisexpire | without `-a` the old history stays marked dead and the new one is left beside it; the run says so at the end (the note could never print before D-77 was finished), and `dhisexpire -u` makes the old one usable |
| `TestDnewsfeedsDirectiveWithoutArgument` | dfeedtest | each numeric dnewsfeeds option (`maxparallel`, `port`, `priority`, `feedertxbuf`, ...) written without its value is a configuration error, not a crash (D-58). **Fails** on main: SIGSEGV |
| `TestDnewsfeedsReloadWithoutArgument` | diablo | saving such a line into dnewsfeeds of a running diablo does not take the master down (D-58). **Fails** on main (the master exits at the next reload); takes 33 s |
| `TestHostCacheEndEndsLabel` | diablo (dprimehostcache) | an `inhost` line after the `end` of a label is not entered into the host cache under that label (D-60). **Fails** on main |
| `TestHostCacheDiabloHostsForms` | diablo (dprimehostcache) | diablo.hosts takes an IPv6 address as well as the `host: label` and `host:label` forms (D-61). **Fails** on main: `2001:db8::25` became host `2001`, label `db8` |
| `TestReaderCheckgroupsControl` | dreaderd | an accepted checkgroups control message updates the description of an existing group and does not create a group it names that does not exist (D-53). **Fails** on main: `0 0 n` in LIST ACTIVE and `211 1 -1 -1` |
| `TestHostCacheLongLabel` | diablo (dprimehostcache) | a 3000-character label name in dnewsfeeds does not overflow the host cache loader's buffer; its hosts are left out and the next label is read. **Fails** on main (aborts or corrupts the stack) |
| `TestDnewslinkRefileKeepsFlags` | dnewslink | entries pending in a stream when the peer goes away come back to the queue file once each, with their `H` and `C<size>` flags (D-63). **Fails** on main: flags lost, one entry written twice |
| `TestFeederMaxHeaderSize` | diablo | an article with 130 KB of headers is refused under `feedermaxheadersize 2k`, and a normal article after it is accepted (D-56). **Fails** on main: the limit was never applied |
| `TestWeightedAllocationWithZeroWeights` | diablo | `allocstrat weighted` with every spool at `weight 0` starts and stores articles (D-64). **Fails** on main: SIGFPE at start |
| `TestReaderListActiveAfterRecordRewrite` | dreaderd | a reader that listed the groups before a control message rewrote a group record lists every group in its next LIST ACTIVE (D-54). **Fails** on main: the appended record was skipped once |
| `TestDnewslinkTermStalledPeer` | diablo, dnewslink | SIGTERM while dnewslink waits for a stalled peer (at the greeting, and on an unanswered CHECK/TAKETHIS): it exits within 5 s and both articles stay queued. **Fails** on main before this fix: dnewslink polled again for the full `-T` timeout (600 s) after the signal |
| `TestReaderFeedAndRead` | dreaderd, dgrpctl | an article fed to the reader by IHAVE shows in GROUP and OVER; BODY and ARTICLE fetch it from the spool server (fake) |
| `TestReaderPerlAuth` | dreaderd | with `PERL_ENABLED` (`./configure`), an authdef `perl <script>` logs in when the script's `checkuser` returns a result starting with 1, and refuses a wrong password; skipped when dreaderd is built without Perl |
| `TestReaderHtpasswdAuth` | dreaderd | `contrib/htpasswd-auth.pl` as the `perl` of an authdef checks an Apache htpasswd file read again for every login: `$apr1$`, `{SHA}`, `{SSHA}` and the formats of the platform's `crypt(3)` (MD5, SHA-256/512, DES, bcrypt, yescrypt) where it has them; wrong passwords, unknown users and plain text entries are refused, and a changed, added or removed entry counts at once; skipped without Perl support |
| `TestReaderRadiusAuth` | dreaderd | with RADIUS (`./configure`; libradius on FreeBSD and SmartOS, radcli on Linux), an authdef `radius <radius.conf>` asks a RADIUS server (fake, in the test): the right password (also one of more than 16 bytes) logs in, a wrong one or an unknown user does not, a Filter-Id in the Access-Accept selects the readerdef of that name, and the request carries the user, the password and Calling-Station-Id; skipped without RADIUS |
| `TestReaderRadiusServerDown` | dreaderd | a RADIUS server that does not answer, and a radius file that does not exist, refuse the login and dreaderd keeps serving |
| `TestReaderRadiusNativeConfig` | dreaderd | radcli only: a radcli `radiusclient.conf` (`authserver host:port:secret`) is read as it is |
| `TestReaderCdbAuth` | dreaderd | with `CDB_ENABLED` (`./configure`, where a cdb library with `cdb_seek` is found), an authdef `cdb <file>` looks the user up in a cdb file whose record is the password (the file is written by the test): the right password logs in, a wrong one and an unknown user do not; skipped without CDB support |
| `TestReaderQuitAlone` | dreaderd | QUIT gets 205 and nothing follows it |
| `TestReaderQuitWhileConnecting` | dreaderd | a client that QUITs while another connects gets 205 and nothing after it. **Fails** on main before jrehmer/diablo-nntp#12 (dreaderd wrote "400 Server too busy" after the 205 when its only DNS worker was busy) |
| `TestReaderPost` | dreaderd | POST is passed to the outgoing ('o') server (fake peer) with IHAVE: body as posted, the vserver's `postpath` in the Path |
| `TestReaderCapabilities` | dreaderd | CAPABILITIES (RFC 3977 §5.2): a reader connection lists VERSION 2, READER, OVER, LIST ACTIVE ACTIVE.TIMES NEWSGROUPS OVERVIEW.FMT DISTRIB.PATS, POST and IMPLEMENTATION, and each of them works; a feed-only connection lists IHAVE and STREAMING; MODE STREAM is 501 in reader mode. Then nntp-testkit's conformance checks run against the reader, and none may fail. **Fails** on main before #41: "500 What?" |
| `TestReaderReplyCodes` | dreaderd | RFC 3977 replies (D-44, D-45, D-47): 412 for LISTGROUP/NEXT/LAST without a group; 420 for ARTICLE/HEAD/BODY/STAT/NEXT/LAST/OVER/HDR in an empty group; LISTGROUP answers "211 count low high group" and honours a range; ARTICLE with a non-number, non-message-id argument is 501; HDR answers 225 (an absent header has an empty value), XHDR still 221; LIST HEADERS and LIST ACTIVE.TIMES answer 215; OVER with a range holding no article is 423, XOVER still an empty 224. **Fails** on main before this change |
| `TestSpoolLongXrefPath` | dreaderd | a spool answer with an Xref: or Path: header over 8 KB is cut to the rewrite buffer; HEAD and ARTICLE by message-id answer and the reader keeps serving (GHSA-5288-8932-6mrw) |
| `TestReaderRangeBeyondLimit` | dreaderd | with `XNUMBERING rfc3977`, an article number above 2147483647 in an OVER, HDR or LISTGROUP range is 501 (D-147, D-148), and a refused LISTGROUP leaves the selected group alone; ordinary ranges still work |
| `TestReaderNewnewsKeepsSelectedGroup` | dreaderd | the group and article selected before `NEWNEWS` are still current afterwards, also when the list has a missing group first, in the middle or alone, and a group after a missing one is still scanned (D-149) |
| `TestReaderNewnewsUnreadableOverview` | dreaderd | a group whose overview index cannot be opened is skipped by the `NEWNEWS` scan: the next group is listed and the client's group stays selected (D-149) |
| `TestNewgroupsDates` | dreaderd | NEWGROUPS date and time (RFC 3977 7.3.2): month 13, day 32 or 40, 31 April, 29 February of a non-leap year, hour 24, minute or second 61 and above and a time that is not six digits are 501; yyyymmdd works for any year; the group made just now shows for an old date and not for a date before 2038; second 60 (a leap second) is accepted. A two-digit year keeps diablo's old +/-50 years window around today (`270101` is 2027), not RFC 3977's "past century" rule: a compatibility exception, so this is not full RFC conformance. **Fails** on main (the invalid dates were rolled over; 19990101 was 501) |
| `TestNewnewsDates` | dreaderd | NEWNEWS uses the same date parser (RFC 3977 7.4.2) with `allownewnews yes`: valid dates, also a leap second (second 60), get 230 and invalid ones 501; with the default (off) the command is 500, as a control. **Fails** on main (invalid dates were rolled over to 230) |
| `TestNewgroupsGMTAnyCase` | dreaderd | with the server in UTC+14, `GMT`, `gmt` and `Gmt` all read the time as UTC, and no third argument reads it as local time. **Fails** on main for `gmt` and `Gmt` |
| `TestReaderAuthinfoCodes` | dreaderd | AUTHINFO (RFC 4643, D-46): PASS before USER is 482, a wrong password 481, AUTHINFO after a successful login 502. **Fails** on main before this change |
| `TestReaderConformanceAuthinfo` | dreaderd | nntp-testkit's conformance checks against a reader that requires AUTHINFO (user `fred`), with the user and password given, so the six RFC 4643 checks run: login, a wrong password (481), PASS before USER (482), GROUP before login (480), AUTHINFO after login (502), and AUTHINFO gone from CAPABILITIES after login. All checks must pass; `mode-reader` is skipped (dreaderd answers 480 before login). One check at a time: the test reader has one DNS/auth worker (`readerdns 1`) and a concurrent login gets "400 Server too busy". **Fails** on main before #67 (D-46) |
| `TestReaderLoginsQueueForDNSWorker` | dreaderd | D-51: with one DNS/auth worker (`readerdns 1`), 8 logins at the same time all get 281. **Fails** on main before this change: some got "400 Server too busy" |
| `TestReaderDNSQueueLimit` | dreaderd | D-51: 40 logins at once against `readerdns 1`: each gets 281 or 400 (queue limit 4 * readerdns + 4), none hangs, and a login works afterwards |
| `TestFeederReplyCodes` | diablo | a transit-only server (D-48): MODE READER is 502, IHAVE and CHECK without a message-id 501. **Fails** on main before this change |
| `TestFeederReadOnlyTakethis` | diablo | TAKETHIS on a read-only connection answers 480 and the article after it is discarded: no reply per article line, and body lines that look like commands are not run. **Fails** on main |
| `TestFeederModeCompressRepeated` | diablo | 600 MODE COMPRESS commands from one peer (each a fresh deflate stream) do not grow the session by more than 3 MB. **Fails** on main: every command leaked its decompression state |
| `TestReaderBindHostPort` | dreaderd | `dreaderd -P 127.0.0.1:port` listens there. **Fails** on main before the -P fix: the whole "host:port" was passed to getaddrinfo |
| `TestReaderCacheHitsRepaired` | dreaderd | a damaged scoreboard (`cache.hits` whose header claims 1 MiB in a 20 KiB file) is replaced when dreaderd opens it with `readercache on` and `cache=scoring(...)`, and the replacement is logged. **Fails** on main before this fix (D-14): the file stays damaged and the scoreboard stays off. 64-bit little-endian only |
| `TestReaderCacheHeaderRewrite` | dreaderd | with `readercache on`, a repeated HEAD/ARTICLE is served from the article cache with the vserver's cluster name in `Xref:` (in place of the spool's host) and in front of `Path:`; an `Xref:` with a host but no groups and an empty `Path:` are passed on unchanged and the other headers and the body are intact. **Fails** on main before this fix: those two lines were rewritten with the rest of the article appended |
| `TestHeaderFeedToReader` | diablo, dnewslink, dreaderd | diablo feeds dreaderd a header-only feed (`headfeed on`, `dnewslink -H`); the reader shows the article in GROUP and OVER and fetches BODY from diablo as its spool server |
| `TestKPDBConcurrentUpdate` | dkp | two writers update one record with values of a new length while the test holds the record's lock, so the second waits inside its first lookup and finds a replaced record: exactly one live record must remain, with both changes. **Fails** on main before #33 (D-11): two live records for the key, one without the first writer's change |
| `TestDexpireKeeptimeTwoSpools` | dexpire | D-76: two spools with different directory ages and a keep time of 3600 s: the 42-minute-old directory of spool 01 must stay. **Fails** on main before #109: it is removed |
| `TestKPDBTornTail` | dkp | D-80: a KP database whose tail is a half-written record or a NUL block: the next append starts on a new line and is found. **Fails** on main before #110 |
| `TestDnewslinkDeferredKeptOnExit` | dnewslink | D-66: entries the peer deferred with 431 are written back to the queue, one per line, when the session ends early. **Fails** on main before #111 |
| `TestDcancelSpoolOnlyUpdatesHistory` | dcancel | D-67: `dcancel -a` and `-s` cancel the first of three articles; the other two stay readable (`-s` now updates the history). **Fails** on main before #112 for `-s` |
| `TestDnewslinkOverlongQueueLine, TestDnewslinkFullDiskKeepsQueue` | dnewslink | D-72: an overlong queue line is skipped, the entries behind it are sent. D-74 (root only, Linux): a queue rewrite onto a full file system keeps the old queue. **Fail** on main before #113 |
| `TestDnewslinkQueueFieldsPerLine` | dnewslink | D-73: after a reconnect a plain queue line does not inherit `C50` from the line before, and a retried entry keeps its flags. **Fails** on main before #114 |
| `TestDnewslinkSmallMaxstream` | dnewslink | D-68: `-M 2` with six queued articles offers all six. **Fails** on main before #117: two |
| `TestDnewslinkBadCompressLevel` | dnewslink | D-69: `-Z12` (a level zlib rejects) with a peer that accepts MODE COMPRESS: dnewslink ends by itself. **Fails** on main before #118: it spins |
| `TestDexpireCtlIndent, TestDexpireCtlLongComment, TestDexpireCtlLongPattern, TestConfigLongLine` | dexpire, diablo | D-83: an indented `dexpire.ctl` rule is honoured. D-82: long lines in `diablo.config` and `dexpire.ctl` are not split. **Fail** on main before #119 and #125 |
| `TestConfigWhitespaceBeforeComment` | any program | a directive followed by so much white space that its comment starts after column 255 is read normally (D-159); a directive that is cut by the buffer is still "too long" |
| `TestReaderdefNumbersValidated` | dreaderd | a malformed or out-of-range number in a readerdef (`maxconnperhost 1x`, `idletimeout abc`, `maxconn 99999999999`, `bytelimit -5`) makes dreader.access invalid, so dreaderd refuses every client (it stays up) instead of running with 0 or a prefix (D-161) |
| `TestReaderdefNumbersRejectedReload`, `TestReaderdefNumberComment` | dreaderd | a dreader.access with a bad readerdef number is refused while dreaderd runs and the old cache keeps serving; an inline `# comment` after a number is a comment (D-161) |
| `TestAccessCacheWithStdinClosed` | dreaderd | dreaderd started with file descriptor 0 closed still loads its access cache (D-160; passes on main too: descriptor 0 does not reach the loader there) |
| `TestDserverHostsNumbers` | dreaderd | a flag number that overflows and a `cachemin`/`cachemax` that is not a number in dserver.hosts are logged and ignored (D-162) |
| `TestDicmdConfigInternalFilter` | dicmd, diablo | D-78: `dicmd config internalfilter B3` keeps the body filter working. **Fails** on main before #124: 8 of 8 identical bodies stored |
| `TestKPDBBlankLine` | dkp | D-79: a blank or short line in a KP database (also as the last line) does not corrupt the sort or crash the lookup. **Fails** on main before #123: SIGSEGV |
| `TestPathPatternPercent` | dpath | a configured path pattern (`path_log %s/lo%dg%%x`) expands with its first `%s` substituted, `%%` as `%`, and anything else literal; a pattern without `%s` stays as it is. **Fails** on main before this fix: the pattern was the snprintf format, so `%d` printed stack garbage (`lo32g%x`) |
| `TestDexpirecacheDefaultDir` | dexpirecache | without `-d`, with and without `-C`: works on the configured cache directory and reports nothing to do. **Fails** on main before this fix: chdir(NULL), exit 1. (The 2 TB overflow itself needs a file system that large.) |
| `TestDlockhistorySleepOption`, `TestDspaminfoClearNeedsCache` | dlockhistory, dspaminfo | `dlockhistory -f -s 1` keeps running; `dspaminfo -c 0` without `-B`/`-N` is refused. **Fail** on main before this fix: dlockhistory crashes (SIGSEGV), dspaminfo selects both caches |
| `TestDcancelDryRunDoesNotRewriteSpool`, `TestDcancelDryRunReaderDoesNotCancelOverview` | dcancel | `dcancel -a -n` skips the spool rewrite path, and `dcancel -n -r` skips the reader overview write; both report the planned cancellation and leave the article readable. **Fails** on main before this fix: `-n -r` cancelled the reader overview |
| `TestDclientOneLineReply` | dclient | `dclient DATE`, and GROUP and DATE on stdin, against a server with one-line replies: dclient returns with the replies. **Fails** on main before this fix: it waited for a `.` line and hung |
| `TestPlockExitStatus` | plock | plock returns the exit status of its command (0 and 3). **Fails** on main before this fix: always 0 |
| `TestDhisbenchNeedsScratchHistory` | dhisbench | adds without `-f` are refused and the site's dhistory is not created; with `-f` they go to the given file. **Fails** on main before this fix: the made-up entries went into dhistory |
| `TestMasscancelLastLine` | dgrpctl | `masscancel` with two invalid Message-IDs on stdin, the last without a newline: both are reported. **Fails** on main before D-17's loop fix: the last line was dropped |
| `TestConfigPathPercent` | dpath | a diablo.config in a directory named `c%dfg`, given with `-C file` and `-Cfile`, is read and its `path_home` used. **Fails** on main before D-50: the config path was the xfopen() format ("Unable to open diablo.config using pattern ...") |
| `TestCheckgroupsExec` | dgrpctl | `checkgroups exec` with a new group, a new moderated group, a changed description, an unchanged group and a group missing from the list: new groups get status `y`/`m`, a description and a creation time, the changed group its description, the missing group is deleted, and a second run changes nothing. **Fails** on main before D-36: new groups had only NB and NE (`flag=n`, no GD, CTS 0) |
| `TestCheckgroupsLongLine`, `TestCheckgroupsEmptyList` | dgrpctl | D-70: `checkgroups` reads a list line of any length as one line (a description is cut at 255 bytes, as `newgroup` does) and its tail is no phantom group; a list without a usable group ends normally and deletes nothing. **Fail** on main: a phantom group `e2e.phantom`; SIGSEGV on the empty list |
| `TestPgpverify` | pgpverify | with a throwaway keyring (testdata/pgp, public key only): a signed newgroup verifies and names its signer; a changed signed header, a duplicate signed header or X-PGP-Sig (any letter case), a NUL byte or a bare CR are refused (exit 3, nothing on stdout). Skipped without gpgv |
| `TestControlDuplicateHeadersRefused` | dreaderd | a control message with two Control: headers is not executed, and the refusal is logged; one with a single Control: is (dcontrol.ctl `newgroup:*:*:doit`) |
| `TestReaderAccessCachePrivate` | dreaderd | the caches compiled from dreader.access (`cache.authdef` holds the `authdef` passwords) are mode 0600 under umask 022, also when older caches and a stale `.new` were 0644; AUTHINFO with the cached user and password still works. **Fails** on main before GHSA-5cwh-wc3c-f5w5: the caches followed the umask (0644) |
| `TestGroupNameSlashRefused` | dgrpctl | a group name with `/` is refused (`Illegal newsgroup name`) and not created; a name with `+`, `-` and digits still is. **Fails** on main before GHSA-5cwh-wc3c-f5w5 |
| `TestDexpire` | diablo, dexpire | `dexpire -a` removes the spool directory of an article older than `keeptime`; STAT then gives 430, and offering the article again gets 435 (still in history) |
| `TestDexpireKeepsNewestDir` | diablo, dexpire | `dexpire -a` keeps a spool's newest directory even when it is older than `keeptime` (diablo may be writing into it): the article in it stays readable and the next one is stored. **Fails** on main before jrehmer/diablo-nntp#25 |
| `TestDexpireKeepsHistoryOfUnremovedDir` | diablo, dexpire | a spool directory that dexpire cannot enter (mode 0) is not removed, its history entries are not expired (STAT still 223), and dexpire says so and ends; before, the history of the directory was expired while its files were there, and dexpire scanned the spool again and again (skipped as root) (D-157) |
| `TestDoverctlConvertMovesCompressedData` | doverctl | `convert` moves the compressed (`.gz`) data files with the plain ones (D-150); before, they stayed in the old layout |
| `TestDoverctlConvertCopyFailure` | doverctl | with `-m` a data file that cannot be copied stops the group, the failure is reported and the group is not reported as converted (D-151) |
| `TestDoverctlCleanRemovesUnknownEntries` | doverctl | `clean` removes an entry that is neither a regular file nor a directory (a fifo) by its real path (D-152) |
| `TestDoverctlCleanMisalignedData` | doverctl | `clean` removes a data file whose first article is not a multiple of the entries per file and keeps an aligned one (D-153) |
| `TestDexpireoverHeaderOnlyOver` | dexpireover | an over file that is only a header (no article slots) is skipped with a message; it used to die with SIGFPE (modulo by zero) |
| `TestDexpireoverBadMaxArts` | dexpireover | an over file whose header promises 0 article slots, or more than the file holds, is skipped with a message (SIGFPE / read behind the mapping before) |
| `TestSpoolDirRemovedWhileWriting` | diablo | the spool directory a connection writes into is removed (as dexpire can do); the next article on the same connection is stored (235, then STAT 223) in a recreated directory. **Fails** on main before jrehmer/diablo-nntp#24 (235, then STAT 430: the article was lost) |
| `TestFeederChildSighupAfterQuit` | diablo | 400 rounds of QUIT, then SIGHUP to the feeder child (as `dicmd exit` and `dicmd pause` send) while it logs its session: no child may be left after 10 s. **Fails** on main before the D-40 fix: 85 to 169 of 1000 children hung in a futex wait inside the SIGHUP handler on glibc, 6 of 2000 on musl. Linux only (/proc); `E2E_SIGHUP_ROUNDS` sets the rounds |
| `TestDiabloCtlFeeder`, `TestDiabloCtlReader` | samples/adm/diablo-ctl | status (3 when stopped), start, a second start ("already running"), restart, stop (port closed), stop again; skipped without the script (`DIABLO_CTL`, default: `samples/adm/diablo-ctl` next to `DIABLO_BIN`) |
| `TestDiabloCtlOnlyItsOwnSite` | samples/adm/diablo-ctl, diablo | `diablo-ctl stop` acts on its own site only: another site of the same user, started first, keeps its diablo and its dnewslink worker (a stand-in in its dqueue); the site's own worker is stopped; diablo writes `log/diablo.pid` and removes it on exit. **Fails** on main before #31: diablo-ctl reported and killed the other site's diablo and dnewslink. The diablo-ctl tests skip an old diablo-ctl that still looks for "the oldest diablo of the user" |
| `TestDiabloCtlChecks` | samples/adm/diablo-ctl | every check before a start, with `check` and with `start`: a missing mount, `-F`, `DIABLO_PREFLIGHT=no`, the history note and `DIABLO_REQUIRE_HISTORY`, a missing and a stale active file, an active file that is off, an unclean shutdown with a fresh and an old log; the refusal names `-F` and `DIABLO_PREFLIGHT` |
| `TestDiabloCtlConfigErrors`, `TestDiabloCtlStopWithBadSettings` | samples/adm/diablo-ctl | an unknown `DIABLO_DAEMONS` entry and a non-numeric wait, age, open-file limit or status area are refused by `start` before anything starts (D-140, D-141) while `stop` and `status` warn, use the default and still work, also on a running site |
| `TestDiabloCtlOptionsNotGlobbed`, `TestDiabloCtlHomeConfBin`, `TestDiabloCtlActiveFileDetails` | samples/adm/diablo-ctl | a `*` in `DIABLO_OPTS` reaches the daemon as a `*` (D-146, with a stand-in diablo); an auto-loaded `diablo-ctl.conf` that names other binaries makes the paths come from those (D-139) and missing binaries are an error; `DIABLO_ACTIVE_MAXAGE=08` is 8, and `active` is found in any letter case |
| `TestDiabloCtlActiveFileReading`, `TestDiabloCtlNumbers`, `TestDiabloCtlNoKnownDaemon` | samples/adm/diablo-ctl | the `active` line is read as diablo reads it (a `#` comment, a CR, an absolute path), and a `dpath` that cannot name the active file fails `check`; a number of 10 digits or more is refused, `DIABLO_PS_PAD=00` leaves no bare `-s`, `restart` refuses a wrong setting like `start`; with no valid daemon in `DIABLO_DAEMONS`, `stop` and `status` look at both daemons |
| `TestDiabloCtlStopRemovesPid`, `TestDiabloCtlDkpTrim` | samples/adm/diablo-ctl, dreaderd, dkp | `stop` removes the pid file dreaderd leaves (so a pid file without a process means an unclean shutdown); `DIABLO_DKP_TRIM=yes` trims the active file before a start, when no daemon of the site runs |
| `TestInitWrappers`, `TestInitSysV`, `TestInitRcdFreeBSD` | samples/init | the files are well formed and call diablo-ctl; the SysV script starts, checks and stops diablo; the rc.d script runs with the real `rc.subr` (FreeBSD only, skipped elsewhere) |
| `TestCacheFilesPrivate`, `TestFeederLogsPrivate` | dreaderd, diablo | with umask 0: the reader's cache files (they copy passwords and secrets of `dreader.access`), its status file and the logs of reader and feeder are not readable by others |
| `TestCanLockVectors` | (Go only) | the Go Cancel-Lock helpers reproduce INN's test vectors (tests/lib/canlock-t.c) |
| `TestCancelLock`, `TestCancelLockSupersedes`, `TestCancelLockEarlyCancel`, `TestCancelLockTrackonlyRefused` | dreaderd | `readercanlock` (RFC 8315): cancels and supersedes against Cancel-Lock in each mode (require-auth, auth, none, all), matching, wrong and missing keys, sha1 in a folded lock with a comment, early cancels, and the trackonly refusal; skipped without readercanlock support |
| `TestCancelLockTrackonlyNone` | dreaderd | `readercanlock none` is allowed with `readerxover trackonly` (it refuses every cancel before it needs a header), where other modes but `all` stop dreaderd |
| `TestCancelLockWithoutLibcanlock` | dreaderd | in a build without libcanlock (skipped in the normal build) no Cancel-Key can be verified and the safe modes stay safe: the default (require-auth), require-auth and none cancel nothing, auth withdraws only an article without a Cancel-Lock, all executes everything; every mode is accepted |
| `TestCanLockGenerate`, `TestCanLockGenerateOff`, `TestCanLockGenerateOwnLock`, `TestCanLockGenerateFolded`, `TestCanLockGenerateServerID` | dreaderd | the Cancel-Lock added to a post (`canlockadmin`, `canlockuser`, sha256 and sha1, `canlocksha1 no`, `addcanlockuser`): per configuration, nothing without secrets, the poster's own lock stays first, a header folded below 76 columns with the most secrets, a lock made from the Message-ID the server gives |
| `TestCanLockGenerateKeys`, `TestCanLockGenerateRoundTrip`, `TestCanLockGenerateRefusesBadHeaders`, `TestCanLockGenerateFoldedTarget` | dreaderd | the Cancel-Key added to a poster's own cancel or supersede (a rotated secret still gets its key; a folded Message-ID too); generation and verification agree in require-auth mode; a poster's Cancel-Lock or Cancel-Key of 1023 bytes or more, with an element over 400 bytes, or a second header is refused with 441 (passed on without the server's locks before) |
| `TestDcanlock`, `TestDcanlockVerify`, `TestDcanlockErrors` | dcanlock | `key` and `lock` for admin and user secrets, the schemes, several Message-IDs, a secret from standard input (no configuration needed); `verify` agrees with the locks dcanlock and dreaderd make; what the tool refuses, with status 2 |
| `TestDcanlockCancelBody`, `TestDcanlockStdinSecretBadAccess` | dcanlock | the body of a cancel is one line that does not start with a dot; `-S` with an explicit `-A` that is missing or has no such vserverdef fails like it does without `-S` |
| `TestDcanlockWithdraw`, `TestDcanlockCancel`, `TestDcanlockCancelNewsgroups`, `TestDcanlockCancelDreadartPath`, `TestDcanlockCancelPost`, `TestDcanlockCancelWithdraw` | dcanlock, dreaderd | the key it prints withdraws an article posted through dreaderd; the whole cancel article with its options and CR LF line ends; Newsgroups read with the `dreadart` next to it (a list that does not fit is an error); its output is POSTed as it is and the cancel the reader sends out withdraws the article |
| `TestDnewslinkConnlife` | diablo, dspoolout, dnewslink | realtime feed with `connlife 8` (skipped without `dnewslink -K`): one TCP session across a queue rotation; after connlife, retirement at the next rotation and a replacement at once (`dspoolout -J`); no descriptor growth over three replacements; every article reaches the peer once, in order. Linux only (reads /proc) |
| `TestDnewslinkConnlifeNearest` | diablo, dspoolout, dnewslink | with `connlife 12`, a replacement worker that carries traffic retires at the rotation 10 s after its start (the one nearest 12 s), not at the next one about 20 s in. **Fails** on main before #30: the worker is still serving 12 s after its start (connlife 60 gave about 120 s). Linux only (reads /proc) |
| `TestInstallConfigFiles` | install-portable.sh | `make install` into a temporary prefix: config files are real 0640 copies of the samples, `dactive.kp` is empty, adm scripts in `<prefix>/adm` keep the execute bit; a reinstall keeps a changed config file and puts a changed adm script's new version next to it (`.new`); uninstall leaves the configuration. Skipped without `install-portable.sh` next to `DIABLO_BIN` |
| `TestInstallKeepsDirectoryModes`, `TestInstallNeedsBuiltPrograms`, `TestInstallStopsWhenFindFails` | install-portable.sh | a reinstall keeps the modes of the home, spool and queue directories and only closes log and run to others (D-142); in a tree where nothing is built the installer says so and installs nothing (D-143); a `find` that fails stops install and uninstall, no success message (D-144) |
| `TestInstallConvertsConfigSymlinks` | install-portable.sh | a config symlink into `samples/` left by an earlier install becomes a real file with the content in use, before `samples/` is replaced; with install and with bininstall |
| `TestAdmHourlyExpire` | adm/hourly.expire, dexpire | the script expires an article older than `keeptime` |
| `TestAdmDailyAtrim` | adm/daily.atrim, adm/rot | the script rotates the logs |
| `TestAdmDailyAtrimDaemonLogs` | adm/daily.atrim | the logs the daemons write themselves (general, incoming, drincoming, path, article) are rotated too |
| `TestAdmDailyAtrimJobLogs` | adm/daily.atrim, djoblog | the job logs (`<job>.log`) are rotated too, including daily.atrim's own, which djoblog opens again for the end of the run |
| `TestDjoblogLogsLines` | djoblog | stdout and stderr lines get a time stamp and the job name; BEGIN and END lines; a last line without newline; nothing printed; the log is `<path_log>/<job>.log` |
| `TestDjoblogExitStatus` | djoblog | the exit status is the command's (128+signal when killed, 127 when it cannot be executed) and the END line says so |
| `TestDjoblogUsage` | djoblog | a bad job name or no command exits 2 and writes nothing |
| `TestDjoblogLines` | djoblog | control characters become `?`, a 5000 character line is split, `-f file`, the command's own options |
| `TestDjoblogRotation` | djoblog | the log renamed while the job runs: the rest goes to a new file |
| `TestDjoblogSyslog` | djoblog | `-s` writes no file and keeps the exit status |
| `TestAdmBiweeklyAtrim` | adm/biweekly.atrim, dhisexpire | the script trims the history while diablo runs; the stored article stays in history |
| `TestAdmReaderScripts` | adm/daily.reader, adm/weekly.reader | both scripts run on a reader site. The adm tests are skipped while `samples/adm` still holds the csh scripts |
| `TestAdmExitStatus` | adm/hourly.expire, daily.reader, weekly.reader, daily.atrim, biweekly.atrim | a job that fails (a stand-in `dexpire`, `dexpireover`, `rot`, `dkp` or `dhisexpire` with a nonzero exit) makes its script exit with that status, and the END line says it (D-135, D-137); before, the END message hid it and the script exited 0 |
| `TestAdmRotArguments` | adm/rot | a count that is not digits (empty, `x`, `-1`, `1.5`, 10 digits) is refused with 2 before any file is moved (D-136); `08` is decimal; a move that fails (read-only directory) makes rot exit 1 |
| `TestAdmDumphist`, `TestAdmHistbackup`, `TestAdmActsync` | adm/dumphist, adm/histbackup, adm/actsync | `dumphist` keeps the newest history copy as `dhistory.snp` and the one before as `.old`; `histbackup` asks a running diablo for the snapshot and says so when `path_dumphist` is not set; `actsync` prints a usage message and does nothing on a host that is one of the active file servers |
| `TestAdmHistbackupDicmdFails` | adm/histbackup | histbackup fails (exit 1) when `dicmd` itself fails, so cron sees it |
| `TestModerationMailsModerator`, `TestModerationFirstModeratorOnly` | dreaderd | an unapproved POST to a moderated group is mailed to the moderator (moderators file, `%s` = group with dashes) with 240 and not passed on; the address is an argument to sendmail, without `-t`; a crosspost is mailed only to the first moderated group's moderator (#45; before, every moderator got a copy). Mail content is checked only with `ci/fake-sendmail` installed (`E2E_FAKE_SENDMAIL=1` for `ci/build-on-host`, as in CI) |
| `TestModerationApproveRequired`, `TestModerationApprovedGoesIn` | dreaderd | readerdef `approve` (#42): a POST with Approved: from a reader without `approve yes` gets 441, to a moderated and to an unmoderated group, and nothing is mailed or passed on; with `approve yes` it goes straight out. **Fails** on main before #42: any reader's Approved: post went out |
| `TestModerationGroupStatus` | dreaderd | group status on POST (#43): a group with status "n" or "x", alone or crossposted, gets `441 Posting to <group> is not allowed (status <c>)`, with or without `checkpostgroups`; the open group still posts. **Fails** on main before #43: the "n" post went out |
| `TestModerationNoModerator441`, `TestModerationMailFailureReported`, `TestModerationAddressFormat` | dreaderd | #45: no moderators line gives `441 No moderator known for <group>, article not posted`; a failing sendmail gives `441 Unable to mail article to the moderator of <group>`; neither is passed on (before: passed on, or 240). In the moderators file `%s` and `%%` are substituted and any other `%` makes the entry unusable (before: snprintf with the entry as the format) |
| `TestModerationAliasStatus` | dreaderd, dgrpctl | #50: a group whose status is an INN alias (`=comp.unix.mx`) posts normally, neither refused as n/x nor mailed as moderated (before: refused) |
| `TestFeederUnapprovedModerated` | diablo | with `active on`, `activedrop yes` and `xrefhost`, an unapproved article to a moderated group gets 437 with the reason `Unapproved/<group>` in the reply and in incoming.log (#46; before, "NotInActive" in incoming.log only); an article to an open group is accepted |
| `TestSoutOverlongLine` | diablo | D-114: an article with a 2.5 KB Newsgroups and a 1.8 KB Path line (the SOUT line on the control pipe is longer than the page-sized buffer) is queued for the outgoing label, and so is a short one after it. **Fails** on main before this fix: only the short article is queued |
| `TestCachedLFSeparator` | dreaderd | D-113: HEAD and BODY of an article in the reader cache whose header/body separator is a bare LF (the cache file is rewritten with LF line ends) have the same lines as for the CRLF original. **Fails** on main: HEAD printed the whole article, BODY nothing |
| `TestDcancelFailedRewriteKeepsHistory` | diablo, dcancel | D-108: a spool rewrite that stops at a damaged third article leaves the history on the old file: the second article is still read with its own text. **Fails** on main before this fix: ARTICLE returned the cancelled first article |
| `TestDsyncgroupsBlankNewsgroupLine`, `TestDsyncgroupsShortActiveLine`, `TestDsyncgroupsPeerClosesAtQuit` | dsyncgroups | D-109: a blank LIST NEWSGROUPS line with `-w`, and a blank or short LIST ACTIVE line (with and without `-w`), are skipped, and a peer that closes instead of answering QUIT gives an error with an empty reply and exit 1. **Fail** on main before this fix: SIGSEGV, or the earlier reply printed from an unset pointer |
| `TestReaderListWildcardLinear` | dreaderd | `LIST ACTIVE` with a 45-character `*?` pattern against a 56-character group name is answered at once and a second client is served meanwhile; ordinary `*` and `?` patterns still match as before. **Fails** on main: the reader stalls |
| `TestReaderAuthRejectKeepsMaster` | dreaderd | eight logins of a file-backend user whose entry has a reject string (`600 1 500 ...`) get 500 and the dreaderd master keeps running. **Fails** on main: the master dies on some of the attempts |
| `TestFeedLFOnlyBodyControl` | dreaderd | an article fed with LF-only line ends and a `Control:` line in its body is accepted (235) and the feeder keeps serving; before, the feeder process died on it |
| `TestPostBareLFHeaders`, `TestPostBareLFAccepted` | dreaderd | a POST header refused with CR LF line ends (`X-Trace`, `NNTP-Posting-Host`, `Control` with `controlpost no`) is refused with 441 when the lines end in LF alone; a post with LF-only line ends in headers and body is still accepted and passed on with its headers on separate lines and the body unchanged. `TestPostBareLFHeaders` **fails** on main before this fix: the LF-only header lines were not checked |
| `TestReaderLongLineClosed`, `TestReaderLongLineAtLimit` | dreaderd | a client line is limited to 1 MiB, newline included: 4 MiB sent without a line end close the connection (and the reader still serves others); a command line of exactly 1 MiB gets a 5xx reply and the connection stays usable, one byte more closes it. **Fails** on main before this fix: the data was buffered without limit |

## Running

You need a Diablo build and Go 1.26 or later. A user and group called `news`
must exist: `diablo` looks them up at startup and exits without them.

```sh
make                      # with RADIUS_ENABLED off in lib/vendor.h on Linux
cd tests/e2e
DIABLO_BIN=$PWD/../../dbin go test ./...
```

Without `DIABLO_BIN` every test is skipped. `NNTP_TEST_DEADLINE` (default
`60s`) bounds every blocking step, so a hung server fails the test instead of
hanging it.

Prefer a test machine. Each test starts its own daemons in a temporary
directory on free ports, and the tests only signal processes of their own
sites. But they run as `news`, like a live site, and a bug there can reach the
live daemons: before jrehmer/diablo-nntp#31, `TestDiabloCtl*` (through
diablo-ctl) stopped the live diablo, dreaderd and dnewslink of the same user.

Run the tests as an ordinary user or as `news`, not as root. As an ordinary
user, `diablo`'s switch to `news` fails and it keeps running as that user,
which owns the test sites.

The tests need a system where 127.0.0.2 can be used as a source address.
Linux routes all of 127.0.0.0/8 to the loopback interface. Elsewhere add the
address first:

- FreeBSD and macOS: `ifconfig lo0 alias 127.0.0.2/32`
- illumos (SmartOS, OmniOS): `ipadm create-addr -T static -a 127.0.0.2/32 lo0/e2e`

`TestDnewslinkConnlife` reads `/proc` and is skipped off Linux.

`ci/build-on-host` installs the build packages (apt, dnf, zypper, pacman, apk,
emerge, or `pkg` on FreeBSD and illumos/OmniOS), creates the `news` user, turns
RADIUS off and builds; run it as root from the top of a throwaway copy of the
source tree. On FreeBSD and illumos it installs gmake (and gcc on illumos) and
builds with gmake; libcanlock and Berkeley DB are used only where a package and
its headers are present, off otherwise. The test binary can be built elsewhere
with `CGO_ENABLED=0 go test -c` and copied to a machine without Go.
`.github/workflows/ci-matrix.yml` does the same across a broad matrix on
GitHub's runners (Ubuntu x64 and arm64, a libcanlock-less build, eight distro
containers, FreeBSD and illumos).

### One distro container, locally

`ci/run-container` runs the suite in a single Linux distro container with the
same commands the `containers` jobs of `ci-matrix.yml` use — the workflow calls
this very script, so the invocation does not drift from CI (the image contents,
distro packages and host kernel can still differ from a given CI run). It needs
Docker and rsync both installed on an amd64 Linux host (rsync is a prerequisite,
as on the CI runner; not a live news server). Build the
pure-Go test binary first, from the same revision, then pass the image, a clean
tree and that binary:

```sh
(cd tests/e2e && CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go test -c -o /tmp/e2e.test.linux ./...)
tests/e2e/ci/run-container alpine:3.24 . /tmp/e2e.test.linux
```

It mounts a clean copy of the tree (never your `.git`), builds as root with
`build-on-host`, runs the tests as `news` with `--init` and the loopback peer
host entries, and removes its own container and root-owned copy on exit or
interrupt. Pass an `image@sha256:...` digest to pin the exact base a CI run
logged; a floating tag on your machine need not match a fresh CI pull.

## Writing a test

```go
func TestSomething(t *testing.T) {
	s := NewSite(t, SiteOptions{})      // diablo with DefaultNewsfeeds: 127.0.0.2 may feed
	p := s.Peer(t)                      // dial from 127.0.0.2, read the greeting
	id := msgID(t)
	expect(t, p, 335, "IHAVE %s", id)
	send(t, p, wire(newArticle(id, "e2e.test", "subject", "body")))
	status(t, p, 235, "IHAVE article")
	s.Dicmd(t, "status")                // dicmd with the site's DIABLO_CONFIG_PATH
}
```

`SiteOptions` adds or replaces `diablo.config` directives, replaces
`dnewsfeeds` or `dspool.ctl`, writes further files, and runs a `Setup`
function before `diablo` starts (e.g. `PrimeHostCache`).
