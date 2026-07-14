from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "ue_version_forensics.py"
SPEC = importlib.util.spec_from_file_location("ue_version_forensics", MODULE_PATH)
assert SPEC and SPEC.loader
forensics = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = forensics
SPEC.loader.exec_module(forensics)


def make_root(root: Path, markers: list[str], version: tuple[int, int, int]) -> None:
    build = root / "Engine" / "Build"
    source = root / "Engine" / "Source" / "Runtime" / "Core" / "Private"
    build.mkdir(parents=True)
    source.mkdir(parents=True)
    (build / "Build.version").write_text(json.dumps({
        "MajorVersion": version[0], "MinorVersion": version[1], "PatchVersion": version[2],
        "Changelist": 0, "CompatibleChangelist": 0,
    }), encoding="utf-8")
    content = "\n".join(f'const char* Marker{index} = "{marker}";' for index, marker in enumerate(markers))
    (source / "Markers.cpp").write_text(content, encoding="utf-8")


class VersionForensicsTests(unittest.TestCase):
    def test_staging_only_binary_markers_favor_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release = root / "release"
            staging = root / "staging"
            start = root / "start"
            make_root(release, ["release evidence alpha marker", "release evidence beta marker", "release evidence gamma marker"], (4, 17, 2))
            staging_markers = ["staging evidence alpha marker", "staging evidence beta marker", "staging evidence gamma marker"]
            make_root(staging, staging_markers, (4, 17, 0))
            make_root(start, staging_markers, (4, 17, 0))
            executable = root / "SoulcaliburVI.exe"
            executable.write_bytes(b"++UE4+Release-4.17\x00" + "\x00".join(staging_markers).encode("ascii"))
            output = root / "output"
            result = forensics.analyze(argparse.Namespace(
                release_root=release, staging_pre_root=staging, staging_start_root=start,
                sc6_executable=executable, output_dir=output, marker_limit=6, corroboration=None,
            ))
            self.assertIn("inconclusive", result["score"]["conclusion"])
            self.assertEqual(0, result["score"]["matched_staging_only"])
            for marker in result["markers"]:
                if marker["baseline"] == "staging-only":
                    marker["corroboration_status"] = "confirmed"
            confirmed = forensics.baseline_score(result["markers"])
            self.assertEqual("staging-4.17-era baseline favored", confirmed["conclusion"])
            self.assertEqual(3, confirmed["matched_staging_only"])
            self.assertTrue((output / "ue_baseline_candidates.json").is_file())
            self.assertTrue((output / "ue_discriminators.json").is_file())
            self.assertTrue((output / "ue_baseline_report.md").is_file())

    def test_fewer_than_three_matches_is_inconclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release = root / "release"
            staging = root / "staging"
            start = root / "start"
            make_root(release, ["release evidence alpha marker", "release evidence beta marker", "release evidence gamma marker"], (4, 17, 2))
            make_root(staging, ["staging evidence alpha marker", "staging evidence beta marker", "staging evidence gamma marker"], (4, 17, 0))
            make_root(start, ["staging evidence alpha marker"], (4, 17, 0))
            executable = root / "SoulcaliburVI.exe"
            executable.write_bytes(b"staging evidence alpha marker")
            result = forensics.analyze(argparse.Namespace(
                release_root=release, staging_pre_root=staging, staging_start_root=start,
                sc6_executable=executable, output_dir=root / "output", marker_limit=6, corroboration=None,
            ))
            self.assertIn("inconclusive", result["score"]["conclusion"])


if __name__ == "__main__":
    unittest.main()
