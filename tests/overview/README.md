# Offline SQLite overview benchmark

`ovshadow` reads disposable snapshots to measure a shared overview layout. It
keeps each article's numbered memberships, its overview fields, and an
optional compressed header record. A numbered read walks the membership
index and joins to the narrow article table. A lookup by Message-ID uses the
exact bytes and returns the newest matching number in the group.

This is a stand-alone test tool. It needs a C99 compiler, POSIX monotonic
clock support, SQLite 3 development headers and library, and Python 3.10 or
newer with SQLite and zlib support. The build and reader configuration for
Diablo are independent of this tool.

## Build and check

```sh
make -C tests/overview
make -C tests/overview check
```

Set `CPPFLAGS` and `LDFLAGS` when SQLite headers and libraries are outside the
compiler's normal search path. The check covers holes, crossposts, repeated
Message-IDs in one group, 64-bit article numbers, byte preservation, and
snapshot integrity after reads.

## Load a fixture

Use a scratch directory and a fresh output name. The loader builds a sealed
SQLite file using the rollback journal. After the transaction finishes and
the database closes, it publishes the file without replacing an existing
snapshot. `ovshadow` opens that offline snapshot read-only.

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
the whole snapshot for the selected group, including numbers older than
Diablo's bounded by-Message-ID scan.

`bench` repeats a numbered query on one SQLite connection and reports
SQLite's version, the requested sample count, rows and bytes per pass, and
elapsed p50 and p95 milliseconds. The timing includes stepping the query
and building article-number prefixes. It excludes database opening, the
input loader, NNTP formatting beyond the stored rows, and socket output.
Expect later passes to benefit from cached pages. Compare byte counts or
hash captured `range` output to check that two trials served equal data.
