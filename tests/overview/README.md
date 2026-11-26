# Offline SQLite overview benchmark

`ovshadow` reads disposable snapshots to measure a shared overview layout. It
keeps each article's numbered memberships, its overview fields, and an
optional compressed header record. A numbered read walks the membership
index and joins to the narrow article table. A lookup by Message-ID uses the
exact bytes and returns the newest matching number in the group.

This is a stand-alone test tool. It needs a C99 compiler, POSIX monotonic
clock support, SQLite development headers and library, and Python 3 with
SQLite and zlib support. SQLite 3.22 and Python 3.7 are minimum versions
inferred from the APIs used. The test runs used newer versions. The build
and reader configuration for Diablo are independent of this tool.

## Build and check

```sh
make -C tests/overview
make -C tests/overview check
```

Set `CPPFLAGS` and `LDFLAGS` when SQLite headers and libraries are outside the
compiler's normal search path. For example, on FreeBSD with the `sqlite3` and
`py312-sqlite3` packages for Python 3.12:

```sh
make -C tests/overview CPPFLAGS=-I/usr/local/include LDFLAGS=-L/usr/local/lib check
```

The check covers holes, crossposts, repeated Message-IDs in one group,
64-bit article numbers, byte preservation, and snapshot integrity after reads.

## Load a fixture

Use a scratch directory and a fresh output name. The loader builds a sealed
SQLite file using the rollback journal. After the transaction finishes and
the database closes, it publishes the file without replacing an existing
snapshot. `ovshadow` opens that offline snapshot read-only.

Close the database before taking a snapshot. For a WAL database, checkpoint
all writes before closing. The reader uses SQLite's `immutable=1` option,
which skips locking and change detection. A read leaves the sealed file
unchanged and creates no WAL or shared-memory sidecars. A copy taken during
a write can contain incomplete or stale data. The reader cannot detect
such copies or recover from a hot journal. [SQLite URI filenames](https://sqlite.org/uri.html)

```sh
cd tests/overview
scratch=$(mktemp -d)
python3 shadow_test.py generate "$scratch/fixture" 250000
python3 shadow_test.py load "$scratch/fixture/rows.tsv" "$scratch/overview.sqlite" \
    --tails "$scratch/fixture/tails.tsv"
```

To use captured data, give `load` a binary TSV file with one record per
membership. Prefix each NNTP `OVER` row with the group name and a tab.
Include the original article number and line ending. Omit the status line
and the terminating dot. Capture from one consistent reader view. If two
memberships have the same Message-ID and different overview bytes, the
loader reports the inconsistency before publishing the snapshot. That
fixture cannot be represented by one shared record per Message-ID.
The built-in generator produces synthetic rows. The stock-daemon round-trip
results came from a separate capture harness outside this tool.

The optional `--tails` input takes one Message-ID, one tab, and a Base64
encoding of the corresponding header bytes per line. Import compresses
each decoded record with zlib. Range and Message-ID queries read only the
overview bytes. Evaluating arbitrary HDR/XPAT and the live writer requires
a separate implementation.

## Read and measure

```sh
./ovshadow "$scratch/overview.sqlite" range fixture.g000 1 5
./ovshadow "$scratch/overview.sqlite" lookup fixture.g000 '<0@fixture.invalid>'
./ovshadow "$scratch/overview.sqlite" bench fixture.g000 1 5000 50
```

`range` prints the stored rows in order with their article numbers. Gaps
produce no output. `lookup` prints zero or one row for a Message-ID within
that group. Both commands succeed on empty results. The SQL lookup searches
the whole snapshot for the named group and prints the stored article number.
In Diablo, an `OVER <Message-ID>` request scans the newest 100,000 slots at
most in the selected group (`FindMsgInGroup()`). It checks the hash and
compares the full Message-ID, then formats the number as zero for a by-ID
reply. The two paths therefore differ in search coverage and response framing.

`bench` repeats a numbered query on one SQLite connection and reports
SQLite's version, the requested sample count, rows and bytes per pass, and
elapsed p50 and p95 milliseconds. The timing includes stepping the query
and building article-number prefixes. It excludes database opening, the
input loader, NNTP formatting beyond the stored rows, and socket output.
Expect later passes to benefit from cached pages. Compare byte counts or
hash captured `range` output to check that two trials served equal data.
The reported percentiles use the lower rank of each sorted sample position.
