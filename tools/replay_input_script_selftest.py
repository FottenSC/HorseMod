#!/usr/bin/env python3
"""Self-test for replay-file input-script extraction used by rollback P2P.

This mirrors Horse::RollbackReplayInputScriptExtractor for the default known
replay. It treats replay files as deterministic input scripts only.
"""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path
from typing import Any


REPO = Path(__file__).resolve().parents[1]
DEFAULT_REPLAY = REPO / "ReplayExample" / "REPLAY_12744704008398858106.bin"

EXPECTED = {
    "block_count": 10,
    "pair_count": 5,
    "frames_p0": 11141,
    "frames_p1": 11141,
    "file_hash": 0xB334473952E09EC4,
    "decompressed_hash": 0x4209601EA59D1C73,
    "input_hash_p0": 0x1DB9F9FD57256C77,
    "input_hash_p1": 0xE09EB472C4B6F4CB,
}


def fnv1a64(data: bytes) -> int:
    h = 1469598103934665603
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def unwrap_payload(file_bytes: bytes) -> tuple[bytes, bool]:
    magic = b"HMRPLY1\0"
    if len(file_bytes) >= 72 and file_bytes[:8] == magic:
        version, header_bytes = struct.unpack_from("<II", file_bytes, 8)
        payload_bytes = struct.unpack_from("<Q", file_bytes, 16)[0]
        payload_hash = struct.unpack_from("<Q", file_bytes, 32)[0]
        if version != 1 or header_bytes != 72:
            raise ValueError("invalid HorseMod replay wrapper")
        if payload_bytes <= 0 or len(file_bytes) != 72 + payload_bytes:
            raise ValueError("invalid HorseMod replay wrapper size")
        payload = file_bytes[72:]
        if fnv1a64(payload) != payload_hash:
            raise ValueError("HorseMod replay wrapper payload hash mismatch")
        return payload, True
    return file_bytes, False


def extract(payload: bytes, *, include_streams: bool = False) -> dict[str, Any]:
    if len(payload) < 8 or payload[:4] != b"ULX1":
        raise ValueError("missing ULX1 replay payload")
    raw = zlib.decompress(payload[8:])

    blocks: list[dict[str, Any]] = []
    for offset in range(0, len(raw) - 4, 4):
        byte_count = struct.unpack_from("<I", raw, offset)[0]
        if byte_count < 256 or byte_count > 60000 or byte_count % 4:
            continue
        end = offset + 4 + byte_count
        if end > len(raw):
            continue
        frame_count = byte_count // 4
        values = list(struct.unpack_from("<" + "I" * frame_count, raw, offset + 4))
        if any(v > 0x3FFF for v in values):
            continue
        if sum(1 for v in values if v) < 20 or len(set(values)) < 5:
            continue
        blocks.append(
            {
                "offset": offset,
                "byte_count": byte_count,
                "frame_count": frame_count,
                "hash": fnv1a64(raw[offset + 4 : end]),
                "values": values,
            }
        )

    filtered: list[dict[str, Any]] = []
    skip_until = 0
    for block in sorted(blocks, key=lambda b: b["offset"]):
        if block["offset"] < skip_until:
            continue
        skip_until = block["offset"] + 4 + block["byte_count"]
        filtered.append(block)

    used = [False] * len(filtered)
    pairs: list[dict[str, Any]] = []
    streams = [[], []]
    for i, block in enumerate(filtered):
        if used[i]:
            continue
        match = None
        for j in range(i + 1, len(filtered)):
            if not used[j] and filtered[j]["byte_count"] == block["byte_count"]:
                match = j
                break
        if match is None:
            continue
        used[i] = True
        used[match] = True
        peer = filtered[match]
        pairs.append(
            {
                "player0_offset": block["offset"],
                "player1_offset": peer["offset"],
                "byte_count": block["byte_count"],
                "frame_count": block["frame_count"],
                "player0_hash": block["hash"],
                "player1_hash": peer["hash"],
            }
        )
        streams[0].extend(block["values"])
        streams[1].extend(peer["values"])

    stream0 = struct.pack("<" + "I" * len(streams[0]), *streams[0])
    stream1 = struct.pack("<" + "I" * len(streams[1]), *streams[1])
    result = {
        "ok": bool(pairs) and len(streams[0]) == len(streams[1]),
        "decompressed_bytes": len(raw),
        "decompressed_hash": fnv1a64(raw),
        "block_count": len(filtered),
        "pair_count": len(pairs),
        "frames_p0": len(streams[0]),
        "frames_p1": len(streams[1]),
        "input_hash_p0": fnv1a64(stream0),
        "input_hash_p1": fnv1a64(stream1),
        "pairs": pairs,
    }
    if include_streams:
        result["streams"] = streams
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--replay", type=Path, default=DEFAULT_REPLAY)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    file_bytes = args.replay.read_bytes()
    payload, wrapper = unwrap_payload(file_bytes)
    result = extract(payload)
    result.update(
        {
            "replay": str(args.replay),
            "wrapper_header": wrapper,
            "file_bytes": len(file_bytes),
            "file_hash": fnv1a64(file_bytes),
            "payload_bytes": len(payload),
            "payload_hash": fnv1a64(payload),
        }
    )

    failures = [
        key
        for key, expected in EXPECTED.items()
        if int(result.get(key, -1)) != expected
    ]
    if failures:
        result["ok"] = False
        result["failures"] = failures

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(
            "replay-input-script "
            f"ok={int(bool(result['ok']))} "
            f"blocks={result['block_count']} "
            f"pairs={result['pair_count']} "
            f"frames={result['frames_p0']}/{result['frames_p1']} "
            f"hash_p0=0x{result['input_hash_p0']:X} "
            f"hash_p1=0x{result['input_hash_p1']:X}"
        )
        if failures:
            print("failures=" + ",".join(failures))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
