#!/usr/bin/env python3
"""Check and benchmark the offline SQLite overview writer."""

import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile

from shadow_test import SCHEMA, overview


WRITER_SCHEMA = Path(__file__).with_name("writer_schema.sql")


def initialize(destination):
    with tempfile.TemporaryDirectory(prefix=".ovwriter-", dir=destination.parent) as tmp:
        database = Path(tmp) / "writer.sqlite"
        with closing(sqlite3.connect(database)) as connection:
            connection.executescript(SCHEMA.read_text() + WRITER_SCHEMA.read_text())
        os.link(database, destination)


def run_writer(writer, database, batch, source, check=True):
    return subprocess.run(
        [writer, database, str(batch), source], check=check, capture_output=True
    )


def membership(group, number, content):
    return group + b"\t" + str(number).encode() + b"\t" + content


def read_database(path):
    return sqlite3.connect(path.as_uri() + "?mode=ro&immutable=1", uri=True)


def state(database):
    with closing(read_database(database)) as connection:
        groups = connection.execute(
            """SELECT g.name, h.number FROM newsgroup AS g
               LEFT JOIN group_high AS h ON h.group_id=g.id ORDER BY g.name"""
        ).fetchall()
        memberships = connection.execute(
            """SELECT g.name, m.number, a.message_id FROM membership AS m
               JOIN newsgroup AS g ON g.id=m.group_id
               JOIN article AS a ON a.id=m.article_id ORDER BY g.name, m.number"""
        ).fetchall()
        articles = connection.execute(
            "SELECT message_id FROM article ORDER BY message_id"
        ).fetchall()
    return groups, memberships, articles


def check(writer, reader):
    alpha, beta, gamma = b"fixture.alpha", b"fixture.beta", b"fixture.gamma"
    one, two, three, four, conflict = (
        f"<{i}@fixture.invalid>".encode() for i in range(1, 6)
    )
    one_over = overview(b"crosspost", one, xref=b"fixture.alpha:7 fixture.beta:15")
    two_over = overview(b"new high", two, xref=b"fixture.alpha:20")
    three_over = overview(b"late lower number", three, xref=b"fixture.alpha:3")
    four_over = overview(b"next committed crosspost", four,
                         xref=b"fixture.alpha:30 fixture.beta:31")
    conflict_over = overview(b"conflicting number", conflict)

    with tempfile.TemporaryDirectory(prefix="ovwriter-test-") as tmp:
        directory = Path(tmp)
        database = directory / "writer.sqlite"
        source = directory / "rows.tsv"
        initialize(database)
        source.write_bytes(
            membership(alpha, 7, one_over) + membership(beta, 15, one_over) +
            membership(alpha, 20, two_over) + membership(alpha, 3, three_over)
        )
        run_writer(writer, database, 2, source)
        first = (
            [(alpha, 20), (beta, 15)],
            [(alpha, 3, three), (alpha, 7, one), (alpha, 20, two), (beta, 15, one)],
            [(one,), (two,), (three,)],
        )
        assert state(database) == first
        run_writer(writer, database, 2, source)
        assert state(database) == first

        source.write_bytes(
            membership(alpha, 30, four_over) + membership(beta, 31, four_over) +
            membership(gamma, 99, conflict_over) + membership(alpha, 20, conflict_over)
        )
        refused = run_writer(writer, database, 1, source, check=False)
        assert refused.returncode != 0 and b"conflicting article number" in refused.stderr
        committed = (
            [(alpha, 30), (beta, 31)],
            [(alpha, 3, three), (alpha, 7, one), (alpha, 20, two), (alpha, 30, four),
             (beta, 15, one), (beta, 31, four)],
            [(one,), (two,), (three,), (four,)],
        )
        assert state(database) == committed

        source.write_bytes(
            membership(alpha, 40, overview(b"earlier article in the same batch", conflict)) +
            membership(gamma, 100, overview(b"changed overview", one))
        )
        refused = run_writer(writer, database, 2, source, check=False)
        assert refused.returncode != 0 and b"conflicting overview" in refused.stderr
        assert state(database) == committed

        staged = membership(alpha, 40, overview(b"full batch before invalid input", conflict))
        malformed = (
            b"fixture.alpha\t41\ttoo few fields\r\n",
            b"fixture.alpha\t41\tSubject\tFrom\tDate\t<invalid>\t\r\n",
            b"fixture.alpha\t41\tSubject\tFrom\tDate\t<invalid>\t\t1024\r\n",
            b"fixture.alpha\t41\tSubject\tFrom\tDate\t<invalid>\t\t\r\n",
            membership(alpha, 0, four_over),
            membership(alpha, -1, four_over),
        )
        for bad_row in malformed:
            source.write_bytes(staged + bad_row)
            refused = run_writer(writer, database, 1, source, check=False)
            assert refused.returncode != 0 and b"invalid overview membership" in refused.stderr
            assert state(database) == committed

        output = subprocess.check_output([reader, database, "range", alpha, "1", "30"])
        assert output == (b"3\t" + three_over + b"7\t" + one_over +
                          b"20\t" + two_over + b"30\t" + four_over)
        assert not (directory / "writer.sqlite-wal").exists()
        assert not (directory / "writer.sqlite-shm").exists()

        source.write_bytes(membership(alpha, 40, one_over))
        run_writer(writer, database, 1, source)
        groups, memberships, articles = state(database)
        assert groups == [(alpha, 40), (beta, 31)]
        assert (alpha, 7, one) in memberships and (alpha, 40, one) in memberships
        assert articles == committed[2]
    print("overview writer checks passed")


def collect_stats(output):
    commits = []
    peak_wal = 0
    summary = None
    for line in output.splitlines():
        fields = line.split()
        if fields[0] == b"commit":
            commits.append(float(fields[1]))
            peak_wal = max(peak_wal, int(fields[2]))
        else:
            assert fields[0] == b"summary"
            summary = fields[1:]
    assert summary is not None and commits
    articles, memberships, elapsed, checkpoint = summary
    commits.sort()
    return {
        "article_runs": int(articles),
        "memberships_seen": int(memberships),
        "commits": len(commits),
        "ingest_ms": float(elapsed),
        "checkpoint_ms": float(checkpoint),
        "commit_p95_ms": commits[(len(commits) * 95 + 99) // 100 - 1],
        "peak_wal_bytes": peak_wal,
    }


def benchmark(writer, source, directory, batches, trials):
    assert source.stat().st_size > 0, "benchmark fixture is empty"
    print(json.dumps({"source_bytes": source.stat().st_size, "setup_sqlite": sqlite3.sqlite_version,
                      "batches": batches, "trials": trials}), flush=True)
    for trial in range(1, trials + 1):
        rotation = (trial - 1) % len(batches)
        for batch in batches[rotation:] + batches[:rotation]:
            with tempfile.TemporaryDirectory(prefix="ovwriter-bench-", dir=directory) as tmp:
                database = Path(tmp) / "writer.sqlite"
                initialize(database)
                result = run_writer(writer, database, batch, source)
                stats = collect_stats(result.stdout)
                with closing(read_database(database)) as connection:
                    stats["articles_stored"] = connection.execute(
                        "SELECT count(*) FROM article"
                    ).fetchone()[0]
                    stats["memberships_stored"] = connection.execute(
                        "SELECT count(*) FROM membership"
                    ).fetchone()[0]
                    stats["group_high_rows"] = connection.execute(
                        "SELECT count(*) FROM group_high"
                    ).fetchone()[0]
                print(json.dumps({"trial": trial, "batch": batch,
                                  "snapshot_bytes": database.stat().st_size,
                                  **stats}), flush=True)


def positive(text):
    number = int(text)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    checks = commands.add_parser("check")
    checks.add_argument("writer", type=Path)
    checks.add_argument("reader", type=Path)
    setup = commands.add_parser("init", help="create a disposable writer database")
    setup.add_argument("database", type=Path)
    bench = commands.add_parser("bench")
    bench.add_argument("writer", type=Path)
    bench.add_argument("rows", type=Path)
    bench.add_argument("directory", type=Path)
    bench.add_argument("--batches", nargs="+", type=positive, default=[16, 128, 1024])
    bench.add_argument("--trials", type=positive, default=3)
    args = parser.parse_args()
    if args.command == "check":
        check(args.writer.resolve(), args.reader.resolve())
    elif args.command == "init":
        initialize(args.database)
    else:
        benchmark(args.writer.resolve(), args.rows, args.directory.resolve(),
                  args.batches, args.trials)


if __name__ == "__main__":
    main()
