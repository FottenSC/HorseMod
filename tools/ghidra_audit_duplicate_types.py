#!/usr/bin/env python3
"""Audit Ghidra MCP data types for duplicate canonical names.

This is intentionally read-only. It catches the class of issue where a type is
created both at the root category and under a project category such as
/HorseMod/ReplaySeek.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.parse
import urllib.request
from collections import defaultdict
from dataclasses import dataclass
from typing import Any


DEFAULT_BASE_URL = "http://127.0.0.1:8089"
DEFAULT_PROGRAM = "SoulcaliburVI.exe"


@dataclass(frozen=True)
class TypeRecord:
    name: str
    source: str
    size: str
    path: str

    @property
    def base_name(self) -> str:
        name = re.sub(r"\s*\*+$", "", self.name)
        return re.sub(r"\[.*?\]$", "", name)

    @property
    def is_pointer_or_array(self) -> bool:
        return "*" in self.name or "[" in self.name


def get_json_or_text(base_url: str, path: str, query: dict[str, Any]) -> Any:
    encoded = urllib.parse.urlencode(query, doseq=True)
    url = f"{base_url}{path}?{encoded}"
    with urllib.request.urlopen(url, timeout=120) as resp:
        text = resp.read().decode("utf-8", errors="replace")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def parse_type_records(raw: Any) -> list[TypeRecord]:
    if isinstance(raw, list):
        lines = [str(item) for item in raw]
    else:
        lines = str(raw).splitlines()

    records: list[TypeRecord] = []
    for line in lines:
        parts = [part.strip() for part in line.split(" | ")]
        if len(parts) < 4:
            continue
        records.append(TypeRecord(parts[0], parts[1], parts[2], parts[3]))
    return records


def command_audit(args: argparse.Namespace) -> int:
    raw = get_json_or_text(
        args.base_url,
        "/list_data_types",
        {"program": args.program, "category": args.category, "offset": 0, "limit": args.limit},
    )
    records = parse_type_records(raw)
    grouped: dict[str, list[TypeRecord]] = defaultdict(list)
    for record in records:
        if args.ignore_pointer_arrays and record.is_pointer_or_array:
            continue
        grouped[record.base_name].append(record)

    duplicate_groups = []
    for name, group in grouped.items():
        paths = {record.path for record in group}
        if len(paths) <= 1:
            continue
        if args.focus_path and not any(args.focus_path in record.path for record in group):
            continue
        duplicate_groups.append((name, group))

    failures = 0
    for name, group in sorted(duplicate_groups):
        sizes = {record.size for record in group}
        status = "SIZE-MISMATCH" if len(sizes) > 1 else "duplicate"
        if status == "SIZE-MISMATCH" or args.fail_on_any_duplicate:
            failures += 1
        print(f"{status}: {name}")
        for record in sorted(group, key=lambda r: r.path):
            print(f"  {record.name} | {record.source} | {record.size} | {record.path}")

    print(
        json.dumps(
            {
                "records": len(records),
                "duplicate_groups": len(duplicate_groups),
                "failures": failures,
            },
            sort_keys=True,
        )
    )
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--program", default=DEFAULT_PROGRAM)
    parser.add_argument("--category", default="")
    parser.add_argument("--limit", type=int, default=5000)
    parser.add_argument("--focus-path", default="/HorseMod/ReplaySeek")
    parser.add_argument("--fail-on-any-duplicate", action="store_true")
    parser.add_argument("--include-pointer-arrays", action="store_false", dest="ignore_pointer_arrays")
    parser.set_defaults(ignore_pointer_arrays=True)
    args = parser.parse_args(argv)
    return command_audit(args)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
