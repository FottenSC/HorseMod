from __future__ import annotations

import argparse
import importlib.util
import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1] / "tools"


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


query_tool = load("query_ghidra_calibur")
summary_tool = load("summarize_ghidra_changes")


def jsonl(path: Path, values: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(item) + "\n" for item in values), encoding="utf-8")


def make_generation(root: Path, *, renamed: bool = False, failures: int = 1) -> None:
    data = root / ".ghidra" / "data"
    jsonl(data / "functions.jsonl", [
        {"address_space": "ram", "address": "140001000", "name": "StartReplay", "qualified_name": "SC6::StartReplay", "signature": "void StartReplay()", "area": "gameplay/replay", "module": "playback", "family": "lux_replay", "origin": "sc6", "origin_provenance": "", "browse_file": "src/gameplay/replay/playback/start_replay.cpp", "browse_line": 12, "watch_tags": ["entry"], "watch_note": "", "watch_status": "active"},
        {"address_space": "ram", "address": "140001100", "name": "DecodePacket" if not renamed else "DecodeReplayPacket", "qualified_name": "SC6::DecodePacket", "signature": "void DecodePacket()", "area": "gameplay/replay", "module": "codec", "family": "lux_replay", "origin": "sc6", "origin_provenance": "", "browse_file": "src/gameplay/replay/codec/decode_packet.cpp", "browse_line": 8, "watch_tags": ["codec"], "watch_note": "", "watch_status": "active"},
        {"address_space": "ram", "address": "140001200", "name": "FUN_140001200", "qualified_name": "FUN_140001200", "signature": "void FUN_140001200()", "area": "unknown", "module": "unknown", "family": "unknown", "origin": "not-assessed", "origin_provenance": "", "browse_file": "src/unknown/unknown.cpp", "browse_line": 4, "watch_tags": [], "watch_note": "", "watch_status": ""},
        {"address_space": "EXTERNAL", "address": "140001100", "name": "ExternalDecode", "qualified_name": "KERNEL32::ExternalDecode", "signature": "void ExternalDecode()", "area": "unknown", "module": "unknown", "family": "unknown", "origin": "not-assessed", "origin_provenance": "", "browse_file": "", "browse_line": 0, "watch_tags": [], "watch_note": "", "watch_status": ""},
    ])
    jsonl(data / "calls.jsonl", [{"caller": "140001000", "caller_address_space": "ram", "callee": "140001100", "callee_address_space": "ram"}, {"caller": "140001100", "caller_address_space": "ram", "callee": "140001200", "callee_address_space": "ram"}])
    jsonl(data / "globals.jsonl", [{"address": "144000000", "name": "g_nReplay", "referring_functions": ["140001000", "140001100"], "referring_function_address_spaces": ["ram", "ram"]}])
    jsonl(data / "strings.jsonl", [{"address": "143000000", "value": "Replay packet", "referring_functions": ["140001100"], "referring_function_address_spaces": ["ram"]}])
    metadata = [dict(item) for item in query_tool.jsonl(data / "functions.jsonl")]
    jsonl(data / "function_metadata.jsonl", metadata)
    (root / "content_manifest.json").write_text(json.dumps({"content": {"counts": {"functions": 3, "calls": 2}, "coverage": {"failures": failures}}}), encoding="utf-8")


class NavigationToolTests(unittest.TestCase):
    def test_cache_is_local_and_all_filters_return_canonical_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            generation, cache_root = root / "generation", root / "cache"
            make_generation(generation)
            cache = cache_root / "generation.sqlite"
            query_tool.build_cache(generation, cache)
            self.assertTrue(cache.is_file())
            self.assertTrue(query_tool.cache_is_usable(cache, generation))
            self.assertFalse(list(generation.rglob("*.sqlite")))
            db = sqlite3.connect(cache)
            try:
                for option, value in (("query", "Decode"), ("address", "140001100"), ("calls", "Decode"), ("called_by", "Start"), ("string", "packet"), ("global_name", "Replay"), ("area", "gameplay/replay"), ("module", "codec"), ("family", "lux_replay"), ("origin", "sc6"), ("tag", "codec")):
                    args = argparse.Namespace(query=None, address=None, calls=None, called_by=None, string=None, global_name=None, area=None, module=None, family=None, origin=None, tag=None, limit=10)
                    setattr(args, option, value)
                    results = query_tool.query(db, args)
                    self.assertTrue(results, option)
                result = query_tool.query(db, argparse.Namespace(query="DecodePacket", address=None, calls=None, called_by=None, string=None, global_name=None, area=None, module=None, family=None, origin=None, tag=None, limit=10))[0]
                self.assertEqual("src/gameplay/replay/codec/decode_packet.cpp", result["browse_file"])
                self.assertIn("StartReplay [0x140001000]", result["callers"])
                self.assertIn("FUN_140001200 [0x140001200]", result["callees"])
                same_address = query_tool.query(db, argparse.Namespace(query=None, address="140001100", calls=None, called_by=None, string=None, global_name=None, area=None, module=None, family=None, origin=None, tag=None, limit=10))
                self.assertEqual({"ram", "EXTERNAL"}, {item["address_space"] for item in same_address})
            finally:
                db.close()

    def test_semantic_summary_ignores_bodies_and_reports_metadata_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            previous, candidate, output = root / "previous", root / "candidate", root / "change_summary.json"
            make_generation(previous, failures=3)
            make_generation(candidate, renamed=True, failures=1)
            before = summary_tool.records(previous)
            after = summary_tool.records(candidate)
            self.assertEqual("DecodePacket", before["ram:140001100"]["name"])
            self.assertEqual("DecodeReplayPacket", after["ram:140001100"]["name"])
            old_manifest, new_manifest = summary_tool.manifest(previous), summary_tool.manifest(candidate)
            changes = summary_tool.core_count_changes(old_manifest, new_manifest)
            self.assertEqual({}, changes)
            summary = {
                "core_artifact_count_changes": changes,
                "decompiler_failures": {"before": 3, "after": 1, "added": 0, "resolved": 2},
            }
            output.write_text(json.dumps(summary), encoding="utf-8")
            self.assertEqual(2, json.loads(output.read_text(encoding="utf-8"))["decompiler_failures"]["resolved"])


if __name__ == "__main__":
    unittest.main()
