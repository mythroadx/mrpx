#!/usr/bin/env python3
"""Convert the MRP dialect probe console log to JSON and CSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path
from typing import TypedDict


class ProbeCase(TypedDict):
    name: str
    status: str
    result: str


class ProbeEvent(TypedDict):
    code: str
    p0: str
    p1: str


class ProbeResults(TypedDict):
    schema: str
    case_count: int
    status_counts: dict[str, int]
    cases: list[ProbeCase]
    events: list[ProbeEvent]


class SavedProbeResults(ProbeResults):
    log_sha256: str


def parse_log(text: str) -> ProbeResults:
    cases: list[ProbeCase] = []
    events: list[ProbeEvent] = []
    names: set[str] = set()
    started = False
    expected: int | None = None

    for line in text.splitlines():
        if line.startswith("DIALECT_EVENT|"):
            parts = line.split("|")
            if not started or len(parts) != 4:
                raise ValueError(f"Malformed event line: {line}")
            events.append({"code": parts[1], "p0": parts[2], "p1": parts[3]})
        elif line.startswith("DIALECT|"):
            parts = line.split("|", 3)
            if len(parts) == 2 and parts[1] == "started":
                if started:
                    raise ValueError("Multiple probe starts in one log")
                started = True
            elif len(parts) == 3 and parts[1] == "complete":
                if not started or expected is not None or not parts[2].isdecimal():
                    raise ValueError(f"Invalid completion line: {line}")
                expected = int(parts[2])
            else:
                if (not started or expected is not None or len(parts) != 4
                        or not parts[1] or parts[2] not in
                        ("ok", "compile_error", "runtime_error")):
                    raise ValueError(f"Invalid probe result line: {line}")
                if parts[1] in names:
                    raise ValueError(f"Duplicate probe case: {parts[1]}")
                names.add(parts[1])
                cases.append({"name": parts[1], "status": parts[2], "result": parts[3]})

    if not started or expected is None or expected == 0 or len(cases) != expected:
        raise ValueError(f"Incomplete probe log: expected {expected}, found {len(cases)} cases")
    counts = Counter(case["status"] for case in cases)
    return {
        "schema": "mrp-dialect-results-v1",
        "case_count": len(cases),
        "status_counts": {status: counts[status]
                          for status in ("ok", "compile_error", "runtime_error")},
        "cases": cases,
        "events": events,
    }


def write_results(log: Path, output: Path) -> SavedProbeResults:
    raw = log.read_bytes()
    document: SavedProbeResults = {
        **parse_log(raw.decode("utf-8-sig")),
        "log_sha256": hashlib.sha256(raw).hexdigest(),
    }

    json_path = output / "dialect-results.json"
    csv_path = output / "dialect-results.csv"
    if json_path.exists() or csv_path.exists():
        raise ValueError(f"Result files already exist in {output}")
    output.mkdir(parents=True, exist_ok=True)
    with json_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    with csv_path.open("x", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=("name", "status", "result"))
        writer.writeheader()
        writer.writerows(document["cases"])
    return document


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="UTF-8 console log from mrp_dialect_probe.mr")
    parser.add_argument("--output", type=Path, required=True, help="Result directory")
    args = parser.parse_args(argv)
    try:
        document = write_results(args.log, args.output)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")
    print(f"Recorded {document['case_count']} cases in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
