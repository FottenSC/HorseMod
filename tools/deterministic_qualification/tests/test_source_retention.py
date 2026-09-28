import json
from pathlib import Path
import subprocess
import zipfile

import pytest

from tools.deterministic_qualification import source_retention as sources


@pytest.mark.workflow
def test_reporting_edits_reuse_native_build_but_consumed_generators_do_not(tmp_path, monkeypatch):
    root = repository(tmp_path)
    build = root / 'build'
    (build / 'replay-tests').mkdir(parents=True)
    graph = dict(roots={'workspace': str(root)}, commands='native compile', targets=['fixture'])
    consumed = {}
    monkeypatch.setattr(sources, 'build_inputs', lambda *_: (consumed.copy(), graph.copy()))
    retained = sources.retain_sources_zip(root, tmp_path/'retained', build)
    binary = build/'mod.dll'
    binary.write_bytes(b'compiled native identity')
    log = build/'build.log'
    log.write_bytes(b'compiler success')
    report = dict(schema=1, source_retention=retained, workspace_fingerprint=sources.workspace_fingerprint(root),
        binaries={'runtime':dict(path=str(binary),sha256=sources._digest(binary.read_bytes()))},
        build_log=dict(path=str(log),sha256=sources._digest(log.read_bytes())))
    (build/'replay-tests/build-provenance.json').write_text(json.dumps(report))
    (root/'tools/replay_test.py').write_bytes(b'new reporting implementation')
    assert sources.require_build_provenance(root,build,{'runtime':binary}) == report
    consumed['workspace/tools/replay_test.py'] = root/'tools/replay_test.py'
    with pytest.raises(RuntimeError,match='input changed'):
        sources.require_build_provenance(root,build,{'runtime':binary})
    consumed.clear()
    (root/'HorseMod/new.hpp').write_bytes(b'new native source')
    with pytest.raises(RuntimeError,match='membership'):
        sources.require_build_provenance(root,build,{'runtime':binary})


def repository(tmp_path):
    root = tmp_path / "checkout"
    root.mkdir()
    subprocess.run(["git", "init", str(root)], check=True, capture_output=True)
    paths = ["HorseMod/horselib/GameImGui/PresentHook.hpp", "RE-UE4SS/UE4SS/src/Hooks.cpp",
             "tools/replay_test.py", "HorseMod/removed.cpp", "CMakeLists.txt"]
    for name in paths:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((name + " original\r\n").encode())
    subprocess.run(["git", "add", "."], cwd=root, check=True, capture_output=True)
    subprocess.run(["git", "-c", "user.name=Source Test", "-c", "user.email=test@invalid",
                    "commit", "-m", "fixture"], cwd=root, check=True, capture_output=True)
    return root


@pytest.mark.workflow
def test_retains_actual_dirty_untracked_bytes_and_deletions(tmp_path):
    root = repository(tmp_path)
    new = root / "tools/replay_seek_state_selftest.inl"
    new.write_bytes(b"untracked test\x00exact bytes\r\n")
    fixture = root / "tools/replay-test-input.fixture"
    fixture.write_bytes(b"untracked generator/test input of an unknown extension")
    (root / "RE-UE4SS/UE4SS/src/Hooks.cpp").write_bytes(b"changed engine override\r\n")
    (root / "HorseMod/removed.cpp").unlink()
    report = sources.retain_sources_zip(root, tmp_path / "evidence")
    with zipfile.ZipFile(report["archive"]) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        assert archive.read("workspace/tools/replay_seek_state_selftest.inl") == new.read_bytes()
        assert archive.read("workspace/tools/replay-test-input.fixture") == fixture.read_bytes()
        assert archive.read("workspace/RE-UE4SS/UE4SS/src/Hooks.cpp") == b"changed engine override\r\n"
        assert archive.read("workspace/HorseMod/horselib/GameImGui/PresentHook.hpp")
        assert archive.read("workspace/tools/replay_test.py")
        deleted = next(row for row in manifest["files"] if row["path"].endswith("removed.cpp"))
        assert deleted == {"path": "workspace/HorseMod/removed.cpp", "deleted": True}
        # Reconstruct into an empty tree and verify against independent files.
        for row in manifest["files"]:
            if row.get("deleted"): continue
            output = tmp_path / "reconstructed" / row["path"]
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(archive.read(row["path"]))
            assert output.read_bytes() == (root / row["path"].removeprefix("workspace/")).read_bytes()
    assert sources.retain_sources_zip(root, tmp_path / "evidence") == report
    new.write_bytes(b"different untracked implementation")
    assert sources.retain_sources_zip(root, tmp_path / "evidence")["manifest_sha256"] != report["manifest_sha256"]


@pytest.mark.workflow
def test_moving_source_is_rejected_without_publishing_archive(tmp_path, monkeypatch):
    root = repository(tmp_path)
    original = zipfile.ZipFile.writestr
    def mutate(self, name, data, *args, **kwargs):
        result = original(self, name, data, *args, **kwargs)
        if getattr(name, "filename", name) == "manifest.json":
            (root / "tools/replay_test.py").write_bytes(b"changed during retention")
        return result
    monkeypatch.setattr(zipfile.ZipFile, "writestr", mutate)
    with pytest.raises(RuntimeError, match="source changed during retention"):
        sources.retain_sources_zip(root, tmp_path / "evidence")
    assert list((tmp_path / "evidence").iterdir()) == []


@pytest.mark.workflow
def test_existing_archive_corruption_is_not_overwritten(tmp_path):
    root = repository(tmp_path)
    report = sources.retain_sources_zip(root, tmp_path / "evidence")
    path = Path(report["archive"])
    path.write_bytes(b"corrupt")
    with pytest.raises(RuntimeError, match="collision/corruption"):
        sources.retain_sources_zip(root, tmp_path / "evidence")
    assert path.read_bytes() == b"corrupt"


@pytest.mark.workflow
def test_build_dependency_closure_retains_ignored_generated_inputs(tmp_path, monkeypatch):
    from types import SimpleNamespace
    root = repository(tmp_path)
    build = root / "build"
    build.mkdir()
    fetched = tmp_path / "fetched"
    fetched.mkdir()
    (build / "CMakeCache.txt").write_text(f"CMAKE_MAKE_PROGRAM:FILEPATH=fake-ninja\nFETCHCONTENT_BASE_DIR:PATH={fetched}\n")
    (build / "build.ninja").write_text("build recipes")
    header = build / "Generated.hpp"
    header.write_bytes(b"generated ABI input")
    dependency = fetched / "dependency.hpp"
    dependency.write_bytes(b"fetched code actually consumed")
    external = tmp_path / "custom-sdk" / "custom.hpp"
    external.parent.mkdir()
    external.write_bytes(b"external source actually consumed")
    output = build / "runtime.obj"
    output.write_bytes(b"rebuildable compiler output")
    original = subprocess.run
    def run(command, **kwargs):
        if command[0] != "fake-ninja": return original(command, **kwargs)
        operation = command[2]
        response = {"inputs": "Generated.hpp\nruntime.obj\nutility\n",
                    "deps": f"runtime.obj: #deps 2\n    {dependency}\n    {external}\n",
                    "targets": "runtime.obj: CXX_COMPILER\nutility: CUSTOM_COMMAND\n",
                    "commands": "compiler /DREPLAY=1 runtime.cpp"}[operation]
        return SimpleNamespace(stdout=response)
    monkeypatch.setattr(subprocess, "run", run)
    report = sources.retain_sources_zip(root, tmp_path / "evidence", build)
    sources.verify_retention(report)
    with zipfile.ZipFile(report["archive"]) as archive:
        assert archive.read("build/Generated.hpp") == header.read_bytes()
        assert archive.read("fetched/dependency.hpp") == dependency.read_bytes()
        assert "build/runtime.obj" not in archive.namelist()
        manifest = json.loads(archive.read("manifest.json"))
        assert manifest["build"]["commands"] == "compiler /DREPLAY=1 runtime.cpp"
        assert manifest["build"]["non_file_nodes"] == {"utility": "CUSTOM_COMMAND"}
        retained = next(row for row in manifest["files"] if row["path"].endswith("custom-sdk/custom.hpp"))
        assert archive.read(retained["path"]) == external.read_bytes()
