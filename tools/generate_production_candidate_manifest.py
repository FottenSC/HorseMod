from __future__ import annotations

import json
import hashlib
import sys
from pathlib import Path


def quoted(value: str) -> str:
    return json.dumps(value)


def main() -> int:
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    document = json.loads(source.read_text(encoding="utf-8"))
    manifest_hash = hashlib.sha256(source.read_bytes()).digest()
    cases = document.get("cases")
    if document.get("schema_version") != 1 or len(cases or ()) != 3:
        raise RuntimeError("production candidate manifest must contain exactly three schema-v1 cases")
    seen: set[str] = set()
    rows: list[str] = []
    required = (
        "case_id", "replay", "fighter_order", "fighter_names",
        "replay_sha256", "replay_metadata_stage", "replay_metadata_map",
        "stage_selection_code", "authored_stage_code", "stage_package_root",
        "map_path", "localization_key", "native_display_name", "rng_policy",
    )
    for case in cases:
        if any(key not in case for key in required):
            raise RuntimeError("candidate case has an uncontracted field")
        if case["case_id"] in seen or len(case["fighter_order"]) != 2:
            raise RuntimeError("candidate case IDs and fighter ordering must be exact")
        if case["rng_policy"] != "authored_stage_only_random_selection_forbidden":
            raise RuntimeError("wildcard/random stage policy is forbidden")
        replay = source.parents[2] / case["replay"]
        if (not replay.is_file()
                or hashlib.sha256(replay.read_bytes()).hexdigest()
                    != case["replay_sha256"]):
            raise RuntimeError("candidate replay hash does not match its frozen manifest")
        seen.add(case["case_id"])
        values = [
            case["case_id"], *case["fighter_order"],
            case["stage_selection_code"], case["authored_stage_code"],
            case["stage_package_root"], case["map_path"],
            case["localization_key"], case["native_display_name"],
            case["replay"],
        ]
        rows.append("    {" + ", ".join(quoted(v) for v in values) + ", true},")
    text = """#pragma once

#include <array>
#include <string_view>

namespace Horse::Deterministic
{
struct ProductionContentEntry
{
    std::string_view case_id;
    std::string_view fighter0;
    std::string_view fighter1;
    std::string_view stage_selection_code;
    std::string_view authored_stage_code;
    std::string_view stage_package_root;
    std::string_view map_path;
    std::string_view localization_key;
    std::string_view stage_display_name;
    std::string_view replay_path;
    bool require_authored_stage;
};

inline constexpr std::array<ProductionContentEntry, 3>
    production_content_candidates{{
""" + "\n".join(rows) + """
    }};
inline constexpr std::array<std::byte, 32> production_candidate_manifest_sha256{{
""" + "    " + ", ".join(f"std::byte{{0x{value:02x}}}" for value in manifest_hash) + """
}};
}
"""
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != text:
        output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
