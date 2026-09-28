"""Read-only, offline verification of the binary contract. Never opens a process."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def verify(executable: Path) -> dict:
    data = executable.read_bytes()
    digest = hashlib.sha256(data).hexdigest().upper()
    expected = "F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553"
    if len(data) != 71_737_344 or digest != expected:
        raise ValueError("Executable size/SHA-256 does not match the admitted SC6 build")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    for i in range(section_count):
        at = pe + 24 + optional_size + i * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, at + 8)
        sections.append((rva, raw_size, raw))

    def read_rva(rva, count):
        for start, size, raw in sections:
            if start <= rva and rva + count <= start + size:
                return data[raw + rva - start:raw + rva - start + count]
        raise ValueError(f"RVA {rva:#x} not backed by file data")

    header = Path(__file__).with_name("WatchRecoverySites.hpp").read_text(encoding="utf-8")
    sites = re.findall(r"\{(0x[0-9a-f]+), \{([^}]+)\}\}, // (\w+)", header)
    if len(sites) != 13:
        raise ValueError("Expected eleven hook sites and two helper dependencies")
    for address, values, name in sites:
        expected_bytes = bytes(int(value.strip(), 16) for value in values.split(","))
        if len(expected_bytes) != 32 or read_rva(int(address, 16), 32) != expected_bytes:
            raise ValueError(f"Prologue mismatch: {name}")
    vtable = 0x3D06478
    for offset, target in ((0x10, 0x142E04C60), (0x30, 0x142E06520)):
        if struct.unpack("<Q", read_rva(vtable + offset, 8))[0] != target:
            raise ValueError("Route-service lookup/acquisition vtable mismatch")
    for rva, value in ((0x3E8A454, 20.0), (0x3E8A474, 30.0)):
        if struct.unpack("<f", read_rva(rva, 4))[0] != value:
            raise ValueError("Native timeout constant mismatch")
    return {"result": "pass", "method": "offline PE file reads", "sha256": digest,
            "verified_sites": [name for _, _, name in sites],
            "route_vtable": "lookup +0x30; acquisition +0x10", "native_timeouts_seconds": [20, 30]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.executable), indent=2))
