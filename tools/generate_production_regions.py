from __future__ import annotations

import json
import hashlib
import sys
from pathlib import Path


CLASSES = {
    "canonical_gameplay": "RegionClass::CanonicalGameplay",
    "derived": "RegionClass::Derived",
    "client_local_diagnostic": "RegionClass::ClientLocalDiagnostic",
    "persistent_presentation": "RegionClass::PersistentPresentation",
    "ephemeral_presentation": "RegionClass::EphemeralPresentation",
}


def main() -> int:
    source, output = map(Path, sys.argv[1:3])
    document = json.loads(source.read_text(encoding="utf-8"))
    manifest_hash = hashlib.sha256(source.read_bytes()).digest()
    regions = document.get("regions")
    required = {"order", "name", "resolver", "address", "size", "class",
                "owner", "type", "writers", "readers", "lifetime", "restore", "failure"}
    if document.get("schema_version") != 1 or not regions:
        raise RuntimeError("production region manifest is empty or has the wrong schema")
    rows = []
    for index, region in enumerate(regions, 1):
        if set(region) != required or region["order"] != index:
            raise RuntimeError(f"uncontracted or out-of-order production region {index}")
        if region["class"] not in CLASSES or region["size"] <= 0:
            raise RuntimeError(f"invalid production region {region['name']}")
        values = (region["owner"], region["type"], region["writers"],
                  region["readers"], region["lifetime"], region["restore"], region["failure"])
        if any(not value for value in values):
            raise RuntimeError(f"incomplete production contract {region['name']}")
        rows.append(
            "    {" + json.dumps(region["name"]) + ", "
            + str(region["address"]) + ", " + str(region["size"]) + ", "
            + CLASSES[region["class"]] + ", " + json.dumps(region["resolver"]) + "},"
        )
    hash_row = ", ".join(f"std::byte{{0x{value:02x}}}" for value in manifest_hash)
    text = "#pragma once\n\ninline constexpr std::array<NativeRegionDescriptor, " + str(len(rows)) + "> production_regions{{\n" + "\n".join(rows) + "\n}};\ninline constexpr std::array<std::byte, 32> production_region_manifest_sha256{{\n    " + hash_row + "\n}};\n"
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != text:
        output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
