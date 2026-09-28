import json
from pathlib import Path

import pytest

from tools.deterministic_qualification.artifacts import sha256_file
from tools.deterministic_qualification.correction_evidence import observed_correction, presentation_outcome, require_combat_boundary
from tools.deterministic_qualification.run_journal import StageJournal
from tools.deterministic_qualification.run_resources import RunResources




def test_actual_depth_and_interval_must_both_match():
    record = ("[HorseMod] online correction converged run_id=h confirmed=2:435 "
              "final=2:444 base=2:426 depth=10 batches=18 coordinates=18 total_us=1 plan_stage=4")
    with pytest.raises(RuntimeError, match="requested depth 11, observed depth 10"):
        observed_correction(record, "h", 11, 2)
    with pytest.raises(RuntimeError, match="interval"):
        observed_correction(record.replace("depth=10", "depth=11"), "h", 11, 2)
    with pytest.raises(RuntimeError, match="generation"):
        observed_correction(record, "h", 10, 3)


def test_intro_epoch_replacement_is_not_combat_round_two():
    intro = ("[HorseMod] online qualification run_id=h combat_phase generation=2 frame=390 "
             "world_mode=1 native_round=1 result=0 combat_round=0 completed_round=0 active=0 valid=1")
    with pytest.raises(RuntimeError, match="combat coverage"):
        require_combat_boundary(intro, "h", 2)
    combat = ("[HorseMod] online qualification run_id=h combat_phase generation=3 frame=5000 "
              "world_mode=2 native_round=2 result=0 combat_round=2 completed_round=1 active=1 valid=1")
    with pytest.raises(RuntimeError, match="preceding played result"):
        require_combat_boundary(intro + "\n" + combat, "h", 2)
    first = combat.replace("frame=5000", "frame=960").replace("native_round=2", "native_round=1").replace("combat_round=2", "combat_round=1").replace("completed_round=1", "completed_round=0")
    result = combat.replace("world_mode=2", "world_mode=3").replace("result=0", "result=2").replace("combat_round=2", "combat_round=1").replace("active=1", "active=0")
    assert require_combat_boundary("\n".join((intro, first, result, combat)), "h", 2)["completed"] == 1
    with pytest.raises(RuntimeError, match="observation"):
        require_combat_boundary(combat, "another-run", 2)


def test_presentation_reuse_and_discard_are_not_changed_publication():
    value = dict(replacement_events=4, reused_events=4, published_events=0,
        changed_published_events=0, payload_identity="abc", final_drain=True,
        discarded_events=0, observation_losses=0)
    row = {"presentation": {"host": value, "sandbox": dict(value)}}
    assert presentation_outcome(row) == "reused"
    value["discarded_events"] = 1
    row["presentation"]["sandbox"] = dict(value)
    with pytest.raises(RuntimeError, match="discarded"):
        presentation_outcome(row)


def test_interruption_requires_cleanup_and_reuses_only_sealed_completion(tmp_path):
    journal_path = tmp_path / "journal.json"
    report = tmp_path / "report.json"
    journal = StageJournal(journal_path)
    raw = tmp_path / "capture.log"
    raw.write_text("run-id=unique\n")
    def execute():
        report.write_text(json.dumps({"result": "pass", "raw_log_artifacts": {
            "host": {"path": str(raw), "sha256": sha256_file(raw)}}, "cleanup": {
            "requests_disarmed": True, "diagnostic_flags_false": True,
            "deployment_restored": True,
            "game_processes_remaining": 0}}))
    def interrupted():
        raise KeyboardInterrupt()
    def failed_cleanup():
        raise RuntimeError("owned process still running")
    with pytest.raises(RuntimeError, match="owned process"):
        journal.run("stage", {"id": 1}, {"runtime": "a"}, report, interrupted, failed_cleanup)
    journal = StageJournal(journal_path)
    with pytest.raises(RuntimeError, match="recovery"):
        journal.run("stage", {"id": 1}, {"runtime": "a"}, report, execute, lambda: None)
    journal.recover(lambda: None)
    assert journal.run("stage", {"id": 1}, {"runtime": "a"}, report, execute, lambda: None)
    assert not journal.run("stage", {"id": 1}, {"runtime": "a"}, report, interrupted, lambda: None)
    raw.write_text("stale capture")
    with pytest.raises(RuntimeError, match="stale"):
        journal.run("stage", {"id": 1}, {"runtime": "a"}, report, execute, lambda: None)


def test_owned_deployment_recovers_and_rejects_external_changes(tmp_path, monkeypatch):
    monkeypatch.setattr("tools.deterministic_qualification.process_control.list_game_processes", lambda: ())
    target = tmp_path / "HorseMod.dll"
    target.write_bytes(b"predecessor")
    replacement = tmp_path / "replacement"
    replacement.write_bytes(b"new runtime")
    resource_path = tmp_path / "resources.json"
    resources = RunResources(resource_path)
    resources.begin("run-42")
    resources.prepare_file(target, [sha256_file(replacement)])
    resources.prepare_requests(tmp_path)
    request = tmp_path / "online_request.txt"
    request.write_text("run_id=run-42-host\n")
    target.write_bytes(replacement.read_bytes())
    RunResources(resource_path).recover()
    assert target.read_bytes() == b"predecessor" and not request.exists()
    RunResources(resource_path).recover()  # Idempotent after completed cleanup.
    resources = RunResources(resource_path)
    resources.begin("run-43")
    resources.prepare_file(target, [sha256_file(replacement)])
    target.write_bytes(b"user changed deployment")
    with pytest.raises(RuntimeError, match="outside this run"):
        RunResources(resource_path).recover()
    assert target.read_bytes() == b"user changed deployment"


def test_partial_publication_and_legacy_recovery_share_installation_lease(tmp_path, monkeypatch):
    from tools.deterministic_qualification import run_resources
    monkeypatch.setattr("tools.deterministic_qualification.process_control.list_game_processes", lambda: ())
    installed = tmp_path / "HorseMod.dll"
    installed.write_bytes(b"installed")
    source = tmp_path / "candidate.dll"
    source.write_bytes(b"candidate complete bytes")
    canonical = run_resources.deployment_journal_path(installed)
    legacy = tmp_path / "old-report" / "resources.json"
    run = RunResources(legacy)
    run.begin("interrupted-copy")
    run.prepare_file(installed, [sha256_file(source)])
    original_copy = run_resources.shutil.copy2
    def interrupted_copy(source, target):
        target.write_bytes(b"partial")
        raise KeyboardInterrupt()
    monkeypatch.setattr(run_resources.shutil, "copy2", interrupted_copy)
    with pytest.raises(KeyboardInterrupt):
        run.publish_copy(source, installed)
    assert installed.read_bytes() == b"installed"
    temporary = Path(RunResources(legacy).document["temporaries"][0])
    assert temporary.read_bytes() == b"partial"
    monkeypatch.setattr(run_resources.shutil, "copy2", original_copy)
    stages = StageJournal(tmp_path / "stages.json")
    stages.document["stages"]["legacy"] = {"state": "running", "resource_journal": str(legacy)}
    stages.save()
    with run_resources.acquire_deployment_lock(canonical):
        with pytest.raises(RuntimeError, match="another runner"):
            stages.recover(lambda: None, deployment_journal=canonical)
    assert temporary.exists() and RunResources(legacy).document["state"] == "active"
    stages.recover(lambda: None, deployment_journal=canonical)
    assert not temporary.exists() and installed.read_bytes() == b"installed"
    assert stages.document["stages"]["legacy"]["state"] == "interrupted"

    resources = RunResources(canonical)
    resources.begin("report-owner")
    report = tmp_path / "replay_report.json"
    report.write_text('{"run_id":"prior-run","evidence":42}')
    predecessor = report.read_bytes()
    resources.prepare_requests(tmp_path)
    resources.park_report(report)
    assert not report.exists()  # The observer cannot see a stale report.
    report.write_text('{"run_id":"report-owner-replay-setup","state":"complete"}')
    partial_request = tmp_path / "replay_result.tmp"
    partial_request.write_bytes(b"version=1\nrun")
    RunResources(canonical).recover()
    assert report.read_bytes() == predecessor and not partial_request.exists()


@pytest.mark.parametrize("arrival", [5, 40])
def test_deferred_steam_launch_is_recovered_without_claiming_steam(tmp_path, monkeypatch, arrival):
    from types import SimpleNamespace
    from tools.deterministic_qualification import run_resources
    clock = [0.0]
    stopped = []
    game = tmp_path / "SoulcaliburVI.exe"
    steam = SimpleNamespace(pid=7, cmdline=lambda: ["steam.exe"])
    native = SimpleNamespace(pid=42, cmdline=lambda: [str(game), "-run=late"],
                             create_time=lambda: 100, exe=lambda: str(game))
    def processes():
        return [steam] + ([native] if clock[0] >= arrival and 42 not in stopped else [])
    def process(pid):
        if pid == 42 and native in processes():
            return native
        raise run_resources.psutil.NoSuchProcess(pid)
    monkeypatch.setattr(run_resources.psutil, "process_iter", processes)
    monkeypatch.setattr(run_resources.psutil, "Process", process)
    monkeypatch.setattr(run_resources.time, "time", lambda: 100)
    monkeypatch.setattr(run_resources.time, "monotonic", lambda: clock[0])
    monkeypatch.setattr(run_resources.time, "sleep", lambda seconds: clock.__setitem__(0, clock[0] + seconds))
    monkeypatch.setattr("tools.deterministic_qualification.process_control.list_game_processes", lambda: ())
    monkeypatch.setattr("tools.deterministic_qualification.process_control.stop_game_processes",
        lambda rows, **kwargs: stopped.extend(row.pid for row in rows))
    journal = tmp_path / "resources.json"
    target = tmp_path / "runtime.dll"
    target.write_bytes(b"installed")
    run = RunResources(journal)
    run.begin("late")
    run.prepare_file(target, [run_resources.bytes_hash("candidate")])
    target.write_bytes(b"candidate")
    run.prepare_process_launch(game, "-run=late", deferred=True)
    if arrival > 30:
        with pytest.raises(RuntimeError, match="launch remains unresolved"):
            run.recover()
        assert run.document["state"] == "cleanup_pending"
        assert target.read_bytes() == b"candidate"
        with pytest.raises(RuntimeError, match="prior run"):
            RunResources(journal).begin("another")
        clock[0] = arrival
    RunResources(journal).recover()
    assert stopped == [42]  # The existing Steam client is never owned.
    assert target.read_bytes() == b"installed"
    assert RunResources(journal).document["state"] == "clean"
