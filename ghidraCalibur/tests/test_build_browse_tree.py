from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "build_browse_tree.py"
SPEC = importlib.util.spec_from_file_location("ghidra_calibur_builder", MODULE_PATH)
assert SPEC and SPEC.loader
builder = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = builder
SPEC.loader.exec_module(builder)


def write_jsonl(path: Path, values: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as output:
        for value in values:
            output.write(json.dumps(value, sort_keys=True, ensure_ascii=False) + "\n")


def update_export_count(export_dir: Path, name: str, count: int) -> None:
    path = export_dir / "export_info.json"
    value = json.loads(path.read_text(encoding="utf-8"))
    value["counts"][name] = count
    path.write_text(json.dumps(value, sort_keys=True), encoding="utf-8")


class WorkspaceFixture:
    def __init__(self, root: Path):
        self.root = root
        self.export = root / "export"
        self.workspace = root / "workspace"
        self.export.mkdir()

    def create(self, *, corrupt_hash: bool = False, duplicate_address: bool = False) -> None:
        bodies = [
            "int BuildNavigationData(){ return 1; }\n".encode("utf-8"),
            "void UpdateReplayInput(){ const char *s = \"ULuxReplayWidget\"; } // café\n".encode("utf-8"),
        ]
        (self.export / "bodies.dat").write_bytes(b"".join(bodies))
        offset = 0
        functions = []
        definitions = [
            ("140001000", "BuildNavigationData", bodies[0], "Global"),
            ("140001100", "UpdateReplayInput", bodies[1], "ULuxReplayWidget"),
        ]
        for address, name, body, namespace in definitions:
            functions.append(
                {
                    "address_space": "ram",
                    "address": address,
                    "name": name,
                    "qualified_name": f"{namespace}::{name}" if namespace != "Global" else name,
                    "namespace": namespace,
                    "signature": f"void {name}(void)",
                    "calling_convention": "__cdecl",
                    "thunk_target": "",
                    "is_thunk": False,
                    "is_external": False,
                    "no_return": False,
                    "body_status": "ok",
                    "body_offset": offset,
                    "body_length": len(body),
                    "body_sha256": ("0" * 64 if corrupt_hash and not functions else hashlib.sha256(body).hexdigest()),
                }
            )
            offset += len(body)
        functions.append(
            {
                "address_space": "ram",
                "address": "140001100" if duplicate_address else "140001200",
                "name": "BuildNavigationData",
                "qualified_name": "Other::BuildNavigationData",
                "namespace": "Other",
                "signature": "void DuplicateName(void)",
                "calling_convention": "__cdecl",
                "thunk_target": "",
                "is_thunk": False,
                "is_external": True,
                "no_return": False,
                "body_status": "external",
                "body_offset": -1,
                "body_length": 0,
                "body_sha256": "",
            }
        )
        write_jsonl(self.export / "functions.jsonl", functions)
        types = [
            {
                "category": "/SC6",
                "name": "FTest",
                "display_name": "FTest",
                "kind": "struct",
                "length": 4,
                "alignment": 4,
                "description": "fixture",
                "components": [{"offset": 0, "length": 4, "name": "value", "type": "int", "comment": ""}],
            }
        ]
        write_jsonl(self.export / "types.jsonl", types)
        write_jsonl(self.export / "globals.jsonl", [{"address_space": "ram", "address": "144000000", "name": "g_nTest", "qualified_name": "g_nTest", "type": "int", "length": 4, "referring_functions": [], "referring_function_address_spaces": []}])
        write_jsonl(self.export / "strings.jsonl", [{"address_space": "ram", "address": "143000000", "value": "café", "type": "unicode", "referring_functions": ["140001100"], "referring_function_address_spaces": ["ram"]}])
        write_jsonl(self.export / "comments.jsonl", [
            {"address_space": "ram", "address": "140001100", "function_address": "140001100", "type": "plate", "text": "fixture"},
            {"address_space": "ram", "address": "140001100", "function_address": "140001100", "type": "decompiler", "text": "decompiler fixture"},
        ])
        write_jsonl(self.export / "calls.jsonl", [{"caller": "140001000", "caller_address_space": "ram", "callee": "140001100", "callee_address_space": "ram"}])
        write_jsonl(self.export / "imports.jsonl", [
            {"address_space": "EXTERNAL", "address": "00000001", "name": "GetTickCount", "signature": "DWORD __stdcall GetTickCount(void)", "library": "KERNEL32.dll"},
        ])
        write_jsonl(self.export / "exports.jsonl", [])
        counts = {
            "functions": len(functions),
            "body_bytes": sum(len(item) for item in bodies),
            "status_ok": 2,
            "status_external": 1,
            "types": 1,
            "globals": 1,
            "strings": 1,
            "comments": 2,
            "calls": 1,
            "imports": 1,
            "exports": 0,
        }
        (self.export / "export_info.json").write_text(
            json.dumps(
                {
                    "schema": builder.SCHEMA,
                    "program_name": "fixture.exe",
                    "program_path": "/fixture.exe",
                    "executable_path": "C:/fixture.exe",
                    "executable_sha256": "ab" * 32,
                    "image_base": "140000000",
                    "language": "x86:LE:64:default",
                    "compiler_spec": "windows",
                    "ghidra_version": "12.0.4",
                    "timeout_seconds": 5,
                    "decompiler_options": {"comment_style": "program-default"},
                    "counts": counts,
                },
                sort_keys=True,
            ),
            encoding="utf-8",
        )

    def args(self, **overrides):
        values = {
            "export_dir": self.export,
            "workspace_dir": self.workspace,
            "previous_manifest": None,
            "overrides": None,
            "origin_registry": None,
            "watchlist": None,
            "allow_partial": False,
            "pipeline_sha256": "cd" * 32,
        }
        values.update(overrides)
        return argparse.Namespace(**values)


class BuilderTests(unittest.TestCase):
    def test_tokenized_categories_do_not_match_substrings(self):
        expected = {
            "BuildNavigationData": [],
            "RemoveBookmark": [],
            "FGuid_Parse": [],
            "QuitGame": [],
            "CommandletMain": [],
            "UpdateReplayInput": ["Input", "Replay"],
        }
        for name, categories in expected.items():
            record = {"name": name, "qualified_name": name, "namespace": "Global"}
            self.assertEqual(categories, builder.categories_for(record)[0])

    def test_sc6_area_routing_uses_priority_and_explicit_override(self):
        replay_input = {"address": "140001000", "name": "UpdateReplayInput", "qualified_name": "UpdateReplayInput", "namespace": "Global"}
        combat = {"address": "140001100", "name": "BuildMoveCombo", "qualified_name": "BuildMoveCombo", "namespace": "Global"}
        unknown = {"address": "140001200", "name": "BuildNavigationData", "qualified_name": "BuildNavigationData", "namespace": "Global"}
        self.assertEqual(("gameplay/replay", "tokenized-name"), builder.area_for(replay_input))
        self.assertEqual(("gameplay/combat", "tokenized-name"), builder.area_for(combat))
        self.assertEqual(("unknown", "none"), builder.area_for(unknown))
        self.assertEqual(("runtime/names", "override"), builder.area_for(unknown, {"area": "runtime/names"}))

    def test_sc6_module_routing_uses_override_rules_and_area_fallback(self):
        replay_codec = {"address": "140001000", "name": "LuxReplay_DecodePacket", "qualified_name": "LuxReplay_DecodePacket", "namespace": "Global"}
        replay_input = {"address": "140001100", "name": "UpdateReplayInput", "qualified_name": "UpdateReplayInput", "namespace": "Global"}
        unknown = {"address": "140001200", "name": "BuildNavigationData", "qualified_name": "BuildNavigationData", "namespace": "Global"}
        self.assertEqual(("codec", "tokenized-name"), builder.module_for(replay_codec, "gameplay/replay"))
        self.assertEqual(("input", "tokenized-name"), builder.module_for(replay_input, "gameplay/replay"))
        self.assertEqual(("unknown", "area-fallback"), builder.module_for(unknown, "unknown"))
        self.assertEqual(("names", "area-fallback"), builder.module_for(unknown, "runtime/names"))
        self.assertEqual(("verified", "override"), builder.module_for(unknown, "unknown", {"module": "verified"}))

    def test_family_routing_prefers_overrides_classes_and_name_prefixes(self):
        replay = {"address": "140001000", "name": "LuxReplay_DecodePacket", "qualified_name": "LuxReplay_DecodePacket", "namespace": "Global"}
        widget = {"address": "140001100", "name": "UpdateReplayInput", "qualified_name": "ULuxReplayWidget::UpdateReplayInput", "namespace": "ULuxReplayWidget"}
        self.assertEqual(("lux_replay", "name-prefix"), builder.family_for(replay, "", "codec"))
        self.assertEqual(("u_lux_replay_widget", "class"), builder.family_for(widget, "ULuxReplayWidget", "input"))
        self.assertEqual(("packet_decoder", "override"), builder.family_for(replay, "", "codec", {"family": "Packet Decoder"}))

    def test_origin_registry_routes_only_reviewed_entries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            registry = root / "origins.json"
            registry.write_text(json.dumps({
                "schema": builder.UE_ORIGIN_SCHEMA,
                "executable_sha256": "ab" * 32,
                "entries": [{
                    "address": "140001000", "address_space": "ram", "disposition": "ue4-confirmed", "module": "core",
                    "feature": "console", "baseline": {"commit": builder.UE4172_COMMIT, "source_path": "Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp", "symbol": "Fixture"},
                    "evidence": [{"kind": "source", "detail": "source"}, {"kind": "binary", "detail": "binary"}],
                    "review": {"reviewer": "Hermes", "status": "approved"},
                }, {"address": "140001100", "address_space": "ram", "disposition": "sc6", "evidence": [{"kind": "source", "detail": "source"}, {"kind": "binary", "detail": "binary"}], "review": {"reviewer": "Hermes", "status": "approved"}}]
            }), encoding="utf-8")
            origins = builder.load_origin_registry(registry, "ab" * 32)
            confirmed = {"address": "140001000", "name": "UObjectFixture", "qualified_name": "UObjectFixture", "namespace": "Global"}
            denied = {"address": "140001100", "name": "UObjectFixture", "qualified_name": "UObjectFixture", "namespace": "Global"}
            unproven = {"address": "140001200", "name": "UObjectFixture", "qualified_name": "UObjectFixture", "namespace": "Global"}
            for record in (confirmed, denied, unproven):
                record.update({"body_status": "ok", "address_space": "ram", "signature": "void Fixture(void)"})
                builder.apply_classification(record, b"", {}, origins, {})
            self.assertEqual(("ue4-confirmed", "engine/ue4/core/console"), (confirmed["origin"], confirmed["area"]))
            self.assertEqual("sc6", denied["origin"])
            self.assertEqual("not-assessed", unproven["origin"])
            with self.assertRaisesRegex(builder.ExportValidationError, "hash"):
                builder.load_origin_registry(registry, "cd" * 32)

    def test_nested_area_and_module_overrides_create_readable_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            overrides = fixture.root / "overrides.json"
            overrides.write_text(json.dumps({"addresses": {
                "140001000": {"area": "runtime/names"},
                "140001100": {"area": "gameplay/replay", "module": "codec"},
            }}), encoding="utf-8")
            builder.build_workspace(fixture.args(overrides=overrides))
            records = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "functions.jsonl"))
            self.assertEqual("src/runtime/names/build_navigation_data.cpp", records[0]["browse_file"])
            self.assertEqual("src/gameplay/replay/codec/u_lux_replay_widget/update_replay_input.cpp", records[1]["browse_file"])

    def test_equivalent_area_module_forms_share_one_canonical_shard(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            overrides = fixture.root / "overrides.json"
            overrides.write_text(json.dumps({"addresses": {
                "140001000": {"area": "runtime", "module": "names"},
                "140001100": {"area": "runtime/names"},
            }}), encoding="utf-8")
            builder.build_workspace(fixture.args(overrides=overrides))
            records = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "functions.jsonl"))
            source_records = [record for record in records if record["body_status"] == "ok"]
            self.assertEqual({"src/runtime/names/build_navigation_data.cpp", "src/runtime/names/u_lux_replay_widget/update_replay_input.cpp"}, {record["browse_file"] for record in source_records})
            self.assertEqual(2, len(list((fixture.workspace / "src").rglob("*.cpp"))))

    def test_overrides_reject_invalid_values_and_normalized_duplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            invalid = root / "invalid.json"
            invalid.write_text(json.dumps({"addresses": {"140001000": "not-an-object"}}), encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "must be an object"):
                builder.load_overrides(invalid)
            duplicate = root / "duplicate.json"
            duplicate.write_text(json.dumps({"addresses": {"140001000": {}, "0x140001000": {}}}), encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "Duplicate classification override"):
                builder.load_overrides(duplicate)
            unused = root / "unused-invalid.json"
            unused.write_text(json.dumps({"addresses": {"deadbeef": {"file_stem": []}}}), encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "Invalid file stem|must be a string"):
                builder.load_overrides(unused)

    def test_workspace_has_one_body_copy_and_hides_metadata_from_visual_studio(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            builder.build_workspace(fixture.args())
            records = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "functions.jsonl"))
            body_records = [record for record in records if record["body_status"] == "ok"]
            paths = [fixture.workspace / record["browse_file"] for record in body_records]
            self.assertTrue(all(str(path).startswith(str(fixture.workspace / "src")) for path in paths))
            source_text = "\n".join(path.read_text(encoding="utf-8") for path in sorted(set(paths)))
            for record in body_records:
                region = f"#pragma region {record['name']}_{record['address']}"
                self.assertEqual(1, source_text.count(region))
                shard = fixture.workspace / record["browse_file"]
                self.assertEqual(hashlib.sha256(shard.read_bytes()).hexdigest(), record["canonical_shard_sha256"])
                self.assertNotIn("body_offset", record)
                self.assertNotIn("body_length", record)
            project = (fixture.workspace / "GhidraCalibur.vcxproj").read_text(encoding="utf-8")
            self.assertNotIn(".ghidra", project)
            self.assertIn("src\\", project)
            self.assertIn("include\\", project)

    def test_rendered_pseudocode_uses_lf_only(self):
        record = {
            "address": "140001000",
            "name": "LineEndingFixture",
            "qualified_name": "LineEndingFixture",
            "signature": "void LineEndingFixture(void)",
            "body_status": "ok",
        }
        rendered = builder.render_function(record, b"void LineEndingFixture() {\r\n\treturn;\r}\n")
        self.assertNotIn(b"\r", rendered)
        self.assertIn(b"void LineEndingFixture() {\n\treturn;\n}", rendered)

    def test_builds_address_keyed_workspace_and_preserves_unicode(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            result = builder.build_workspace(fixture.args())
            self.assertFalse(result["partial"])
            self.assertTrue((fixture.workspace / "content_manifest.json").is_file())
            self.assertTrue((fixture.workspace / "GhidraCalibur.sln").is_file())
            shards = list((fixture.workspace / "src").rglob("*.cpp"))
            self.assertEqual(2, len(shards))
            text = "\n".join(shard.read_text(encoding="utf-8") for shard in shards)
            self.assertIn("int BuildNavigationData(){ return 1; }", text)
            self.assertIn("café", text)
            records = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "functions.jsonl"))
            metadata = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "function_metadata.jsonl"))
            self.assertEqual(["140001000", "140001100", "140001200"], [item["address"] for item in records])
            self.assertEqual(["140001000", "140001100", "140001200"], [item["address"] for item in metadata])
            self.assertNotIn("body_sha256", metadata[0])
            self.assertNotIn("browse_file", metadata[0])
            self.assertEqual([], records[0]["categories"])
            self.assertEqual(["Input", "Replay", "UI"], records[1]["categories"])
            self.assertEqual("ULuxReplayWidget", records[1]["class"])
            self.assertEqual("gameplay/replay", records[1]["area"])
            self.assertEqual("unknown", records[0]["module"])
            self.assertEqual("input", records[1]["module"])
            self.assertEqual("src/unknown/build_navigation_data.cpp", records[0]["browse_file"])
            self.assertEqual("src/gameplay/replay/input/u_lux_replay_widget/update_replay_input.cpp", records[1]["browse_file"])
            rendered = [record for record in records if record["body_status"] == "ok"]
            self.assertTrue(all(record["browse_shard_stem"] in Path(record["browse_file"]).stem for record in rendered))
            self.assertTrue(all(record["address"] not in Path(record["browse_file"]).name for record in rendered))
            self.assertTrue(all("address shard:" in (fixture.workspace / record["browse_file"]).read_text(encoding="utf-8") for record in rendered))
            self.assertTrue((fixture.workspace / "include" / "imports" / "kernel32.dll.h").is_file())
            self.assertTrue((fixture.workspace / "include" / "exports" / "sc6_exports.h").is_file())
            self.assertTrue(list((fixture.workspace / "include" / "types").rglob("*.h")))
            self.assertTrue(list((fixture.workspace / "include" / "globals").rglob("*.h")))
            self.assertTrue((fixture.workspace / ".ghidra" / "index" / "modules.csv").is_file())
            self.assertTrue((fixture.workspace / ".ghidra" / "index" / "families.csv").is_file())
            self.assertFalse(list(fixture.workspace.rglob("*.md")))

    def test_relationship_banners_are_compact_and_named(self):
        relationships = {
            "ram:140001000": {"callers": ["StartReplay"], "callees": ["UpdateReplayInput"], "globals": ["g_nReplayMode"], "strings": ["Replay data"]}
        }
        record = {"address_space": "ram", "address": "140001000", "name": "StartReplay", "qualified_name": "StartReplay", "signature": "void StartReplay(void)", "body_status": "ok"}
        rendered = builder.render_function(record, b"void StartReplay() {}", relationships).decode("utf-8")
        self.assertIn("// Callers: StartReplay", rendered)
        self.assertIn("// Calls: UpdateReplayInput", rendered)
        self.assertIn("// Globals: g_nReplayMode", rendered)
        self.assertIn("// Strings: Replay data", rendered)

    def test_global_placement_type_paths_and_navigation_are_provenanced(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            write_jsonl(fixture.export / "types.jsonl", [
                {"category": "/Gameplay/Replay", "name": "FReplay", "display_name": "FReplay", "kind": "struct", "length": 4, "alignment": 4, "description": "", "components": []},
                {"category": "/Runtime/Replay", "name": "FRuntimeReplay", "display_name": "FRuntimeReplay", "kind": "struct", "length": 4, "alignment": 4, "description": "", "components": []},
            ])
            update_export_count(fixture.export, "types", 2)
            write_jsonl(fixture.export / "globals.jsonl", [
                {"address_space": "ram", "address": "144000000", "name": "g_nReplay", "qualified_name": "g_nReplay", "type": "int", "length": 4, "referring_functions": ["140001100"], "referring_function_address_spaces": ["ram"]},
                {"address_space": "ram", "address": "144000100", "name": "g_nShared", "qualified_name": "g_nShared", "type": "int", "length": 4, "referring_functions": ["140001000", "140001100"], "referring_function_address_spaces": ["ram", "ram"]},
            ])
            update_export_count(fixture.export, "globals", 2)
            watchlist = fixture.root / "watchlist.json"
            watchlist.write_text(json.dumps({"schema": builder.WATCHLIST_SCHEMA, "executable_sha256": "ab" * 32, "entries": [{"address_space": "ram", "address": "140001100", "tags": ["replay", "decode"], "note": "Start here", "status": "active", "entry_point": True}]}), encoding="utf-8")
            builder.build_workspace(fixture.args(watchlist=watchlist))
            self.assertTrue((fixture.workspace / "include" / "types" / "gameplay" / "replay" / "replay_0001.h").is_file())
            self.assertTrue((fixture.workspace / "include" / "types" / "runtime" / "replay" / "replay_0001.h").is_file())
            self.assertTrue((fixture.workspace / "include" / "globals" / "gameplay" / "replay" / "input" / "globals_0001.h").is_file())
            self.assertTrue((fixture.workspace / "include" / "globals" / "shared" / "globals_0001.h").is_file())
            source = next((fixture.workspace / "src").rglob("update_replay_input.cpp")).read_text(encoding="utf-8")
            self.assertIn("Placement: gameplay/replay/input", source)
            self.assertIn("Watchlist: decode, replay (active)", source)
            self.assertIn("Heuristic navigation insights", (fixture.workspace / "include" / "navigation" / "insights.h").read_text(encoding="utf-8"))
            self.assertIn("UpdateReplayInput [0x140001100]", (fixture.workspace / "include" / "navigation" / "entry_points.h").read_text(encoding="utf-8"))

    def test_watchlist_rejects_identity_mismatch_and_unused_entries(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            watchlist = fixture.root / "watchlist.json"
            watchlist.write_text(json.dumps({"schema": builder.WATCHLIST_SCHEMA, "executable_sha256": "cd" * 32, "entries": []}), encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "hash"):
                builder.build_workspace(fixture.args(watchlist=watchlist))
            watchlist.write_text(json.dumps({"schema": builder.WATCHLIST_SCHEMA, "executable_sha256": "ab" * 32, "entries": [{"address_space": "ram", "address": "1400dead", "tags": [], "note": "", "status": "active"}]}), encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "absent"):
                builder.build_workspace(fixture.args(watchlist=watchlist))

    def test_watchlist_rejects_control_characters_in_generated_comment_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            watchlist = Path(directory) / "watchlist.json"
            watchlist.write_text(json.dumps({"schema": builder.WATCHLIST_SCHEMA, "executable_sha256": "ab" * 32, "entries": [{"address_space": "ram", "address": "140001000", "tags": ["replay\nnot-a-comment"], "note": "", "status": "active"}]}), encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "tags"):
                builder.load_watchlist(watchlist, "ab" * 32)

    def test_descriptive_stems_handle_generic_labels_overrides_and_collisions(self):
        self.assertEqual("", builder.readable_file_stem("FUN_140001000"))
        self.assertEqual("", builder.readable_file_stem("SUB_140001000"))
        self.assertEqual("", builder.readable_file_stem("DAT_140001000"))
        self.assertEqual("", builder.readable_file_stem("COM9"))
        self.assertEqual("unnamed", builder.safe_component("LPT9"))
        self.assertEqual("", builder.readable_file_stem("0x140001000"))
        self.assertEqual("lux_replay_decode_input_packets", builder.readable_file_stem("LuxReplay_DecodeInputPackets"))
        self.assertEqual("café_loader", builder.readable_file_stem("CaféLoader"))
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            overrides = fixture.root / "overrides.json"
            overrides.write_text(json.dumps({"addresses": {"140001000": {"file_stem": "Replay Packet Decoder"}}}), encoding="utf-8")
            builder.build_workspace(fixture.args(overrides=overrides))
            records = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "functions.jsonl"))
            self.assertEqual("src/unknown/replay_packet_decoder.cpp", records[0]["browse_file"])
            self.assertEqual("replay_packet_decoder", records[0]["browse_shard_stem"])

    def test_distinct_address_file_stem_overrides_split_shards(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            overrides = fixture.root / "overrides.json"
            overrides.write_text(json.dumps({"addresses": {
                "140001000": {"area": "runtime", "module": "names", "file_stem": "first_name_helper"},
                "140001100": {"area": "runtime", "module": "names", "file_stem": "second_name_helper"},
            }}), encoding="utf-8")
            builder.build_workspace(fixture.args(overrides=overrides))
            records = list(builder.iter_jsonl(fixture.workspace / ".ghidra" / "data" / "functions.jsonl"))
            self.assertIn("first_name_helper", records[0]["browse_file"])
            self.assertIn("second_name_helper", records[1]["browse_file"])

    def test_file_stem_override_does_not_leak_to_unoverridden_function(self):
        with tempfile.TemporaryDirectory() as directory:
            writer = builder.ShardWriter(Path(directory), "runtime", "names", "names", {})
            first = {"address_space": "ram", "address": "140001000", "file_stem_override": "special_name"}
            second = {"address_space": "ram", "address": "140001100", "name": "GetRegularName"}
            writer.add(first, b"void SpecialName() {}\n")
            writer.add(second, b"void GetRegularName() {}\n")
            writer.flush()
            paths = sorted(Path(directory).rglob("*.cpp"))
            self.assertEqual(2, len(paths))
            self.assertTrue(any(path.name.startswith("special_name") for path in paths))
            self.assertTrue(any(path.name.startswith("get_regular_name") for path in paths))

    def test_override_fields_require_strings(self):
        record = {"address": "140001000", "name": "Fixture", "qualified_name": "Fixture", "namespace": "Global"}
        with self.assertRaisesRegex(builder.ExportValidationError, "must be a string"):
            builder.area_for(record, {"area": ["runtime"]})
        with self.assertRaisesRegex(builder.ExportValidationError, "must be a string"):
            builder.family_for(record, "", "runtime", {"family": 5})

    def test_rejects_corrupt_body_hash_without_touching_other_workspace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = WorkspaceFixture(root)
            fixture.create(corrupt_hash=True)
            prior = root / "prior"
            prior.mkdir()
            marker = prior / "marker.txt"
            marker.write_text("safe", encoding="utf-8")
            with self.assertRaisesRegex(builder.ExportValidationError, "SHA-256 mismatch"):
                builder.build_workspace(fixture.args())
            self.assertEqual("safe", marker.read_text(encoding="utf-8"))

    def test_rejects_invalid_utf8_even_with_a_matching_body_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            records = list(builder.iter_jsonl(fixture.export / "functions.jsonl"))
            bodies = bytearray((fixture.export / "bodies.dat").read_bytes())
            bodies[0] = 0xFF
            (fixture.export / "bodies.dat").write_bytes(bodies)
            first_length = records[0]["body_length"]
            records[0]["body_sha256"] = hashlib.sha256(bytes(bodies[:first_length])).hexdigest()
            write_jsonl(fixture.export / "functions.jsonl", records)
            with self.assertRaisesRegex(builder.ExportValidationError, "Invalid UTF-8 body"):
                builder.build_workspace(fixture.args())

    def test_rejects_structurally_invalid_core_jsonl(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            globals_path = fixture.export / "globals.jsonl"
            invalid = list(builder.iter_jsonl(globals_path))
            del invalid[0]["length"]
            write_jsonl(globals_path, invalid)
            with self.assertRaisesRegex(builder.ExportValidationError, "length.*integer"):
                builder.build_workspace(fixture.args())

    def test_rejects_duplicate_addresses_even_when_names_differ(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create(duplicate_address=True)
            with self.assertRaisesRegex(builder.ExportValidationError, "strictly address ordered|Duplicate"):
                builder.build_workspace(fixture.args())

    def test_rejects_external_function_with_a_renderable_body(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            records = list(builder.iter_jsonl(fixture.export / "functions.jsonl"))
            records[0]["is_external"] = True
            write_jsonl(fixture.export / "functions.jsonl", records)
            with self.assertRaisesRegex(builder.ExportValidationError, "External-function status invariant"):
                builder.build_workspace(fixture.args())

    def test_import_library_aliases_merge_but_filename_collisions_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            imports_path = fixture.export / "imports.jsonl"
            imports = list(builder.iter_jsonl(imports_path))
            imports.append({
                "address_space": "EXTERNAL", "address": "00000002", "name": "Sleep",
                "signature": "void __stdcall Sleep(unsigned int)", "library": "kernel32.DLL",
            })
            write_jsonl(imports_path, imports)
            update_export_count(fixture.export, "imports", len(imports))
            builder.build_workspace(fixture.args())
            header = fixture.workspace / "include" / "imports" / "kernel32.dll.h"
            self.assertIn("GetTickCount", header.read_text(encoding="utf-8"))
            self.assertIn("Sleep", header.read_text(encoding="utf-8"))

        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            write_jsonl(fixture.export / "imports.jsonl", [
                {"address_space": "EXTERNAL", "address": "00000001", "name": "First", "signature": "void First(void)", "library": "foo/bar.dll"},
                {"address_space": "EXTERNAL", "address": "00000002", "name": "Second", "signature": "void Second(void)", "library": "foo?bar.dll"},
            ])
            update_export_count(fixture.export, "imports", 2)
            with self.assertRaisesRegex(builder.ExportValidationError, "normalize to the same header path"):
                builder.build_workspace(fixture.args())

    def test_export_header_preserves_named_ordinal_export(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            write_jsonl(fixture.export / "exports.jsonl", [{
                "address_space": "ram", "address": "140001000", "name": "Sc6Export",
                "signature": "void Sc6Export(void)", "ordinal": 17,
            }])
            update_export_count(fixture.export, "exports", 1)
            builder.build_workspace(fixture.args())
            header = (fixture.workspace / "include" / "exports" / "sc6_exports.h").read_text(encoding="utf-8")
            self.assertIn("ordinal 17", header)
            self.assertIn("void Sc6Export(void);", header)

    def test_generation_is_deterministic_across_output_directories(self):
        with tempfile.TemporaryDirectory() as first_directory, tempfile.TemporaryDirectory() as second_directory:
            first = WorkspaceFixture(Path(first_directory))
            second = WorkspaceFixture(Path(second_directory))
            first.create()
            second.create()
            first_result = builder.build_workspace(first.args())
            second_result = builder.build_workspace(second.args())
            self.assertEqual(first_result["generation_id"], second_result["generation_id"])
            self.assertEqual(
                (first.workspace / "content_manifest.json").read_bytes(),
                (second.workspace / "content_manifest.json").read_bytes(),
            )

    def test_rejects_trailing_unreferenced_body_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            with (fixture.export / "bodies.dat").open("ab") as output:
                output.write(b"orphan")
            with self.assertRaisesRegex(builder.ExportValidationError, "trailing unreferenced"):
                builder.build_workspace(fixture.args())

    def test_accepts_complete_non_overlapping_retry_body_order(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = WorkspaceFixture(Path(directory))
            fixture.create()
            records = list(builder.iter_jsonl(fixture.export / "functions.jsonl"))
            original = (fixture.export / "bodies.dat").read_bytes()
            first_length = records[0]["body_length"]
            first = original[:first_length]
            second = original[first_length:]
            (fixture.export / "bodies.dat").write_bytes(second + first)
            records[0]["body_offset"] = len(second)
            records[1]["body_offset"] = 0
            write_jsonl(fixture.export / "functions.jsonl", records)
            result = builder.build_workspace(fixture.args())
            self.assertFalse(result["partial"])

    def test_partial_override_only_bypasses_coverage(self):
        statuses = builder.Counter({"ok": 1000, "decompile-error": 3})
        with self.assertRaises(builder.ExportValidationError):
            builder.enforce_coverage(statuses, 0, False)
        partial, coverage = builder.enforce_coverage(statuses, 0, True)
        self.assertTrue(partial)
        self.assertEqual(3, coverage["failures"])
        with self.assertRaises(builder.ExportValidationError):
            builder.enforce_coverage(builder.Counter({"ok": 1, "cancelled": 1}), None, True)

    def test_failure_regression_compares_same_executable_and_options_not_modification_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text(json.dumps({
                "content": {
                    "program": {"executable_sha256": "ab" * 32, "modification_number": 100},
                    "export_options": {"timeout_seconds": 5, "decompiler_options": {"cache_size": 32}},
                    "coverage": {"failures": 7},
                }
            }), encoding="utf-8")
            current = {
                "executable_sha256": "ab" * 32,
                "modification_number": 101,
                "timeout_seconds": 5,
                "decompiler_options": {"cache_size": 32},
            }
            self.assertEqual(7, builder.previous_failures(manifest, current))


if __name__ == "__main__":
    unittest.main()
