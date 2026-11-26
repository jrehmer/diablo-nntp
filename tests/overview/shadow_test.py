#!/usr/bin/env python3
"""Load and check offline SQLite overview snapshots."""

import argparse
import base64
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import random
import sqlite3
import subprocess
import tempfile
import zlib


SCHEMA = Path(__file__).with_name("schema.sql")
MAX_NUMBER = (1 << 63) - 1


def load_rows(database, source):
    groups = {}
    with source.open("rb") as rows:
        for line_number, line in enumerate(rows, 1):
            try:
                group, number_text, overview = line.split(b"\t", 2)
                if not group or b"\0" in group or not line.endswith(b"\n"):
                    raise ValueError("missing group name or line ending")
                number = int(number_text)
                if not 1 <= number <= MAX_NUMBER or str(number).encode() != number_text:
                    raise ValueError("invalid article number")
                fields = overview.split(b"\t", 6)
                if len(fields) < 7 or not fields[3] or b"\0" in fields[3]:
                    raise ValueError("missing overview fields or invalid Message-ID")
                message_id = fields[3]
            except ValueError as error:
                raise ValueError(f"overview line {line_number}: {error}") from error

            if group not in groups:
                groups[group] = database.execute(
                    "INSERT INTO newsgroup (name) VALUES (?)", (group,)
                ).lastrowid
            article = database.execute(
                "SELECT id, overview FROM article WHERE message_id = ?", (message_id,)
            ).fetchone()
            if article is None:
                article_id = database.execute(
                    "INSERT INTO article (message_id, overview) VALUES (?, ?)",
                    (message_id, overview),
                ).lastrowid
            else:
                article_id, previous = article
                if overview != previous:
                    raise ValueError(
                        f"overview line {line_number}: conflicting payload for {message_id!r}"
                    )
            try:
                database.execute(
                    "INSERT INTO membership VALUES (?, ?, ?)",
                    (groups[group], number, article_id),
                )
            except sqlite3.IntegrityError as error:
                raise ValueError(f"overview line {line_number}: {error}") from error


def load_tails(database, source):
    with source.open("rb") as tails:
        for line_number, line in enumerate(tails, 1):
            try:
                message_id, encoded = line.rstrip(b"\r\n").split(b"\t", 1)
                compressed = zlib.compress(base64.b64decode(encoded, validate=True), 1)
                insert = database.execute(
                    """INSERT INTO header_tail
                       SELECT id, ? FROM article WHERE message_id = ?""",
                    (compressed, message_id),
                )
                if insert.rowcount != 1:
                    raise ValueError("Message-ID is absent from the overview")
            except (ValueError, sqlite3.IntegrityError) as error:
                raise ValueError(f"header tail line {line_number}: {error}") from error


def load_snapshot(source, destination, tails=None):
    # Publish only the sealed file. A failed load leaves an existing snapshot alone.
    with tempfile.TemporaryDirectory(prefix=".ovshadow-", dir=destination.parent) as tmp:
        building = Path(tmp) / "snapshot.sqlite"
        with closing(sqlite3.connect(building)) as database:
            database.execute("PRAGMA journal_mode = DELETE")
            database.executescript(SCHEMA.read_text())
            with database:
                load_rows(database, source)
                if tails is not None:
                    load_tails(database, tails)
        os.link(building, destination)


def overview(subject, message_id, references=b"", xref=b"fixture.test:1"):
    return b"\t".join([
        subject, b"Fixture User <test@example.invalid>", b"Thu, 01 Jan 1970 00:00:00 +0000",
        message_id, references, b"1024", b"12", b"Xref: fixture.invalid " + xref,
    ]) + b"\r\n"


def generate(directory, article_count):
    directory.mkdir()
    rng = random.Random(23173)
    sequences = [0] * 256
    with (directory / "rows.tsv").open("wb") as rows, (directory / "tails.tsv").open("wb") as tails:
        for i in range(article_count):
            message_id = f"<{i}@fixture.invalid>".encode()
            subject = f"Archive sample {i // 11}: message {i % 11}".encode()
            reference = f"<{i - 1}@fixture.invalid>".encode() if i and i % 11 else b""
            primary = 0 if rng.random() < 0.40 else rng.randrange(1, len(sequences))
            destinations = [primary]
            if rng.random() < 0.20:
                extra = rng.randrange(1, len(sequences))
                if extra != primary:
                    destinations.append(extra)
            memberships = []
            for group in destinations:
                sequences[group] += 2 if i % 19 == 0 and sequences[group] else 1
                memberships.append((f"fixture.g{group:03}", sequences[group]))
            xref = " ".join(f"{group}:{number}" for group, number in memberships).encode()
            wire = overview(subject, message_id, reference, xref)
            for group, number in memberships:
                rows.write(f"{group}\t{number}\t".encode() + wire)
            token = b"".join(hashlib.sha512(f"{i}-{part}".encode()).hexdigest().encode()
                             for part in range(1 + i % 6))
            tail = b"Path: fixture.invalid!not-for-mail\r\nX-Trace: " + token + b"\r\n"
            tails.write(message_id + b"\t" + base64.b64encode(tail) + b"\n")


def check_immutable_path(binary, directory, original, expected):
    unusual = directory / "directory %23?#"
    unusual.mkdir()
    snapshot = unusual / "file:copy%3F?#.sqlite"
    snapshot.write_bytes(original)
    with closing(sqlite3.connect(snapshot)) as writable:
        assert writable.execute("PRAGMA journal_mode = WAL").fetchone() == ("wal",)
        assert writable.execute("PRAGMA wal_checkpoint(TRUNCATE)").fetchone()[0] == 0
    before = snapshot.read_bytes()
    assert set(unusual.iterdir()) == {snapshot}

    for path in (snapshot, snapshot.relative_to(directory)):
        result = subprocess.run(
            [binary, path, "range", "fixture.a", "1", "20001"],
            cwd=directory, check=True, capture_output=True,
        )
        assert result.stdout == expected
        assert snapshot.read_bytes() == before
        assert set(unusual.iterdir()) == {snapshot}


def check(binary):
    first_id = b"<One@fixture.invalid>"
    second_id = b"<two@fixture.invalid>"
    first = overview(b"First subject \xff", first_id)
    second = overview(b"Second subject", second_id, first_id)
    high = str(MAX_NUMBER).encode()
    first_tail = b"X-Test: value\r\n\tcontinued\r\n"
    with tempfile.TemporaryDirectory(prefix="ovshadow-test-") as tmp:
        directory = Path(tmp)
        source = directory / "rows.tsv"
        tails = directory / "tails.tsv"
        snapshot = directory / "overview.sqlite"
        source.write_bytes(
            b"fixture.a\t1\t" + first + b"fixture.b\t" + high + b"\t" + first +
            b"fixture.a\t15001\t" + second + b"fixture.a\t20001\t" + first
        )
        tails.write_bytes(first_id + b"\t" + base64.b64encode(first_tail) + b"\n")
        load_snapshot(source, snapshot, tails)
        snapshot.chmod(0o444)
        original = snapshot.read_bytes()
        original_names = set(directory.iterdir())

        def call(*arguments):
            return subprocess.run(
                [binary, snapshot, *arguments], check=True, capture_output=True
            ).stdout

        expected = b"1\t" + first + b"15001\t" + second + b"20001\t" + first
        assert call("range", "fixture.a", "1", "20001") == expected
        assert call("range", "fixture.a", "2", "15000") == b""
        assert call("range", "fixture.b", "1", str(MAX_NUMBER)) == high + b"\t" + first
        assert call("range", "fixture.missing", "1", "2") == b""
        assert call("lookup", "fixture.a", first_id) == b"20001\t" + first
        assert call("lookup", "fixture.b", first_id) == high + b"\t" + first
        assert call("lookup", "fixture.b", second_id) == b""
        assert call("lookup", "fixture.a", b"<one@fixture.invalid>") == b""
        bad_range = subprocess.run(
            [binary, snapshot, "range", "fixture.a", "1", str(MAX_NUMBER + 1)],
            capture_output=True,
        )
        assert bad_range.returncode != 0 and not bad_range.stdout
        summary = json.loads(call("bench", "fixture.a", "1", "20001", "5"))
        assert summary["rows"] == 3 and summary["bytes"] == len(expected)
        assert 0 <= summary["p50_ms"] <= summary["p95_ms"]

        with closing(sqlite3.connect(snapshot.as_uri() + "?mode=ro", uri=True)) as database:
            assert database.execute("SELECT count(*) FROM article").fetchone() == (2,)
            data = database.execute("SELECT zlib_data FROM header_tail").fetchone()
            assert zlib.decompress(data[0]) == first_tail
        assert snapshot.read_bytes() == original
        assert set(directory.iterdir()) == original_names
        check_immutable_path(binary, directory, original, expected)

        try:
            load_snapshot(source, snapshot, tails)
        except FileExistsError:
            pass
        else:
            raise AssertionError("existing snapshot overwritten")
        assert snapshot.read_bytes() == original

        source.write_bytes(
            b"fixture.a\t1\t" + first +
            b"fixture.b\t2\t" + overview(b"Conflicting subject", first_id)
        )
        failed = directory / "failed.sqlite"
        try:
            load_snapshot(source, failed)
        except ValueError as error:
            assert "conflicting payload" in str(error)
        else:
            raise AssertionError("conflicting crosspost accepted")
        assert not failed.exists()
        print("overview shadow checks passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    checks = commands.add_parser("check", help="test the native reader against a small snapshot")
    checks.add_argument("binary", type=Path)
    loader = commands.add_parser("load", help="build a sealed snapshot from captured overview")
    loader.add_argument("rows", type=Path)
    loader.add_argument("snapshot", type=Path)
    loader.add_argument("--tails", type=Path)
    generator = commands.add_parser("generate", help="write a deterministic synthetic fixture")
    generator.add_argument("directory", type=Path)
    generator.add_argument("articles", type=int)
    opts = parser.parse_args()
    try:
        if opts.command == "check":
            check(opts.binary.resolve())
        elif opts.command == "load":
            load_snapshot(opts.rows, opts.snapshot, opts.tails)
        else:
            if opts.articles < 1:
                parser.error("articles must be positive")
            generate(opts.directory, opts.articles)
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(1, f"{parser.prog}: {error}\n")


if __name__ == "__main__":
    main()
