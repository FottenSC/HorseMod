#!/usr/bin/env python3
"""Write a semantic, body-independent comparison between two GhidraCalibur workspaces."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


FIELDS = (
    "name", "qualified_name", "namespace", "signature",
    "origin", "origin_confidence", "ue_module", "ue_feature",
    "area", "module", "family",
)


def records(path: Path) -> dict[str, dict]:
    target = path / ".ghidra" / "data" / "function_metadata.jsonl"
    if not target.is_file(): return {}
    result = {}
    for line in target.read_text(encoding="utf-8").splitlines():
        if line.strip():
            item = json.loads(line)
            key = f"{str(item.get('address_space', 'unknown')).casefold()}:{str(item['address']).lower().removeprefix('0x')}"
            if key in result:
                raise ValueError(f"Duplicate function identity in metadata: {key}")
            result[key] = item
    return result


def is_generic(name: str) -> bool:
    return name.upper().startswith(("FUN_", "SUB_", "THUNK_", "LAB_", "DAT_"))


def manifest(path: Path | None) -> dict:
    if not path:
        return {}
    target = path / "content_manifest.json"
    return json.loads(target.read_text(encoding="utf-8")) if target.is_file() else {}


def core_count_changes(before: dict, after: dict) -> dict[str, dict[str, int]]:
    old = before.get("content", {}).get("counts", {})
    new = after.get("content", {}).get("counts", {})
    return {
        name: {"before": int(old.get(name, 0)), "after": int(new.get(name, 0))}
        for name in sorted(set(old).union(new))
        if int(old.get(name, 0)) != int(new.get(name, 0))
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--previous", type=Path)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before, after = records(args.previous) if args.previous else {}, records(args.candidate)
    before_manifest, after_manifest = manifest(args.previous), manifest(args.candidate)
    changed = []
    for address in sorted(set(before).intersection(after)):
        fields = [field for field in FIELDS if before[address].get(field) != after[address].get(field)]
        if fields:
            item = after[address]
            changed.append({"address_space": item.get("address_space", "unknown"), "address": f"0x{str(item['address']).lower().removeprefix('0x')}", "fields": fields, "before": {field: before[address].get(field) for field in fields}, "after": {field: after[address].get(field) for field in fields}})
    summary = {
        "baseline": "none" if not before else "previous-generation",
        "functions_added": len(set(after).difference(before)),
        "functions_removed": len(set(before).difference(after)),
        "generic_names_resolved": sum(is_generic(before[address].get("name", "")) and not is_generic(after[address].get("name", "")) for address in set(before).intersection(after)),
        "changed_functions": changed,
        "core_artifact_count_changes": core_count_changes(before_manifest, after_manifest),
        "decompiler_failures": {
            "before": int(before_manifest.get("content", {}).get("coverage", {}).get("failures", 0)),
            "after": int(after_manifest.get("content", {}).get("coverage", {}).get("failures", 0)),
            "added": max(0, int(after_manifest.get("content", {}).get("coverage", {}).get("failures", 0)) - int(before_manifest.get("content", {}).get("coverage", {}).get("failures", 0))),
            "resolved": max(0, int(before_manifest.get("content", {}).get("coverage", {}).get("failures", 0)) - int(after_manifest.get("content", {}).get("coverage", {}).get("failures", 0))),
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"added": summary["functions_added"], "removed": summary["functions_removed"], "changed": len(changed), "generic_names_resolved": summary["generic_names_resolved"], "failure_delta": summary["decompiler_failures"]["after"] - summary["decompiler_failures"]["before"]}))
    return 0


if __name__ == "__main__": raise SystemExit(main())
