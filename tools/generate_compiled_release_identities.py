from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path


def byte_array(name: str, value: bytes) -> str:
    row = ", ".join(f"std::byte{{0x{item:02x}}}" for item in value)
    return f"inline constexpr std::array<std::byte, 32> {name}{{{{\n    {row}\n}}}};\n"


def main() -> int:
    if len(sys.argv) != 4:
        raise RuntimeError("usage: generate_compiled_release_identities.py ROOT SCHEMA OUTPUT")
    root, schema, output = map(Path, sys.argv[1:4])
    if not schema.is_file():
        raise RuntimeError(f"schema does not exist: {schema}")
    # Reject malformed generated contracts before their byte hash can enter a
    # release identity. The release loader binds the same parsed schema used by
    # the qualification runners, not merely an arbitrary byte sequence.
    json.loads(schema.read_text(encoding="utf-8"))
    sys.path.insert(0, str(root.resolve()))
    from tools.deterministic_qualification.artifacts import source_identity
    canonical_source = json.dumps(
        source_identity(root), sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    text = (
        "#pragma once\n\n#include <array>\n#include <cstddef>\n\n"
        "namespace Horse::Deterministic\n{\n"
        + byte_array("production_compiled_schema_sha256", hashlib.sha256(schema.read_bytes()).digest())
        + byte_array("production_compiled_source_sha256", hashlib.sha256(canonical_source).digest())
        + "}\n"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != text:
        output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
