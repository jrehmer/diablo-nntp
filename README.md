# Diablo 6 — Portable Make Build Guide

Diablo 6 supports building with GNU Make on Linux, FreeBSD, and illumos-based operating systems, as well as native BSD Make on FreeBSD. This guide also records the build-system conversion and the subsequent cross-platform source changes so that the repository and pull request retain a consolidated technical history.

This build system replaces the requirement for Matt Dillon's `xmake` while preserving the original XMakefiles for reference.

**Current tested build identifier:** `6-CUR-20261024-BWH` (`VERS=6`, `SUBREV=CUR-20261024-BWH`). This denotes the selected build version, not confirmation of a published Git release or tag.

## Supported Platforms

| Operating System | Make Implementation | Compiler | Build Command | Status |
|---|---|---|---|---|
| FreeBSD | BSD Make | Clang | `make -j4` | Validated |
| FreeBSD | GNU Make | Clang (or GCC) | `gmake -j4` | Clang validated; GCC optional |
| SmartOS (illumos) | GNU Make | GCC | `gmake -j4` | Validated |
| OmniOS (illumos) | GNU Make | GCC | `gmake -j4` | Validated |
| Linux | GNU Make | GCC | `make -j4` (also tested with `-j16`) | Validated; 0 warnings |

Traditional Solaris Make is not supported. On illumos systems, use GNU Make (`gmake`) rather than the system's native `make` when it is Solaris Make.

## Build Requirements

A working C compiler and the appropriate Make implementation are required.

**FreeBSD requires neither GCC nor GNU Make (`gmake`) for a standard build.** Its base system provides Clang and native BSD Make, which automatically reads `BSDmakefile`. Administrators may optionally install GCC and/or GNU Make if they prefer those tools; GNU Make reads the separate top-level `Makefile`.

Core dependencies include:

- OpenSSL development headers and libraries (`libcrypto`).
- zlib development headers and libraries.
- Standard system development headers and libraries.

The other libraries are optional. `./configure` (below) looks for each of them and turns on the options that this host can build.

The availability and installation commands for these dependencies vary by operating system and package repository.

### Linux Build Packages

On RPM-based Linux distributions that provide these package names, install the following development packages before building:

```sh
sudo dnf install zlib-devel libdb-devel openssl-devel libcanlock-devel
```

On Debian and Ubuntu (tested on Ubuntu 26.04):

```sh
sudo apt install gcc make libssl-dev zlib1g-dev libdb5.3-dev libcanlock-dev
```

On RPM-based systems, add `gcc` and `make` to the `dnf` line if they aren't installed.

These supply the zlib, Berkeley DB, Cancel-Lock and OpenSSL headers and libraries of the tested Linux configuration. Other Linux distributions may use different package names. The optional libraries are listed under "Optional Features" below.

**The news user:** create the `news` user and group, home `/news`, before `make install`; see `INSTALL`.

### SmartOS Build Packages

On SmartOS (pkgsrc), install the following before building:

```sh
pkgin -y install gcc13 gmake db4 openssl libradius libcanlock
```

`db4` (Berkeley DB), `libradius` (RADIUS) and `libcanlock` (Cancel-Lock) are optional: `./configure` turns on the features whose libraries are installed. `cdb`, `libldap` and `p5-*` are not in the list; install the OpenLDAP package (`openldap-client`) for LDAP authentication.

This list was tested on a fresh `base-64-lts` 25.4.0 zone: `gmake -j8` built all 39 programs with no warnings.

After `gmake install`, two SmartOS details:

- **Cron and the news user.** SmartOS creates new accounts locked (`*LK*` in `/etc/shadow`), and cron refuses their jobs (`'news' not allowed to execute cronjobs`), so `dspoolout` and the `adm` scripts never run. Mark the account as having no password instead; it still can't log in:

  ```sh
  passwd -N news
  ```

- **Feeder and reader in one zone.** `diablo` and `dreaderd` both listen on port 119 by default. Move the feeder to another port and point the reader's post server at it:

  ```
  # diablo.config
  feederbindport  120
  # dserver.hosts
  localhost       s1o1p120
  ```

  and start diablo with `-P 120` (`DIABLO_OPTS` in `diablo-ctl.conf`).

### Optional Features and `./configure`

Run `./configure` first, then `make` (`gmake` on FreeBSD with GNU Make and on illumos). `./configure` probes the host for each optional library by compiling and linking a small test program, then

- sets the matching option in `lib/vendor.h` to `#define` where the library works and to `#undef` where it does not, and
- writes `config.mk` with the compiler and linker flags that the makefiles need (package prefixes such as `/usr/local` and `/opt/local`, the libraries, Perl's embedding flags). `Makefile` and `BSDmakefile` include it; it is not committed.

All of these options are `#undef` in `lib/vendor.h` as shipped, so a build without `./configure` is the minimal one. Run `./configure` again after installing or removing a library, and `make clean` before the next `make`. `lib/vendor.h` is changed by it; keep your own edits of the other options, which it does not touch.

| Option in `lib/vendor.h` | `./configure` name | Needs | Used for |
|---|---|---|---|
| `USE_ZLIB` | `zlib` | zlib (`zlib.h`, `-lz`) | compressed spool objects and feeds (zlib itself is needed to build) |
| `CANLOCK_ENABLED` | `canlock` | libcanlock 3.3.0 or later (`libcanlock-3/canlock.h`) | Cancel-Lock checks and generation (RFC 8315) |
| `DB_ENABLED`, `REJECT_DB` | `db` | Berkeley DB with the 1.85 interface, `dbopen()`: `db_185.h` and `-ldb` (`db4`, `db5`) on Linux and illumos, the C library on FreeBSD | DB authentication and the reject database |
| `CDB_ENABLED` | `cdb` | a cdb library with `cdb_seek()`: tinycdb (`libcdb-dev` on Debian and Ubuntu); the cdb of FreeBSD ports and pkgsrc no longer has it | CDB authentication |
| `PAM_ENABLED` | `pam` | `security/pam_appl.h`, `-lpam` | PAM authentication |
| `LDAP_ENABLED` | `ldap` | OpenLDAP (`ldap.h`, `libldap`, `liblber`) | LDAP authentication |
| `PERL_ENABLED` | `perl` | an embeddable perl (`perl.h`, `libperl`; Debian `libperl-dev`) | Perl authentication (an authdef `perl <script>`) |
| `RADIUS_ENABLED`, `RADIUS_RADCLI` | `radius` | the BSD `radlib.h` with `libradius` (FreeBSD base, `libradius` on SmartOS), or radcli (`radcli/radcli.h`; Debian and Ubuntu `libradcli-dev`, Fedora `radcli-devel`, openSUSE `radcli radcli-devel`, Alpine `radcli-dev`, Arch `radcli`) | RADIUS authentication |

`./configure --without-NAME` leaves an option off without looking, `./configure --with-NAME` stops with an error if the host cannot build it, `./configure --with-radcli` uses radcli even where libradius is found, and `./configure -n` only reports. For example `./configure --without-perl --without-ldap`.

On Linux, zlib and OpenSSL are the only packages `make` needs; the optional ones you install are picked up. A typical Debian or Ubuntu set is `libdb-dev libcanlock-dev libradcli-dev libpam0g-dev libldap-dev libperl-dev libcdb-dev`.

**Perl authentication against an htpasswd file.** `contrib/htpasswd-auth.pl` is a ready-made `perl` script for an authdef (`PERL_ENABLED`): it checks the user and password against an Apache `htpasswd` file, which it reads again on every login attempt, so a new user, a changed password or a removed one counts at once. It checks `$apr1$` (Apache MD5), `{SHA}` and `{SSHA}` itself and gives everything else to the C library's `crypt(3)`, so the schemes of the platform work: DES, `$1$`, `$5$`, `$6$`, bcrypt (`$2y$`, `htpasswd -B`) and yescrypt where the library has them. Copy it to `/news` and use `authdef webusers` / `perl /news/htpasswd-auth.pl -f /news/htpasswd` in `dreader.access`: the words after the script are its arguments (`-f FILE`, and `--plaintext` to accept plain text entries; double quotes keep a path with spaces together), and an authdef with another `-f` file has its own interpreter. The header of the script has an example. A change of the script itself needs a restart of dreaderd. DES crypt looks at the first 8 characters of the password only.

**Cancel-Lock.** Without libcanlock no Cancel-Key can be verified: the default `readercanlock require-auth` then refuses every cancel and supersede (dreaderd logs a warning at start), `readercanlock all` executes them unchecked, and the Cancel-Lock tests skip.

**RADIUS.** On FreeBSD and SmartOS it uses the BSD `radlib.h` library; on Linux it uses radcli. The `radius <file>` of an authdef is a BSD `radius.conf` (`auth host[:port] secret [timeout [tries]]`) with either library; with radcli the file can also be a radcli `radiusclient.conf` (a line `authserver ...`), and a line `dictionary <path>` names the radcli dictionary if it is not where `./configure` found it. A secret with `:` or `,` is not supported with radcli.

## Building Diablo

### FreeBSD — Native BSD Make

```sh
./configure
make -j4
```

The `BSDmakefile` is selected automatically by FreeBSD's native Make implementation. **No GCC or `gmake` installation is necessary:** FreeBSD's base-system Clang and BSD Make are sufficient.

Install libcanlock first (`pkg install libcanlock`) to get Cancel-Lock; the package puts its header and library under `/usr/local`, which `./configure` finds.

### FreeBSD — GNU Make

```sh
./configure
gmake -j4
```

GNU Make uses the top-level `Makefile`. This is an optional alternative to the native BSD Make build; GCC is also optional. To build with GNU Make and GCC explicitly, use:

```sh
gmake CC=gcc -j4
```

Install `gmake` and GCC separately if you choose this combination; they are not required for the default FreeBSD build.

### SmartOS and OmniOS

```sh
./configure
gmake -j4
```

GNU Make detects illumos through the `SunOS` operating-system identifier and applies the appropriate platform-specific configuration.

### Linux

```sh
./configure
make -j4
```

Linux support has been validated with GCC and GNU Make, including a successful `make -j16` build linking all 39 executables without warnings, with Berkeley DB-enabled build flags.

## Build Version Configuration

Both build systems read the shared `build-version.mk` file, replacing the previously hard-coded compiler definitions `-DVERS=\"6\"` and `-DSUBREV=\"CUR-20170901-00\"`.

For the current build, configure the file as:

```makefile
VERS ?= 6
SUBREV ?= CUR-20261024-BWH
```

These values produce the version identifier **`6-CUR-20261024-BWH`**. The same definitions are used by GNU `Makefile` and FreeBSD `BSDmakefile`.

To override the version without editing the file:

```sh
# GNU Make (SmartOS, OmniOS, Linux, or optional FreeBSD GNU Make)
gmake VERS=6 SUBREV=CUR-20261024-BWH -j4

# Native FreeBSD BSD Make
make VERS=6 SUBREV=CUR-20261024-BWH -j4
```

On Linux, substitute `make` for `gmake` where appropriate. Editing `build-version.mk` causes object files to rebuild because both Makefiles declare it as a prerequisite. **When changing `VERS` or `SUBREV` only via command-line overrides, perform a clean build first**, since a variable-value change alone does not trigger recompilation:

```sh
gmake clean
gmake VERS=6 SUBREV=CUR-20261024-BWH -j4
```

The shared configuration and selected version have been reported to compile cleanly. Version tagging and release publication are separate steps.

## Build Output

Compilation generates files in two directories:

| Directory | Contents |
|---|---|
| `obj/` | Object files, static libraries, generated prototype headers, dependency files, and detected build configuration |
| `dbin/` | Compiled Diablo executables |

Generated files are not intended to be committed to Git.

## Berkeley DB Detection

`./configure` finds the Berkeley DB 1.85 interface (`dbopen()`) the way the build needs it: on FreeBSD `<db.h>` in the C library, elsewhere `db_185.h` and a library (`-ldb`, `-ldb4`, `-ldb5`) in the system directories or under `/opt/local`, `/opt/ooce` and `/usr/local`, checked by compiling and linking a test program. It writes the include and library flags to `config.mk`.

| Distribution | Header Directory | Library |
|---|---|---|
| Debian, Ubuntu, Fedora | `/usr/include` | `-ldb` |
| SmartOS / pkgsrc | `/opt/local/include/db4` | `-ldb4` |
| OmniOS / OOCE | `/opt/ooce/include` | `-ldb` |
| FreeBSD | base system | C library |

This replaces `compat/detect-db185.sh` and `obj/db185-config.mk`, which no longer exist. If the installed Berkeley DB version, compiler, or library paths change, run `./configure` again and then `make clean`.

## illumos Compatibility

The build includes compatibility code for interfaces required by Diablo or its third-party dependencies.

### RADIUS Header Compatibility

`compat/smartos_radlib.h` supplies BSD compatibility definitions required by some pkgsrc RADIUS headers.

The compatibility header is included conditionally on illumos.

### srandomdev Compatibility

`compat/srandomdev.c` implements the BSD `srandomdev()` interface required by certain static RADIUS libraries.

The implementation seeds `random()` using `arc4random()`.

The illumos GNU Make reader link currently includes this compatibility object for the platform; the compatibility code supports RADIUS library expectations.

### Berkeley DB Compatibility

Diablo uses the Berkeley DB 1.85 API, including `dbopen()`.

On illumos, `dreaderd/dns.c` includes `db_185.h` as required for either DB authentication or the rejection database to access the compatibility API provided by supported Berkeley DB libraries.

### Platform-Specific Paths

`./configure` discovers the Berkeley DB, RADIUS and other optional libraries under the usual system and package directories.

Nonstandard installations may require additional configuration.

## Incremental and Parallel Builds

Both GNU Make and native FreeBSD Make support parallel compilation.

Examples:

```sh
gmake -j4
```

```sh
make -j4
```

Compiler-generated dependency files are used to track header dependencies.

Generated prototype headers and static libraries are also managed through Make dependencies.

A successful incremental build should not recompile or relink unchanged targets.

To inspect the commands Make would execute:

```sh
gmake -n
```

Or on FreeBSD with native Make:

```sh
make -n
```

If the build is up to date, no compilation or linking commands should appear.

**Archive timestamps:** Source archives should preserve valid Unix timestamps. Source files with modification times in the future can cause repeated recompilation.

Do not share the same `obj/` and `dbin/` directories between different operating systems or compiler configurations.

## Cleaning the Build

Remove generated build outputs:

```sh
gmake clean
```

Or with FreeBSD's native Make:

```sh
make clean
```

The `distclean` target is also available.

Cleaning removes generated objects, libraries, executables and dependency files. `distclean` also removes `config.mk`, which `./configure` writes; `lib/vendor.h` keeps the options it set.

It does not remove source files or the original XMakefiles.

## Installation

The portable build system preserves Diablo's original installation layout.

The default installation prefix is:

`/news`

Both GNU Make and native FreeBSD Make use the shared installation script:

`install-portable.sh`

### Default Installation Layout

| Component | Default Path |
|---|---|
| Executables | `/news/dbin/` |
| Sample configurations (read-only reference) | `/news/samples/` |
| Configuration files (`diablo.config`, `dnewsfeeds`, `dreader.access`, ...) | `/news/` |
| Active file (`dactive.kp`, created empty) | `/news/` |
| Cron and maintenance scripts | `/news/adm/` |
| Article spool | `/news/spool/` |
| Queue | `/news/dqueue/` |
| Runtime files | `/news/run/` |
| Logs | `/news/log/` |
| Manual pages | `/news/man/man5/` and `/news/man/man8/` |

### Full Installation

```sh
gmake install
```

On FreeBSD with native Make:

```sh
make install
```

A live installation requires appropriate permissions and the configured `news` user and group.

`install` copies the configuration files from `samples/` into `/news` as real files, mode 0640 (owner and group `news`, as `dreader.access` can hold secrets), but only those that don't exist yet: an existing configuration file is never overwritten, and one readable by everyone gets a note recommending `chmod 640`. `dactive.kp` is created empty (`samples/dactive.kp` is only an example). The scripts from `samples/adm` go to `/news/adm` with their execute bit; a locally changed script is kept and the new version is installed next to it as `<name>.new`.

Earlier installs symlinked the configuration files to `samples/`, which every install replaces. `install` and `bininstall` turn such symlinks into real files with the content in use, before `samples/` is updated, so a reinstall can't change the configuration.

### Binary Installation

```sh
gmake bininstall
```

This installs the executables, samples, scripts and manual pages without creating configuration files, making it suitable for updating an existing installation.

### Custom Installation Prefix

Override the default `/news` installation directory:

```sh
gmake PREFIX=/opt/diablo install
```

`PREFIX` only changes where the files are installed. The programs still look for their configuration in `/news` (compiled in), so start them with `-C /opt/diablo/diablo.config` or set `DIABLO_CONFIG_PATH`, and set `path_home` in that diablo.config.

### Staged Installation

Use `DESTDIR` to stage an installation without modifying the live filesystem:

```sh
gmake PREFIX=/news DESTDIR=/tmp/diablo-stage install
```

The resulting installation is located under:

`/tmp/diablo-stage/news/`

Staged installations intentionally skip ownership changes.

### Ownership

For live installations, the installer applies the configured owner and group, defaulting to `news:news`.

For staged installations using `DESTDIR`, ownership changes are skipped to support packaging and testing without requiring root privileges.

### Uninstallation

```sh
gmake uninstall
```

For a staged installation:

```sh
gmake PREFIX=/news DESTDIR=/tmp/diablo-stage uninstall
```

The uninstall process is designed to preserve operational data, including article spools, history, queues, logs, and locally modified configuration files.

Review the installation prefix and destination carefully before running installation or uninstallation commands on a production server.

## Validation Status

The portable build system has been tested on:

- FreeBSD with native BSD Make.
- SmartOS with GNU Make and GCC.
- OmniOS with GNU Make and GCC.
- Linux with GNU Make and GCC (including `-j16`, 39 executables).

Successful compilation, incremental builds, and installation functionality have been confirmed during development.

The final observed compiler-warning counts were **FreeBSD/Clang: 0; SmartOS/GCC: 0; Linux/GCC: 0**. Linux builds linked against Berkeley DB after the `__db185_open` dependency was corrected. The empty-host-cache regression test was confirmed at runtime. The `dicmd config` error-reporting fix also passed SmartOS runtime testing for both an unsupported and a supported option. Additional runtime testing passed for inbound peer authorization, incoming NNTP article transfer, history/spool creation, and `dexpire` expiration behavior. Outgoing feed configuration, cron-launched `dnewslink`, and receipt of the outgoing feed by the destination host were also tested successfully. The general-log ownership and recursive-error fix was also confirmed by SmartOS runtime testing. The shared `build-version.mk` configuration has also been reported to compile successfully using version `6-CUR-20261024-BWH`.

Compilation success does not guarantee complete runtime compatibility. These specific feed, storage, and expiration paths have been tested successfully, while other workflows and production-scale behavior still require validation.

## Original Build System

The original Matt Dillon `xmake` configuration files remain in the source tree for reference.

The portable Makefiles are maintained separately:

- `Makefile` — GNU Make.
- `BSDmakefile` — Native FreeBSD Make.

Changes affecting source files, optional dependencies, or executable targets should be reflected in both build systems where applicable.

## Detailed Change History (Build Conversion and 22 Follow-up Patches)

This section documents the full effort for commit / PR reviewers. The `XMakefile` conversion was implemented first and committed separately; the subsequent **13 compiler-warning passes and nine focused follow-up patches** address source portability, configuration, Linux builds, and a host-cache correctness bug. The description is grouped by behavior rather than by patch chronology. The nine follow-ups comprise the illumos compatibility cleanup, GNU Make parser fix, Linux Berkeley DB link fix, Linux warning pass, host-cache fix, sample configuration version fix, `dicmd config` error-reporting fix, the general-log ownership/recursive-error fix, and the shared build-version configuration.

### A. Replace `xmake` and the `XMakefile` build workflow

The original build relied on Matt Dillon's `xmake` and multiple `XMakefile` configurations. The conversion retains original `XMakefile` files as reference material while making standard Make implementations sufficient to build and install Diablo.

- Add a top-level **GNU `Makefile`** for Linux, FreeBSD (`gmake`), and illumos (`gmake`). Detect the host OS with `uname -s` and select appropriate compiler flags and link dependencies.
- Add a separate **native FreeBSD `BSDmakefile`**. Keep source lists and build targets synchronized without assuming GNU Make syntax is accepted by BSD Make (or vice versa).
- Build the same primary daemons, feeders, support utilities, static libraries and generated headers. Linux verification confirmed **39 executable outputs**, including `diablo`, `dreaderd` and `dnewslink`.
- Preserve `obj/` for objects, archives, dependencies, generated prototypes and configuration; preserve `dbin/` for executable outputs.
- Generate existing prototype headers (`obj/lib-protos.h`, `obj/filter-protos.h`, `obj/dreaderd-protos.h`) from source `Prototype` annotations; wire them as prerequisites to the relevant compilation rules.
- Generate compiler dependency `.d` files via `-MMD -MP` and include them in subsequent builds, so changing headers rebuilds affected objects; unchanged trees remain incremental.
- Define explicit static-library, executable and directory prerequisites for correct parallel execution. GNU Make uses order-only prerequisites for build directories; both implementations were exercised with parallel builds.
- Add `compat/detect-db185.sh` to discover and compile/link-test the Berkeley DB 1.85 compatibility API on illumos; generate `obj/db185-config.mk` containing the platform-specific flags. Handle SmartOS/pkgsrc and OmniOS/OOCE layouts without hard-coding one install path.
- Add illumos compatibility code (including `compat/srandomdev.c` and RADIUS-header adaptation), and appropriate illumos networking/linker flags for `libsocket`, `libnsl`, and related system libraries.
- Retain optional vendor features from `lib/vendor.h` rather than forcing one universal feature set. The GNU Make detector recognizes when `RADIUS_ENABLED`, `DB_ENABLED`, or `REJECT_DB` is enabled in `lib/vendor.h`; the latter two require the DB 1.85-compatible API.
- Replace `xmake` installation commands with a shared POSIX shell installer, `install-portable.sh`, invoked by both Make variants. Maintain the `/news` default prefix, samples, man pages and executables; configuration defaults are real files, with legacy sample symlinks migrated on install.
- Support `install`, `bininstall`, `uninstall`, `clean`, `distclean`, and `help` as documented above, along with `PREFIX`, `DESTDIR`, and owner/group options. Staged installation skips ownership changes. The uninstaller protects spools, history, queues, logs, operational directories and modified samples.
- Document prerequisites, automatic detection, build commands, incremental/parallel operation, installation, packaging and validation in this guide.

**Build-system limits:** Traditional Solaris Make is not a target; use GNU Make on illumos. The vendor-option detector matches active-looking literal `#define` lines but does **not** implement a full C preprocessor. The generated `obj/` artifacts must not be shared between toolchains or platforms. Installation modifies filesystem contents; review `PREFIX` and `DESTDIR` before running it on a live server.

### B. GNU Make portability and optional library integration

After the initial Make conversion, Linux testing exposed independent build-system issues:

- Move inline AWK-based feature detection out of `$(shell ...)` arguments into `compat/detect-vendor-option.awk` to avoid unbalanced-parenthesis parsing failures on some GNU Make versions (including attempts to run `make clean`). Support `#define` and `# define` forms without interpreting commented directives as enabled options.
- Detect when `REJECT_DB` is enabled in `lib/vendor.h` separately from when `DB_ENABLED` is enabled in `lib/vendor.h`. Either option can introduce `dbopen()` references in `dreaderd`; a configuration enabling only the rejection database still requires a Berkeley DB compatibility library.
- Conditionally add `-ldb` on Linux when a DB-dependent feature is enabled; include the DB compatibility header when required. This resolves the observed `__db185_open` undefined references without silently turning the feature off.
- Retain the illumos compile/link detection mechanism, expanded to cover both DB-dependent feature combinations.

### C. Modern illumos / Solaris configuration

`lib/config.h` retains the active Solaris (`__SVR4`) settings required on illumos but removes unnecessary assumptions:

- Recognize `__sun` as well as historical `sun` to select the SunOS/Solaris configuration reliably.
- Set `HAVE_STRSEP=1` for illumos libc, excluding Diablo's fallback implementation when that branch is selected.
- Remove obsolete Solaris-only `FDPASS_USES_CMSG` / `FDPASS_USES_ACC` selectors. The active reader code already uses `cmsghdr`, `CMSG_DATA()` and `SCM_RIGHTS`.
- Keep SysV shared memory and precommit/spam cache choices, `statvfs` compatibility, `/dev/zero`-based mapping, signal compatibility settings, and other established behavior unchanged.
- Leave sendmail and PGP executable locations to administrators building each platform's package; these paths were explicitly **out of scope**.

### D. Compiler compatibility and legacy C cleanup

Across **13 incremental compiler-warning patches**, resolve modern GCC and Clang diagnostics while minimizing changes to Diablo's established interfaces:

- Modernize K&R-style definitions and correct prototypes, declarations, signedness, integer types, socket argument types and header selection/order.
- Use appropriate socket address length types such as `socklen_t` and correct network-address storage to avoid overwrites or inconsistent address-family handling.
- Remove genuinely unused and unreachable declarations and inactive utility code, while preserving exposed command compatibility where practical.
- Correct mistaken pointer-size calculations and array bounds; repair MD5 context clearing so it addresses the complete context rather than a pointer-sized quantity.
- Replace deprecated OpenSSL SHA-256 context APIs with their supported EVP digest equivalents where used.
- Preserve output and status formatting while allocating enough intermediate space for constructed strings.
- Fix feeder configuration propagation so the configured maximum streaming value is assigned to the correct `NewslinkInfo` field.

**Measured warning reduction:** FreeBSD/Clang **73 → 0**, SmartOS/GCC **109 → 0**. Linux/GCC reached **0** after the subsequent Linux-specific pass; a comparable pre-cleanup Linux baseline was not established. These figures count compiler diagnostics in the observed full build logs and are not a claim of formal static-analysis completeness.

### E. Memory, string, path and format-string safety

The source cleanup also corrects unsafe operations that could affect actual runtime behavior:

- Replace unbounded `sprintf()` / `vsprintf()` use where necessary, check formatting results and destination lengths, and fix a format-string vulnerability in `drequeue`.
- Reject unsafe or unrepresentable spool, group, compressed-output and temporary paths rather than silently truncating them.
- Correct overview processing to respect explicit byte lengths instead of assuming data buffers are NUL-terminated; improve allocation and error handling for compressed overview reads.
- Size `dnewslink`'s temporary batch filename buffer to accommodate an added `.tmp` suffix.
- Check numeric peer address lengths before populating small connection-tracking fields; avoid overflow and unintended identity collisions.
- Improve null-pointer checks and cleanup paths exposed by compiler warnings and targeted boundary tests.

### F. NNTP reader, DNS, authentication and IPC

- Validate address-conversion success before using network addresses in reader connection accounting.
- Validate received descriptor-passing ancillary data, including control-message type, length, and truncation, before interpreting `SCM_RIGHTS` data.
- Correct reader zlib output-capacity handling and an NNTP `XZVER` availability condition.
- Prevent unchecked truncation of DNS names used as reader identities; use a complete numeric address as fallback when a hostname exceeds an IPC response field.
- Handle an authenticated username and appended default realm only when the result fits the fixed identity buffers; reject an unrepresentable identity instead of silently truncating it.
- Preserve the full **63-character** ignored `AUTHINFO` username in the existing fixed-size IPC structure. Use a dedicated `DF_IGNORED_AUTH_LONG` flag for the exceptional representation, prevent long ignored names from aliasing normal names in per-user connection accounting, and preserve the ignored marker in logs and `X-Trace`.
- Prevent the exceptional ignored-authentication representation from receiving a posting-address exemption reserved for authenticated identities.

The structure sizes used for DNS request/response IPC were intentionally not enlarged, and changes were designed to preserve normal-length identity behavior. Boundary testing included 61-, 62-, and 63-character ignored usernames and checks for unterminated input.

### G. Feeder, compression, spool, overview and history

- Correct zlib input/output pointer types, available-output checks and compressed-stream metadata construction.
- Fix unbounded feeder logging or filename construction; preserve the existing article-transfer and storage formats.
- Correct `NewslinkInfo` streaming configuration assignment.
- Improve path bounds and error handling in spool, overview and compressed-file operations.
- Replace predictable `tmpnam()` plus `open()` temporary-file creation in overview rewriting with atomic `mkstemp()` creation; ensure opened descriptors are closed along affected error and cleanup paths.
- On Linux, replace deprecated `sigblock()` / `sigsetmask()` around article history/precommit updates with `sigprocmask()`, saving and restoring the previous signal mask. Keep non-Linux code paths unchanged in that patch.

### H. Host authorization cache correctness (`dhosts.cache`)

Fix a pre-existing bug in `lib/hostauth.c` when the final incoming host/feed entry is deleted from `/news/dnewsfeeds`:

1. The rebuilt host cache correctly becomes empty.
2. The old code treats an empty `.new` file as invalid whenever a prior `dhosts.cache` exists.
3. The stale cache retains a deleted feed label, producing `502 DIABLO misconfiguration, label missing in dnewsfeeds` instead of the correct `502 Transfer permission denied` response.

The fix permits a **valid zero-byte cache** to replace the old file, publishes it via rename, and checks failed write/close/rename operations so I/O failures do not silently publish an invalid cache. The reproduced regression was resolved and subsequently confirmed during runtime testing.

**Behavioral consequence:** Removing the last `inhost` entry now invalidates the final cached authorization; new unauthorized connections are denied correctly without manually deleting `/news/dhosts.cache`.

### I. Legacy `dhisctl -a` command

The undocumented bulk-history scanning implementation behind `dhisctl -a` used an invalid descriptor and unsafe/incomplete history traversal. Instead of retaining a potentially dangerous path solely to eliminate warnings, `-a` now fails clearly with:

```text
dhisctl: -a bulk history scan is unsupported
```

Other supported `dhisctl` operations are intended to remain unchanged. This is an **intentional behavior change** and should be called out in commit and release notes.

### J. Fix missing version directives in sample configurations

The feeder-only and reader-only sample files, `samples/feeder/diablo.config` and `samples/reader/diablo.config`, omitted the required configuration `version` directive. Without it, settings such as `path_dumphist` could be skipped, even when the corresponding executable script existed and had executable permissions. This surfaced during testing when `dicmd dumphist` returned `400 No dumphist program configured`.

- Add `version 2.0` near the beginning of **both** sample files, consistent with `samples/diablo.config`.
- Preserve all other sample settings and configuration paths.
- This corrects startup configuration examples; it does not add runtime configurability to any `path_` option.

### K. Correct misleading `dicmd config` success responses

The runtime `config` command previously checked only the return value of `SetCommand()` when deciding whether to print a success message. For an unsupported runtime option, `SetCommand()` could return `0` while setting the `cmdErr` output flag, resulting in a misleading `Set ...` confirmation without applying the setting.

- Update the command handler in `util/diablo.c` to report success **only if** `SetCommand()` returns `0` **and** `cmdErr == 0`.
- Leave the underlying `SetCommand()` implementation and supported runtime options unchanged.
- `path_` options, including `path_dumphist`, remain **startup-only** options read from `diablo.config`; they cannot be set through `dicmd config`.

**Native SmartOS runtime regression tests passed:**

```text
$ dicmd config path_dumphist /news/adm/dumphist2
Invalid config command: path_dumphist /news/adm/dumphist2
Usage: config command option

$ dicmd config maxconnect 100
Set 'maxconnect' to '100'
```

These results demonstrate that the unsupported path setting is correctly rejected while an existing supported runtime setting still succeeds. The `maxconnect` example **changes the daemon's live setting**, so use an appropriate value when reproducing the test.

### L. Fix general-log ownership and recursive open-error handling

Enabling `path_generallog %s/general.log` exposed an interaction between daemon startup privileges and per-connection privilege dropping on SmartOS. During startup, the root-running master opened or created `/news/log/general.log`, leaving a newly created file owned by `root:root` with permissions such as `0644`. Incoming connection handlers ran as the configured `news` account and could not reopen that file for append. Unlike other log files already owned by `news`, general logging therefore failed for connection children.

A SmartOS `truss` trace confirmed that the child repeatedly attempted to open `/news/log/general.log`, received `EACCES`, and eventually terminated with `SIGSEGV`. The immediate crash was caused by the `openLog()` error handler calling `logit()` when opening the general log failed; `logit()` tried to open the same general log again, recursively.

The combined fix changes **`lib/log.c` and `util/diablo.c`**:

- Add `ChownGeneralLog(uid, gid)` and call it **before** the feeder master drops privileges to its configured runtime user and group.
- For an already-open, root-owned **regular** general-log file, use `fstat()` and `fchown()` on its existing file descriptor to assign the runtime UID/GID. Using the open descriptor avoids following a substituted pathname during log handling.
- Leave non-regular files, logs owned by another account, disabled general logging, and syslog-only configurations untouched. Report `fstat()` or `fchown()` failures through syslog.
- On failure to open a log file, report the error directly through `syslog()` rather than calling `logit()`. Retain the existing syslog fallback and avoid recursive reopening and stack exhaustion.

**SmartOS runtime regression confirmed:** The user verified that the combined patch resolves both issues. Before patching, changing `general.log` ownership manually to `news:news` restored incoming NNTP service, independently confirming the permissions cause. With the combined patch applied, the ownership and recursive logging failures were reported resolved.

**Operational note:** If an existing general log is owned by `root`, the patched master can correct it during startup before privilege dropping. Log rotation, custom account configuration, and intentionally inaccessible log paths should still be tested under site-specific operating conditions.

### L. Shared build version configuration

- Add `build-version.mk` to define `VERS` and `SUBREV` once for GNU Make and native BSD Make.
- Replace the hard-coded `6-CUR-20170901-00` compiler definitions in both `Makefile` and `BSDmakefile` with references to these shared variables.
- Allow `VERS` and `SUBREV` to be overridden from the Make command line without editing the Makefiles.
- Make object targets depend on `build-version.mk` so editing the shared defaults rebuilds objects carrying version information. A command-line override alone still requires a clean build when changing version identifiers.
- Select `VERS=6` and `SUBREV=CUR-20261024-BWH`, yielding **`6-CUR-20261024-BWH`**; the new configuration compiles cleanly in user testing.

## Validation and Regression-Test Status

| Environment | Build system | Compiler | Final warnings | Evidence |
|---|---|---|---:|---|
| FreeBSD | Native BSD Make | Clang | **0** | Full native clean build and executable linking |
| FreeBSD | GNU Make | Clang | Not counted separately | Portable-build validation earlier in development |
| SmartOS / illumos | GNU Make | GCC | **0** | Full native build, including illumos configuration cleanup |
| OmniOS / illumos | GNU Make | GCC | Not counted separately | Earlier portable-build validation |
| Linux | GNU Make | GCC | **0** | Full native `make -j16`, 39 executable targets, Berkeley DB linked |

Additional checks performed during development:

- Repeated incremental builds, parallel compilation and installation/staging tests on the portable Make implementations.
- Successive application and whitespace verification for the compiler cleanup patch series.
- Targeted GCC and Clang builds for changed source files.
- AddressSanitizer / UndefinedBehaviorSanitizer boundary checks for ignored-username storage and flag behavior.
- Host-cache tests covering nonempty-to-nonempty label changes and removal of the last authorized incoming host; user-confirmed successful runtime fix.
- Defined an inbound peer and verified that host authorization permitted the expected connection (**passed**).
- Accepted articles over an inbound NNTP stream from a remote server (**passed**).
- Verified that the history file and article spool storage were generated as expected for received articles (**passed**).
- Ran `dexpire` and verified that the expected spool and history objects were removed (**passed**).
- Successfully configured an outgoing NNTP feed (**passed**).
- Verified that `dnewslink` started through cron as configured (**passed**).
- Confirmed that the destination host received the outgoing feed (**passed**).
- Both enabled and disabled optional-feature configurations tested in GNU Make detection logic; Linux's actual Berkeley DB-enabled link succeeded.
- Confirmed the sample feeder/reader configuration omission and corrected both files to declare `version 2.0`.
- On SmartOS, verified `dicmd config` rejects unsupported `path_dumphist` and still accepts supported `maxconnect` (**both passed**).
- On SmartOS, confirmed `general.log` created as `root:root` could not be reopened by the `news` connection child; `truss` showed repeated `EACCES` failures followed by `SIGSEGV`. Manual `chown news:news` restored connections; the combined code patch was subsequently confirmed to resolve both issues (**passed**).

**Not yet proven by these tests:** Full reader authentication and posting workflows, all expiration configurations, and history-database behavior under production workloads. The successful inbound NNTP transfer, outgoing feed delivery using cron-started `dnewslink`, history/spool creation, and `dexpire` tests establish basic functionality for those paths, not exhaustive production equivalence.

## Compatibility Notes and Intentional Behavior Changes

The modernization is intended to preserve the NNTP wire protocol, on-disk article/spool and history record formats, the directory layout, and fixed-size DNS IPC structs. The following changes do intentionally alter handling of bad or edge-case inputs:

- Overlong constructed paths, names and identities now fail or take a safe fallback rather than silently truncating or overflowing.
- Malformed ancillary socket messages are rejected.
- Ignored full-length AUTHINFO users are accounted for without collisions, with a distinguishable audit/log representation.
- An empty host cache is published so removed feed authorizations do not persist.
- Temporary overview files use atomic, unique creation.
- `dhisctl -a` reports unsupported instead of executing its nonfunctional scanner.
- Linux signal masks are saved and restored rather than using deprecated bitmask operations.
- Feeder and reader sample configurations now declare `version 2.0`, ensuring version-gated configuration directives are handled as intended.
- `dicmd config` no longer falsely confirms unsupported runtime settings; `path_` settings remain startup-only.
- A root-created regular general log is reassigned to the configured runtime account before privilege dropping, and failure to open a log now falls back to syslog without recursive calls or a child crash.

No protocol/storage format migration is intended or required by these changes. For an existing installation, back up configuration and state and test against representative feed/reader workloads before deployment.

## Maintenance Guidance and Future Work

- Keep `Makefile` and `BSDmakefile` source lists, targets and installer options synchronized.
- Update optional-feature detection if complex `vendor.h` conditional expressions become necessary; the AWK detector does not evaluate nested `#if` expressions.
- Re-test when Berkeley DB installations, OpenSSL versions, compiler toolchains or platform library layouts change. On illumos, clear build output to trigger `db185-config.mk` redetection.
- Add repeatable CI builds for FreeBSD, Linux and illumos if suitable runners are available.
- Evaluate a future `MAP_ANON` allocator transition, signal-mask modernization on non-Linux platforms, and integer-type modernization separately, with runtime comparison rather than bundling them into portability maintenance.
- Expand runtime coverage beyond the successful inbound peer authorization, inbound article transfer, outgoing feed configuration and delivery via cron-started `dnewslink`, history/spool creation, `dexpire`, and general-log tests: extended outgoing-feed scenarios (including retries and recovery), NNTP posting and reader authentication, article retrieval, queues, precommit caching, compressed overview, alternative expiration scenarios, log rotation and log-open failures under different permissions, and restart/reload behavior.

This document serves both as a practical build guide and as an expanded implementation/review record for the portable Make conversion and subsequent source fixes.

