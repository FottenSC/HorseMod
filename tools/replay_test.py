"""Ordered local replay development gates; live runs own deployment and cleanup.

Only implemented gates are exposed. A captured trajectory is not a seek pass.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time

from deterministic_qualification.replay_timing import timed, span
from deterministic_qualification.replay_outcomes import producer
from deterministic_iteration import BUILD, GAME_ROOT, ROOT, _developer_command
from deterministic_qualification.artifacts import sha256_file
from deterministic_qualification.replay_reporting import reported_cli, publish, render, read_status, inspect_report, status_difference, compact_document
from deterministic_qualification.source_retention import retain_sources, workspace_fingerprint, verify_retention, require_build_provenance
from deterministic_qualification.replay_control import run_replay_control
from deterministic_qualification.replay_fidelity import bounded_checkpoint_window, ground_commit_case, trace_render_recovery_case, private_hud_drained_case, execution_fallback_case, execution_fallback_receipt, retained_source_range, indexed_seek_boundary_streams, validate_boundary_capture, expected_recovery_rewind, compare_passive_controls, validate_trajectory_timing, validate_world_resume_timing, validate_move_state_interval, validate_intro_skips


DEFAULT_REPLAY = ROOT / "ReplayExample" / "REPLAY_12744704008398858106.bin"
OUTPUT = BUILD / "replay-tests"
CAPTURE_REPORTS = []
ACTIVE_PROFILE = None
ACTIVE_STARTUP_LOADING = {}
ACTIVE_CAPTURE_SOURCES = None
ACTIVE_DIAGNOSTICS = None
PRODUCTION_REPLAY_MEMORY_LIMIT = 1024 * 1024 * 1024


def validate_consumer_suspension(raw):
    held=re.findall(r"consumer task held tick=(\d+) epoch=(\d+) owner_index=(\d+) owner_generation=(\d+) native_started=false recovery=false",raw)
    done=re.findall(r"consumer suspension complete tick=(\d+) epoch=(\d+) polls=(\d+) native_executions=1 application_complete=true recovery=false",raw)
    if (len(held)!=1 or len(done)!=1 or held[0][:2]!=done[0][:2]
            or int(done[0][2])<1 or int(held[0][3])<1 or "consumer resume rejected" in raw):
        raise RuntimeError("consumer suspension ownership/completion evidence missing")
    return {"result":"pass","tick":int(done[0][0]),"polls":int(done[0][2]),"recovery":False}


def current_identities(args):
    paths = {"runtime": args.dll, "observer": args.replay_mod, "framework": args.framework,
             "game": args.game_executable, "replay": args.replay}
    if getattr(args, "ucrt", None):
        paths["ucrt"] = args.ucrt
    if getattr(args, "physx", None):
        paths["physx"] = args.physx
    return {key: sha256_file(path) for key, path in paths.items()}


def build_binary_paths() -> dict:
    return {"runtime": BUILD / "HorseMod" / "HorseMod.dll",
            "observer": BUILD / "HorseMod" / "ReplayQualificationMod.dll",
            "framework": BUILD / "LessEqual421__Shipping__Win64" / "bin" / "UE4SS.dll",
            "core_test": BUILD / "HorseMod" / "DeterministicCoreSelfTest.exe",
            "native_test": BUILD / "HorseMod" / "NativeCandidateRegionsSelfTest.exe"}


def ensure_build(jobs: int) -> dict:
    try:
        proven = require_build_provenance(ROOT, BUILD, build_binary_paths())
    except (OSError, RuntimeError, KeyError, ValueError):
        proven = build(jobs)
    run_local_regressions(proven)
    print("build: reusing exact binaries with verified retained source inputs", flush=True)
    return proven


def run_local_regressions(proven: dict) -> dict:
    """Compilation/provenance alone cannot authorize a live launch."""
    native = BUILD / "HorseMod" / "NativeCandidateRegionsSelfTest.exe"
    physics = GAME_ROOT.parents[2] / "Engine/Binaries/ThirdParty/PhysX/Win64/VS2015/PhysX3_x64.dll"
    game = GAME_ROOT / "SoulcaliburVI.exe"
    commands = [["ctest", "--test-dir", str(BUILD), "--output-on-failure",
                 "-R", "^(DeterministicCoreSelfTest|NativeCandidateRegionsSelfTest)$"],
                [str(native), "--physics-markers", str(physics), str(game)]]
    outputs = []
    code = 0
    for command in commands:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        output = result.stdout + result.stderr
        outputs.append(output)
        print(output, end="", flush=True)
        code = result.returncode
        if code:
            break
    raw = "\n".join(outputs).encode("utf-8")
    log = OUTPUT / "local-regressions.log"
    log.write_bytes(raw)
    retained = OUTPUT / "source-archives" / ("local-regressions-" + sha256_file(log) + ".log")
    if not retained.exists():
        retained.write_bytes(raw)
    if retained.read_bytes() != raw:
        raise RuntimeError("local regression log retention mismatch")
    evidence = {"result": "fail" if code else "pass", "path": str(retained),
                "sha256": sha256_file(retained), "binaries": proven["binaries"],
                "source_retention": proven["source_retention"],
                "native_fixture_inputs": {"physics": sha256_file(physics), "game": sha256_file(game)},
                "game_launched": False}
    (OUTPUT / "local-regressions.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    if code:
        raise RuntimeError(f"local regressions failed ({code}); live launch prohibited; {retained}")
    return evidence


@timed('build')
def build(jobs: int) -> dict:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    log = OUTPUT / "build.log"
    before = workspace_fingerprint(ROOT)
    command = _developer_command(
        f'cmake --build {BUILD} --target UE4SS HorseMod ReplayQualificationMod '
        f'DeterministicCoreSelfTest NativeCandidateRegionsSelfTest -j {jobs}')
    print("build: HorseMod and replay observer", flush=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        from deterministic_qualification.replay_evidence import store_file
        evidence = store_file(OUTPUT / 'evidence' / 'objects', log)
        print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-40:]))
        raise RuntimeError(f"build failed; {evidence['path']}")
    if workspace_fingerprint(ROOT) != before:
        raise RuntimeError("production/test sources changed during build; binaries are not source-qualified")
    retained = retain_sources(ROOT, OUTPUT / "source-archives", BUILD)
    verify_retention(retained)
    if workspace_fingerprint(ROOT)!=before:
        raise RuntimeError('production/test sources changed while retaining the completed build')
    binaries = {name: {"path": str(path), "sha256": sha256_file(path)} for name, path in build_binary_paths().items()}
    retained_log = OUTPUT / "source-archives" / ("build-" + retained["manifest_sha256"] + ".log")
    if retained_log.exists() and retained_log.read_bytes() != log.read_bytes():
        retained_log = retained_log.with_name(retained_log.stem + "-" + sha256_file(log) + ".log")
    if not retained_log.exists(): retained_log.write_bytes(log.read_bytes())
    provenance = {"schema": 1, "source_retention": retained, "workspace_fingerprint": before,
                  "binaries": binaries, "build_log": {"path": str(retained_log), "sha256": sha256_file(retained_log)}}
    # The executable manifest is part of the retained build inputs. Capture
    # reports already carry this provenance; keep its policy identity explicit.
    from deterministic_qualification.replay_evidence import store_file
    policy = ROOT / "HorseMod/horselib/deterministic/ReplayStatePolicy.hpp"
    versions = re.findall(r'inline constexpr std::uint32_t version = ([0-9]+);', policy.read_text(encoding="utf-8"))
    if len(versions) != 1:
        raise RuntimeError("state-policy manifest has no unique version")
    provenance["state_policy"] = {"version": int(versions[0]), "manifest": store_file(OUTPUT / "evidence/objects", policy)}
    (OUTPUT / "build-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    return provenance


def control_args(replay: Path, mode: str, name: str, samples: int, timeout: float, full_match=False):
    deployed = GAME_ROOT / "ue4ss" / "Mods" / "HorseMod" / "dlls" / "main.dll"
    return argparse.Namespace(
        mode=mode, dll=BUILD / "HorseMod" / "HorseMod.dll", deployed_dll=deployed,
        replay_mod=BUILD / "HorseMod" / "ReplayQualificationMod.dll", replay=replay,
        framework=BUILD / "LessEqual421__Shipping__Win64" / "bin" / "UE4SS.dll",
        ucrt=Path(os.environ["SystemRoot"]) / "System32" / "ucrtbase.dll",
        game_executable=GAME_ROOT / "SoulcaliburVI.exe", log=GAME_ROOT / "ue4ss" / "UE4SS.log",
        config=deployed.with_name("rollback.ini"), watch_frames=samples, timeout=timeout, full_match=full_match,
        report=OUTPUT / f"{name}.json")


def apply_capture_startup_setup(args):
    # Stage-specific capture namespaces are fresh objects. Carry parsed CLI
    # setup into both sides before control lookup as well as native launch.
    # Named profiles still own their resolved values (including false).
    args.experiment_profile = ACTIVE_PROFILE
    profile_settings=(ACTIVE_PROFILE or {}).get('resolved_settings',{})
    for name in ('no_async_loading_thread','flush_startup_loading'):
        requested=bool(getattr(args,name,False) or ACTIVE_STARTUP_LOADING.get(name,False))
        setattr(args,name,bool(profile_settings.get(name,requested)))
    args.no_async_loading_thread |= bool(getattr(args,'no_native_threading',False) or getattr(args,'single_game_thread',False))


@timed('capture')
def capture(args) -> dict:
    CAPTURE_REPORTS.append(args.report)
    apply_capture_startup_setup(args)
    args.capture_source_retention = ACTIVE_CAPTURE_SOURCES
    args.diagnostic_settings = ACTIVE_DIAGNOSTICS
    print(f"{args.report.stem}: {args.watch_frames} observations, {args.mode}", flush=True)
    if run_replay_control(args):
        raise RuntimeError(f"capture failed; inspect {args.report}")
    return json.loads(args.report.read_text(encoding="utf-8"))


@timed('control.execution')
def independent_control(args, candidate=None, first_tick=None):
    from deterministic_qualification.replay_controls_catalog import control_spec, find_control, register_control
    apply_capture_startup_setup(args)
    args.diagnostic_settings = ACTIVE_DIAGNOSTICS
    spec = control_spec(args)
    from deterministic_qualification.replay_controls_catalog import startup_signature
    unchanged = not any(getattr(args, name, False) for name in ('changed_inputs', 'corrected_inputs', 'source_revision'))
    startup = startup_signature(candidate, first_tick) if candidate is not None and unchanged else None
    cached = find_control(OUTPUT / "controls", spec, startup)
    if cached is not None:
        # The retained catalog path is immutable; include it in stage evidence.
        from deterministic_qualification.replay_controls_catalog import key
        CAPTURE_REPORTS.append(Path(cached["catalog_manifest"]))
        print("reusing compatible independent stock control " + cached['run_id'], flush=True)
        return dict(cached, reuse=dict(reason="exact control-side identities, setup, protocol and coverage", key=key(spec)))
    report = capture(args)
    register_control(OUTPUT / "controls", report, spec, startup)
    return report


def validate_tick_index(native_text: str, candidate_text: str, native_id: str, candidate_id: str, *, index_seek=False, index_recovery="") -> dict:
    if native_id == candidate_id:
        raise RuntimeError("index qualification requires an independent native control")
    native_rows = validate_boundary_capture(native_text, native_id)
    supported=retained_source_range(candidate_text,candidate_id)
    complete = re.findall(r"index "+("supported complete" if supported else "complete")+r" run_id=(\S+) entries=(\d+) bytes=(\d+) intervals=(\d+) empty=(\d+) multi=(\d+) "+("native_finish=false unsupported_native_tail=true" if supported else "native_finish=true final_tail=true"), candidate_text)
    release = f"index released run_id={candidate_id} bytes=0 hooks_removed=true"
    start = re.findall(r"index started run_id=(\S+) entries=1 bytes=(\d+) native_tick=0", candidate_text)
    if len(complete)!=1 or complete[0][0]!=candidate_id or len(start)!=1 or start[0][0]!=candidate_id or candidate_text.count(release)!=1:
        raise RuntimeError("index lacks unique baseline, native completion or retirement")
    _, count, storage, intervals, empty, multi = complete[0]
    if not 2<=int(count)<=36001 or not 0<int(storage)<=4*1024*1024 or storage!=start[0][1]:
        raise RuntimeError("index has invalid bounds or changed storage reservation")
    endpoints=re.findall(r"native replay endpoint run_id="+re.escape(native_id)+r" native_frame=(\d+)",native_text)
    retained=re.findall(r"index retained coverage source_tick=(\d+) last_tick=(\d+) tail_ticks=(\d+) entries=(\d+) all_native_ticks_indexed=true",candidate_text)
    if len(endpoints)!=1:
        raise RuntimeError("index lacks independent native source endpoint")
    source_end=int(endpoints[0])
    native_source_end=source_end
    if supported:
        source_end=supported["source_tick"]
        if not source_end<int(count)-1<native_source_end:
            raise RuntimeError("supported retained range is not covered by independent native execution")
    elif retained:
        if len(retained)!=1 or tuple(map(int,retained[0]))!=(source_end,int(count)-1,int(count)-1-source_end,int(count)) or int(count)<=source_end+1:
            raise RuntimeError("retained index coverage disagrees with its source and complete range")
        published=re.findall(r"index retained range run_id="+re.escape(candidate_id)+r" source_tick=(\d+) last_tick=(\d+) tail_ticks=(\d+) all_native_ticks_indexed=true",candidate_text)
        if published!=[retained[0][:3]]:
            raise RuntimeError("retained range publication disagrees with host coverage")
    elif int(count)!=source_end+1:
        raise RuntimeError("legacy index does not cover the independent full native endpoint")
    closures=[]
    for text,identity in ((native_text,native_id),(candidate_text,candidate_id)):
        expected_end=native_source_end if identity==native_id else source_end
        closure=re.findall(r"index closure run_id="+re.escape(identity)+r" replay_tick=(\d+) native_tick=(\d+) callbacks_retained=true",text)
        frozen=f"index terminal observed run_id={identity} replay_tick={expected_end} frozen=true"
        if len(closure)!=1 or int(closure[0][0])!=expected_end or not 0<=int(closure[0][1])-int(closure[0][0])<=300 or text.count(frozen)!=1:
            raise RuntimeError("index lacks its frozen endpoint and complete closure accounting")
        closures.append(tuple(map(int,closure[0])))
    # BattleScene polls completion through a0.1-second one-shot timer. Its
    # notification is not a fixed simulation offset. Validate both physical
    # suffixes independently. The retained-range protocol indexes every
    # candidate traversal through its held boundary; source completion remains
    # separate and startup-dependent notification timing is not normalized.
    candidate_rows=(indexed_seek_boundary_streams(candidate_text,candidate_id,recovery=index_recovery)[1] if index_seek
                    else validate_boundary_capture(candidate_text,candidate_id))
    for boundary_rows,closure in zip((native_rows,candidate_rows),closures):
        closure_frames={int(row["frame"]) for row in boundary_rows if int(row["frame"])>closure[0]}
        if closure_frames!=set(range(closure[0]+1,closure[1]+1)):
            raise RuntimeError("post-replay native callbacks were dropped")
    rows=[dict(re.findall(r"(\w+)=([^\s]+)",line)) for line in candidate_text.splitlines() if "[ReplayQualification] index row " in line]
    if len(rows)!=int(count) or candidate_text.index("index supported complete run_id=" if supported else "index complete run_id=")>=candidate_text.index(release):
        raise RuntimeError("index row count or retirement order disagrees")
    final=re.findall(r"replay index final phase=2 entries=(\d+) intervals=(\d+) empty=(\d+) multi=(\d+) bytes=(\d+) native_finish=true final_tail=true",candidate_text)
    if supported:
        final=re.findall(r"retained source stop complete source_tick=\d+ last_tick=\d+ entries=(\d+) intervals=(\d+) empty=(\d+) multi=(\d+) bytes=(\d+) native_finish=false application_idle=true bindings_valid=true unsupported_native_tail=true",candidate_text)
    if final!=[(count,intervals,empty,multi,storage)]:
        raise RuntimeError("index lacks matching host application-tail completion")
    host_closure=re.findall(r"\[HorseMod\] index closure replay_tick=(\d+) executor_tick=(\d+) application_idle=true",candidate_text)
    if supported:
        host_closure=re.findall(r"retained source stop complete source_tick=(\d+) last_tick=(\d+) entries=\d+ intervals=\d+ empty=\d+ multi=\d+ bytes=\d+ native_finish=false application_idle=true bindings_valid=true unsupported_native_tail=true",candidate_text)
    if len(host_closure)!=1 or int(host_closure[0][0])!=source_end or not source_end<=int(host_closure[0][1])<=closures[1][1]:
        raise RuntimeError("index host completion lies outside the observed native closure")
    if (retained or supported) and (int(host_closure[0][1])!=int(count)-1 or closures[1][1]!=int(count)-1):
        raise RuntimeError("retained index omitted native closure ticks")
    if rows[0].get("interval")!="0" or rows[0].get("publications")!="0" or rows[0].get("phase")!="0":
        raise RuntimeError("index baseline is not the initial idle executor")
    nonempty={}
    for row in rows[1:]:
        interval=int(row["interval"])
        if not 1<=interval<=int(intervals): raise RuntimeError("indexed interval outside completed native range")
        nonempty[interval]=nonempty.get(interval,0)+1
    if int(empty)!=int(intervals)-len(nonempty) or int(multi)!=sum(n>1 for n in nonempty.values()):
        raise RuntimeError("index interval coverage counters disagree with traversals")
    native={}
    for row in native_rows:
        if row["phase"] in ("round_sequence","callback_a30"):
            native.setdefault((int(row["frame"]),row["phase"]),set()).add(tuple(row[k] for k in ("round","round_frame","cursor","round_state","source_active","move_state")))
    repeated=0
    previous=None
    for tick,row in enumerate(rows):
        if int(row["tick"])!=tick or int(row["native_tick"])!=tick:
            raise RuntimeError(f"index missing or duplicated tick{tick}")
        if tick:
            phase={9:"round_sequence",5:"callback_a30",16:"callback_a30"}.get(int(row["phase"]))
            expected=tuple(row[k] for k in ("round","round_tick","cursor","round_state"))+(str(bool(int(row["source_active"]))).lower(),row["move_state"])
            observed=native.get((tick,phase),set())
            if observed!={expected}:
                raise RuntimeError(f"index/native boundary mismatch tick{tick} phase={phase} expected={expected} observed={sorted(observed)}")
            if int(row["interval"])<int(previous["interval"]) or int(row["publications"])<int(previous["publications"]):
                raise RuntimeError(f"index scheduling coordinates regressed at tick{tick}")
            repeated+=row["publications"]==previous["publications"] and row["interval"]==previous["interval"]
        previous=row
    return {"result":"pass","entries":int(count),"last_tick":int(count)-1,"source_end_tick":source_end,
            "full_native_replay":not bool(supported),"unsupported_tail":({"first_tick":int(count),"through_native_source_endpoint":native_source_end,"later_native_UI_tail":"unsupported"} if supported else None),
            "native_tail_indexed":bool(retained),"native_tail_entries":int(count)-1-source_end if retained else 0,"bytes":int(storage),
            "completed_intervals":int(intervals),"zero_tick_intervals":int(empty),"multi_tick_intervals":int(multi),
            "same_publication_ticks":repeated,"native_boundary_comparisons":int(count)-1,
            "post_replay_ticks_observed":{"native":closures[0][1]-closures[0][0],"executor":closures[1][1]-closures[1][0]},
            "post_replay_coordinates_equal":closures[0]==closures[1],
            "full_execution_equivalence_complete":False,
            "index_complete":True,"checkpoint_retention_complete":False,"arbitrary_seek_complete":False}




def replay_memory_limit(text: str, identity: str) -> int:
    rows=re.findall(r"replay memory budget run_id=(\S+) bytes=(\d+) diagnostic=(\w+) native_tick=(\d+)",text)
    if not rows:return PRODUCTION_REPLAY_MEMORY_LIMIT
    if rows!=[(identity,str(2*PRODUCTION_REPLAY_MEMORY_LIMIT),"true","0")]:
        raise RuntimeError("invalid or repeated replay debug-budget admission receipt")
    return 2*PRODUCTION_REPLAY_MEMORY_LIMIT


def indexed_seek_costs(text: str, identity: str, target: int, *, budget_evidence: str | None = None) -> dict:
    limit=replay_memory_limit(text if budget_evidence is None else budget_evidence,identity)
    ready=re.findall(r"indexed seek ready run_id=(\S+) target=(\d+) elapsed_us=(\d+) owned_bytes=(\d+) diagnostic_gpu_readbacks=false",text)
    rate=re.findall(r"indexed seek resume rate run_id=(\S+) target=(\d+) ticks=120 elapsed_us=(\d+)",text)
    if (len(ready)!=1 or len(rate)!=1 or ready[0][:2]!=rate[0][:2] or ready[0][0]!=identity
            or int(ready[0][1])!=target or min(int(ready[0][2]),int(rate[0][2]))<=0
            or not 0<int(ready[0][3])<=limit):
        raise RuntimeError("indexed seek lacks bounded request-to-ready memory/timing evidence")
    tps=120_000_000/int(rate[0][2])
    origins=re.findall(r"indexed seek resume rate run_id="+re.escape(identity)+r" target=\d+ ticks=120 elapsed_us=\d+ origin=(\d+)",text)
    resume_origin=int(origins[0]) if len(origins)==1 else target
    control_resume=re.findall(r"playback controls resumed run_id="+re.escape(identity)+r" tick=(\d+) held_us=\d+ frames=\d+ unchanged=true application_complete=true",text)
    if control_resume and (len(control_resume)!=1 or len(origins)!=1 or resume_origin!=int(control_resume[0])):
        raise RuntimeError("indexed resume pacing includes or misidentifies the deliberate control hold")
    application=re.findall(r"indexed seek application cost run_id=(\S+) target=(\d+) preparation_us=(\d+) prefix_us=(\d+) engine_us=(\d+) tail_us=(\d+) frame_sync_us=(\d+) includes_observers=true",text)
    completion=re.findall(r"indexed seek completion cost run_id=(\S+) target=(\d+) request_to_completed_us=(\d+) validation_hold_us=(\d+) completion_us=(\d+) application_idle=true pending_task=false release_completed=true diagnostic_gpu_readbacks=false",text)
    if(len(application)!=1 or len(completion)!=1 or application[0][:2]!=ready[0][:2] or completion[0][:2]!=ready[0][:2]):
        raise RuntimeError("indexed seek lacks completed application/retirement cost evidence")
    full,dwell,tail=map(int,completion[0][2:]);prep,prefix,engine,app_tail,sync=map(int,application[0][2:])
    if(full<=int(ready[0][2]) or dwell<500000 or tail<=0 or full-dwell<int(ready[0][2])
            or abs(full-int(ready[0][2])-dwell-tail)>10000 or sync>app_tail or prep>int(ready[0][2])):
        raise RuntimeError("indexed seek completion timing phases disagree")
    return {"request_to_ready_us":int(ready[0][2]),"ceiling_us":500000,
            "request_to_completed_us":full,"validation_hold_us":dwell,"completed_work_excluding_validation_hold_us":full-dwell,
            "preparation_us":prep,"advance_prefix_us":prefix,"engine_us":engine,"application_tail_us":app_tail,
            "frame_sync_us":sync,"completion_and_retirement_us":tail,"includes_observers":True,
            "seek_latency_result":"pass" if full-dwell<=500000 else "fail",
            "owned_bytes":int(ready[0][3]),"diagnostic_gpu_readbacks":False,
            "resume_ticks":120,"resume_origin_tick":resume_origin,"resume_elapsed_us":int(rate[0][2]),"resume_tps":tps,
            "resume_rate_result":"pass" if tps>=58 else "fail",
            "practical_rollback_qualified":False,"memory_limit_bytes":limit,
            "diagnostic_budget":limit>PRODUCTION_REPLAY_MEMORY_LIMIT,"configured_production_budget":limit==PRODUCTION_REPLAY_MEMORY_LIMIT,"production_budget_qualified":False,
            "peak_ownership_result":"not_measured","owned_bytes_scope":"ready-point accounted ownership; not full native allocation high-water"}


def compare_indexed_hud(native_text: str, candidate_text: str, identity: str, target: int, *, window_evidence: str | None = None) -> dict:
    marker="[ReplayQualification] HUD observation window "
    if marker not in native_text and marker not in candidate_text:
        return {"result":"not_measured","scope":"legacy observer has no target HUD window"}
    expected_window=f"HUD observation window target={target} first={target-16} last={target+120} read_only=true"
    if expected_window not in native_text or expected_window not in (window_evidence if window_evidence is not None else candidate_text):
        raise RuntimeError("indexed HUD observation windows differ or are missing")
    resume=f"indexed seek resumed run_id={identity} tick={target} target_tails_complete=true"
    if candidate_text.count(resume)!=1:raise RuntimeError("indexed HUD lacks unique resumed boundary")
    fields={"tick","p1","p2","combo1","combo2","timer","damage","types","pool","active","type_pool","type_active","type_players","read_only"}
    def rows(text):
        result={}
        for line in text.splitlines():
            if "[ReplayQualification] HUD state tick=" not in line:continue
            row=dict(re.findall(r"(\w+)=([^\s]+)",line));tick=int(row["tick"])
            if not target<tick<=target+120:continue
            if not fields<=row.keys() or row["read_only"]!="true":raise RuntimeError("indexed HUD record is incomplete")
            if tick in result:raise RuntimeError("indexed HUD tick is duplicated")
            result[tick]={key:row[key] for key in fields}
        if set(result)!=set(range(target+1,target+121)):raise RuntimeError("indexed HUD lacks 120 independent tick observations")
        return result
    expected=rows(native_text);actual=rows(candidate_text.split(resume,1)[1])
    for tick in expected:
        if expected[tick]!=actual[tick]:raise RuntimeError(f"indexed HUD first mismatch at tick {tick}")
    return {"result":"pass","ticks":120,"active_type_ticks":sum(int(row["type_active"])>0 for row in expected.values()),
            "scope":"independent native HUD fields, pool counts and active TypeEff logical clocks; visual coherence and pixels are separate"}


def validate_automatic_end_hold(text: str, run: str, *, require_indexed_tail=False) -> dict:
    supported=retained_source_range(text,run)
    if supported:
        armed=list(re.finditer(r"retained source stop armed source_tick=(\d+) target=(\d+) native_finish=false unsupported_native_tail=true",text))
        held=list(re.finditer(r"retained source stop complete source_tick=(\d+) last_tick=(\d+) .*?unsupported_native_tail=true",text))
        begin=re.search(r"indexed seek begin run_id="+re.escape(run)+r" origin=(\d+) target=(\d+)",text)
        if (len(armed)!=1 or len(held)!=1 or not begin or armed[0].groups()!=held[0].groups()
            or int(begin[1])!=supported["last_tick"] or not armed[0].start()<held[0].start()<begin.start()
            or re.search(r"\[ReplayQualification\] boundary ordinal=",text[held[0].end():begin.start()])):
            raise RuntimeError("retained source-stop hold lacks ordered native ownership evidence")
        return {"result":"pass",**supported,"scope":"retained source-stop boundary; native victory/finish tail explicitly unsupported"}
    armed=list(re.finditer(r"replay end hold armed replay_tick=(\d+) native_target=(\d+) native_finish_returned=true application_tail_complete=true",text))
    held=list(re.finditer(r"replay end retained replay_tick=(\d+) native_tick=(\d+) completed_application=true native_bindings_valid=true surface_retained=true",text))
    begin=re.search(r"indexed seek begin run_id="+re.escape(run)+r" origin=(\d+) target=(\d+) full_index=true",text)
    if len(armed)!=1 or len(held)!=1 or not begin:
        raise RuntimeError("automatic end hold lacks unique native ownership evidence")
    a,h=armed[0],held[0]
    if a.groups()!=h.groups() or int(h[2])!=int(begin[1]) or int(h[2])<=int(h[1]) or not a.start()<h.start()<begin.start():
        raise RuntimeError("automatic end hold coordinates or publication order disagree")
    if re.search(r"\[ReplayQualification\] boundary ordinal=",text[h.end():begin.start()]):
        raise RuntimeError("gameplay callbacks advanced during the retained end hold")
    coverage=re.findall(r"index retained coverage source_tick=(\d+) last_tick=(\d+) tail_ticks=(\d+) entries=(\d+) all_native_ticks_indexed=true",text)
    if (require_indexed_tail or coverage) and coverage!=[(h[1],h[2],str(int(h[2])-int(h[1])),str(int(h[2])+1))]:
        raise RuntimeError("automatic end hold lacks complete indexed native tail")
    return {"result":"pass","last_replay_tick":int(h[1]),"held_native_tick":int(h[2]),
            "native_tail_ticks":int(h[2])-int(h[1]),"native_tail_indexed":bool(coverage),
            "scope":"completed native finish/application/display ownership; full lifecycle and visual coherence separate"}


def compare_indexed_seek_suffix(native_path: Path,candidate_path: Path) -> dict:
    reports=[json.loads(path.read_text()) for path in (native_path,candidate_path)]
    texts=[]
    for report in reports:
        path=Path(report["raw_log"]["path"])
        if sha256_file(path)!=report["raw_log"]["sha256"]:raise RuntimeError("indexed seek raw evidence changed")
        texts.append(path.read_text(encoding="utf-8",errors="replace"))
    admitted_budget=replay_memory_limit(texts[1],reports[1]["run_id"])
    if admitted_budget!=reports[1].get("replay_budget_gib",1)*PRODUCTION_REPLAY_MEMORY_LIMIT:
        raise RuntimeError("requested replay budget differs from native admission")
    end_protocol=reports[1].get("index_end_hold_protocol")
    if end_protocol not in (None,"native_finish_completed_application_v1","retained_native_range_v2","retained_source_stop_v3"):
        raise RuntimeError("unsupported retained-end protocol")
    end_hold=(validate_automatic_end_hold(texts[1],reports[1]["run_id"],require_indexed_tail=end_protocol=="retained_native_range_v2")
        if end_protocol in ("native_finish_completed_application_v1","retained_native_range_v2","retained_source_stop_v3")
        else {"result":"not_exercised","scope":"legacy caller-owned endpoint pause"})
    _,_,suffix,origin=indexed_seek_boundary_streams(texts[1],reports[1]["run_id"])
    native=validate_boundary_capture(texts[0],reports[0]["run_id"])
    if origin.get("sequence"):
        expected_sequence=reports[1].get("index_sequence")
        if (not reports[0].get("index_sequence") or expected_sequence not in ("early","late")
                or origin['target']!=(208 if expected_sequence=="early" else 5574)
                or origin['target']!=reports[1].get('index_seek_target')):
            raise RuntimeError("indexed sequence control/request lacks matching observation windows")
        all_rows=indexed_seek_boundary_streams(texts[1],reports[1]['run_id'])[0]
        compared=compare_execution_callback_segments(native,all_rows,max(int(row['frame']) for row in all_rows))
        poses=compare_native_poses(texts,reports,origin['target'],"")
        cases=[]
        for case in origin['cases']:
            part=texts[1][case['start_offset']:case['end_offset']]
            costs=indexed_seek_costs(part,reports[1]['run_id'],case['target'],budget_evidence=texts[1])
            hud=compare_indexed_hud(texts[0],part,reports[1]['run_id'],case['target'],window_evidence=texts[1])
            cases.append({**case,'costs':costs,'native_hud':hud,'gameplay_result':'pass'})
        return {'result':'fail' if any(c['costs']['resume_rate_result']=='fail' for c in cases) else 'pass',
                'scope':'all sequence callbacks including bridge ticks; independent poses/HUD and resume pacing; latency separate',
                'diagnostic_budget':admitted_budget>PRODUCTION_REPLAY_MEMORY_LIMIT,'configured_production_budget':admitted_budget==PRODUCTION_REPLAY_MEMORY_LIMIT,'production_budget_qualified':False,
                'peak_ownership_result':'not_measured',
                'cases':cases,'callbacks_compared':compared,'native_poses':poses,'end_hold':end_hold,
                'full_arbitrary_seek_complete':False,'cancellation_sequence_qualified':False,'pixel_equality_required':False}
    # Locate by completed intervals, not a tick filter that could steal the next
    # interval's input publication at its preceding tick.
    def interval_slice(rows,start,end):
        begins=[i+1 for i,r in enumerate(rows) if r["phase"]=="actor_tail" and int(r["frame"])==start]
        ends=[i+1 for i,r in enumerate(rows) if r["phase"]=="actor_tail" and int(r["frame"])==end]
        if len(begins)!=1 or len(ends)!=1 or begins[0]>=ends[0]:raise RuntimeError("indexed suffix lacks exact application boundaries")
        return rows[begins[0]:ends[0]]
    target=origin["target"];end=origin["last_tick"];count=origin["continuation_ticks"]
    if target!=reports[1].get("index_seek_target",208) or count!=reports[1].get("index_seek_continuation",120):raise RuntimeError("indexed seek window differs from request")
    expected=interval_slice(native,target,end)
    if target==origin["checkpoint"]:
        # A completed checkpoint is already the start boundary. Its restored
        # stream must begin with the next input publication, not a fabricated
        # actor-tail observation for a traversal that never ran.
        if not suffix or suffix[0]["phase"]!="input_cache_publication" or int(suffix[0]["frame"])!=target:
            raise RuntimeError("checkpoint target suffix lacks its first native input publication")
        observed=suffix
    else:
        observed=interval_slice(suffix,target,end)
    if len(expected)!=len(observed):raise RuntimeError("indexed suffix callback counts differ")
    for ordinal,(a,b) in enumerate(zip(expected,observed),1):
        fields=(a.keys()|b.keys())-{"ordinal"}
        difference=[k for k in fields if a.get(k)!=b.get(k)]
        if difference:raise RuntimeError(f"indexed suffix first callback mismatch {ordinal}, tick{b.get('frame')}: {difference}")
    # A round reset can hide an earlier restoration defect. Compare every
    # executed callback from the checkpoint, including the approach to target.
    complete_expected=interval_slice(native,origin["checkpoint"],end)
    if len(complete_expected)!=len(suffix):
        raise RuntimeError("indexed resimulation callback counts differ")
    for ordinal,(a,b) in enumerate(zip(complete_expected,suffix),1):
        difference=sorted(k for k in (a.keys()|b.keys())-{"ordinal"} if a.get(k)!=b.get(k))
        if difference:
            raise RuntimeError(f"indexed resimulation first callback mismatch {ordinal}, tick{b.get('frame')}, phase={b.get('phase')}: {difference}")
    poses=compare_native_poses(texts,reports,target,"")
    if poses["result"]!="pass":raise RuntimeError(f"indexed suffix pose mismatch: {poses}")
    costs=indexed_seek_costs(texts[1],reports[1]["run_id"],target) if "index_seek_target" in reports[1] else {"result":"not_measured"}
    hud=compare_indexed_hud(texts[0],texts[1],reports[1]["run_id"],target)
    return {"result":"fail" if costs.get("resume_rate_result")=="fail" else "pass",
            "scope":"native gameplay/pose continuation and measured resume pacing; seek latency reported separately",
            "gameplay_result":"pass","resume_pacing_result":costs.get("resume_rate_result","not_measured"),
            "target":target,"continuation_ticks":count,"callbacks_compared":len(expected),"costs":costs,
            "complete_resimulation":{"result":"pass","checkpoint":origin["checkpoint"],"last_tick":end,
                                      "callbacks_compared":len(complete_expected),"expected_state_installed":False},
            "native_poses":poses,"native_hud":hud,"origin":origin,"end_hold":end_hold,"full_arbitrary_seek_complete":False,"pixel_equality_required":False}


def compare_indexed_recovery(native_path: Path, candidate_path: Path) -> dict:
    reports=[json.loads(path.read_text(encoding="utf-8")) for path in (native_path,candidate_path)]
    texts=[]
    for report in reports:
        path=Path(report["raw_log"]["path"])
        if sha256_file(path)!=report["raw_log"]["sha256"]:raise RuntimeError("indexed recovery raw evidence changed")
        texts.append(path.read_text(encoding="utf-8",errors="replace"))
    _,_,discarded,metadata=indexed_seek_boundary_streams(texts[1],reports[1]["run_id"],recovery=reports[1]["index_recovery"])
    native=validate_boundary_capture(texts[0],reports[0]["run_id"])
    compared=0
    if discarded:
        start=[i+1 for i,row in enumerate(native) if row["phase"]=="actor_tail" and int(row["frame"])==metadata["checkpoint"]]
        end=[i+1 for i,row in enumerate(native) if row["phase"]=="actor_tail" and int(row["frame"])==metadata.get("discarded_end_tick",metadata["target"])]
        if len(start)!=1 or len(end)!=1 or start[0]>=end[0]:raise RuntimeError("recovery control lacks exact A/C boundaries")
        expected=native[start[0]:end[0]]
        if len(expected)!=len(discarded):raise RuntimeError("discarded recovery execution callback count differs")
        for ordinal,(a,b) in enumerate(zip(expected,discarded),1):
            fields=(a.keys()|b.keys())-{"ordinal"}
            difference=[key for key in fields if a.get(key)!=b.get(key)]
            if difference:raise RuntimeError(f"discarded recovery execution first mismatch {ordinal}, tick{b.get('frame')}: {difference}")
        compared=len(expected)
    if reports[1]["index_recovery"]=="interior":
        for index,expected_count in ((0,1),(1,2)):
            interventions=re.findall(r"completion repeat intervention run_id=(\S+) tick=208 ordinal=(\d+) native_repeat=1",texts[index])
            if interventions!=[(reports[index]["run_id"],str(i)) for i in range(1,expected_count+1)]:
                raise RuntimeError("indexed interior recovery lacks the identical native repeat intervention")
    menu_exit={"result":"not_exercised"}
    if reports[1].get('indexed_control_protocol')=='retained_menu_exit_v1':
        from deterministic_qualification.replay_controls import validate_indexed_menu_exit
        menu_exit=validate_indexed_menu_exit(reports[1],texts[1])
    return {**metadata["recovery"],"discarded_callbacks_independently_compared":compared,
            "native_menu_exit":menu_exit,"B_authored_continuation_ticks":0,"full_arbitrary_seek_complete":False}


def validate_indexed_same_process_hud(report: dict) -> dict:
    path=Path(report["raw_log"]["path"])
    if sha256_file(path)!=report["raw_log"]["sha256"]:raise RuntimeError("indexed HUD raw evidence changed")
    text=path.read_text(encoding="utf-8",errors="replace")
    marker=f"indexed seek begin run_id={report['run_id']} "
    if report.get("index_sequence"):
        from deterministic_qualification.indexed_sequence import sequence_cases
        cases=sequence_cases(text,report['run_id']);prefix=text[:cases[0]['start_offset']]
        results=[compare_indexed_hud(prefix,text[c['start_offset']:c['end_offset']],report['run_id'],c['target'],window_evidence=text) for c in cases]
        return {'result':'pass','cases':results,'scope':'same-process HUD only; independent continuation remains required'}
    if text.count(marker)!=1:raise RuntimeError("indexed HUD lacks unique original/restore separation")
    result=compare_indexed_hud(text.split(marker,1)[0],text,report["run_id"],report["index_seek_target"])
    return {**result,"comparison":"same-process original/restored; independent control still required"}


def validate_index_preparation_fallback(raw: str, identity: str, require_cancellation: bool = False) -> dict:
    cancelled=re.findall(r"indexed preparation retry cancellation run_id=(\S+) checkpoint=(\d+) B=(\d+) elapsed_us=(\d+) unchanged=true callbacks_unchanged=true epoch_unchanged=true fallback_started=false native_released=true",raw)
    renewed=re.findall(r"indexed preparation retry renewed run_id=(\S+) target=(\d+) B=(\d+) new_request_clock=true",raw)
    if cancelled or renewed or require_cancellation:
        if len(cancelled)!=1 or len(renewed)!=1 or cancelled[0][0]!=identity or renewed[0][0]!=identity:
            raise RuntimeError("retry cancellation lacks unique completed recovery and renewal")
        marker=f"indexed preparation retry renewed run_id={identity} "
        before,after=raw.split(marker,1)
        observations=re.findall(r"index_preparation_B_(before|after) ordinal=1 phase=engine_post (sample_version=[^\r\n]+)",before)
        first_fault=re.findall(r"seek preparation fault checkpoint=(\d+) B=(\d+) complete_B=true target_prepared=true publication=false",before)
        cp,original,elapsed=map(int,cancelled[0][1:])
        if len(observations)!=2 or [row[0] for row in observations]!=["before","after"] or observations[0][1]!=observations[1][1]:
            raise RuntimeError("retry cancellation B observations differ or are missing")
        if (first_fault!=[(str(cp),str(original))] or int(renewed[0][2])!=original or elapsed<=0
                or "seek preparation fallback rejected=" in before
                or not before.index("index_preparation_B_before ordinal=")<before.index("seek preparation fault checkpoint=")
                    <before.index("index_preparation_B_after ordinal=")<before.index("indexed preparation retry cancellation")):
            raise RuntimeError("retry cancellation did not preserve B before a fresh request")
        result=validate_index_preparation_fallback(after,identity)
        if result["rejected_checkpoint"]!=cp or result["original_tick"]!=original:
            raise RuntimeError("renewed fallback used a different B or checkpoint")
        result["retry_cancellation"]={"result":"pass","elapsed_us":elapsed,"B_observations_match":True,
            "scope":"unchanged native B before publication; no post-cancellation B continuation was run"}
        return result
    faults=re.findall(r"seek preparation fault checkpoint=(\d+) B=(\d+) complete_B=true target_prepared=true publication=false",raw)
    switches=re.findall(r"seek preparation fallback rejected=(\d+) selected=(\d+) failure=(\d+) B_recovered=true preparation_released=true publication=false",raw)
    if len(faults)!=1 or len(switches)!=1:
        raise RuntimeError("preparation fallback lacks unique fault and recovered release")
    rejected,original=map(int,faults[0]);old,selected,code=map(int,switches[0])
    if rejected<=170 or old!=rejected or not 170<=selected<rejected or code==0 or original<=rejected:
        raise RuntimeError("preparation fallback coordinates or failure disagree")
    markers=[f"restore undo captured target={rejected} original={original} gpu_complete=true before_target_preparation=true",
             f"seek preparation fault checkpoint={rejected} B={original}",
             f"seek preparation fallback rejected={rejected} selected={selected}",
             f"restore undo captured target={selected} original={original} gpu_complete=true before_target_preparation=true",
             f"indexed checkpoint fallback run_id={identity} rejected={rejected} selected={selected} reason=retired_preparation B_recovered=true",
             f"indexed checkpoint selected run_id={identity} tick={selected} extra={rejected} automatic=true"]
    if any(raw.count(marker)!=1 for marker in markers) or [raw.index(m) for m in markers]!=sorted(raw.index(m) for m in markers):
        raise RuntimeError("preparation fallback lost B identity or retirement/publication order")
    return {"result":"pass","rejected_checkpoint":rejected,"selected_checkpoint":selected,"original_tick":original,
            "B_recovered_before_retry":True,"independent_continuation":"validated separately"}


def validate_managed_checkpoints(raw: str, identity: str, requested: int, preparation_fallback: bool) -> dict:
    captures=re.findall(r"index checkpoint retained run_id=(\S+) tick=(\d+) entries=(\d+) elapsed_us=(\d+) owned_bytes=(\d+) callbacks_unchanged=true application_idle=true",raw)
    if not 3<=len(captures)<=16 or any(row[0]!=identity for row in captures):
        raise RuntimeError("managed index lacks a bounded capture inventory")
    ticks=[int(row[1]) for row in captures]
    if ticks[0]!=170 or ticks!=sorted(set(ticks)) or (requested and not requested<=ticks[1]<=requested+240):
        raise RuntimeError("managed index placement coordinates disagree")
    initial=re.findall(r"initial index checkpoint selected origin=169 target=170 native_phase_tick=(\d+) placement_owner=host",raw)
    if len(initial)!=1 or int(initial[0])<6:raise RuntimeError("initial managed placement evidence missing")
    complete=raw.index("index supported complete run_id=" if retained_source_range(raw,identity) else "index complete run_id=")
    release=f"index released run_id={identity} bytes=0 hooks_removed=true"
    if raw.count(release)!=1:raise RuntimeError("managed index lacks final release")
    skipped=list(re.finditer(r"optional index checkpoint skipped run_id=(\S+) tick=(\d+) capture=false retained=false callbacks_unchanged=true application_idle=true monitor_released=true",raw))
    if (len(skipped)!=raw.count("optional index checkpoint skipped run_id=")
            or len(skipped)!=raw.count("optional index checkpoint unsupported tick=")
            or len(skipped)+len(captures)>16):
        raise RuntimeError("optional checkpoint skip inventory is incomplete")
    unsupported=[]
    for receipt in skipped:
        tick=int(receipt[2])
        if receipt[1]!=identity or tick<=170 or tick in ticks or any(r['tick']==tick for r in unsupported):
            raise RuntimeError("optional checkpoint skip identity or capture contradicts receipt")
        selection=list(re.finditer(rf"(?:next-round|replacement) index checkpoint selected origin={tick-1} target={tick} native_phase_tick=(\d+) placement_owner=host",raw))
        validate_replacement_checkpoint_placement(raw,tick)
        markers=[f"host index checkpoint armed run_id={identity} origin={tick-1} target={tick}",
            f"host index checkpoint adopted run_id={identity} tick={tick} capture_retention_and_retirement=host observer=read_only",
            receipt[0],f"optional index checkpoint unsupported tick={tick} original_anchor_retained=true",
            f"host index checkpoint completed run_id={identity} tick={tick} pending=false cancelled=false"]
        if (len(selection)!=1 or int(selection[0][1])<6 or any(raw.count(m)!=1 for m in markers)
                or not selection[0].start()<raw.index(markers[0])
                or [raw.index(m) for m in markers]!=sorted(raw.index(m) for m in markers)
                or not raw.index(markers[-1])<complete):
            raise RuntimeError("optional checkpoint lacks stable hold and completed resume")
        unsupported.append(dict(tick=tick,reason='target_eligibility',captured=False,retained=False,resume_completed=True))
    peak=0;images=[];previous_completion=-1
    for ordinal,(row,tick) in enumerate(zip(captures,ticks),1):
        if int(row[2])!=tick+1 or int(row[3])<=0:raise RuntimeError("checkpoint capture coordinates/cost invalid")
        ownership=list(re.finditer(rf"index checkpoint owned tick={tick} retained=(\d+) owned_bytes=(\d+)",raw))
        if len(ownership)!=1:raise RuntimeError("checkpoint lacks unique ownership receipt")
        capture_position=ownership[0].start()
        retained_count=1+sum(raw.index(f"index checkpoint release witness tick={prior}")>capture_position for prior in ticks[:ordinal-1])
        if int(ownership[0][1])!=retained_count:raise RuntimeError("checkpoint simultaneous retained count disagrees with releases")
        owned=[ownership[0][2]]
        life=re.findall(rf"index checkpoint release witness tick={tick} current_tick=(\d+) vfx_owners_live=(true|false) target_shape_supported=true owned_bytes=(\d+)",raw)
        # A backward seek can release a later checkpoint at an earlier logical
        # tick. Lifetime follows capture/completion/native-retirement event
        # order below, not an ordering between rewound simulation coordinates.
        if len(owned)!=1 or len(life)!=1:
            raise RuntimeError("checkpoint lacks unique ownership and lifetime evidence")
        size=max(int(row[4]),int(owned[0]),int(life[0][2]))
        if not 0<size<=replay_memory_limit(raw,identity):raise RuntimeError("checkpoint exceeds memory budget")
        peak=max(peak,size)
        placement=validate_host_index_checkpoint(raw,identity,False,True,tick=tick,ordinal=ordinal if ordinal>1 else 2,retained_count=retained_count)
        begin=capture_position
        done=raw.index(f"host index checkpoint completed run_id={identity} tick={tick} pending=false cancelled=false")
        retiring=raw.index(f"index checkpoint release witness tick={tick}")
        retired=raw.find("seek checkpoint retired tick=",retiring)
        invalidation={"result":"not_observed", "restorability_proven":False}
        if retiring<complete:
            samples=list(re.finditer(rf"index checkpoint ownership sample checkpoint={tick} tick=(\d+) phase=3 last_valid=(\d+) first_invalid=(\d+) valid_samples=(\d+) deferred_samples=(\d+) failure=(\d+) retained=true restorability_unproven=true",raw))
            if len(samples)!=1:raise RuntimeError("early retirement lacks confirmed ownership invalidation")
            sample=samples[0];observed,last_valid,first_invalid,valid,deferred,failure=map(int,sample.groups())
            marker=f"invalid index checkpoint retirement tick={tick} observed_tick={observed} indexing=true native_publication=false"
            if (not tick<=first_invalid==observed or not failure
                    or (not tick<=last_valid<first_invalid if valid else last_valid!=0)
                    or int(life[0][0])!=observed or life[0][1]!="false" or raw.count(marker)!=1
                    or not done<sample.start()<retiring<raw.index(marker)<retired<complete):
                raise RuntimeError("early retirement invalidation/lease/native completion order disagrees")
            invalidation={"result":"observed", "last_valid_tick":last_valid if valid else None,"first_invalid_tick":first_invalid,
                          "valid_samples":valid,"deferred_samples":deferred,"failure":failure,
                          "exact_invalidating_instruction_known":False,"restorability_proven":False}
        elif not complete<retiring:
            raise RuntimeError("checkpoint retirement has no completed index")
        if not previous_completion<begin<done<retiring<retired<raw.index(release):
            raise RuntimeError("checkpoint capture/resume/retirement order disagrees")
        previous_completion=done
        images.append({'checkpoint_tick':tick,'capture_us':int(row[3]),'observed_peak_owned_bytes':size,'placement':placement,
                       'checkpoint_resources_retired':True,'ownership_invalidation':invalidation,'retained_count_at_capture':retained_count})
    parts=[raw]
    if "indexed sequence case run_id=" in raw:
        from deterministic_qualification.indexed_sequence import sequence_cases
        parts=[raw[c['start_offset']:c['end_offset']] for c in sequence_cases(raw,identity)]
    selections=[]
    for part in parts:
        selected=re.findall(r"indexed checkpoint selected run_id=(\S+) tick=(\d+) extra=(\d+) automatic=true",part)
        begin=re.findall(r"indexed seek begin run_id=(\S+) origin=(\d+) target=(\d+) supported_index=true host_checkpoint=true",part)
        if len(selected)!=1 or len(begin)!=1 or selected[0][0]!=identity or begin[0][0]!=identity:
            raise RuntimeError("managed index lacks unique automatic seek selection")
        chosen=int(selected[0][1]);target=int(begin[0][2]);origin=int(begin[0][1])
        eligible=[tick for tick in ticks if tick<=min(target,origin)]
        if not eligible or chosen not in eligible or int(selected[0][2]) not in (0,ticks[-1]):
            raise RuntimeError("selected checkpoint is not an admitted predecessor")
        fallback=chosen!=eligible[-1]
        recovery=None
        natural=list(re.finditer(r"indexed natural preparation fallback run_id=(\S+) selected=(\d+) attempts=(\d+) last_failure=(\d+) B=(\d+) automatic=true",part))
        if natural:
            if len(natural)!=1 or preparation_fallback or not fallback:
                raise RuntimeError("natural preparation fallback scope differs from selection")
            receipt=natural[0];attempts=int(receipt[3])
            hops=list(re.finditer(r"seek preparation fallback rejected=(\d+) selected=(\d+) failure=(\d+) B_recovered=true preparation_released=true publication=false",part))
            live=[image['checkpoint_tick'] for image in images if image['checkpoint_tick'] in eligible
                  and image['ownership_invalidation']['result']!='observed']
            if (receipt[1]!=identity or int(receipt[2])!=chosen or int(receipt[5])!=origin
                    or not 0<attempts<len(ticks) or len(hops)!=attempts or not live):
                raise RuntimeError("natural preparation fallback lacks complete bounded native chain")
            current=max(live);previous=-1;rejected=[]
            for hop in hops:
                old,new,code=map(int,hop.groups())
                earlier=[t for t in live if t<old]
                if old!=current or not earlier or new!=max(earlier) or not code:
                    raise RuntimeError("natural fallback skipped an unaccounted predecessor")
                capture=f"restore undo captured target={old} original={origin} gpu_complete=true before_target_preparation=true"
                # CPU preflight records the first failure itself; the outer
                # preparation driver preserves it rather than logging a second
                # render-preparation failure. Both precede A publication.
                failures=list(re.finditer(
                    rf"(?:restore preparation failed participant=\S+ code={code} target={old} original={origin} before_publication=true"
                    rf"|historical restore preflight participant=\S+ code={code} target={old} original={origin} bytes=\d+)",part))
                recovered=f"restore preparation recovery completed original={origin} observed={origin} ground_released=true render_released=true before_A_publication=true"
                if part.count(capture)!=1 or len(failures)!=1:
                    raise RuntimeError("natural fallback lacks original B capture and matching failure")
                restored=part.find(recovered,failures[0].end())
                next_capture=f"restore undo captured target={new} original={origin} gpu_complete=true before_target_preparation=true"
                next_position=part.find(next_capture,hop.end())
                if not previous<part.index(capture)<failures[0].start()<restored<hop.start()<next_position<receipt.start():
                    raise RuntimeError("natural fallback lacks ordered complete B recovery and fresh preparation")
                rejected.append(old);current=new;previous=hop.end()
            if current!=chosen or int(hops[-1][3])!=int(receipt[4]):
                raise RuntimeError("natural fallback result differs from native recovery chain")
            recovery=dict(attempts=attempts,rejected=rejected,selected=chosen,original=origin,complete_B_recovered=True)
        elif preparation_fallback:
            proof=validate_index_preparation_fallback(part,identity)
            if not fallback or proof['rejected_checkpoint']!=eligible[-1] or proof['selected_checkpoint']!=chosen:
                raise RuntimeError("preparation fallback differs from retained predecessor inventory")
        elif fallback:
            marker=f"indexed checkpoint fallback run_id={identity} rejected={eligible[-1]} selected={chosen} reason=retained_owner_lifetime"
            if part.count(marker)!=1:raise RuntimeError("earlier selection lacks lifetime rejection")
        selections.append(dict(target=target,origin=origin,selected_tick=chosen,automatic_fallback=fallback,
                               **({'preparation_recovery':recovery} if recovery else {})))
    return {'result':'pass','checkpoints':images,'unsupported_placements':unsupported,'checkpoint_tick':ticks[-1],
            'selected_tick':selections[0]['selected_tick'],'automatic_fallback':any(c['automatic_fallback'] for c in selections),
            'selections':selections,'observed_peak_owned_bytes':peak,
            'checkpoint_resources_retired':True,'full_checkpoint_coverage':False}


def validate_index_checkpoint_retention(raw: str, identity: str, extra: int = 0, preparation_fallback: bool = False) -> dict:
    capture=re.findall(r"index checkpoint retained run_id=(\S+) tick=170 entries=171 elapsed_us=(\d+) owned_bytes=(\d+) callbacks_unchanged=true application_idle=true",raw)
    owned=re.findall(r"index checkpoint owned tick=170 retained=1 owned_bytes=(\d+)",raw)
    lifetime=re.findall(r"index checkpoint release witness tick=170 current_tick=(\d+) vfx_owners_live=(true|false) target_shape_supported=true owned_bytes=(\d+)",raw)
    release=f"index released run_id={identity} bytes=0 hooks_removed=true"
    if len(capture)!=1 or capture[0][0]!=identity or len(owned)!=1 or len(lifetime)!=1 or raw.count(release)!=1:
        raise RuntimeError("index checkpoint lacks unique capture, ownership, lifetime or retirement evidence")
    peak=max(int(capture[0][2]),int(owned[0]),int(lifetime[0][2]))
    if not 0<peak<=replay_memory_limit(raw,identity) or int(lifetime[0][0])<=170:
        raise RuntimeError("index checkpoint has invalid memory or lifetime coordinates")
    begin=raw.index("index checkpoint owned tick=170")
    retained=raw.index("index checkpoint retained run_id=")
    scratch=f"index checkpoint capture retired run_id={identity} tick=170 immutable_owner_retained=true"
    if raw.count(scratch)!=1:raise RuntimeError("index capture operation was not retired before playback")
    complete=raw.index("index supported complete run_id=" if retained_source_range(raw,identity) else "index complete run_id=")
    retiring=raw.index("index checkpoint release witness tick=170")
    retired=raw.find("seek checkpoint retired tick=",retiring)
    if not begin<retained<raw.index(scratch)<complete<retiring<retired<raw.index(release):
        raise RuntimeError("index checkpoint release preceded actual owner retirement")
    additional={"result":"not_requested"}
    if ("later-middle index checkpoint selected " in raw
            or len(re.findall(r"(?:next-round|replacement) index checkpoint selected ", raw)) >= 2):
        additional=validate_managed_checkpoints(raw,identity,extra,preparation_fallback)
        peak=max(peak,additional['observed_peak_owned_bytes'])
    elif extra:
        requested=extra
        managed="automatic replay session indexed " in raw
        captures=re.findall(r"index checkpoint retained run_id=(\S+) tick=(\d+) entries=",raw)
        later=[int(tick) for run,tick in captures if run==identity and int(tick)!=170]
        if len(later)!=1 or not requested<=later[0]<=requested+240 or (not managed and (later[0]-requested)%30):
            raise RuntimeError("additional checkpoint is outside its bounded placement window")
        extra=later[0]
        deferred=re.findall(r"indexed checkpoint deferred run_id=(\S+) requested=(\d+) tick=(\d+) reason=(\w+) captured=(true|false)",raw)
        if [row[:3] for row in deferred]!=([] if managed else [(identity,str(requested),str(tick)) for tick in range(requested,extra,30)]):
            raise RuntimeError("additional checkpoint movement lacks explicit unsupported-boundary evidence")
        for run,wanted,tick,reason,captured in deferred:
            if (reason,captured) not in (("active_HUD_players","false"),("unsupported_capture","true")):
                raise RuntimeError("additional checkpoint was deferred for an unknown failure")
            if captured=="true":
                retired=f"indexed checkpoint capture rejected run_id={identity} tick={tick} code=5 resources_retired=true operation_idle=true"
                deferred_line=f"indexed checkpoint deferred run_id={identity} requested={requested} tick={tick} reason={reason} captured=true"
                if raw.count(retired)!=1 or raw.index(retired)>raw.index(deferred_line):
                    raise RuntimeError("unsupported optional capture lacks completed retirement before resume")
        second=re.findall(rf"index checkpoint retained run_id=(\S+) tick={extra} entries={extra+1} elapsed_us=(\d+) owned_bytes=(\d+) callbacks_unchanged=true application_idle=true",raw)
        second_owned=re.findall(rf"index checkpoint owned tick={extra} retained=2 owned_bytes=(\d+)",raw)
        second_release=re.findall(rf"index checkpoint release witness tick={extra} current_tick=(\d+) vfx_owners_live=(true|false) target_shape_supported=true owned_bytes=(\d+)",raw)
        selected=re.findall(r"indexed checkpoint selected run_id=(\S+) tick=(\d+) extra=(\d+) automatic=true",raw)
        scratch_extra=f"index checkpoint capture retired run_id={identity} tick={extra} immutable_owner_retained=true"
        if (len(second)!=1 or second[0][0]!=identity or len(second_owned)!=1 or len(second_release)!=1
                or raw.count(scratch_extra)!=1 or len(selected)!=1 or selected[0][0]!=identity
                or int(selected[0][2])!=extra or int(selected[0][1]) not in (170,extra)):
            raise RuntimeError("additional index checkpoint lacks capture, automatic selection or retirement evidence")
        extra_peak=max(int(second[0][2]),int(second_owned[0]),int(second_release[0][2]))
        if (not 0<extra_peak<=replay_memory_limit(raw,identity) or int(second_release[0][0])<=extra
                or not raw.index(scratch)<raw.index(f"index checkpoint owned tick={extra}")<raw.index(scratch_extra)<complete
                or not complete<raw.index(f"index checkpoint release witness tick={extra}")<raw.index(release)):
            raise RuntimeError("additional checkpoint timing/memory/retirement order disagrees")
        selected_tick=int(selected[0][1]);fallback=selected_tick==170
        if preparation_fallback:
            validate_index_preparation_fallback(raw,identity)
        elif fallback and raw.count(f"indexed checkpoint fallback run_id={identity} rejected={extra} selected=170 reason=retained_owner_lifetime")!=1:
            raise RuntimeError("earlier checkpoint selection lacks explicit lifetime rejection")
        placement=(validate_host_index_checkpoint(raw,identity,False,True,tick=extra)
            if "automatic replay session indexed " in raw else {"placement_owner":"explicit_test_request"})
        additional={"result":"pass","requested_checkpoint_tick":requested,"checkpoint_tick":extra,"capture_us":int(second[0][1]),"placement":placement,
                    "unsupported_boundaries":[{"tick":int(row[2]),"reason":row[3],"capture_attempted":row[4]=="true"} for row in deferred],
                    "observed_peak_owned_bytes":extra_peak,"selected_tick":selected_tick,"automatic_fallback":fallback,
                    "vfx_owners_live_at_release":second_release[0][1]=="true","checkpoint_resources_retired":True}
        peak=max(peak,extra_peak)
    return {"result":"pass","checkpoint_tick":170,"capture_us":int(capture[0][1]),"additional_checkpoint":additional,
            "observed_peak_owned_bytes":peak,"release_tick":int(lifetime[0][0]),
            "vfx_owners_live_at_release":lifetime[0][1]=="true",
            "checkpoint_resources_retired":True,"cross_round_restore_proven":False,
            "full_checkpoint_coverage":False}


def validate_replacement_checkpoint_placement(raw: str, tick: int) -> dict | None:
    selections=list(re.finditer(rf"replacement index checkpoint selected origin={tick-1} target={tick} native_phase_tick=\d+ placement_owner=host",raw))
    receipts=list(re.finditer(rf"index replacement placement target={tick} rejected_checkpoint=(\d+) invalidated_at=(\d+) attempt=(\d+) retirement_idle=true",raw))
    if not selections and not receipts:return None
    if len(selections)!=1 or len(receipts)!=1:raise RuntimeError("replacement placement lacks unique ownership receipt")
    old,invalid,attempt=map(int,receipts[0].groups())
    retire=f"invalid index checkpoint retirement tick={old} observed_tick={invalid} indexing=true native_publication=false"
    health=list(re.finditer(rf"index checkpoint ownership sample checkpoint={old} tick={invalid} phase=3 last_valid=(\d+) first_invalid={invalid} valid_samples=\d+ deferred_samples=\d+ failure=([1-9]\d*) retained=true restorability_unproven=true",raw))
    if (not old<=invalid<=tick-31 or not 1<=attempt<=2 or len(health)!=1 or raw.count(retire)!=1
            or int(health[0][1])>invalid or not health[0].start()<raw.index(retire)<selections[0].start()<receipts[0].start()):
        raise RuntimeError("replacement placement lacks prior invalidation and retired ownership")
    return dict(rejected_checkpoint=old,first_invalid_tick=invalid,attempt=attempt,retirement_idle=True)


def validate_host_index_checkpoint(raw: str, identity: str, cancelled: bool, armed: bool = False, *, tick: int = 170, ordinal: int = 2, retained_count: int | None = None) -> dict:
    if tick!=170:
        if cancelled or not armed or tick<=170:raise RuntimeError("invalid additional managed checkpoint scope")
        if not 2<=ordinal<=16:raise RuntimeError("unsupported managed checkpoint ordinal")
        if retained_count is None:retained_count=ordinal
        # A short recording can have no later-middle round. Placement is
        # witnessed by the native combat boundary, not inferred from ordinal.
        kind="(?:later-middle|next-round|replacement)" if ordinal==3 else "(?:next-round|replacement)"
        selected=[m for m in re.finditer(rf"{kind} index checkpoint selected origin=(\d+) target=(\d+) native_phase_tick=(\d+) placement_owner=host",raw) if int(m[2])==tick]
        if len(selected)!=1 or tuple(map(int,selected[0].groups()[:2]))!=(tick-1,tick) or int(selected[0][3])<6:
            raise RuntimeError("additional checkpoint lacks native combat placement")
        markers=[f"host index checkpoint armed run_id={identity} origin={tick-1} target={tick}",
            f"host index checkpoint adopted run_id={identity} tick={tick} capture_retention_and_retirement=host observer=read_only",
            f"index checkpoint owned tick={tick} retained={retained_count}",
            f"index checkpoint retained run_id={identity} tick={tick} entries={tick+1}",
            f"index checkpoint capture retired run_id={identity} tick={tick} immutable_owner_retained=true",
            f"host index checkpoint completed run_id={identity} tick={tick} pending=false cancelled=false"]
        if any(raw.count(marker)!=1 for marker in markers) or not selected[0].start()<raw.index(markers[0]) or [raw.index(m) for m in markers]!=sorted(raw.index(m) for m in markers):
            raise RuntimeError("additional managed checkpoint lacks completed host ownership")
        result={"result":"pass","checkpoint_tick":tick,"placement_owner":"host","capture_retention_and_retirement_owner":"host","completed_resume":True}
        replacement=validate_replacement_checkpoint_placement(raw,tick)
        if replacement:result['replacement']=replacement
        return result
    adopted=f"host index checkpoint adopted run_id={identity} tick=170 capture_retention_and_retirement=host observer=read_only"
    owned="index checkpoint owned tick=170 retained=1"
    retained=f"index checkpoint retained run_id={identity} tick=170 entries=171"
    settled=(f"index held cancellation completed run_id={identity} tick=170 entries=0 bytes=0" if cancelled
             else f"index checkpoint capture retired run_id={identity} tick=170 immutable_owner_retained=true")
    completed=f"host index checkpoint completed run_id={identity} tick=170 pending=false cancelled={str(cancelled).lower()}"
    markers=[adopted,owned,retained,settled,completed]
    if armed:
        markers.insert(0,f"host index checkpoint armed run_id={identity} origin=169 target=170")
    if any(raw.count(marker)!=1 for marker in markers) or [raw.index(marker) for marker in markers]!=sorted(raw.index(marker) for marker in markers):
        raise RuntimeError("host index checkpoint lacks ordered adoption, retention, retirement or completed resume")
    if cancelled:
        intent=f"index held cancellation queued run_id={identity} tick=170 entries=171"
        if raw.count(intent)!=1 or not raw.index(retained)<raw.index(intent)<raw.index(settled):
            raise RuntimeError("host index cancellation lacks intent after retention and before completed retirement")
    return {"result":"pass","checkpoint_tick":170,"boundary_arming_owner":"host" if armed else "caller",
            "capture_retention_and_retirement_owner":"host",
            "observer_role":"read-only validation and optional cancellation intent","cancelled":cancelled,"completed_resume":True}


def validate_held_index_cancellation(raw: str, identity: str) -> dict:
    queued=list(re.finditer(r"index held cancellation queued run_id=(\S+) tick=170 entries=171 bytes=(\d+) complete=false",raw))
    completed=list(re.finditer(r"index held cancellation completed run_id=(\S+) tick=170 entries=0 bytes=0 callbacks_unchanged=true epoch_unchanged=true application_unchanged=true",raw))
    retired=list(re.finditer(r"seek checkpoint retired tick=170 application_idle=true",raw))
    if (len(queued)!=1 or len(completed)!=1 or len(retired)!=1
            or queued[0][1]!=identity or completed[0][1]!=identity
            or not 0<int(queued[0][2])<=PRODUCTION_REPLAY_MEMORY_LIMIT
            or not queued[0].start()<retired[0].start()<completed[0].start()
            or "index complete run_id=" in raw or "replay index final phase=" in raw):
        raise RuntimeError("held index cancellation lacks stable native retirement without false completion")
    rows=validate_boundary_capture(raw,identity)
    following=[row for row in rows if row["phase"]=="actor_tail" and int(row["frame"])>170]
    if not following or int(following[-1]["frame"])<290:
        raise RuntimeError("held index cancellation lacks 120 resumed ticks")
    return {"cancelled_tick":170,"retired_while_held":True,"map_bytes_after":0,
            "simulation_callbacks_epoch_and_application_unchanged":True,
            "continuation_end":int(following[-1]["frame"]),"complete_timeline_claimed":False}


def validate_host_session_entry(raw: str) -> dict:
    entry=list(re.finditer(r'automatic replay session indexed native_tick=0 entries=1 source_round=(-?\d+) source_cursor=0 executor_owner=host checkpoint_placement=host',raw))
    placement=list(re.finditer(r'initial index checkpoint selected origin=(\d+) target=(\d+) native_phase_tick=(\d+) placement_owner=host',raw))
    if len(entry)!=1 or len(placement)!=1 or entry[0].start()>=placement[0].start() or 'automatic replay entry rejected' in raw:
        raise RuntimeError('host session requires unique native entry and host placement receipts')
    origin,target,phase=map(int,placement[0].groups())
    if int(entry[0][1])>0 or (origin,target,phase)!=(169,170,6):
        raise RuntimeError('host session placement differs from the bounded combat control')
    return dict(result='pass',entry_tick=0,checkpoint_tick=target,placement_owner='host',scope='bounded native entry; no full index or re-entry claim')


def validate_native_session_exit(raw: str, identity: str) -> dict:
    patterns=[
        r'native session exit requested run_id='+re.escape(identity)+r' observed_prefix_tick=(\d+) index_entries=(\d+) checkpoint170_retained=true cleanup_owner=host',
        r'native replay exit deferred tick=(\d+) admitted=true original_invocations=0',
        r'native replay exit cleanup completed requested_tick=(\d+) recovered_tick=(\d+) index_empty=true host_detached=true native_stop_invocations=1',
        r'native session exit completed run_id='+re.escape(identity)+r' observed_prefix_tick=(\d+) elapsed_us=(\d+) native_terminate_observed=true scene=replay_list executor_disabled=true host_inactive=true',
    ]
    matches=[list(re.finditer(pattern,raw)) for pattern in patterns]
    if any(len(items)!=1 for items in matches) or 'native replay exit blocked' in raw:
        raise RuntimeError('native session exit lacks unique completed ownership receipts')
    rows=[items[0] for items in matches]
    if any(a.start()>=b.start() for a,b in zip(rows,rows[1:])):
        raise RuntimeError('native session exit published before its cleanup predecessors')
    prefix,entries=map(int,rows[0].groups()); requested=int(rows[1][1])
    undo_request,recovered=map(int,rows[2].groups()); final,elapsed=map(int,rows[3].groups())
    if prefix<170 or entries!=prefix+1 or requested<prefix or undo_request!=requested or recovered<requested or final!=prefix or elapsed<=0:
        raise RuntimeError('native session exit coordinates do not describe the observed live-index experiment')
    return dict(result='pass',observed_prefix_tick=prefix,native_stop_requested_tick=requested,
                cleanup_tick=recovered,elapsed_us=elapsed,scope='retained index native exit; no historical seek cancellation or re-entry claim')


def qualify_retained_index(index_checkpoint=False) -> dict:
    """Read-only map proof; do not promote variable scene closure to full equivalence."""
    from deterministic_qualification.replay_fidelity import summarize_native_execution, validate_setup_capture
    paths=([OUTPUT / "index-owned-checkpoint-native.json",OUTPUT / "index-owned-checkpoint-candidate.json"] if index_checkpoint
           else [OUTPUT / "equivalence-full-native.json",OUTPUT / "equivalence-full-executor.json"])
    reports=[json.loads(p.read_text(encoding="utf-8")) for p in paths]
    if any(not r.get("record_index") or not r.get("full_match") or not r.get("include_setup") for r in reports):
        raise RuntimeError("index proof requires the full indexed protocol in both captures")
    host_checkpoint = None
    if reports[1].get("host_index_checkpoint"):
        host_checkpoint = validate_host_index_checkpoint(
            Path(reports[1]["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace"),
            reports[1]["run_id"], False, bool(reports[1].get("host_index_checkpoint_arming")))
    comparison=compare_passive_controls(*paths,index_checkpoint=index_checkpoint)
    if comparison["result"]!="pass" and comparison.get("failure")!="native boundary counts differ after an exact common prefix":
        raise RuntimeError("indexed replay diverged before scene-closure length comparison")
    texts=[Path(r["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace") for r in reports]
    index=validate_tick_index(*texts,reports[0]["run_id"],reports[1]["run_id"])
    boundaries=[validate_boundary_capture(t,r["run_id"]) for t,r in zip(texts,reports)]
    prefix=[]
    for rows in boundaries:
        terminal=[i for i,row in enumerate(rows) if row["phase"]=="actor_tail" and int(row["frame"])==index["last_tick"]
                  and row["world_mode"]=="10" and row["round_state"]=="10" and row["source_active"]=="false"]
        if len(terminal)!=1: raise RuntimeError("index lacks its unique completed terminal native interval")
        prefix.append(rows[:terminal[0]+1])
    if prefix[0]!=prefix[1]: raise RuntimeError("indexed replay native callbacks differ")
    setups=[validate_setup_capture(t,r["run_id"]) for t,r in zip(texts,reports)]
    trajectories=[[dict(re.findall(r"(\w+)=([^\s]+)",line)) for line in t.splitlines()
                   if "[ReplayQualification] trajectory ordinal=" in line] for t in texts]
    if setups[0]!=setups[1] or trajectories[0]!=trajectories[1]:
        raise RuntimeError("indexed setup or completed-interval observations differ")
    proof=executor_proof(reports[1],True)
    physical={"executor":proof,"native_boundary_counts":{"actor_tail":sum(r["phase"]=="actor_tail" for r in boundaries[1])},
              "native_execution_coverage":summarize_native_execution(boundaries[1])}
    verify_executor_native_counts(physical)
    result={"result":"pass","scope":"full replay tick index and independently matching replay prefix; not complete execution qualification",
            "identities":reports[1]["identities"],"runs":[r["run_id"] for r in reports],
            "raw_logs":[r["raw_log"] for r in reports],"tick_index":index,
            "native_replay_callbacks_compared":len(prefix[0]),"trajectory_observations_compared":len(trajectories[0]),
            "executor_physical_tail_accounting":physical,
            "post_replay_comparison":{"result":comparison["result"],"failure":comparison.get("failure"),
                "common_callbacks_compared":min(map(len,boundaries))-len(prefix[0]),
                "uncompared_callbacks":abs(len(boundaries[0])-len(boundaries[1])),
                "limitation":"Scene polling completion varies; extra candidate continuation has no independent control in these captures."},
            "execution_gate_complete":False,"arbitrary_seek_complete":False,
            "cleanup":[r["cleanup"] for r in reports]}
    if index_checkpoint:
        result["index_checkpoint"]=validate_index_checkpoint_retention(texts[1],reports[1]["run_id"])
        result["intro_skip_policy"]=validate_intro_skips(texts[1],reports[1]["run_id"])
        result["comparison"]=comparison
    if host_checkpoint is not None:
        result["host_index_checkpoint"] = host_checkpoint
    return result


def compare_same_process_render(path: Path) -> dict:
    """Causal output comparison only; never supplies data to the running game."""
    report = json.loads(path.read_text(encoding="utf-8"))
    raw = report["raw_log"]
    if (report.get("result") != "captured" or report.get("mode") != "runtime"
            or report.get("historical_cancel") or report.get("cleanup") != {"complete": True, "games_remaining": 0}
            or report.get("loaded_runtime", {}).get("sha256") != report["identities"]["runtime"]
            or sha256_file(Path(raw["path"])) != raw["sha256"]):
        raise RuntimeError("same-process render evidence is incomplete or changed")
    text = Path(raw["path"]).read_text(encoding="utf-8", errors="replace")
    if text.count("historical combat committed run_id=" + report["run_id"] + " from_tick=210 to_tick=205") != 1:
        raise RuntimeError("missing unique historical commit witness")
    selections, outputs, final = {}, {}, {}
    for line in text.splitlines():
        kind = ("selection" if "render pass selection " in line else "output" if "render pass output " in line
                else "final" if "native presentation run_id=" in line else "")
        if not kind:
            continue
        row = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if row.get("run_id") != report["run_id"]:
            raise RuntimeError("foreign render observation")
        if row.get("tick") != "206":
            if kind != "final":
                raise RuntimeError("render selection coordinate mismatch")
            continue
        generation = int(row["generation"])
        destination = selections if kind == "selection" else final if kind == "final" else outputs
        key = (generation, int(row["ordinal"])) if kind == "output" else generation
        if key in destination:
            raise RuntimeError("duplicate render observation")
        destination[key] = row
    if set(selections) != {0, 1} or set(final) != {0, 1}:
        raise RuntimeError("missing original/restored render outputs")
    for generation, row in selections.items():
        count = int(row["selected"])
        if row["overflow"] != "false" or count <= 0 or int(row["bytes"]) > 128 * 1024 * 1024:
            raise RuntimeError("render selection capacity or coverage failure")
        if {k for k in outputs if k[0] == generation} != {(generation, i) for i in range(count)}:
            raise RuntimeError("incomplete GPU output set")
    if len(outputs) != sum(int(x["selected"]) for x in selections.values()):
        raise RuntimeError("unexpected output generation")
    if selections[0]["selected"] != selections[1]["selected"] or selections[0]["skipped"] != selections[1]["skipped"]:
        raise RuntimeError("render selection paths differ")
    comparisons = []
    for i in range(int(selections[0]["selected"])):
        a, b = outputs[0, i], outputs[1, i]
        if a["gpu_complete"] != "true" or b["gpu_complete"] != "true":
            raise RuntimeError("render output lacks GPU completion")
        if any(a.get(k) != b.get(k) for k in ("format", "width", "height", "last_draw", "group", "target")):
            raise RuntimeError("selected render boundary or descriptor differs")
        comparisons.append({"ordinal": i, "last_draw": int(a["last_draw"]), "format": int(a["format"]),
                            "group": a.get("group"), "target": a.get("target"),
                            "original_hash": a["hash"], "restored_hash": b["hash"], "equal": a["hash"] == b["hash"]})
    mismatches = [x["ordinal"] for x in comparisons if not x["equal"]]
    final_equal = final[0]["hash"] == final[1]["hash"]
    return {"result": "fail" if mismatches or not final_equal else "pass", "scope": "same-process selected render outputs only",
            "milestone_complete": False, "independent_continuation_validated": False,
            "first_differing_selected_output": mismatches[0] if mismatches else None,
            "selected_outputs": comparisons, "final_image_equal": final_equal, "selections": selections,
            "run_id": report["run_id"], "identities": report["identities"], "raw_log": raw, "cleanup": report["cleanup"]}


def host_seek_first_target(report: dict) -> int:
    return int(report.get("host_seek_first_target",0)) or (210 if report.get("checkpoint_pair") else 209 if report.get("host_seek_target",208)==208 else 208)


def seek_application_costs(text: str, run_id: str) -> dict:
    pattern=(r"host seek application cost run_id=(\S+) target=(\d+) prefix_us=(\d+) engine_us=(\d+) "
             r"tail_us=(\d+) frame_sync_us=(\d+) sync_subset_of_tail=true includes_observers=true")
    costs=re.findall(pattern,text)
    if not costs:
        return {"result":"not_measured"}
    ready=re.findall(r"host seek ready run_id=(\S+) target=(\d+) elapsed_us=(\d+) owned_bytes=\d+ paused=true",text)
    if len(costs)!=len(ready):
        raise RuntimeError("seek application costs lack matching request-to-ready evidence")
    result=[]
    for measured,request in zip(costs,ready):
        identity,target,*values=measured
        prefix,engine,tail,sync=map(int,values)
        elapsed=int(request[2])
        if identity!=run_id or request[:2]!=(identity,target) or sync>tail or prefix+engine+tail>elapsed:
            raise RuntimeError("seek application cost identity, attribution or total is invalid")
        result.append({"target":int(target),"request_to_ready_us":elapsed,"prefix_us":prefix,
            "engine_us":engine,"tail_us":tail,"frame_sync_us":sync,
            "outside_advance_calls_us":elapsed-prefix-engine-tail})
    return {"result":"measured","requests":result,"frame_sync_is_tail_subset":True,
            "includes_observer_cost":True,"scope":"wall time; CPU work and waits are not separately measured"}


def continuation_generation(report: dict, cancellation: str) -> int:
    if report.get("consumer_mutation"): return 1
    # An advance-failure cancellation has already traversed restored A. Its
    # B recovery moves forward in replay time and stays in that generation.
    from deterministic_qualification.replay_fidelity import particle_lifetime_recovery, particle_lifetime_abort_tick
    if particle_lifetime_recovery(report):
        # The failed C boundary is explicitly bounded and independently tied
        # to recovery ownership. B recovery can itself rewind presentation.
        text=Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace")
        return 1+int(particle_lifetime_abort_tick(text)>210)
    if execution_fallback_case(report):
        text=Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace")
        rows=re.findall(r"native pose completed run_id=(\S+) tick=220 generation=(\d+) ",text)
        # Recovery may publish a render task at B without a gameplay tick.
        # Require one final target generation; compare every subsequent pose.
        if len(rows)!=1 or rows[0][0]!=report["run_id"] or int(rows[0][1]) not in (2,3):
            raise RuntimeError("execution fallback lacks its unique resumed pose generation")
        return int(rows[0][1])
    if report.get("host_seek_repeat"):
        # The observer generations count decreasing native render ticks, not
        # transactions. A first landing without traversal adds no second rewind.
        return 1 if cancellation or host_seek_first_target(report)==report.get("historical_anchor_tick",205) else 2
    return 1 if not cancellation or report.get("seek_advance_failure") or report.get("completion_repeat") else 0


def coherence_readback_expected(report, tick, generation):
    target=report.get("observation_target") or (300 if report.get("source_revision_profile")=="guard201" else report.get("index_seek_target",208))
    selected=(target+22,target+92) if target>340 else (300,400) if target==300 else (230,300)
    return bool(report.get("coherence_gpu_readbacks")) and ((tick==190 and generation==0) or tick in selected)


def read_native_pose_streams(texts, reports):
    streams = []
    for text, report in zip(texts, reports):
        stream, completed = {}, {}
        for line in text.splitlines():
            publication = "native pose publication run_id=" in line
            if not publication and "native pose completed run_id=" not in line:
                continue
            row = dict(re.findall(r"(\w+)=([^\s]+)", line))
            if row.get("run_id") != report["run_id"]:
                raise RuntimeError("native pose observation has a foreign run identity")
            key = int(row["generation"]), int(row["tick"])
            if publication:
                entries = stream.setdefault(key, {})
                ordinal = int(row["ordinal"])
                if ordinal in entries:
                    raise RuntimeError("duplicate native pose ordinal")
                entries[ordinal] = tuple(row[k] for k in ("player", "bones", "mapping", "hash"))
            else:
                if key in completed:
                    raise RuntimeError("duplicate native pose completion")
                completed[key] = int(row["evaluations"])
        for key, count in completed.items():
            if count <= 0 or set(stream.get(key, {})) != set(range(count)):
                raise RuntimeError("native pose completion lacks its exact evaluation set")
        if set(stream) != set(completed):
            raise RuntimeError("native pose publication lacks completion")
        streams.append({key: sorted(rows.values()) for key, rows in stream.items()})
    return streams


def compare_native_poses(texts: list[str], reports: list[dict], resumed_tick: int, cancellation: str) -> dict:
    """Validate complete native pose publications, retaining multiplicity across workers."""
    streams = read_native_pose_streams(texts, reports)
    count=reports[1].get("index_seek_continuation",120) if reports[1].get("index_seek") else 120
    result = {"result": "pass", "prefix_ticks": 20, "continuation_ticks": count,
              "scope": "complete Lux animation-proxy outputs; not downstream native rendering proof"}
    if reports[1].get("index_sequence"):
        from deterministic_qualification.indexed_sequence import sequence_cases
        cases=sequence_cases(texts[1],reports[1]['run_id'])
        for case in cases:
            for tick in range(case['target']+1,case['last_tick']+1):
                a,b=streams[0].get((0,tick)),streams[1].get((case['case']+1,tick))
                if a is None or b is None or a!=b:
                    raise RuntimeError(f"indexed sequence pose mismatch/missing case{case['case']} tick{tick}")
        return {"result":"pass","cases":len(cases),"continuation_ticks_each":120,"scope":"complete Lux pose publications; pixels optional"}
    traversals = [
            ("prefix", 0, range(185, 205)),
            ("continuation", continuation_generation(reports[1],cancellation), range(resumed_tick + 1, resumed_tick + count + 1))]
    if reports[1].get("host_seek_repeat"):
        traversals.insert(1,("first_seek",1,range(207 if reports[1].get("checkpoint_pair") else reports[1].get("historical_anchor_tick",205)+1,host_seek_first_target(reports[1])+1)))
    if reports[1].get("historical_anchor_tick")==170 and not cancellation:
        generation=continuation_generation(reports[1],cancellation) if execution_fallback_case(reports[1]) else 2 if reports[1].get("host_seek_repeat") else 1
        if ground_commit_case(reports[1]):
            if resumed_tick!=2510 or any(r.get("observation_target")!=2510 for r in reports):
                raise RuntimeError("ground commit requires the exact shared pose observation window")
            # Native ObserveTick intentionally samples early combat plus target.
            # Gameplay callbacks still compare every resimulated tick separately.
            traversals.insert(1,("anchor_resimulation",generation,list(range(171,450))+list(range(2504,2511))))
            result["anchor_resimulation_coverage"]={"result":"partial_observation",
                "observed_ranges":[[171,449],[2504,2510]],"unobserved_ranges":[[450,2503]],
                "scope":"pose publications only; full gameplay resimulation is a separate comparison"}
        else:
            traversals.insert(1,("anchor_resimulation",generation,range(171,resumed_tick+1)))
    for label, generation, ticks in traversals:
        for tick in ticks:
            a, b = streams[0].get((0, tick)), streams[1].get((generation, tick))
            if a is None or b is None:
                raise RuntimeError(f"missing native pose {label} at tick {tick}")
            if a != b:
                result["result"] = "fail"
                result[label + "_first_mismatch"] = tick
                break
    return result


def compare_native_presentation(texts: list[str], reports: list[dict], resumed_tick: int, cancellation: str) -> dict:
    """Full native backbuffer fingerprints; sampled pixels only diagnose the first mismatch."""
    streams = []
    for text, report in zip(texts, reports):
        completed = re.findall(r"native presentation completed run_id=(\S+) frames=(\d+) bytes=(\d+) error=(\d+) "
            r"tagged_tasks=(\d+) presents=(\d+) tagged_presents=(\d+) detached=true", text)
        if (len(completed) != 1 or completed[0][0] != report["run_id"] or int(completed[0][3])
                or int(completed[0][1]) < 120 or int(completed[0][2]) > 128 * 1024 * 1024):
            raise RuntimeError("native presentation lacks completed, bounded, detached observation")
        stream = {}
        for line in text.splitlines():
            if "[ReplayQualification] native presentation run_id=" not in line:
                continue
            row = dict(re.findall(r"(\w+)=([^\s]+)", line))
            if row.get("run_id") != report["run_id"] or len(row.get("pixels", "")) != 96 * 54 * 8:
                raise RuntimeError("native presentation pixel evidence is incomplete")
            key = int(row["generation"]), int(row["tick"])
            if key in stream:
                raise RuntimeError("native presentation has duplicate task/tick evidence")
            stream[key] = row
        if len(stream) != int(completed[0][1]):
            raise RuntimeError("native presentation completion count disagrees with raw frames")
        streams.append(stream)
    generation = continuation_generation(reports[1],cancellation)
    comparisons = [("native_presentation_prefix", 0, tick) for tick in range(185, 205)]
    comparisons += [("native_presentation_continuation", generation, tick) for tick in range(resumed_tick + 1, resumed_tick + 121)]
    first_by_scope = {}
    matched = {"native_presentation_prefix": 0, "native_presentation_continuation": 0}
    for kind, observed_generation, tick in comparisons:
        a, b = streams[0].get((0, tick)), streams[1].get((observed_generation, tick))
        if a is None or b is None:
            raise RuntimeError(f"native presentation missing {kind} tick {tick}, generation {observed_generation}")
        if all(a[k] == b[k] for k in ("width", "height", "format", "hash", "pixels")):
            matched[kind] += 1
            continue
        if kind in first_by_scope:
            continue
        # Retain only the first failure in each scope. A prefix failure must
        # not prevent evaluating the independently required 120-frame suffix.
        paths = []
        for label, row in (("native", a), ("restored", b)):
            colors = bytes.fromhex(row["pixels"])
            rgb = bytearray()
            for offset in range(0, len(colors), 4):
                rgb.extend(colors[offset:offset+3])
            scope = "prefix" if kind.endswith("prefix") else "continuation"
            path = Path(reports[1]["raw_log"]["path"]).parent / f"combat-presentation-{scope}-{label}.ppm"
            path.write_bytes(b"P6\n96 54\n255\n" + rgb)
            paths.append(str(path))
        first_by_scope[kind] = {"first_mismatch": kind, "frame": tick,
            "expected_hash": a["hash"], "observed_hash": b["hash"], "sample_images": paths}
    evidence = {"prefix_frames_compared": 20, "continuation_frames_compared": 120,
                "matching_prefix_frames": matched["native_presentation_prefix"],
                "matching_continuation_frames": matched["native_presentation_continuation"]}
    if first_by_scope:
        return {"result": "fail", **next(iter(first_by_scope.values())), **evidence,
                "first_by_scope": first_by_scope}
    return {"result": "pass", "prefix_frames": 20, "continuation_frames": 120, **evidence,
            "scope": "native backbuffer full-image fingerprints before overlay; sampled diagnostic pixels also equal"}


def compare_same_process_trace(report_path: Path) -> dict:
    """Read-only causality check, before paying for another native control."""
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("historical_cancel") or report.get("corrected_inputs"):
        return {"result": "not_applicable", "reason": "no identical-input A suffix"}
    raw = report["raw_log"]
    if sha256_file(Path(raw["path"])) != raw["sha256"]:
        raise RuntimeError("trace evidence changed")
    rows = {}
    for line in Path(raw["path"]).read_text(encoding="utf-8", errors="replace").splitlines():
        if "trace source run_id=" not in line:
            continue
        row = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if row.get("run_id") != report["run_id"]:
            raise RuntimeError("mixed trace observer run identity")
        key = (int(row["generation"]), int(row["tick"]), int(row["player"]))
        if key in rows:
            raise RuntimeError("duplicate trace source boundary")
        rows[key] = row
    first = None
    advanced_tick = report.get("historical_advanced_tick", 210)
    checkpoint_window=bounded_checkpoint_window(report)
    if not checkpoint_window and not ground_commit_case(report) and not trace_render_recovery_case(report) and advanced_tick not in ((210,220,2504) if execution_fallback_case(report) else (220,2504) if report.get("historical_anchor_tick")==170 else (211,) if report.get("checkpoint_pair") else (209,210)):
        raise RuntimeError("unsupported trace comparison interval")
    first_tick=report["historical_anchor_tick"]+1 if checkpoint_window else 206
    last_tick=advanced_tick if checkpoint_window else min(advanced_tick,211)
    for tick in range(first_tick,last_tick+1):
        for player in range(2):
            a, b = rows.get((0,tick,player)), rows.get((2 if report.get("host_seek_repeat") else 1,tick,player))
            if not a or not b:
                return {"result":"incomplete", "missing":{"tick":tick,"player":player}}
            membership_field="membership"
            if a.get("membership_version") or b.get("membership_version"):
                if (a.get("membership_version")!="2" or b.get("membership_version")!="2"
                        or any(not re.fullmatch(r"[0-9a-f]{16}",r.get("topology","")) for r in (a,b))):
                    raise RuntimeError("invalid or mixed trace membership protocol")
                membership_field="topology"
            for field in ("states","expired","attached","fading","completion","pending","strong","child",membership_field,"hash"):
                if a[field] != b[field] and first is None:
                    first = {"tick":tick,"player":player,"field":field,"original":a[field],"restored":b[field],
                             "original_clock":{k:a.get(k) for k in ("clock_hash","remainder","time","scale","steps")},
                             "restored_clock":{k:b.get(k) for k in ("clock_hash","remainder","time","scale","steps")}}
    return {"result":"fail" if first else "pass", "ticks":last_tick-first_tick+1, "players":2,"first_mismatch":first,
            "scope":"same-process retained trace source; independent startup variation is separate",
            "run_id":report["run_id"],"identities":report["identities"],"raw_log":raw}


def validate_callback_admission(text: str, advanced_tick: int, capture_cancel: bool = False, restore_reuse: bool = False, repeat_seek: bool = False, repeat_origin: int = 209, checkpoint_pair: bool = False, anchor_tick: int = 205, checkpoint_fallback: bool = False, preparation_cancel_run: str | None = None) -> dict:
    captures = re.findall(r"callback checkpoint tick=(\d+) latent=(\d+) streamable=(\d+)(?: domain=(\d+))? policy=(empty_registered_v1|quiescent_streamable_v2)", text)
    stops = re.findall(r"callback admission stopped latent=(\d+) streamable=(\d+) detached=true", text)
    expected=([anchor_tick]*(2 if capture_cancel else 1)+([205] if checkpoint_fallback else [206] if checkpoint_pair else [])+[advanced_tick]*(2 if restore_reuse or (checkpoint_fallback and advanced_tick==210) else 1)+([repeat_origin] if repeat_seek else []))
    preparation=None
    if preparation_cancel_run is not None:
        # A cancellation can finish before complete B acquisition. Require the
        # native retirement/unchanged-B receipt, not an invented B checkpoint.
        # This is one unpublished request; repeated/publication cases keep their
        # existing complete-B requirements and cannot enter this exception.
        if not preparation_cancel_run or any((capture_cancel,restore_reuse,repeat_seek,checkpoint_pair,checkpoint_fallback)):
            raise RuntimeError("unsupported preparation cancellation callback scope")
        run=re.escape(preparation_cancel_run)
        patterns=[rf"host seek accepted run_id={run} checkpoint={anchor_tick} origin={advanced_tick} target=\d+ aliases_rejected=true",
            rf"historical cancellation injected run_id={run} point=before tick={advanced_tick} preparation_pending=true render_command_pending=(true|false) gpu_completion_pending=(true|false)",
            rf"restore preparation failed participant=preparation_cancelled code=31 target={anchor_tick} original={advanced_tick} before_publication=true",
            rf"restore preparation recovery completed original={advanced_tick} observed={advanced_tick} ground_released=true render_released=true before_A_publication=true",
            rf"historical cancellation recovered run_id={run} point=before tick={advanced_tick}",
            r"callback admission stopped latent=\d+ streamable=\d+ detached=true"]
        receipts=[list(re.finditer(pattern,text)) for pattern in patterns]
        if (any(len(rows)!=1 for rows in receipts)
                or any(a[0].start()>=b[0].start() for a,b in zip(receipts,receipts[1:]))
                or receipts[1][0].groups() not in (("true","false"),("false","true"))
                or any(marker in text for marker in ("host seek publication observed", "restore request prepared target=", "seek ownership state=APublished", "seek ownership state=Committing"))):
            raise RuntimeError("preparation cancellation lacks ordered native unchanged-B recovery")
        undo=list(re.finditer(rf"restore undo captured target={anchor_tick} original={advanced_tick} gpu_complete=true before_target_preparation=true owned_bytes=(\d+)",text))
        if text.count("restore undo captured ")!=len(undo) or len(undo)>1 or (undo and (
                not receipts[0][0].end()<undo[0].start()<receipts[1][0].start()
                or not 0<int(undo[0].group(1))<=PRODUCTION_REPLAY_MEMORY_LIMIT)):
            raise RuntimeError("preparation cancellation B acquisition is out of order")
        expected=[anchor_tick]+([advanced_tick] if undo else [])
        preparation={"result":"pass","original_tick":advanced_tick,"complete_B_captured":bool(undo),
            "A_published":False,"native_recovery_completed":True,
            "cancelled_pending_work":"render_command" if receipts[1][0].group(1)=="true" else "gpu_completion"}
    if ([int(row[0]) for row in captures] != expected or len(stops) != 1
            or any(row[1:] != captures[0][1:] for row in captures[1:])
            or "callback lease invalidated" in text or "callback capture admission code=" in text):
        raise RuntimeError("callback checkpoint admission or detach evidence is missing or invalid")
    return {"result": "pass", "policy": captures[0][4],
            "capture_registration_stamps": [list(map(int, row[1:3])) for row in captures],
            "domain_identity": int(captures[0][3]) if captures[0][3] else None,
            "pending_streamable_loads_rejected": captures[0][4] == "quiescent_streamable_v2",
            "final_registration_stamp": list(map(int, stops[0])),
            "active_loading_owned": False, "registration_invalidation_exercised": False,
            **({"preparation_cancellation":preparation} if preparation else {})}


def validate_trace_render_reconstruction(text: str, original_tick: int=329) -> dict:
    rows=list(re.finditer(r"trace visible B reconstruction count=(\d+) source_animation_unchanged=true B_retained=true render_drain_required=true",text))
    complete="stage render recovery state=Complete B_retained=true GPU_completed=true"
    publication="stage render recovery state=PublicationPending B_retained=true native_destinations_rebound=true"
    if original_tick not in (329,5695,6538):raise RuntimeError("unsupported trace recovery boundary")
    if len(rows)!=1 or (original_tick==329 and int(rows[0][1])<1) or text.count(complete)!=1 or text.count(publication)!=1:
        raise RuntimeError("trace visible B native reconstruction or completion was not exercised")
    private_children=0
    if original_tick==5695:
        captures=re.findall(r"trace capture roots=2 states=(\d+) values=\d+ bindings=\d+ bytes=\d+ success=true",text)
        # Coverage witness only. The production transaction independently checks
        # fixed-pool membership, private child lifetimes/values and complete B.
        if len(captures)<3 or not 1<=int(captures[1])-int(captures[0])<=16:
            raise RuntimeError("private B trace-child ownership was not exercised")
        private_children=int(captures[1])-int(captures[0])
    if not rows[0].start()<text.index(publication)<text.index(complete):
        raise RuntimeError("trace B reconstruction lacks ordered native publication and GPU completion")
    return {"visible_meshes_reconstructed":int(rows[0][1]),"reconstruction_exercised":int(rows[0][1])>0,
            "private_B_children_retained":private_children,"GPU_completed":True,"B_retained":True,
            "scope":"native B destination recovery and exact B trace-value checks; zero reconstruction does not claim pixel equality or complete visual coherence"}


def compare_private_hud_continuation(native_text: str, restored_suffix: str, start_tick: int=300, require_active: bool=True) -> dict:
    def read(text):
        result={}
        for payload in re.findall(r"HUD state (.*)",text):
            row=dict(re.findall(r"(\w+)=([^\s]+)",payload))
            tick=int(row.get("tick",-1))
            if not start_tick<tick<=start_tick+120:continue
            required={"tick","p1","p2","combo1","combo2","timer","damage","types","pool","active","type_pool","type_active","type_players","read_only"}
            if tick in result or not required<=row.keys() or row["read_only"]!="true":
                raise RuntimeError("private B HUD observation duplicated or incomplete")
            result[tick]=row
        if set(result)!=set(range(start_tick+1,start_tick+121)):
            raise RuntimeError("private B HUD requires all120 independent continuation ticks")
        return result
    expected,actual=read(native_text),read(restored_suffix)
    for tick in expected:
        if expected[tick]!=actual[tick]:raise RuntimeError(f"private B HUD differs at tick{tick}")
    active=[tick for tick,row in expected.items() if int(row["type_active"])>0]
    if require_active and not active:raise RuntimeError("private B HUD did not exercise active type-player continuation")
    return {"result":"pass","ticks":120,"active_type_ticks":active,
            "scope":"independent native HUD fields and logical active type-player clocks; no pixel equality claim"}


def compare_widget_continuation(native_text: str, restored_suffix: str, resumed_tick: int) -> dict:
    # Early causal observation ends at230; later bounded cases observe B+120.
    # Compare actual
    # independently advanced player times, not just a correct delta applied
    # to potentially wrong restored state. No observations feed the runtime.
    pattern = r"HUD clock tick=(\d+) widget=(\S+) player=(\d+) delta=(\S+) slate_delta=(\S+) before=(\S+) after=(\S+) read_only=true"
    def samples(text):
        result = {}
        for tick, widget, player, delta, _wall, before, after in re.findall(pattern, text):
            if not resumed_tick < int(tick) <= (resumed_tick+120 if resumed_tick>230 else 230):
                continue
            key = (int(tick), widget, int(player))
            # The producer logs class and per-widget player index, not a
            # stable widget identity. Preserve native traversal order and
            # multiplicity; never collapse distinct widgets into one sample.
            result.setdefault(key, []).append((delta, before, after))
        return result
    native, restored = samples(native_text), samples(restored_suffix)
    def hud_states(text):
        result={}
        for line in text.splitlines():
            if "[ReplayQualification] HUD state tick=" not in line: continue
            row=dict(re.findall(r"(\w+)=([^\s]+)",line))
            tick=int(row["tick"])
            if not resumed_tick<tick<=resumed_tick+10: continue
            if tick in result: raise RuntimeError("HUD state observation is duplicated")
            if not {"p1","p2","combo1","combo2","timer","damage","types","pool","active","read_only"}<=row.keys() or row["read_only"]!="true":
                raise RuntimeError("HUD state observation is incomplete")
            result[tick]=row
        return result
    native_hud,restored_hud=hud_states(native_text),hud_states(restored_suffix)
    if native_hud or restored_hud:
        if set(native_hud)!=set(range(resumed_tick+1,resumed_tick+11)) or native_hud!=restored_hud:
            raise RuntimeError("HUD state continuation differs or lacks ten independent observations")
    if not native and not restored and native_hud and all(int(row["active"])==0 for row in native_hud.values()):
        return {"result":"pass","active_player_updates":0,"inactive_player_observations":len(native_hud),
                "first_tick":min(native_hud),"last_tick":max(native_hud),
                "scope":"independent native HUD fields and explicitly empty active damage-player pools; active playback not exercised"}
    if not native or native.keys() != restored.keys():
        raise RuntimeError("HUD continuation lacks matching independent active-player coverage")
    for key, actual in restored.items():
        if actual != native[key]:
            raise RuntimeError(f"HUD continuation differs at {key}: expected {native[key]}, observed {actual}")
    return {"result": "pass", "active_player_updates": sum(map(len,native.values())),
            "first_tick": min(key[0] for key in native), "last_tick": max(key[0] for key in native),
            "instance_identity":"unobserved",
            "scope": "independent ordered widget-class/player clock observations with exact multiplicity; not per-widget identity, complete widget state or pixel proof"}


def validate_particle_owner_copy(text, run_id, anchor=205, registration=False):
    """Constructor receipt only; independent continuation is checked separately."""
    rows = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines()
            if "[ReplayQualification] particle owner copy run_id=" in line]
    if len(rows) != 1:
        raise RuntimeError("particle owner copy requires exactly one receipt")
    row = rows[0]
    required = dict(run_id=run_id, tick=str(anchor), check="inactive_copy_retired" if registration else "cold_copy_retired",
                    created="true", detached="false" if registration else "true", destroyed="true", lease_released="true",
                    source_unchanged="true", registration_performed="true" if registration else "false",
                    activation_performed="false", rollback_proof="false")
    if any(row.get(key) != value for key, value in required.items()):
        raise RuntimeError("particle owner copy incomplete construction/retirement receipt")
    for key in ("properties", "arrays", "outer_members"):
        if not row.get(key, "").isdigit() or int(row[key]) <= 0:
            raise RuntimeError("particle owner copy missing exercised ownership")
    try:
        source, copy, outer = (int(row[key], 16) for key in ("source", "copy", "outer"))
    except (KeyError, ValueError):
        raise RuntimeError("particle owner copy missing native identities") from None
    if not source or not copy or not outer or source == copy:
        raise RuntimeError("particle owner copy aliases source")
    return {"result": "pass", "scope": "inactive registration and retirement only" if registration else "cold construction and retirement only",
            "rollback_proof": False, "receipt": row}


def validate_particle_gpu_owner(text, run_id, anchor=205):
    rows=[dict(re.findall(r"(\w+)=([^\s]+)",line)) for line in text.splitlines()
          if '[ReplayQualification] particle GPU owner run_id=' in line]
    if len(rows)!=1: raise RuntimeError('particle GPU owner requires one completed retirement receipt')
    row=rows[0]
    required=dict(run_id=run_id,tick=str(anchor),constructed='true',native_retirement_returned='true',
                  gpu_completed='true',graph_published='false',rollback_proof='false')
    if any(row.get(k)!=v for k,v in required.items()): raise RuntimeError('particle GPU owner retirement incomplete')
    try:
        identities=[int(row[k],16) for k in ('component','root','render')]
        valid=all(identities) and len(set(identities))==3 and 0<int(row['charged_bytes'])<=1024*1024 and int(row['completion_serial'])==1
    except (ValueError,KeyError): valid=False
    if not valid: raise RuntimeError('particle GPU owner identities or completion serial invalid')
    return dict(result='pass',scope='native GPU construction and completed unused retirement only',rollback_proof=False,receipt=row)


def combat_seek_performance(completed_cost, readiness_pass, measured, diagnostic=False):
    return {"result":"not_measured" if not measured or not completed_cost else "diagnostic_only" if diagnostic else "pass" if completed_cost["elapsed_us"]<=500000 else "fail",
        "readiness_result":"not_measured" if not measured else "pass" if readiness_pass else "fail",
        "scope":"completed application and retirement required; request-to-ready is reported separately",
        "ceiling_us":500000,"rollback_cost_measured":bool(completed_cost)}


def validate_seek_recovery_ownership(text: str, anchor: int, original: int, failed_tick: int, target: int, settlement: bool=False, drained: bool=False, deferred: bool=False, lifetime: bool=False, consumer: bool=False) -> dict:
    rows = re.findall(r"seek ownership state=(\w+) B_tick=(\d+) target=(\d+) current_tick=(\d+) "
        r"completed_tail_tick=(\d+) B_retained=(true|false) commit_decided=(true|false) "
        r"executed_ticks=(\d+) executed_intervals=(\d+)", text)
    expected = ["BRetained", "APublished", "ExecutionActive", "RecoveryQuiescing", "Recovering", "Recovered"]
    if lifetime or consumer: expected[3:3]=["FailedRecoverable"]
    if settlement: expected[3:3]=["CSettled","CompletingTargetTails","FailedRecoverable"]
    if deferred: expected[3:3]=["CSettled","CompletingTargetTails","CSettled"]
    if [row[0] for row in rows] != expected:
        raise RuntimeError("seek recovery lacks the complete ordered ownership chain")
    if any(int(row[1]) != original or int(row[2]) != target or row[6] != "false"
           or row[5] != ("false" if row[0] == "Recovered" else "true") for row in rows):
        raise RuntimeError("B was not retained through C execution and recovery")
    observed_prefix=[original,anchor,anchor]+[failed_tick]*(4 if settlement or deferred else 2 if lifetime or consumer else 1)
    if [int(row[3]) for row in rows[:len(observed_prefix)]] != observed_prefix:
        raise RuntimeError("recovery did not follow the requested external C boundary")
    if settlement and not drained and text.count("seek CPU settlement reopened B_retained=true native_allocations_freed=0")!=1:
        raise RuntimeError("late recovery lacks coherent CPU reopening evidence")
    if drained:
        initial=f"seek render drain completed tick={failed_tick} recovery=false gpu_complete=true render_settled=false"
        boundary=f"seek drained cancellation boundary tick={failed_tick} gpu_complete=true C_captured=true render_settled=false cpu_settled=0 B_retained=true"
        routed=f"seek drained recovery routed tick={failed_tick} gpu_complete=true render_settled=false cpu_settled=0 B_retained=true fresh_retirement_drain_required=true"
        fresh=f"seek render drain completed tick={failed_tick} recovery=true gpu_complete=true render_settled=false"
        markers=[initial,boundary,"state=FailedRecoverable","state=RecoveryQuiescing",routed,"seek C-only retirement",fresh,"state=Recovering"]
        if (not settlement or any(text.count(m)!=1 for m in markers)
                or [text.index(m) for m in markers]!=sorted(text.index(m) for m in markers)
                or "seek CPU settlement reopened" in text):
            raise RuntimeError("drained cancellation lacks pre-settlement retirement and a fresh GPU completion")
    if consumer:
        from deterministic_qualification.replay_fidelity import consumer_mutation_abort_tick
        if (anchor,original,target)!=(210,217,217) or consumer_mutation_abort_tick(text)!=failed_tick:
            raise RuntimeError("consumer recovery coordinates differ")
        drain=f"seek render drain completed tick={failed_tick} recovery=true gpu_complete=true render_settled=false"
        if text.count(drain)!=1 or not text.index("state=RecoveryQuiescing")<text.index(drain)<text.index("state=Recovering"):
            raise RuntimeError("consumer recovery lacks actual GPU retirement")
    settled, recovered = rows[-2:]
    end, ticks, intervals = int(settled[4]), int(settled[7]), int(settled[8])
    if (end < failed_tick or (consumer and end!=failed_tick) or int(settled[3]) != end or ticks != end - anchor or intervals <= 0
            or int(recovered[3]) != original or recovered[4] != settled[4] or recovered[7:] != settled[7:]):
        raise RuntimeError("C tail completion or recovered B coordinate/accounting is inconsistent")
    births = re.findall(r"seek C-only retirement owners=(\d+) completed=(\d+) B_retained=true native_render_drain_required=true", text)
    terminal_cpu=re.findall(r"emitter native retirement settled kind=cpu component=([0-9a-f]+) ordinal=(\d+) destructor_completed=true slot_null=true B_retained=true C_freed_by_native=true",text)
    emitter_recovery=anchor==205 and original==210 and target==208 and (settlement or deferred)
    if emitter_recovery and not terminal_cpu:
        raise RuntimeError("emitter recovery did not exercise completed native CPU destruction")
    trace_reconstruction=(anchor,original,failed_tick,target,settlement,drained)==(170,original,2510,2510,True,True) and original in (329,5695,6538)
    if trace_reconstruction:validate_trace_render_reconstruction(text,original)
    if len(births) != 1 or not (0 if emitter_recovery or lifetime or consumer or trace_reconstruction else 1) <= int(births[0][0]) <= 32 or births[0][0] != births[0][1]:
        raise RuntimeError("recovery did not exercise complete C-only particle retirement")
    if not text.index("state=RecoveryQuiescing") < text.index("seek C-only retirement") < text.index("state=Recovering"):
        raise RuntimeError("C-only retirement did not precede completed recovery ownership")
    return {"states": expected, "completed_C_tick": end, "executed_ticks": ticks,
            "executed_intervals": intervals, "C_only_particles": int(births[0][0]), "B_recovered_tick": original,
            "native_destroyed_CPU_members":len(set(terminal_cpu)),
            "cancellation_boundary": "ConsumerPrerequisite" if consumer else "ProtectedParticleLifetime" if lifetime else "CompletionBlocked" if deferred else "Render::Drained" if drained else "CPU settled" if settlement else "execution"}


def validate_seek_commit_ownership(text: str, anchor: int, original: int, target: int, stepped: bool=False, deferred: bool=False) -> dict:
    rows = re.findall(r"seek ownership state=(\w+) B_tick=(\d+) target=(\d+) current_tick=(\d+) "
        r"completed_tail_tick=(\d+) B_retained=(true|false) commit_decided=(true|false) "
        r"executed_ticks=(\d+) executed_intervals=(\d+)", text)
    expected=["BRetained","APublished","ExecutionActive","CSettled"]
    coordinates=[original,anchor,anchor,target]
    if deferred:
        if not stepped: raise RuntimeError("bounded deferred completion requires its explicit step")
        expected += ["CompletingTargetTails","CSettled"]
        coordinates += [target,target]
    step_offset=len(expected)
    if stepped:
        expected += ["ExecutionActive","CSettled"]
        coordinates += [target,target+1]
    final_target=target+int(stepped)
    expected += ["CompletingTargetTails","TargetTailsCompleted","Committing","Committed"]
    coordinates += [final_target]
    if [r[0] for r in rows] != expected:
        raise RuntimeError("seek commit lacks the complete ordered ownership chain")
    for i,r in enumerate(rows):
        expected_target=final_target if stepped and i>=step_offset else target
        irreversible=i>=len(rows)-2
        if (int(r[1])!=original or int(r[2])!=expected_target
                or r[5]!=("false" if irreversible else "true") or r[6]!=("true" if irreversible else "false")):
            raise RuntimeError("seek released B before completed target tails")
    if [int(r[3]) for r in rows[:-3]] != coordinates:
        raise RuntimeError("seek commit lacks exact target settlement")
    end=int(rows[-3][4])
    if end!=final_target or any(int(r[3])!=end or int(r[4])!=end or int(r[7])!=end-anchor
                         or (end>anchor and int(r[8])<=0) for r in rows[-3:]):
        raise RuntimeError("seek commit tail coordinates or accounting differ")
    return {"states":expected,"target_tick":target,"stepped_target":final_target if stepped else None,
            "completed_tail_tick":end,"executed_ticks":int(rows[-3][7]),"executed_intervals":int(rows[-3][8])}


def completed_seek_observed_rewind(text: str, run: str, anchor: int, original: int, target: int) -> tuple:
    """Count discarded original execution from native observers, not executor totals."""
    validate_seek_commit_ownership(text, anchor, original, target)
    begin=f"historical A captured run_id={run} tick={anchor} "
    end=f"restore undo captured target={anchor} original={original} "
    if text.count(begin)!=1 or text.count(end)!=1 or text.index(begin)>=text.index(end):
        raise RuntimeError("completed seek lacks unique captured A/original B boundaries")
    prefix=text[text.index(begin):text.index(end)]
    observed=re.findall(r"\[ReplayQualification\] boundary ordinal=\d+ phase=(round_sequence|actor_tail) sample_version=\d+ frame=(\d+) ",prefix)
    traversals=[int(frame) for phase,frame in observed if phase=="round_sequence"]
    intervals=[int(frame) for phase,frame in observed if phase=="actor_tail"]
    if traversals!=list(range(anchor+1,original+1)) or not intervals or intervals[-1]!=original:
        raise RuntimeError("completed seek rewind lacks exact native traversal coverage")
    # Empty intervals after A and multiple traversals within one interval are
    # independently counted. Do not assume interval count equals tick distance.
    if any(frame<anchor or frame>original for frame in intervals) or intervals!=sorted(intervals):
        raise RuntimeError("completed seek original interval observations are invalid")
    return run,str(len(traversals)),str(len(intervals))


def revalidate_advance_failure_capture(args) -> None:
    """Parser-only recovery of a completed capture; never relabel a live failure."""
    from deterministic_qualification.replay_fidelity import (validate_payload_handoff,
        validate_initial_snapshot,validate_trajectory_completion,validate_source_extents)
    report=json.loads(args.report.read_text(encoding="utf-8"))
    apply_capture_startup_setup(args)
    if any(bool(report.get(name,False))!=getattr(args,name)
           for name in ('no_async_loading_thread','flush_startup_loading')):
        raise RuntimeError('retained candidate startup loading setup differs')
    raw=report.get("raw_log",{}); path=Path(raw.get("path",""))
    deferred=bool(report.get("completion_repeat")) and report.get("historical_anchor_tick")==205 and report.get("historical_advanced_tick")==210 and report.get("host_seek_target")==208
    window=bounded_checkpoint_window(report)
    requested_window=bounded_checkpoint_window(vars(args))
    window_matches=bool(window and requested_window and report.get("result")=="captured"
        and report.get("mode")=="runtime" and report.get("include_setup") is True and report.get("skip_intros") is True
        and all(report.get(key)==getattr(args,key,None) for key in (
            "historical_anchor_tick","historical_advanced_tick","host_seek_target","historical_exact_advance",
            "historical_cancel","complete_seek_application","observation_target"))
        and bool(report.get("coherence_images_requested"))==bool(args.coherence_images))
    copy_matches=bool(report.get('particle_owner_copy') is True and getattr(args,'particle_owner_copy',False)
        and bool(report.get('particle_owner_registration'))==bool(getattr(args,'particle_owner_registration',False))
        and bool(report.get('particle_owner_gpu'))==bool(getattr(args,'particle_owner_gpu',False))
        and report.get('result')=='captured' and report.get('mode')=='runtime'
        and report.get('include_setup') is True and report.get('skip_intros') is True
        and report.get('historical_anchor_tick')==205 and report.get('historical_advanced_tick')==210
        and report.get('host_seek_target')==208 and report.get('historical_cancel')=='before'
        and report.get('host_seek') is True and not report.get('historical_exact_advance')
        and all(report.get(key)==getattr(args,key,None) for key in (
            'historical_anchor_tick','historical_advanced_tick','host_seek_target','historical_cancel',
            'historical_exact_advance','complete_seek_application','observation_target'))
        and not any(report.get(key) for key in ('capture_cancel','restore_reuse','host_seek_repeat',
            'checkpoint_pair','checkpoint_fallback','corrected_inputs','changed_inputs','source_revision',
            'pixel_diagnostics','pass_diagnostics','coherence_images_requested','completion_repeat',
            'seek_advance_failure','seek_observer_failure','seek_preparation_failure')))
    legacy=report.get("host_seek") and report.get("historical_cancel")=="after" and (
        deferred or private_hud_drained_case(report) or trace_render_recovery_case(report) or (
            report.get("seek_advance_failure") and report.get("historical_anchor_tick")==170
            and report.get("historical_advanced_tick") in (220,2504) and report.get("host_seek_target") in (214,5750,11000)))
    if (not (window_matches or copy_matches or legacy)
            or report.get("seek_ownership_protocol")!="retained_B_through_C_v1"
            or report.get("observations_requested")!=args.watch_frames
            or report.get("cleanup")!={"complete":True,"games_remaining":0}
            or report.get("result") not in ("captured","fail")
            or (report.get("result")=="fail" and report.get("failure") not in (
                "native boundary sequence is missing or invalid",
                "historical boundary capture lacks the exact admitted transaction sequence"))
            or any(report.get("identities",{}).get(key)!=value for key,value in current_identities(args).items())
            or not path.is_file() or sha256_file(path)!=raw.get("sha256")):
        raise RuntimeError("retained candidate is incomplete, changed, or not a parser-only failure")
    verify_retention(report["build_provenance"]["source_retention"])
    text=path.read_text(encoding="utf-8",errors="replace")
    if "run failed run_id=" in text:
        raise RuntimeError("a live failure cannot be revalidated as a completed capture")
    run=report["run_id"]
    checks={"payload_handoff":validate_payload_handoff(text,run),
        "initial_snapshot":validate_initial_snapshot(text,run),
        "timing":validate_trajectory_timing(text,run),
        "source_extents":validate_source_extents(text,run),
        "completion":validate_trajectory_completion(text,run,args.watch_frames,False)}
    from deterministic_qualification.replay_control import expected_historical_rewinds
    planned_rewinds=[] if copy_matches else expected_historical_rewinds(report) if window else [(report["historical_advanced_tick"],report["historical_anchor_tick"])]
    rows=validate_boundary_capture(text,run,historical_restore=bool(planned_rewinds),expected_rewinds=planned_rewinds,retained_execution=True,recovery_rewind=expected_recovery_rewind(report,text))
    if copy_matches:
        validate_particle_owner_copy(text,run,205,bool(report.get('particle_owner_registration')))
        if report.get('particle_owner_gpu'): validate_particle_gpu_owner(text,run,205)
        validate_callback_admission(text,210,anchor_tick=205,preparation_cancel_run=run)
    if not window and not copy_matches:
        validate_seek_recovery_ownership(text,report["historical_anchor_tick"],report["historical_advanced_tick"],report.get("host_seek_target",214) if report.get("seek_settlement_failure") else 208,report.get("host_seek_target",214),bool(report.get("seek_settlement_failure")),bool(report.get("seek_drained_cancel")),deferred=deferred)
    executor_proof(report,True)
    saved=OUTPUT/("candidate-before-revalidation-"+sha256_file(args.report)+".json")
    if not saved.exists(): saved.write_bytes(args.report.read_bytes())
    checks.update(result="captured",native_boundary_observations=len(rows),
        revalidation={"original_report":str(saved),"original_report_sha256":sha256_file(saved),
            "parser_source_retention":json.loads((OUTPUT/"build-provenance.json").read_text())["source_retention"],
            "scope":"parser-only; original binary/source identity and raw log unchanged"})
    report.update(checks);report.pop("failure",None)
    args.report.write_text(json.dumps(report,indent=2),encoding="utf-8")
    print("revalidated retained completed candidate; no live candidate rerun",flush=True)


def validate_seek_release_retirement(text: str, released: str, held_tick: int, count: int = 1,
                                    *, completed_release_required: bool = False) -> dict:
    retirements=list(re.finditer(r"seek checkpoint retired tick=(\d+) application_idle=true",text))
    if text.count(released)!=1 or len(retirements)!=count or int(retirements[-1][1])<held_tick:
        raise RuntimeError("seek release lacks completed-application checkpoint retirement")
    # The completed-application driver checks Completed directly and need not
    # encounter/log AcceptedPending. Its explicit contract still requires prior
    # retirement; absence of a pending poll is not legacy release semantics.
    completed_protocol=(completed_release_required or bool(re.search(r"host seek release waiting [^\n]*release_result=2",text))
                        or "seek_release_result=completed" in text)
    release_at=text.index(released);retire_at=retirements[-1].start()
    if (retire_at<release_at)!=completed_protocol:
        raise RuntimeError("seek release/retirement ordering contradicts its API protocol")
    if completed_protocol:
        pending=list(re.finditer(r"host seek release waiting [^\n]*release_result=2",text))
        if pending and pending[-1].start()>=retire_at:
            raise RuntimeError("accepted release work followed completed retirement")
    return {"completed_release_after_retirement":completed_protocol,"retirement_tick":int(retirements[-1][1])}


def validate_topology_admissions(text: str, identity: str, expected: list[tuple[int,int,bool]]) -> None:
    rows=re.findall(r"historical topology admitted run_id="+re.escape(identity)+r" target_tick=(\d+) original_tick=(\d+) particle_birth=(\S+)",text)
    if any(row[2] not in ("true","false") for row in rows):
        raise RuntimeError("invalid particle topology admission witness")
    observed=[(int(a),int(b),birth=="true") for a,b,birth in rows]
    # Distinct transactions can legitimately share A and B coordinates.
    # Compare the entire ordered sequence, not each marker's global count.
    if observed!=expected:
        raise RuntimeError("particle topology admission sequence is missing, duplicated or reordered")


def validate_corrected_revision_order(text: str, publication_label: str,
                                      completed_application: bool = False) -> None:
    markers=(publication_label+" run_id=", "source revision activated owner=runtime",
             "seek ownership state=Committing " if completed_application else "historical combat committed run_id=")
    if any(text.count(marker)!=1 for marker in markers):
        raise RuntimeError("source revision lacks unique publication, activation and commit evidence")
    positions=[text.index(marker) for marker in markers]
    if positions!=sorted(positions):
        raise RuntimeError("source revision was not activated after A publication and before commit")


def changed_input_target_coverage(target: int, publications: list[int], gameplay: list[int]) -> dict:
    # A publication at C feeds later execution; it does not prove changed-input
    # resimulation before C. Final equality is checked by the enclosing native
    # trajectory/callback comparison, never by these coverage counts alone.
    before_publications=[tick for tick in publications if tick<target]
    before_gameplay=[tick for tick in gameplay if tick<target]
    return {"result":"pass" if before_publications and before_gameplay else "not_exercised",
            "target":target,"changed_publication_ticks":before_publications,
            "changed_gameplay_ticks":before_gameplay,
            "scope":"changed inputs and gameplay before the target; requires enclosing independent native comparison"}


def compare_repeated_particle_seeds(text: str, run_id: str, anchor: int, target: int) -> dict:
    """Compare observations only; native tick entry at A traverses to A+1."""
    entry=rf"\[ReplayQualification\] boundary ordinal=\d+ phase=input_cache_publication sample_version=\d+ frame={anchor} "
    end=rf"\[ReplayQualification\] trajectory ordinal=\d+ phase=engine_post sample_version=\d+ frame={target} "
    starts=list(re.finditer(entry,text))
    if len(starts)!=2:
        raise RuntimeError("particle seed comparison requires exactly two native traversal entries")
    traversals=[]
    for i,start in enumerate(starts):
        stop=re.search(end,text[start.end():])
        if stop is None:
            raise RuntimeError("particle seed comparison lacks completed traversal")
        stop_at=start.end()+stop.end()
        if i==0 and stop_at>=starts[1].start():
            raise RuntimeError("particle seed comparison crosses a restore boundary")
        body=text[start.start():stop_at]
        pattern=rf"\[ReplayQualification\] particle seed run_id={re.escape(run_id)} tick=(\d+) game_thread=(\w+) initial=(\d+) current=(\d+)"
        rows=[(int(t),thread,int(initial),int(current)) for t,thread,initial,current in re.findall(pattern,body)]
        if any(thread!='true' for _,thread,_,_ in rows):
            raise RuntimeError("particle seed comparison observed a foreign thread")
        traversals.append(rows)
    expected,observed=traversals
    for i in range(max(len(expected),len(observed))):
        a=expected[i] if i<len(expected) else None
        b=observed[i] if i<len(observed) else None
        if a!=b:
            return {"result":"fail","first_mismatch":i,"expected":a,"observed":b,
                    "anchor":anchor,"target":target,"expected_events":len(expected),"observed_events":len(observed)}
    return {"result":"pass" if expected else "not_exercised","events":len(expected),"anchor":anchor,"target":target,
            "scope":"same-process ordered game-thread native particle seeds; independent gameplay/lifecycle control remains required"}


def compare_particle_lifecycle(native: dict, executor: dict, anchor: int, target: int, *, original_traversal=False) -> dict:
    """Compare ordered native lifecycle/seed receipts through the target tail."""
    def read(report, occurrence=-1):
        text=Path(report['raw_log']['path']).read_text(encoding='utf-8',errors='replace')
        starts=list(re.finditer(rf'\[ReplayQualification\] boundary ordinal=\d+ phase=input_cache_publication sample_version=\d+ frame={anchor} ',text))
        if not starts: raise RuntimeError('particle lifecycle lacks native traversal entry')
        start=starts[occurrence]
        end=re.search(rf'\[ReplayQualification\] trajectory ordinal=\d+ phase=engine_post sample_version=\d+ frame={target} ',text[start.end():])
        if not end: raise RuntimeError('particle lifecycle lacks completed target tail')
        body=text[start.start():start.end()+end.end()]
        rows=[]
        for line in body.splitlines():
            match=re.search(r'\[ReplayQualification\] particle (GPU initialized|retirement|seed) run_id=(\S+) (.*)',line)
            if not match: continue
            kind,run_id,fields=match.groups()
            if run_id!=report['run_id']: raise RuntimeError('particle lifecycle run identity changed')
            if kind=='retirement' and ('peer_notifications_and_destructor_returned=true' not in fields or 'slot_cleared=true' not in fields):
                raise RuntimeError('particle lifecycle retirement incomplete')
            if kind=='seed' and 'game_thread=true' not in fields: raise RuntimeError('particle lifecycle foreign RNG thread')
            fields=re.sub(r'(?:component|emitter)=[0-9a-f]+ ', '',fields)
            rows.append((kind,fields))
        return rows
    expected,observed=read(native,0 if original_traversal else -1),read(executor)
    for i in range(max(len(expected),len(observed))):
        a=expected[i] if i<len(expected) else None;b=observed[i] if i<len(observed) else None
        if a!=b:return {'result':'fail','first_mismatch':i,'expected':a,'observed':b,'anchor':anchor,'target':target}
    return {'result':'pass' if expected else 'not_exercised','events':len(expected),
            'initializers':sum(k=='GPU initialized' for k,_ in expected),
            'retirements':sum(k=='retirement' for k,_ in expected),'seeds':sum(k=='seed' for k,_ in expected),
            'anchor':anchor,'target':target,'independent':not original_traversal,
            'scope':'ordered native lifecycle and RNG receipts; native owner addresses are process-local'}


def coherence_capture_coverage(native: dict, executor: dict) -> dict:
    if not executor.get("coherence_images_requested"):
        return {"result":"not_requested"}
    targets=[int(executor.get("host_seek_target",208))]
    if executor.get("host_seek_repeat"):
        targets.append(host_seek_first_target(executor))
    guard=executor.get("source_revision_profile")=="guard201"
    if guard:
        targets.append(int(executor.get("historical_advanced_tick",2504)))
    required=[("executor",True,tick) for tick in targets]
    observation_target=executor.get("observation_target",0)
    resumed_images=(observation_target+22,observation_target+92) if observation_target>340 else (300,400) if guard else (230,300)
    required += [(owner,False,tick) for owner in ("native","executor") for tick in resumed_images]
    missing=[]
    for owner,held,tick in required:
        report=executor if owner=="executor" else native
        candidates=[item for item in report.get("coherence_images",{}).values()
                    if item.get("held")==held and item.get("tick")==tick
                    and item.get("run_id")==report.get("run_id") and item.get("gpu_complete")
                    and (owner=="native" or held or item.get("generation",0)>0)]
        if not candidates:
            missing.append(f"{owner}:{'held' if held else 'native'}:{tick}")
    return {"result":"fail" if missing else "pass","missing":missing,
            "scope":"required captures present; visual review remains separate; held images do not prove resumed rendering"}


def validate_publication_handoff_cancel(text: str, identity: str, anchor: int, original: int) -> dict:
    if (anchor,original)!=(170,2504):raise RuntimeError("publication cancellation requires the active-B plan")
    marker=f"historical cancellation injected run_id={identity} point=after tick=170 execution_handed_off=true simulation_ticks=0"
    publication=f"host seek publication observed run_id={identity} tick=170"
    handoff=f"host seek phase run_id={identity} phase=6 pending=true commit_decided=false"
    recovery=f"historical cancellation recovered run_id={identity} point=after tick=2504"
    if (any(text.count(m)!=1 for m in (publication,handoff,marker,recovery))
            or not text.index(publication)<text.index(handoff)<text.index(marker)<text.index(recovery)
            or "historical combat execution admitted run_id=" in text):
        raise RuntimeError("publication handoff cancellation lacks zero-traversal recovery evidence")
    tails=list(re.finditer(r"publication render tail completed tick=170 interval=(\d+) components=(\d+) gameplay_traversals=0 native_jobs_joined=true GPU_drain_pending=true B_retained=true",text))
    if len(tails)!=1 or min(map(int,tails[0].group(1,2)))<=0 or not text.index(marker)<tails[0].start()<text.index(recovery):
        raise RuntimeError("publication cancellation lacks nonempty native component completion before B recovery")
    between=text[text.index(handoff):text.index(recovery)]
    if "[ReplayQualification] boundary ordinal=" in between:
        raise RuntimeError("publication cancellation advanced native gameplay before B recovery")
    return {"result":"pass","boundary":"after execution handoff before simulation","B":original,
            "authored_B_continuation_required":120}


def validate_completion_repeat(texts, reports):
    if not any(report.get("completion_repeat") for report in reports):
        return {"result":"not_run"}
    if not all(report.get("completion_repeat") for report in reports):
        raise RuntimeError("completion repeat intervention differs between native and candidate")
    for text, report, count in zip(texts, reports, (1,2)):
        rows=re.findall(r"completion repeat intervention run_id=(\S+) tick=208 ordinal=(\d+) native_repeat=1",text)
        if rows != [(report["run_id"],str(i)) for i in range(1,count+1)]:
            raise RuntimeError("completion repeat intervention missing, duplicated or mismatched")
    held=re.findall(r"completion repeat held run_id=(\S+) tick=208 elapsed_us=(\d+) application_updates=(\d+) surface_frames=(\d+) unchanged=true pending_task=true B_retained=true callbacks_unchanged=true release_requires_resume=true",texts[1])
    if len(held)!=1 or held[0][0]!=reports[1]["run_id"] or int(held[0][1])<500000 or int(held[0][2])<30 or int(held[0][3])<20:
        raise RuntimeError("completion repeat lacks an independent responsive held boundary")
    deferred="seek exact completion deferred tick=208 simulation_phase=8 pending_task=true B_retained=true resume_required=true"
    cancel=reports[1].get("historical_cancel")=="after"
    step=("historical cancellation injected run_id="+reports[1]["run_id"]+" point=after tick=208 completion_deferred=true dispatch=host_api" if cancel else
          "historical single step requested run_id="+reports[1]["run_id"]+" origin=208 target=209 dispatch=host_api B_retained=true")
    if texts[1].count(deferred)!=1 or texts[1].count(step)!=1 or not texts[1].index(deferred)<texts[1].index("completion repeat held run_id=")<texts[1].index(step):
        raise RuntimeError("completion repeat lacks ordered exact deferral and explicit step")
    return {"result":"pass","held_tick":208,"explicit_step_tick":None if cancel else 209,"cancellation_boundary":"CompletionBlocked" if cancel else None,"application_updates":int(held[0][2]),
            "elapsed_us":int(held[0][1]),"native_intervention":"manager repeat208 in original, restored and independent native execution",
            "scope":"paused completion deferral then explicit host API cancellation or step; B recovery/continuation are compared separately", "ui_click_coverage":False}


def rolling_revision_assignments(wire,cycles,first):
    from deterministic_qualification.replay_control import rolling_correction_groups
    groups=rolling_correction_groups(wire)
    if not wire or not 1<=cycles<=600:raise ValueError('scheduled rolling requires bounded cycles and corrections')
    arrivals={}
    for revision,group in enumerate(groups):
        values=list(map(int,group[0].split(':')))
        if values[1]!=revision or not first+7<=values[0]<=first+6+cycles:
            raise ValueError('rolling correction revision or arrival outside campaign')
        arrivals[values[0]]=revision+1
    current=0;assignments={0:0}
    for ordinal in range(1,cycles+1):
        current=arrivals.get(first+6+ordinal,current);assignments[ordinal]=current
    return assignments


@timed('comparison.rolling')
def compare_scheduled_rolling_callbacks(controls,candidate,cycles):
    from deterministic_qualification.replay_control import expected_historical_rewinds,rolling_correction_prefix
    first=candidate.get('historical_anchor_tick',210)
    wire=candidate.get('rolling_corrections','')
    assignments=rolling_revision_assignments(wire,cycles,first)
    if set(controls)!=set(assignments.values()):raise RuntimeError('rolling independent histories incomplete')
    def read(report):
        if report.get('result')!='captured' or report.get('cleanup')!={'complete':True,'games_remaining':0}:
            raise RuntimeError('rolling capture or cleanup incomplete')
        path=Path(report['raw_log']['path'])
        if sha256_file(path)!=report['raw_log']['sha256']:raise RuntimeError('rolling raw log changed')
        return path.read_text(encoding='utf-8',errors='replace')
    candidate_text=read(candidate)
    costs=validate_rolling_candidate(candidate,cycles)['costs']
    for ordinal,row in enumerate(costs,1):
        revision=assignments[ordinal]
        if row.get('revision')!=str(revision) or row.get('checkpoint_generation')!=str(revision+1):
            raise RuntimeError('rolling committed checkpoint revision differs from authored schedule')
    observed=validate_boundary_capture(candidate_text,candidate['run_id'],historical_restore=True,
        expected_rewinds=expected_historical_rewinds(candidate),retained_execution=True)
    last=first+6+cycles+120
    if max(int(row['frame']) for row in observed)<last:raise RuntimeError('rolling lacks 120 native continuation ticks')
    histories={};traversals=[];hud=[];identities={candidate['run_id']}
    for revision,control in sorted(controls.items()):
        if control['run_id'] in identities:raise RuntimeError('rolling histories are not independent runs')
        identities.add(control['run_id'])
        if control.get('mode')!='stock' or control.get('loaded_runtime',{}).get('verification')!='owned_process_runtime_absent':
            raise RuntimeError('rolling history does not prove native runtime absence')
        if any(value!=candidate['identities'].get(key) for key,value in control['identities'].items() if key!='runtime'):
            raise RuntimeError('rolling history identities differ')
        if control.get('rolling_corrections','')!=rolling_correction_prefix(wire,revision):
            raise RuntimeError('rolling control authored history differs')
        text=read(control)
        histories[revision]=validate_boundary_capture(text,control['run_id'])
        ordinals=[ordinal for ordinal,rev in assignments.items() if ordinal and rev==revision]
        if ordinals:
            traversals.append(compare_rolling_traversals(text,candidate_text,control['run_id'],candidate['run_id'],cycles,first,ordinals=ordinals))
            hud.append(compare_rolling_hud(text,candidate_text,control['run_id'],candidate['run_id'],cycles,first,ordinals=ordinals))
    compared=compare_execution_callback_segments(histories[0],observed,last,
        segment_histories={ordinal:histories[revision] for ordinal,revision in assignments.items()})
    return {'result':'pass','rolling_cycles':cycles,'native_callbacks_compared':compared,
        'continuation_ticks':120,'revision_assignments':assignments,'traversals':traversals,'hud':hud,
        'scope':'independent authored history per traversal; normal-renderer coherence and performance remain separate'}


def validate_rolling_candidate(report, cycles):
    first=report.get('historical_anchor_tick',210)
    text=Path(report['raw_log']['path']).read_text(encoding='utf-8',errors='replace')
    rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if '[HorseMod] rolling cycle ordinal=' in line]
    if len(rows)!=cycles:raise RuntimeError('rolling lacks all consecutive committed cycles')
    for i,row in enumerate(rows):
        expected={'ordinal':str(i+1),'T':str(first+7+i),'A':str(first+i),'resimulated_ticks':'7',
            'committed':'true','tails_completed':'true','retirement_complete':'true'}
        if any(row.get(k)!=v for k,v in expected.items()):raise RuntimeError(f'rolling cycle mismatch at {i+1}')
    complete=re.findall(rf'rolling completed run_id=(\S+) first={first} last=(\d+) cycles=(\d+) resimulated_ticks=(\d+) resimulated_intervals=(\d+) peak_bytes=(\d+) missed=0',text)
    if len(complete)!=1 or complete[0][:4]!=(report['run_id'],str(first+6+cycles),str(cycles),str(7*cycles)):
        raise RuntimeError('rolling completion does not match requested window')
    motion=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if '[ReplayQualification] ground motion perturbed ' in line]
    probe=report.get('ground_motion_perturb',False)
    if bool(motion)!=bool(probe) or (probe and len(motion)!=1):raise RuntimeError('ground motion intervention missing, duplicate or unrequested')
    if probe:
        row=motion[0]
        if (cycles!=407 or first!=210 or row.get('run_id')!=report['run_id'] or row.get('tick')!='623'
                or any(row.get(k)!='true' for k in ('native_move','logical_unchanged','rng_unchanged','before_resume'))
                or any(not row.get(k,'').isdigit() for k in ('roots','meshes','changed','owned_bytes'))
                or not 1<=int(row['roots'])<=4 or not 1<=int(row['meshes'])<=64
                or row['changed']!=row['meshes'] or not 0<int(row['owned_bytes'])<=1024**3
                or text.index('[ReplayQualification] ground motion perturbed ')<text.index('[ReplayQualification] rolling completed ')):
            raise RuntimeError('ground motion intervention is invalid or incomplete')
    result={'result':'pass','cycles':cycles,'first_T':first+7,'last_T':first+6+cycles,'resimulated_ticks':7*cycles,
        'peak_bytes':int(complete[0][5]),'costs':rows,'scope':'native transaction receipts; independent comparison required'}
    from deterministic_qualification.replay_coverage import rolling_receipts
    result['observed_coverage']=rolling_receipts(text,report.get('rolling_corrections',''))
    if probe:result['motion_probe']={k:int(motion[0][k]) for k in ('roots','meshes','changed','owned_bytes')}
    return result


def read_particle_receiver_stream(text, identity):
    """Native receiver return observations, joined to completed world traversals."""
    stream={}; pending=[]
    fields={'tick','ordinal','asset','routes','events','event_hash','before_count','after_count',
            'before_hash','after_hash','rng_before','rng_after','delta','native_returned','game_thread','read_only'}
    for line in text.splitlines():
        if '[ReplayQualification] particle receiver run_id=' in line:
            row=dict(re.findall(r'(\w+)=([^\s]+)',line))
            if row.pop('run_id',None)!=identity or not fields<=row.keys():
                raise RuntimeError('rolling receiver identity/fields missing')
            if any(row[k]!='true' for k in ('native_returned','game_thread','read_only')):
                raise RuntimeError('rolling receiver native return/thread invalid')
            if (int(row['ordinal'])!=len(pending) or not 1<=int(row['routes'])<=128
                    or any(not 0<=int(row[k])<=4096 for k in ('before_count','after_count'))
                    or not re.fullmatch(r'\d+,\d+,\d+,\d+,\d+',row['events'])
                    or any(int(n)>4096 for n in row['events'].split(','))):
                raise RuntimeError('rolling receiver order/bounds invalid')
            if any(not re.fullmatch(r'[0-9a-f]+',row[k]) for k in
                   ('asset','event_hash','before_hash','after_hash','rng_before','rng_after','delta')):
                raise RuntimeError('rolling receiver state malformed')
            pending.append(row)
        elif '[ReplayQualification] particle receivers completed run_id=' in line:
            row=dict(re.findall(r'(\w+)=([^\s]+)',line))
            if (row.get('run_id')!=identity or row.get('schema')!='1' or row.get('read_only')!='true'
                    or not {'tick','generation','calls'}<=row.keys()):
                raise RuntimeError('rolling receiver completion invalid')
            key=(int(row['generation']),int(row['tick']))
            if key in stream or not 0<=int(row['calls'])<=1024 or int(row['calls'])!=len(pending):
                raise RuntimeError('rolling receiver completion missing/duplicated calls')
            stream[key]=pending;pending=[]
    return stream


def compare_rolling_traversals(native_text, candidate_text, native_id, candidate_id, cycles, first=210,*,ordinals=None):
    """Compare every regenerated pose/lifecycle traversal plus ordinary progress."""
    from bisect import bisect_left, bisect_right
    texts=(native_text,candidate_text)
    poses=read_native_pose_streams(texts,[{'run_id':native_id},{'run_id':candidate_id}])
    receivers=[read_particle_receiver_stream(text,identity) for text,identity in zip(texts,(native_id,candidate_id))]
    inventories=[]
    for text,identity in zip(texts,(native_id,candidate_id)):
        starts={}
        for m in re.finditer(r'\[ReplayQualification\] boundary ordinal=\d+ phase=input_cache_publication sample_version=\d+ frame=(\d+) ',text):
            starts[int(m[1])]=m.start() # Last occurrence is this rolling window's traversal.
        tails={}
        for m in re.finditer(r'\[ReplayQualification\] trajectory ordinal=\d+ phase=engine_post sample_version=\d+ frame=(\d+) ',text):
            tails.setdefault(int(m[1]),[]).append(m.end())
        events=[];positions=[]
        for m in re.finditer(r'\[ReplayQualification\] particle (GPU initialized|retirement|seed) run_id=(\S+) ([^\r\n]+)',text):
            kind,run,fields=m.groups()
            if run!=identity:raise RuntimeError('rolling lifecycle run identity changed')
            if kind=='retirement' and ('peer_notifications_and_destructor_returned=true' not in fields or 'slot_cleared=true' not in fields):
                raise RuntimeError('rolling lifecycle retirement incomplete')
            if kind=='seed' and 'game_thread=true' not in fields:raise RuntimeError('rolling lifecycle foreign RNG thread')
            positions.append(m.start());events.append((kind,re.sub(r'(?:component|emitter)=[0-9a-f]+ ', '', fields)))
        inventories.append((starts,tails,positions,events))
    def window(inventory,first,last):
        starts,tails,positions,events=inventory
        if first not in starts:raise RuntimeError(f'rolling lifecycle missing entry tick{first}')
        start=starts[first];ends=tails.get(last,[]);at=bisect_right(ends,start)
        if at==len(ends):raise RuntimeError(f'rolling lifecycle missing tail tick{last}')
        return events[bisect_left(positions,start):bisect_left(positions,ends[at])]
    pose_count=event_count=receiver_count=0
    for ordinal in (range(1,cycles+1) if ordinals is None else ordinals):
        if not 1<=ordinal<=cycles:raise RuntimeError("invalid rolling traversal ordinal")
        anchor=first-1+ordinal;target=anchor+7
        last=target+(120 if ordinal==cycles else 1)
        for tick in range(anchor+1,last+1):
            a,b=poses[0].get((0,tick)),poses[1].get((ordinal,tick))
            if a is None or b is None or a!=b:
                raise RuntimeError(f'rolling native pose mismatch/missing cycle{ordinal} tick{tick}')
            pose_count+=1
            a,b=receivers[0].get((0,tick)),receivers[1].get((ordinal,tick))
            # This lifecycle gate requires a positive receiver witness on each
            # tested traversal, including all120 continuation ticks. A stage
            # with no receiver cannot silently qualify this coverage.
            if not a or a!=b:
                raise RuntimeError(f'rolling receiver mismatch/missing cycle{ordinal} tick{tick}')
            receiver_count+=len(a)
        a,b=window(inventories[0],anchor,last),window(inventories[1],anchor,last)
        if a!=b:
            i=next(i for i in range(max(len(a),len(b))) if i>=len(a) or i>=len(b) or a[i]!=b[i])
            raise RuntimeError(f'rolling lifecycle mismatch cycle{ordinal} event{i}: expected={a[i] if i<len(a) else None} observed={b[i] if i<len(b) else None}')
        event_count+=len(a)
    return {'result':'pass','cycles':cycles,'pose_traversals':pose_count,'lifecycle_events':event_count,
        'receiver_traversals':pose_count,'receiver_calls':receiver_count,
        'continuation_ticks':120,'scope':'independent complete Lux poses, ordered lifecycle and native receiver return/state/RNG receipts; downstream coherence and performance separate'}


def compare_rolling_hud(native_text, candidate_text, native_id, candidate_id, cycles, first=210,*,ordinals=None):
    """Join application HUD reads to their following completed pose traversal."""
    streams=[]
    required={'tick','p1','p2','combo1','combo2','timer','damage','types','pool','active','type_pool','type_active','type_players','read_only'}
    for text,identity in zip((native_text,candidate_text),(native_id,candidate_id)):
        pending=[];stream={}
        for line in text.splitlines():
            if '[ReplayQualification] HUD state tick=' in line or '[ReplayQualification] HUD clock tick=' in line:
                kind='state' if 'HUD state tick=' in line else 'clock'
                row=dict(re.findall(r'(\w+)=([^\s]+)',line));row.pop('slate_delta',None)
                pending.append((kind,row))
            m=re.search(r'native pose completed run_id=(\S+) tick=(\d+) generation=(\d+) evaluations=',line)
            if not m:continue
            if m[1]!=identity:raise RuntimeError('rolling HUD completion identity differs')
            tick,generation=int(m[2]),int(m[3]);key=(generation,tick)
            rows=[(kind,row) for kind,row in pending if int(row['tick'])==tick];pending=[]
            if key in stream:raise RuntimeError('rolling HUD completion duplicated')
            stream[key]=rows
        streams.append(stream)
    count=clocks=0
    for generation in (range(1,cycles+1) if ordinals is None else ordinals):
        if not 1<=generation<=cycles:raise RuntimeError("invalid rolling HUD traversal ordinal")
        anchor=first-1+generation;last=anchor+7+(120 if generation==cycles else 1)
        for tick in range(anchor+1,last+1):
            a,b=streams[0].get((0,tick)),streams[1].get((generation,tick))
            states=[row for kind,row in (a or []) if kind=='state']
            if len(states)!=1 or not required<=states[0].keys() or states[0]['read_only']!='true' or a!=b:
                raise RuntimeError(f'rolling HUD mismatch/missing cycle{generation} tick{tick}')
            updates=[row for kind,row in a if kind=='clock']
            if int(states[0]['active']) and not updates:
                raise RuntimeError(f'rolling HUD active clock missing cycle{generation} tick{tick}')
            if any(not {'widget','player','delta','before','after','read_only'}<=row.keys() or row['read_only']!='true' for row in updates):
                raise RuntimeError('rolling HUD clock incomplete')
            count+=1;clocks+=len(updates)
    return {'result':'pass','hud_traversals':count,'active_player_updates':clocks,'continuation_ticks':120,
        'scope':'independent HUD fields and ordered widget-class/player clocks; physical Slate delta excluded; widget instance identity unobserved'}


def compare_rolling_callbacks(native, candidate, cycles):
    from deterministic_qualification.replay_control import expected_historical_rewinds
    if native['run_id']==candidate['run_id'] or any(v!=candidate['identities'].get(k) for k,v in native['identities'].items() if k!='runtime'):
        raise RuntimeError('rolling control identities differ')
    texts=[]
    for report in (native,candidate):
        if report.get('result')!='captured' or report.get('cleanup')!={'complete':True,'games_remaining':0}:
            raise RuntimeError('rolling capture or cleanup incomplete')
        if sha256_file(Path(report['raw_log']['path']))!=report['raw_log']['sha256']:raise RuntimeError('rolling raw log changed')
        texts.append(Path(report['raw_log']['path']).read_text(encoding='utf-8',errors='replace'))
    reference=validate_boundary_capture(texts[0],native['run_id'])
    observed=validate_boundary_capture(texts[1],candidate['run_id'],historical_restore=True,
        expected_rewinds=expected_historical_rewinds(candidate),retained_execution=True)
    first=candidate.get('historical_anchor_tick',210)
    last=first+6+cycles+120
    if max(int(r['frame']) for r in observed)<last:raise RuntimeError('rolling lacks 120 native continuation ticks')
    count=compare_execution_callback_segments(reference,observed,last)
    fidelity=compare_rolling_traversals(*texts,native['run_id'],candidate['run_id'],cycles,first)
    hud=compare_rolling_hud(*texts,native['run_id'],candidate['run_id'],cycles,first)
    return {'result':'pass','traversals':fidelity,'hud':hud,'rolling_cycles':cycles,'native_callbacks_compared':count,'continuation_ticks':120,
        'scope':'independent rolling callbacks, complete Lux poses and lifecycle; coherence review and performance separate'}


def compare_execution_callback_segments(native_rows,candidate_rows,last_tick,*,segment_histories=None):
    def value(row): return {key:val for key,val in row.items() if key!="ordinal"}
    reference=[value(row) for row in native_rows if int(row["frame"])<=last_tick]
    segments=[]
    for row in candidate_rows:
        if int(row["frame"])>last_tick: continue
        if not segments or int(row["frame"])<int(segments[-1][-1]["frame"]): segments.append([])
        segments[-1].append(value(row))
    if not segments: raise RuntimeError("execution fallback has no native callback segments")
    compared=0
    if segment_histories is not None and set(segment_histories)!=set(range(len(segments))):
        raise RuntimeError("rolling callback history missing or extra traversal")
    for ordinal,segment in enumerate(segments):
        if segment_histories is not None:
            reference=[value(row) for row in segment_histories[ordinal] if int(row["frame"])<=last_tick]
        starts=[i for i,row in enumerate(reference) if row==segment[0]]
        if sum(reference[i:i+len(segment)]==segment for i in starts)!=1:
            offset=0
            if starts:
                source=reference[starts[0]:starts[0]+len(segment)]
                offset=next((i for i,row in enumerate(segment) if i>=len(source) or row!=source[i]),0)
            bad=segment[offset]
            raise RuntimeError(f"execution fallback native callback mismatch phase={bad['phase']} tick={bad['frame']}")
        compared+=len(segment)
    return compared


def compare_execution_fallback(reports):
    native,executor=reports
    def native_dependencies(report):
        return {key: value for key, value in report["identities"].items() if key != "runtime"}
    if native["run_id"] == executor["run_id"] or native_dependencies(native) != native_dependencies(executor):
        raise RuntimeError("combat restore requires independent runs with identical binaries/replay")
    texts = []
    for report, mode in zip(reports, ("stock", "runtime")):
        if report.get("result") != "captured" or report.get("mode") != mode or not report.get("skip_intros") or not report.get("native_fixed_seed") or not report.get("serial_particles"):
            raise RuntimeError("combat restore capture/protocol is incomplete")
        if report.get("cleanup") != {"complete": True, "games_remaining": 0}:
            raise RuntimeError("combat restore cleanup is incomplete")
        proof = report["loaded_runtime"]
        required = "owned_process_runtime_absent" if mode == "stock" else "owned_process_mapped_file_and_sha256"
        if proof.get("verification") != required or (mode == "runtime" and proof.get("sha256") != report["identities"]["runtime"]):
            raise RuntimeError("combat restore runtime mapping is unverified")
        for key in ("observer", "framework", "ucrt", "physx"):
            loaded = report.get("loaded_" + key, {})
            if (loaded.get("sha256") != report["identities"].get(key) or loaded.get("pid") != proof.get("pid")
                    or loaded.get("process_created") != proof.get("process_created")):
                raise RuntimeError("combat restore dependency identity is mixed: " + key)
        raw = report["raw_log"]
        if sha256_file(Path(raw["path"])) != raw["sha256"]:
            raise RuntimeError("combat restore raw evidence changed")
        text = Path(raw["path"]).read_text(encoding="utf-8", errors="replace")
        prefix=executor.get('authored_prefix','')
        if report.get('authored_prefix','')!=prefix:raise RuntimeError('short diagnostic authored histories differ')
        if prefix:
            from deterministic_qualification.replay_control import authored_prefix_request
            authored_prefix_request(prefix)
            observed=re.findall(r'rolling authored control run_id=(\S+) arrival=(\d+) expected_revision=(\d+) round=(\d+) sample=(\d+) players=(\d+) raw0=(\d+) raw1=(\d+) native_source=true',text)
            expected=[(report['run_id'],*row.split(':')) for row in prefix.split(';')]
            if observed!=expected:raise RuntimeError('short diagnostic authored prefix receipt differs')
        validate_intro_skips(text, report["run_id"])
        # The executable leaves RingIn travel distance uninitialized. Both
        # independent runs must use the explicit construction correction;
        # this is not an unmodified-stock presentation comparison.
        owner = "control" if mode == "stock" else "runtime"
        starts = re.findall(r"wind construction started owner=(\w+) policy=(\w+) tick=(\d+)", text)
        stops = re.findall(r"wind construction stopped owner=(\w+) policy=(\w+) initialized=(\d+) failed=(\w+) detached=true", text)
        if (starts != [(owner, "zero_initial_distance_v1", "0")] or len(stops) != 1
                or stops[0][:2] != (owner, "zero_initial_distance_v1")
                or int(stops[0][2]) == 0 or stops[0][3] != "false"):
            raise RuntimeError("combat restore wind construction protocol is missing or failed")
        render_starts = re.findall(r"replay rendering started owner=(\w+) policy=(\w+) tick=(\d+)", text)
        render_stops = re.findall(r"replay rendering stopped owner=(\w+) policy=(\w+) constructed=(\d+) completed=(\d+) failed=(\w+) detached=true", text)
        if (render_starts != [(owner, "fixed60_render_v7", "0")] or len(render_stops) != 1
                or render_stops[0][:2] != (owner, "fixed60_render_v7")
                or int(render_stops[0][2]) < 120 or render_stops[0][2] != render_stops[0][3]
                or render_stops[0][4] != "false"):
            raise RuntimeError("combat restore logical rendering protocol is missing or failed")
        texts.append(text)
    from deterministic_qualification.replay_fidelity import validate_payload_handoff
    receipt=execution_fallback_receipt(texts[1],executor["run_id"])
    split=receipt["switch_position"]
    recovery=validate_seek_recovery_ownership(texts[1][:split],205,210,receipt["completed_tick"],220,lifetime=True)
    commit=validate_seek_commit_ownership(texts[1][split:],170,210,220)
    if recovery["executed_ticks"]!=receipt["discarded_ticks"] or recovery["executed_intervals"]!=receipt["discarded_intervals"]:
        raise RuntimeError("execution fallback discard accounting differs")
    native_rows=validate_boundary_capture(texts[0],native["run_id"])
    candidate_rows=validate_boundary_capture(texts[1],executor["run_id"],historical_restore=True,
        expected_rewinds=[(210,205),(210,170)],retained_execution=True,execution_retry=True)
    ignored={"ordinal"}
    def value(row): return {key:val for key,val in row.items() if key not in ignored}
    compared=compare_execution_callback_segments(native_rows,candidate_rows,340)
    final=texts[1][texts[1].index("historical combat execution admitted",split):]
    final_rows=[row for row in candidate_rows if 220<int(row["frame"])<=340]
    if {int(row["frame"]) for row in final_rows}!=set(range(221,341)) or sum(row["phase"]=="actor_tail" for row in final_rows)!=120:
        raise RuntimeError("execution fallback lacks120 independently observed continuation ticks")
    # Complete B recovery has its own independent read, not a simulation input.
    def trajectories(text,label):
        return [dict(re.findall(r"(\w+)=([^\s]+)",line)) for line in text.splitlines()
                if "[ReplayQualification] "+label+" ordinal=" in line]
    original=trajectories(texts[1],"historical_original")
    recovered=trajectories(texts[1],"execution_fallback_recovered_B")
    controls=[row for row in trajectories(texts[0],"trajectory") if row.get("frame")=="210"]
    if len(original)!=1 or len(recovered)!=1 or len(controls)!=1 or value(original[0])!=value(recovered[0]) or value(original[0])!=value(controls[0]):
        raise RuntimeError("execution fallback B observation differs from independent native B")
    payloads=[validate_payload_handoff(text,report["run_id"]) for text,report in zip(texts,reports)]
    if payloads[0]!=payloads[1]: raise RuntimeError("execution fallback replay payload identity differs")
    if validate_intro_skips(texts[0],native["run_id"])!=validate_intro_skips(texts[1],executor["run_id"]):
        raise RuntimeError("execution fallback intro-skip intervention differs")
    poses=compare_native_poses(texts,reports,220,"")
    hud=compare_widget_continuation(texts[0],final,220)
    pacing=validate_world_resume_timing(texts[1],executor["run_id"])
    pacing["result"]="pass" if pacing["tick_rate_milli"]>=58000 else "fail"
    held=re.findall(r"interior pause held run_id=(\S+) native_frame=220 epoch=\d+ elapsed_us=(\d+) application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) unchanged=true pending_event=true",texts[1])
    if len(held)!=1 or held[0][0]!=executor["run_id"] or int(held[0][1])<1000000 or min(map(int,held[0][2:]))<30:
        raise RuntimeError("execution fallback target lacks a stable external interior hold")
    released=f"host seek released run_id={executor['run_id']} tick=220 surface_pending=false"
    retirement=validate_seek_release_retirement(texts[1],released,220,count=2)
    captures=re.findall(r"capture operation ready tick=(\d+) elapsed_us=(\d+) owned_bytes=(\d+) immutable=true gpu_complete=true",texts[1])
    if [int(row[0]) for row in captures]!=[170,205] or any(not 0<int(row[2])<=PRODUCTION_REPLAY_MEMORY_LIMIT for row in captures):
        raise RuntimeError("execution fallback capture ownership/memory is incomplete")
    return {"result":"pass" if poses["result"]==hud["result"]==pacing["result"]=="pass" else "fail",
        "scope":"natural lifetime failure, complete B recovery, automatic earlier retry, completed220 and120 native continuation ticks",
        "recovery":recovery,"commit":commit,"release":retirement,"callbacks_compared":compared,"native_poses":poses,"native_hud":hud,
        "resume_timing":pacing,"capture_owners":captures,"pixel_equality_required":False,
        "visual_coherence":{"result":"incomplete","scope":"native poses/HUD checked; no new images"},
        "practical_rollback_qualified":False,"expected_state_installed":False}


@timed('comparison')
@producer
def compare_historical_combat(native_path: Path, executor_path: Path) -> dict:
    """Fixed combat transaction; observations are never fed back to the game."""
    from deterministic_qualification.replay_fidelity import validate_boundary_capture, validate_payload_handoff, particle_lifetime_recovery, particle_lifetime_abort_tick
    reports = [json.loads(path.read_text(encoding="utf-8")) for path in (native_path, executor_path)]
    native, executor = reports
    if execution_fallback_case(executor): return compare_execution_fallback(reports)
    anchor = int(executor.get("historical_anchor_tick",205))
    checkpoint_window=bounded_checkpoint_window(executor)
    cancellation = executor.get("historical_cancel", "")
    consumer_mutation=bool(executor.get("consumer_mutation"))
    lifetime_recovery=particle_lifetime_recovery(executor)
    advance_failure=bool(executor.get("seek_advance_failure"))
    settlement_failure=bool(executor.get("seek_settlement_failure"))
    owned_execution=executor.get("seek_ownership_protocol")=="retained_B_through_C_v1"
    restore_reuse=bool(executor.get("restore_reuse"))
    host_seek=bool(executor.get("host_seek"))
    complete_seek_application=bool(executor.get("complete_seek_application"))
    if complete_seek_application and (not host_seek or cancellation or executor.get("host_seek_repeat")
            or executor.get("historical_single_step") or executor.get("pixel_diagnostics")
            or executor.get("coherence_images_requested")):
        raise RuntimeError("completed-application cost requires one committed seek without diagnostic images")
    repeat_seek=bool(executor.get("host_seek_repeat"))
    pair=bool(executor.get("checkpoint_pair"))
    if pair and (not repeat_seek or not host_seek or executor.get("changed_inputs") or executor.get("corrected_inputs")):
        raise RuntimeError("checkpoint pair requires the unchanged-input repeated host seek")
    if (anchor not in (170,205) and not checkpoint_window) or (anchor==170 and (not host_seek or pair
            or (repeat_seek and (cancellation or executor.get("changed_inputs")))
            or ((executor.get("corrected_inputs") or executor.get("changed_inputs"))
                and (not executor.get("corrected_inputs") or not executor.get("source_revision") or cancellation)))):
        raise RuntimeError("unsupported anchor protocol")
    deferred_cancel=bool(executor.get("completion_repeat")) and cancellation=="after"
    if executor.get("completion_repeat") and (not host_seek or repeat_seek or anchor!=205
            or (not executor.get("historical_single_step") if not cancellation else not deferred_cancel or executor.get("historical_single_step"))):
        raise RuntimeError("invalid completion-repeat experiment")
    first_checkpoint=206 if pair else anchor
    if repeat_seek and (not host_seek or cancellation not in (("","before","after") if pair else ("","after")) or executor.get("corrected_inputs") or (executor.get("pixel_diagnostics") and not pair)):
        raise RuntimeError("invalid repeated seek protocol")
    exact_target=int(executor.get("host_seek_target",208)) if host_seek else 208
    midpoint_recovery=(settlement_failure and exact_target in (5750,11000) and executor.get("historical_advanced_tick")==2504)
    private_hud_drained=private_hud_drained_case(executor)
    trace_render_recovery=trace_render_recovery_case(executor)
    ground_commit=ground_commit_case(executor)
    guard_resimulation=(exact_target==300 and anchor==170 and executor.get("historical_advanced_tick") in (300,2504)
        and executor.get("corrected_inputs") and executor.get("source_revision")
        and executor.get("source_revision_profile")=="guard201" and not cancellation and not repeat_seek)
    if exact_target not in range(205,221) and not midpoint_recovery and not guard_resimulation and not private_hud_drained and not trace_render_recovery and not ground_commit and not checkpoint_window: raise RuntimeError("unsupported exact seek target")
    emitter_recovery=settlement_failure and anchor==205 and executor.get("historical_advanced_tick")==210 and exact_target==208
    if advance_failure and (not owned_execution or not host_seek or cancellation!="after" or (not emitter_recovery and not lifetime_recovery and not private_hud_drained and not trace_render_recovery and (anchor!=170
            or (exact_target!=214 and not midpoint_recovery))) or repeat_seek or executor.get("seek_observer_failure") or executor.get("seek_preparation_failure")):
        raise RuntimeError("invalid bounded advance-failure protocol")
    first_target=host_seek_first_target(executor)
    preparation_failure=bool(executor.get("seek_preparation_failure"))
    if preparation_failure and (not repeat_seek or pair or cancellation!="after" or first_target!=220 or exact_target!=208):
        raise RuntimeError("invalid preparation-failure recovery protocol")
    publication_label="host seek publication observed" if host_seek else "historical target held"
    if host_seek and (restore_reuse or (not cancellation and not executor.get("historical_exact_advance"))):
        raise RuntimeError("host seek protocol mismatch")
    if restore_reuse and cancellation: raise RuntimeError("restore reuse requires a final commit")
    restore_count=2 if restore_reuse or repeat_seek else 1
    publication_count=restore_count-(1 if cancellation=="before" or preparation_failure else 0)
    if cancellation not in ("", "before", "after"):
        raise RuntimeError("invalid historical cancellation evidence")
    advanced_tick = executor.get("historical_advanced_tick", 210)
    private_hud_recovery=(anchor==170 and advanced_tick==300 and host_seek and exact_target==208
        and cancellation in ("before","after") and not repeat_seek
        and not any(executor.get(k) for k in ("corrected_inputs","changed_inputs","source_revision","seek_advance_failure","seek_settlement_failure")))
    if advanced_tick not in ((220,2504) if anchor==170 else (211,) if pair else (209,210)) and not (advanced_tick==300 and guard_resimulation) and not private_hud_recovery and not private_hud_drained and not trace_render_recovery and not ground_commit and not checkpoint_window:
        raise RuntimeError("unsupported historical interval evidence")
    exact_advance = bool(executor.get("historical_exact_advance", False))
    if exact_advance and cancellation and not repeat_seek and not checkpoint_window: raise RuntimeError("exact advancement cannot validate cancellation")
    undo_tick=first_target if repeat_seek else advanced_tick
    resumed_tick = undo_tick if cancellation else anchor
    single_step = bool(executor.get("historical_single_step", False))
    if single_step and not exact_advance: raise RuntimeError("single step requires exact advancement")
    if single_step and (repeat_seek or cancellation or exact_target!=208): raise RuntimeError("unsupported single-step seek combination")
    held_tick = resumed_tick if cancellation else (209 if single_step else exact_target) if exact_advance else resumed_tick
    continuation_end = held_tick + 120
    compared_ticks = continuation_end - resumed_tick
    def native_dependencies(report):
        return {key: value for key, value in report["identities"].items() if key != "runtime"}
    if native["run_id"] == executor["run_id"] or native_dependencies(native) != native_dependencies(executor):
        raise RuntimeError("combat restore requires independent runs with identical binaries/replay")
    texts = []
    for report, mode in zip(reports, ("stock", "runtime")):
        if report.get("result") != "captured" or report.get("mode") != mode or not report.get("skip_intros") or not report.get("native_fixed_seed") or not report.get("serial_particles"):
            raise RuntimeError("combat restore capture/protocol is incomplete")
        if report.get("cleanup") != {"complete": True, "games_remaining": 0}:
            raise RuntimeError("combat restore cleanup is incomplete")
        proof = report["loaded_runtime"]
        required = "owned_process_runtime_absent" if mode == "stock" else "owned_process_mapped_file_and_sha256"
        if proof.get("verification") != required or (mode == "runtime" and proof.get("sha256") != report["identities"]["runtime"]):
            raise RuntimeError("combat restore runtime mapping is unverified")
        for key in ("observer", "framework", "ucrt", "physx"):
            loaded = report.get("loaded_" + key, {})
            if (loaded.get("sha256") != report["identities"].get(key) or loaded.get("pid") != proof.get("pid")
                    or loaded.get("process_created") != proof.get("process_created")):
                raise RuntimeError("combat restore dependency identity is mixed: " + key)
        raw = report["raw_log"]
        if sha256_file(Path(raw["path"])) != raw["sha256"]:
            raise RuntimeError("combat restore raw evidence changed")
        text = Path(raw["path"]).read_text(encoding="utf-8", errors="replace")
        validate_intro_skips(text, report["run_id"])
        # The executable leaves RingIn travel distance uninitialized. Both
        # independent runs must use the explicit construction correction;
        # this is not an unmodified-stock presentation comparison.
        owner = "control" if mode == "stock" else "runtime"
        starts = re.findall(r"wind construction started owner=(\w+) policy=(\w+) tick=(\d+)", text)
        stops = re.findall(r"wind construction stopped owner=(\w+) policy=(\w+) initialized=(\d+) failed=(\w+) detached=true", text)
        if (starts != [(owner, "zero_initial_distance_v1", "0")] or len(stops) != 1
                or stops[0][:2] != (owner, "zero_initial_distance_v1")
                or int(stops[0][2]) == 0 or stops[0][3] != "false"):
            raise RuntimeError("combat restore wind construction protocol is missing or failed")
        render_starts = re.findall(r"replay rendering started owner=(\w+) policy=(\w+) tick=(\d+)", text)
        render_stops = re.findall(r"replay rendering stopped owner=(\w+) policy=(\w+) constructed=(\d+) completed=(\d+) failed=(\w+) detached=true", text)
        if (render_starts != [(owner, "fixed60_render_v7", "0")] or len(render_stops) != 1
                or render_stops[0][:2] != (owner, "fixed60_render_v7")
                or int(render_stops[0][2]) < 120 or render_stops[0][2] != render_stops[0][3]
                or render_stops[0][4] != "false"):
            raise RuntimeError("combat restore logical rendering protocol is missing or failed")
        texts.append(text)
    from deterministic_qualification.replay_fidelity import consumer_mutation_abort_tick
    consumer_tick=consumer_mutation_abort_tick(texts[1]) if consumer_mutation else None
    lifetime_tick=particle_lifetime_abort_tick(texts[1]) if lifetime_recovery else None
    widget_clock={"result":"not_exercised"}
    if any("replay widget clock started owner=" in text for text in texts):
        calls=[]
        native_clock_rows=[]
        for text,owner in zip(texts,("control","runtime")):
            starts=re.findall(r"replay widget clock started owner=(\w+) policy=(\w+) tick=(\d+)",text)
            stops=re.findall(r"replay widget clock stopped owner=(\w+) policy=(\w+) calls=(\d+) failed=(\w+) detached=true",text)
            if starts!=[(owner,"fixed60_application_widget_v1","0")] or len(stops)!=1 or stops[0][:2]!=(owner,"fixed60_application_widget_v1") or int(stops[0][2])<120 or stops[0][3]!="false":
                raise RuntimeError("widget scheduling clock admission or retirement is incomplete")
            calls.append(int(stops[0][2]))
            rows_clock=re.findall(r"HUD clock tick=(\d+) widget=(\S+) player=(\d+) delta=([0-9.]+) slate_delta=([0-9.]+) before=([0-9.]+) after=([0-9.]+) read_only=true",text)
            # Native141826F90 clamps completed players to their authored end,
            # and also supports looping/reverse traversal. Delta is fixed60;
            # after-before need not equal it. Nonlinear candidate transitions
            # require the exact independently observed native tick/player/time
            # tuple. Slate wall time is deliberately excluded from that tuple.
            if not rows_clock or any(abs(float(row[3])-1/60)>1e-8 or (
                    abs(float(row[6])-float(row[5])-float(row[3]))>2e-8
                    and owner!="control" and row[:4]+row[5:] not in
                        {native_row[:4]+native_row[5:] for native_row in native_clock_rows}) for row in rows_clock):
                raise RuntimeError("damage widget still consumes held wall time or advances unexpectedly")
            if owner=="control": native_clock_rows=rows_clock
        widget_clock={"result":"pass","policy":"fixed60_application_widget_v1","calls":calls,
            "scope":"admitted native widget calls keep native skip/callback semantics; fixed delta and independently witnessed nonlinear player transitions; continuation compared separately"}
    topology_observed=True
    topology_birth=advanced_tick>=210
    expected_topology=[]
    if "historical_advanced_tick" in executor:
        topology_birth=advanced_tick>=210
        if advanced_tick==2504 or checkpoint_window:
            observed_topology=re.findall(r"historical topology admitted run_id="+re.escape(executor['run_id'])+rf" target_tick={first_checkpoint} original_tick={advanced_tick} particle_birth=(true|false)",texts[1])
            if len(observed_topology)!=(0 if cancellation=="before" else 1):
                raise RuntimeError("cross-round topology observation is missing or duplicated")
            topology_birth=bool(observed_topology and observed_topology[0]=="true")
        topology = f"historical topology admitted run_id={executor['run_id']} target_tick={first_checkpoint} original_tick={advanced_tick} particle_birth={str(topology_birth).lower()}"
        if host_seek and cancellation=="before" and texts[1].count(topology)==0:
            # Cancellation reaches the host before publication admission. Do
            # not invent that coverage from the requested B coordinate.
            topology_observed=False
        else:
            expected_topology=[(first_checkpoint,advanced_tick,topology_birth)]*(1 if repeat_seek else restore_count)
        if repeat_seek and cancellation!="before" and not preparation_failure:
            expected_topology.append((anchor,first_target,first_target>=(177 if anchor==170 else 210)))
        validate_topology_admissions(texts[1],executor['run_id'],expected_topology)
    second_undo_captured=preparation_failure and f"restore undo captured target=205 original={first_target} gpu_complete=true before_target_preparation=true" in texts[1]
    preparation_cancel_run=(executor['run_id'] if host_seek and cancellation=="before" and not repeat_seek
        and f"historical cancellation injected run_id={executor['run_id']} point=before tick={advanced_tick} preparation_pending=true " in texts[1] else None)
    callbacks = (validate_callback_admission(texts[1], advanced_tick, bool(executor.get("capture_cancel")),restore_reuse,repeat_seek and (not preparation_failure or second_undo_captured),first_target,pair,anchor,bool(executor.get("checkpoint_fallback")),preparation_cancel_run)
                 if executor.get("callback_admission_policy") in ("empty_registered_v1", "quiescent_streamable_v2")
                 else {"result": "not_exercised"})
    scene_fields = {"result": "not_exercised"}
    if executor.get("scene_vector_field_policy") == "reject_nonempty_v1" and publication_count:
        evidence = re.findall(r"historical scene fields run_id=(\S+) empty=true slots=(\d+) checks=(\d+) policy=reject_nonempty_v1", texts[1])
        if len(evidence) != publication_count or any(row[0] != executor["run_id"] or int(row[1])!=0 or int(row[2]) < 1 for row in evidence):
            raise RuntimeError("historical publication lacks empty scene-field ownership admission")
        scene_fields = {"result": "pass", "policy": "reject_nonempty_v1", "slots": int(evidence[0][1]),
                        "scope": "empty registry at capture/publication admission; active fields unsupported"}
    changed_inputs = bool(executor.get("changed_inputs", False))
    corrected_inputs = bool(executor.get("corrected_inputs", False))
    if bool(native.get("corrected_inputs", False)) != corrected_inputs:
        raise RuntimeError("combat comparison mixes correction protocols")
    if bool(native.get("changed_inputs", False)) != changed_inputs:
        raise RuntimeError("combat comparison mixes input policies")
    source_revision = bool(executor.get("source_revision", False))
    if source_revision != bool(native.get("source_revision", False)):
        raise RuntimeError("combat comparison mixes source-revision protocols")
    input_experiment = {"result": "not_exercised"}
    profile=executor.get("source_revision_profile","historical")
    if profile!=native.get("source_revision_profile","historical") or profile not in ("historical","guard201"):
        raise RuntimeError("combat comparison mixes source revision profiles")
    if profile=="guard201" and (not source_revision or not corrected_inputs):
        raise RuntimeError("guard correction requires deferred source revision")
    if source_revision:
        if not corrected_inputs and not cancellation: raise RuntimeError("future source revision requires cancellation")
        first=201 if profile=="guard201" else 161 if corrected_inputs else 166
        policy="authored_guard_201_230_v1" if profile=="guard201" else f"authored_samples_{first}_{first+29}_v1"
        streams=[]
        for index,text in enumerate(texts):
            owner="runtime" if index else "control"
            evidence=re.findall(rf"source revision activated owner=(\w+) policy={policy} cursor=(\d+) revision=(\d+) late_edit_rejected=(\w+) native_recording_unchanged=(\w+)",text)
            if len(evidence)!=1 or evidence[0][0]!=owner or evidence[0][2:] != (("1","true","true") if index else ("0","false","false")):
                raise RuntimeError("source revision lacks independent authored control or runtime admission evidence")
            cursor=int(evidence[0][1])
            if cursor>first or (index and cursor<1):
                raise RuntimeError("source revision was installed after its publication frontier")
            if index and corrected_inputs:
                restored_cursors=re.findall(rf"historical_restored .*? frame={anchor} round=0 cursor=(\d+) ",text)
                if restored_cursors!=[str(cursor)]:
                    raise RuntimeError("source revision cursor does not match the restored native frontier")
            elif index and cursor!=first:
                raise RuntimeError("future B revision lacks its expected publication frontier")
            samples=[dict(re.findall(r"(\w+)=([^\s]+)",line)) for line in text.splitlines() if "source revision sample policy=" in line]
            if [int(row["sample"]) for row in samples]!=list(range(first,first+30)):
                raise RuntimeError("authored source revision samples missing or reordered")
            if not any(row["original"]!=row["input"] for row in samples): raise RuntimeError("source correction changed no inputs")
            for row in samples:
                wanted=(int(row["original"],16)&~0x3c0f)|(0x1008 if profile=="guard201" else 0x401 if int(row["sample"])<first+15 else 0)
                if row["round"]!="0" or row["slot"]!="0" or int(row["input"],16)!=wanted:
                    raise RuntimeError("authored source revision policy mismatch")
            if index and corrected_inputs:
                validate_corrected_revision_order(text,publication_label,complete_seek_application)
            streams.append(samples)
        if streams[0]!=streams[1]: raise RuntimeError("authored control and runtime input revisions differ")
        if cancellation:
            text=texts[1]
            if not (text.index("source revision activated owner=runtime") < text.index("input revision transaction prepared A=0 B=1") < text.index("historical cancellation recovered run_id=")):
                raise RuntimeError("B revision was not retained before cancellation")
            if cancellation=="after" and not (text.index("input revision installed A=0 B=1") < text.index("input revision recovered B=1") < text.index("historical cancellation recovered run_id=")):
                raise RuntimeError("nonzero B source revision recovery lacks publication/undo evidence")
        input_experiment={"result":"pass","policy":policy,"samples":30,
            "corrected_historical_suffix":corrected_inputs,"native_cache_publication":True,"late_edits_rejected":True,
            "scope":"runtime tracker-output overrides compared with independently edited native authored samples"}
    if changed_inputs and not source_revision:
        streams = []
        first = 161 if corrected_inputs else 166
        policy = f"p0_raw_cache_{first}_{first+29}_v1"
        for run_index, text in enumerate(texts):
            starts = re.findall(rf"changed input started policy=(\w+) round=0 first_sample={first} last_sample={first+29}", text)
            stops = re.findall(r"changed input completed policy=(\w+) reads=(\d+) differing_reads=(\d+) failed=false detached=true", text)
            samples = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines() if "changed input sample policy=" in line]
            if starts != [policy] or len(stops) != 1 or stops[0][0] != starts[0] or int(stops[0][1]) != 30 or int(stops[0][2]) <= 0:
                raise RuntimeError("changed-input source coverage or cleanup is incomplete")
            if [int(row["sample"]) for row in samples] != list(range(first,first+30)):
                raise RuntimeError("changed input samples missing, repeated or reordered")
            for row in samples:
                original, value = int(row["original"],16), int(row["input"],16)
                wanted = (original & ~0x3c0f) | (0x401 if int(row["sample"]) < first+15 else 0)
                if row["slot"] != "0" or row["round"] != "0" or value != wanted:
                    raise RuntimeError("changed-input policy was not applied exactly")
            if corrected_inputs:
                activation=f"changed input activated policy={policy} after_restore=true"
                if text.count(activation)!=(1 if run_index else 0):
                    raise RuntimeError("corrected-input activation evidence is incomplete")
                if run_index and not (text.index("historical combat committed run_id=") > text.index(activation)
                                      > text.index(publication_label+" run_id=")):
                    raise RuntimeError("correction was not activated after historical publication")
            streams.append(samples)
        if streams[0] != streams[1]:
            raise RuntimeError("native and restored input interventions differ")
        input_experiment = {"result": "pass", "policy": policy, "samples": 30, "corrected_historical_suffix": corrected_inputs,
                            "scope": (f"changed inputs inside previously traversed205-{advanced_tick} suffix after restoring{anchor}" if corrected_inputs else f"changed future gameplay input after B{advanced_tick}; does not exercise correction of the already traversed suffix")}
    if validate_payload_handoff(texts[0], native["run_id"]) != validate_payload_handoff(texts[1], executor["run_id"]):
        raise RuntimeError("combat restore imported recordings differ")
    markers = (f"historical A captured run_id={executor['run_id']} tick={anchor} ",
               f"{publication_label} run_id={executor['run_id']} tick={anchor}"+("" if host_seek else " "),
               f"historical combat committed run_id={executor['run_id']} from_tick={undo_tick if repeat_seek else advanced_tick} to_tick={anchor}",
               (f"host seek resume accepted run_id={executor['run_id']} tick={held_tick}" if host_seek and not single_step and not complete_seek_application else
                f"historical combat resumed run_id={executor['run_id']} held_tick={held_tick}"))
    if complete_seek_application and executor.get("seek_ownership_protocol")=="retained_B_through_C_v1":
        completed_seek_observed_rewind(texts[1],executor['run_id'],anchor,advanced_tick,exact_target)
        markers=(markers[0],markers[1],
                 f"seek ownership state=Committed B_tick={advanced_tick} target={exact_target} current_tick={exact_target} ",markers[3])
    elif host_seek and owned_execution:
        # Repeated transactions may have the same A/B coordinates but land at
        # different C ticks. The target is part of the completion receipt.
        markers=(markers[0],markers[1],markers[2]+f" target_tick={exact_target}",markers[3])
    if cancellation:
        markers = (markers[0],
                   f"historical cancellation injected run_id={executor['run_id']} point={cancellation} tick={consumer_tick if consumer_mutation else lifetime_tick if lifetime_recovery else exact_target if settlement_failure else 208 if advance_failure or deferred_cancel else undo_tick if cancellation == 'before' or preparation_failure else anchor}",
                   f"historical cancellation recovered run_id={executor['run_id']} point={cancellation} tick={undo_tick}",
                   (f"host seek resume accepted run_id={executor['run_id']} tick={undo_tick}" if repeat_seek else f"historical combat resumed run_id={executor['run_id']} held_tick={undo_tick}"))
        if not repeat_seek and "historical combat committed run_id=" in texts[1]:
            raise RuntimeError("cancelled transaction was committed")
    marker_counts=[1,restore_count,1,1] if restore_reuse or (repeat_seek and not cancellation and not pair) else [1]*4
    marker_positions=[texts[1].rfind(marker) for marker in markers]
    if any(texts[1].count(marker)!=count for marker,count in zip(markers,marker_counts)) or sorted(marker_positions)!=marker_positions:
        raise RuntimeError("combat restore lacks one assembled capture/publication/hold/commit/resume")
    if exact_advance and not cancellation:
        checkpoint_landing=host_seek and exact_target==anchor
        required = ((f"historical checkpoint landing requested run_id={executor['run_id']} origin={anchor} target={anchor} resimulated_ticks=0" if checkpoint_landing else
                     f"historical exact requested run_id={executor['run_id']} origin={anchor} target={exact_target}"),
                    f"historical exact held run_id={executor['run_id']} tick={exact_target} pending_world={str(not checkpoint_landing and not complete_seek_application).lower()} pending_task={str(not checkpoint_landing and not complete_seek_application).lower()}")
        if any(texts[1].count(marker) != 1 for marker in required):
            raise RuntimeError("exact advance lacks request and externally held target")
        holds = re.findall(r"interior pause held run_id=(\S+) native_frame=(\d+) epoch=(\d+) elapsed_us=(\d+) application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) unchanged=true pending_event=true", texts[1])
        expected_holds=([first_target] if repeat_seek and first_target!=anchor else [])+([] if checkpoint_landing or complete_seek_application else [exact_target])
        if ([int(row[1]) for row in holds] != ([208,209] if single_step else expected_holds)
                or any(row[0] != executor["run_id"] or int(row[3]) < 1_000_000
                    or min(int(row[4]),int(row[5])) < 30 or int(row[6]) <= 0 for row in holds)):
            raise RuntimeError("exact advance lacks independent unchanged state, pending event and responsive hold")
        if checkpoint_landing or (repeat_seek and first_target==anchor):
            completed_holds=re.findall(rf"checkpoint pause held run_id=(\S+) native_frame={anchor} epoch=(\d+) elapsed_us=(\d+) application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) unchanged=true pending_event=false application_idle=true",texts[1])
            if len(completed_holds)!=1 or completed_holds[0][0]!=executor['run_id'] or int(completed_holds[0][2])<1_000_000 or min(map(int,completed_holds[0][3:5]))<30 or int(completed_holds[0][5])<=0:
                raise RuntimeError("checkpoint landing lacks unchanged completed-application hold")
        lifecycle=(required[0],required[1],markers[2],markers[3]) if owned_execution else (markers[2],required[0],required[1],markers[3])
        if complete_seek_application and owned_execution:
            # This hold validates the already completed application after
            # commit/retirement. Interior settlement is separately witnessed
            # by the mandatory ownership chain before commit.
            lifecycle=(required[0],markers[2],required[1],markers[3])
        positions=[texts[1].index(marker) for marker in lifecycle]
        if positions!=sorted(positions) or len(set(positions))!=len(positions):
            raise RuntimeError("exact advance lifecycle ordering is invalid")
        if single_step:
            step = f"historical single step requested run_id={executor['run_id']} origin=208 target=209"
            final = f"historical exact held run_id={executor['run_id']} tick={first_target} pending_world=true pending_task=true"
            if texts[1].count(step)!=1 or texts[1].count(final)!=1 or not (texts[1].index(required[1]) < texts[1].index(step) < texts[1].index(final) < texts[1].index(markers[3])):
                raise RuntimeError("single step lacks ordered request, second external hold and continuation")
            if "historical step control waiting" in texts[1]:
                validate_step_control(texts[1], executor)
    def rows(text, label):
        return [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in text.splitlines()
                if f"[ReplayQualification] {label} ordinal=" in line]
    def values(row):
        return {key: value for key, value in row.items() if key != "ordinal"}
    reference = rows(texts[0], "trajectory")
    target = [row for row in reference if row.get("frame") == str(anchor)]
    captured, restored = rows(texts[1], "historical_target"), rows(texts[1], "historical_restored")
    if len(target) != 1 or len(captured) != 1 or len(restored) != publication_count:
        raise RuntimeError("combat restore target observation is missing/duplicated")
    comparisons = [("captured_target", target[0], captured[0])]
    completed_cost=None
    if complete_seek_application:
        cost_rows=rows(texts[1],"historical_cost_completed")
        control_rows=[row for row in reference if row.get("frame")==str(exact_target)]
        request=f"seek cost completion requested run_id={executor['run_id']} origin={exact_target} deliberate_target_dwell=false"
        cost_records=re.findall(r"seek completed application cost run_id=(\S+) checkpoint=(\d+) target=(\d+) elapsed_us=(\d+) pending_task=false application_idle=true deliberate_target_dwell=false",texts[1])
        cost_holds=re.findall(r"seek cost completed hold run_id=(\S+) tick=(\d+) elapsed_us=(\d+) surface_frames=(\d+) unchanged=true",texts[1])
        retirement=f"seek checkpoint retired tick={exact_target} application_idle=true"
        if (len(cost_rows)!=1 or len(control_rows)!=1 or len(cost_records)!=1 or len(cost_holds)!=1
                or cost_records[0][:3]!=(executor['run_id'],str(anchor),str(exact_target))
                or cost_holds[0][:2]!=(executor['run_id'],str(exact_target))
                or int(cost_records[0][3])<=0 or int(cost_holds[0][2])<500000 or int(cost_holds[0][3])<30
                or texts[1].count(request)!=1 or texts[1].count(retirement)!=1
                or not texts[1].index(request)<texts[1].index(retirement)<texts[1].index("historical_cost_completed ordinal=")
                    <texts[1].index("seek completed application cost run_id=")<texts[1].index("seek cost completed hold run_id=")):
            raise RuntimeError("completed seek cost lacks exact target, completed retirement and independently observed unchanged hold")
        comparisons.append(("cost_completed_application_target",control_rows[0],cost_rows[0]))
        completed_cost={"result":"measured","elapsed_us":int(cost_records[0][3]),"resimulated_ticks":exact_target-anchor,
            "endpoint":"exact completed application, engine/world/task/arena idle, checkpoint GPU retirement completed",
            "includes_B_undo":True,"includes_checkpoint_retirement":True,"deliberate_target_dwell":False,
            "diagnostic_gpu_readbacks":False,"pacing":"unpaced seek prefix, native pending tails retained",
            "unpaced_rollback_cost":True,"scope":"bounded full transaction cost; not low-latency rollback qualification"}
    if exact_advance and host_seek and (exact_target==anchor or (repeat_seek and first_target==anchor)) and not cancellation:
        checkpoint_held=rows(texts[1],"historical_checkpoint_held")
        if len(checkpoint_held)!=1: raise RuntimeError("checkpoint held observation missing or duplicated")
        comparisons.append(("checkpoint_held_target",target[0],checkpoint_held[0]))
    if pair:
        second=[row for row in reference if row.get("frame")=="206" and row.get("round")=="0"]
        captured_second=rows(texts[1],"historical_second_target")
        if len(second)!=1 or len(captured_second)!=1 or [row.get("frame") for row in restored]!=( ["206"] if cancellation=="before" else ["206","205"]):
            raise RuntimeError("checkpoint pair lacks both captured and restored targets")
        comparisons += [("second_captured_target",second[0],captured_second[0]),("first_restored_target",second[0],restored[0])]
        if cancellation!="before": comparisons.append(("second_restored_target",target[0],restored[1]))
    else:
        comparisons += [("restored_target", target[0], row) for row in restored]
    settled=rows(texts[1],"historical_settled")
    completed_application={"result":"not_exercised"}
    if completed_cost:
        completed_application={"result":"pass","origin":exact_target,"completed_tick":exact_target,
            "pending_work_drained":True,"extra_tick_completion_coverage":False}
    if owned_execution and single_step:
        completed=f"host step completed run_id={executor['run_id']} tick=209 native_tails_complete=true"
        native_settled=[row for row in reference if row.get("frame")=="209" and row.get("round")=="0"]
        if len(settled)!=1 or len(native_settled)!=1 or texts[1].count(completed)!=1:
            raise RuntimeError("host step lacks independently observed completed target tails")
        comparisons.append(("completed_application_target",native_settled[0],settled[0]))
        completed_application={"result":"pass","origin":209,"completed_tick":209,"pending_work_drained":True,"extra_tick_completion_coverage":False}
    elif settled or "historical application completion requested run_id=" in texts[1]:
        completed=re.findall(r"historical application completed held run_id=(\S+) origin=209 tick=(\d+) elapsed_us=(\d+) surface_frames=(\d+) pending_task=false application_idle=true unchanged=true",texts[1])
        native_settled=[row for row in reference if row.get("frame")=="209" and row.get("round")=="0"]
        request=f"historical application completion requested run_id={executor['run_id']} origin=209"
        if not single_step or len(settled)!=1 or len(native_settled)!=1 or len(completed)!=1 or completed[0][0]!=executor['run_id'] or completed[0][1]!="209" or int(completed[0][2])<500000 or int(completed[0][3])<30 or texts[1].count(request)!=1 or not texts[1].index(request)<texts[1].index("historical_settled ordinal=")<texts[1].index("historical application completed held run_id=")<texts[1].index(markers[3]):
            raise RuntimeError("interior application completion lacks its independently compared completed hold")
        comparisons.append(("completed_application_target",native_settled[0],settled[0]))
        completed_application={"result":"pass","origin":209,"completed_tick":209,"pending_work_drained":True,
                               "extra_tick_completion_coverage":False}
    if restore_reuse:
        original_images = rows(texts[1], "historical_original")
        if len(original_images) != 2:
            raise RuntimeError("checkpoint reuse lacks both original B observations")
        comparisons.append(("reused_B", original_images[0], original_images[1]))
    if cancellation:
        original = [row for row in reference if row.get("frame") == str(undo_tick)]
        saved, recovered = rows(texts[1], "historical_repeat_original" if repeat_seek else "historical_original"), rows(texts[1], "historical_recovered")
        if len(original) != 1 or len(saved) != 1 or len(recovered) != 1:
            raise RuntimeError("cancellation lacks independent B observations")
        comparisons += [("original_B", original[0], saved[0]), ("recovered_B", original[0], recovered[0])]
        held_marker = f"{publication_label} run_id={executor['run_id']} tick={anchor}"+("" if host_seek else " ")
        if texts[1].count(held_marker) != (0 if cancellation == "before" else 1 if pair or preparation_failure else restore_count):
            raise RuntimeError("cancellation publication/hold evidence is inconsistent")
        if cancellation == "after" and texts[1].rindex(held_marker) > texts[1].index(markers[1]):
            raise RuntimeError("after-publication cancellation occurred before target hold")
    suffix_start=(f"historical combat execution admitted run_id={executor['run_id']} from_tick={undo_tick if repeat_seek else advanced_tick} to_tick={anchor} B_retained=true"
                  if owned_execution and not cancellation else markers[2])
    # The final transaction can reuse the first transaction's A/B coordinates.
    # Earlier traversals are compared separately below, including all callbacks.
    suffix_text = texts[1].rsplit(suffix_start, 1)[1]
    if ground_commit:
        widget_clock["committed_target_HUD_continuation"]=compare_private_hud_continuation(texts[0],suffix_text,exact_target,False)
    if trace_render_recovery:
        widget_clock["trace_B_HUD_continuation"]=compare_private_hud_continuation(texts[0],suffix_text,advanced_tick,False)
        widget_clock["trace_B_native_reconstruction"]=validate_trace_render_reconstruction(texts[1],advanced_tick)
    if private_hud_recovery or private_hud_drained:
        widget_clock["private_B_type_continuation"]=compare_private_hud_continuation(texts[0],suffix_text)
        parked=re.findall(r"HUD private B type parked slot=(\d+) player=(\d+) backing_bytes=(\d+) unchanged=true",texts[1])
        recovered=re.findall(r"HUD private B type recovered slot=(\d+) player=(\d+) unchanged=true original_backing=true",texts[1])
        if "HUD private B type retired" in texts[1] or (cancellation=="before" and (parked or recovered)):
            raise RuntimeError("private B HUD cancellation retired B or changed membership before publication")
        if cancellation=="after" and (len(parked)!=1 or recovered!=[parked[0][:2]] or int(parked[0][2])<=0):
            raise RuntimeError("private B HUD lacks complete original-player and allocation recovery")
    if widget_clock["result"] == "pass":
        try:
            widget_clock["independent_continuation"] = compare_widget_continuation(texts[0], suffix_text, held_tick if executor.get("completion_repeat") or ground_commit else resumed_tick)
        except RuntimeError as error:
            if ground_commit or trace_render_recovery or str(error)!="HUD continuation lacks matching independent active-player coverage": raise
            widget_clock["independent_continuation"]={"result":"missing_coverage","reason":str(error)}
    first_suffix = None
    if repeat_seek:
        first_commit=f"historical combat committed run_id={executor['run_id']} from_tick={advanced_tick} to_tick={first_checkpoint}"
        if host_seek and owned_execution:first_commit+=f" target_tick={first_target}"
        if texts[1].count(first_commit)!=1 or texts[1].index(first_commit)>=texts[1].index(markers[2]):
            raise RuntimeError("repeated seek lacks first committed traversal")
        second_boundary=markers[1] if cancellation=="before" or preparation_failure else f"host seek publication observed run_id={executor['run_id']} tick={anchor}" if cancellation else markers[2]
        first_start=(f"historical combat execution admitted run_id={executor['run_id']} from_tick={advanced_tick} to_tick={first_checkpoint} B_retained=true" if owned_execution else first_commit)
        first_end=suffix_start if owned_execution and not cancellation else second_boundary
        first_suffix=texts[1].split(first_start,1)[1].split(first_end,1)[0]
        initial=[row for row in rows(first_suffix,"trajectory") if row.get("round")=="0" and first_checkpoint<int(row["frame"])<=first_target]
        control=[row for row in reference if row.get("round")=="0" and first_checkpoint<int(row["frame"])<=first_target]
        if len(initial)!=first_target-first_checkpoint or len(control)!=first_target-first_checkpoint:
            raise RuntimeError("repeated seek lacks independently observed first traversal")
        comparisons += [("first_seek_continuation",a,b) for a,b in zip(control,initial)]
        original=rows(texts[1],"historical_repeat_original")
        if len(original)!=1: raise RuntimeError("second seek lacks completed B209 observation")
        comparisons.append(("second_seek_B",target[0] if first_target==anchor else control[-1],original[0]))
        repeated=f"host seek repeated run_id={executor['run_id']} checkpoint={anchor} origin={first_target} target={exact_target} "+("pending_world=false pending_task=false previous_commit_complete=true" if owned_execution else "pending_world=true pending_task=true")
        first_pending=str(first_target!=anchor).lower()
        first_held=f"historical exact held run_id={executor['run_id']} tick={first_target} pending_world={first_pending} pending_task={first_pending}"
        if texts[1].count(repeated)!=1 or texts[1].count(first_held)!=1 or not ((texts[1].index(first_held)<texts[1].index(first_commit) if owned_execution else texts[1].index(first_commit)<texts[1].index(first_held)) and texts[1].index(first_commit if owned_execution else first_held)<texts[1].index(repeated)<(texts[1].index(markers[1]) if cancellation=="before" or preparation_failure else texts[1].rindex("host seek publication observed run_id="))):
            raise RuntimeError("repeated seek lacks ordered interior request and second publication")
        if cancellation:
            hold_kind="checkpoint" if first_target==anchor else "interior"
            suffix="false application_idle=true" if first_target==anchor else "true"
            holds=re.findall(rf"{hold_kind} pause held run_id=(\S+) native_frame=(\d+) epoch=(\d+) elapsed_us=(\d+) application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) unchanged=true pending_event={suffix}",texts[1])
            if len(holds)!=1 or holds[0][0]!=executor['run_id'] or holds[0][1]!=str(first_target) or int(holds[0][3])<1000000 or min(int(holds[0][4]),int(holds[0][5]))<30 or int(holds[0][6])<=0:
                raise RuntimeError("second cancellation lacks the first independent held boundary")
    # Absolute tick identifies the native boundary, including round transitions.
    # Round remains part of each independently compared observation below.
    expected = [row for row in reference if resumed_tick < int(row["frame"]) <= continuation_end]
    observed = [row for row in rows(suffix_text, "trajectory") if resumed_tick < int(row["frame"]) <= continuation_end]
    expected_coordinates=list(range(resumed_tick+1,continuation_end+1))
    if executor.get("completion_repeat") and 208 in expected_coordinates:
        # Native repeats208/209 share one application: no engine-post sample
        # exists at208. Its callbacks/held observation remain independently
        # required; all120 post-step ticks210..329 still require native samples.
        expected_coordinates.remove(208)
    if ([int(row["frame"]) for row in expected]!=expected_coordinates
            or [int(row["frame"]) for row in observed]!=expected_coordinates):
        raise RuntimeError("combat restore needs all 120 independently observed continuation ticks")
    if corrected_inputs:
        correction_end=min(advanced_tick,continuation_end)
        original_suffix = [row for row in rows(texts[1].split(suffix_start,1)[0], "trajectory")
                           if row.get("round")=="0" and 205<int(row["frame"])<=correction_end]
        corrected_suffix = [row for row in observed if 205<int(row["frame"])<=correction_end]
        if len(original_suffix)!=correction_end-205 or len(corrected_suffix)!=correction_end-205:
            raise RuntimeError("corrected-input test lacks the original and reproduced complete suffix")
        altered_ticks = [int(b["frame"]) for a,b in zip(original_suffix,corrected_suffix)
                         if a.get("published_pairs")!=b.get("published_pairs")]
        if not altered_ticks:
            raise RuntimeError("corrected inputs did not change native publications in the historical suffix")
        input_experiment["changed_publication_ticks"] = altered_ticks
        gameplay_fields=("p0_sim","p1_sim","p0_vital","p1_vital","p0_moves","p1_moves")
        gameplay_changes=[int(b["frame"]) for a,b in zip(original_suffix,corrected_suffix)
                          if any(a.get(key)!=b.get(key) for key in gameplay_fields)]
        input_experiment["historical_comparison_window"]={"first_tick":206,"last_tick":correction_end,
            "scope":"observed corrected suffix; not reproduction through original B when B lies later"}
        input_experiment["changed_gameplay_ticks"]=gameplay_changes
        input_experiment["before_target"]=changed_input_target_coverage(exact_target,altered_ticks,gameplay_changes)
        if guard_resimulation and input_experiment["before_target"]["result"]!="pass":
            raise RuntimeError("guard resimulation lacks changed native publications and gameplay before C")
        if profile=="guard201" and not gameplay_changes:
            raise RuntimeError("guard correction changed publications but no observed gameplay state")
    comparisons += [("continuation", a, b) for a, b in zip(expected, observed)]
    mt_coverage = {"result": "not_observed"}
    if all(row.get("sample_version") in ("3", "4", "5") for row in expected + observed):
        if any(not {"mt_cursor", "mt_hash"} <= row.keys() for row in expected + observed):
            raise RuntimeError("MT native observation is incomplete")
        mt_coverage = {"result": "compared", "bytes": 5008, "distinct_suffix_states": len({row["mt_hash"] for row in expected}),
                       "scope": "complete native state comparison; consumption coverage requires a state transition"}
    native_boundaries = validate_boundary_capture(texts[0], native["run_id"])
    candidate_boundaries = validate_boundary_capture(texts[1], executor["run_id"], historical_restore=not cancellation or repeat_seek or advance_failure or consumer_mutation or deferred_cancel, retained_execution=owned_execution, historical_advanced_tick=advanced_tick, expected_rewinds=([(advanced_tick,first_checkpoint)] if cancellation else [(advanced_tick,first_checkpoint),(first_target,anchor)]) if repeat_seek else ([(advanced_tick,anchor)] if not cancellation or advance_failure or consumer_mutation or deferred_cancel else []), recovery_rewind=expected_recovery_rewind(executor,texts[1]))
    if not candidate_boundaries:
        raise RuntimeError("historical comparison lacks independent native boundaries")
    def tail_index(boundaries, tick):
        matches = [i for i, row in enumerate(boundaries) if row["phase"] == "actor_tail" and row["frame"] == str(tick)]
        if len(matches) != 1:
            raise RuntimeError(f"historical comparison needs one completed native interval at tick {tick}")
        return matches[0]
    start, end = tail_index(native_boundaries, resumed_tick), tail_index(native_boundaries, continuation_end)
    expected_boundaries = native_boundaries[start + 1:end + 1]
    after_commit = rows(suffix_text, "boundary")
    end = tail_index(after_commit, continuation_end)
    observed_boundaries = after_commit[:end + 1]
    if len(expected_boundaries) != len(observed_boundaries):
        return {"result": "fail", "first_mismatch": "native_callback_count", "expected": len(expected_boundaries), "observed": len(observed_boundaries)}
    if repeat_seek:
        first_boundaries=rows(first_suffix,"boundary")
        first_expected=native_boundaries[tail_index(native_boundaries,first_checkpoint)+1:tail_index(native_boundaries,first_target)+1]
        if len(first_boundaries)!=len(first_expected): raise RuntimeError("first seek callback coverage mismatch")
        comparisons += [("first_seek_callback",a,b) for a,b in zip(first_expected,first_boundaries)]
    comparisons += [("native_callback", a, b) for a, b in zip(expected_boundaries, observed_boundaries)]
    for kind, a, b in comparisons:
        a, b = values(a), values(b)
        if a != b:
            return {"result": "fail", "first_mismatch": kind, "frame": b.get("frame"), "phase": b.get("phase"),
                    "different_fields": sorted(key for key in a.keys() | b.keys() if a.get(key) != b.get(key)),
                    "expected": a, "observed": b}
    pacing = validate_world_resume_timing(texts[1], executor["run_id"])
    pacing["result"]=("pass" if min(pacing["native_ticks"],pacing["viewport_frames"])*1_000_000>=58*pacing["elapsed_us"] else "fail")
    # Preserve independent simulation results even when the pacing gate fails.
    # A failed timing window is never promoted to a successful experiment.
    birth_marker=f"historical particle birth reproduced run_id={executor['run_id']} tick=210 combat_particles=8"
    # A pair's cancelled second request recovers B210. Its continuation begins
    # at211, so birth210 is required in the independently compared first seek,
    # never invented in the recovered suffix.
    if (pair or preparation_failure) and cancellation:
        if first_suffix is None or first_suffix.count(birth_marker)!=1 or birth_marker in suffix_text:
            raise RuntimeError("checkpoint-pair cancellation lacks the first traversal particle birth")
    elif not checkpoint_window and (not cancellation or repeat_seek) and suffix_text.count(birth_marker) != 1:
        raise RuntimeError("historical continuation did not reproduce the combat particle birth")
    completion_repeat = validate_completion_repeat(texts,reports)
    poses = compare_native_poses(texts, reports, held_tick, cancellation)
    if any(report.get("pixel_diagnostics", False) != reports[0].get("pixel_diagnostics", False)
           or report.get("pass_diagnostics", False) != reports[0].get("pass_diagnostics", False) for report in reports):
        raise RuntimeError("combat comparison mixes diagnostic protocols")
    presentation = (compare_native_presentation(texts, reports, resumed_tick, cancellation)
                    if reports[0].get("pixel_diagnostics", False) else
                    {"result": "not_run", "required": False, "gpu_readbacks": False})
    display_boundaries = []
    if not reports[0].get("pixel_diagnostics", False):
        for text, report, generation in zip(texts, reports, (0, continuation_generation(executor,cancellation))):
            displayed = {}
            for line in text.splitlines():
                if "native display boundary run_id=" not in line:
                    continue
                row = dict(re.findall(r"(\w+)=([^\s]+)", line))
                expected_readback = coherence_readback_expected(report,int(row["tick"]),int(row["generation"]))
                if row.get("run_id") != report["run_id"] or row.get("gpu_readbacks") != str(expected_readback).lower():
                    raise RuntimeError("native display boundary protocol mismatch")
                key = int(row["generation"]), int(row["tick"])
                displayed[key] = displayed.get(key, 0) + 1
            if any(displayed.get((generation, tick)) != 1 for tick in range(held_tick + 1, held_tick + 121)):
                raise RuntimeError("continuation lacks exactly one native display boundary per tick")
            display_boundaries.append(120)
    costs = {}
    for kind in ("capture", "restore"):
        matches = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in texts[1].splitlines()
                   if f"historical {kind} cost run_id=" in line]
        if len(matches) != (publication_count if kind=="restore" else 1):
            raise RuntimeError("historical cost evidence missing or duplicated: " + kind)
        if matches:
            row = matches[-1]
            if row["run_id"] != executor["run_id"] or row["diagnostic_gpu_readbacks"] != str(bool(executor.get("pixel_diagnostics") or executor.get("coherence_gpu_readbacks"))).lower():
                raise RuntimeError("historical cost protocol mismatch")
            if row.get("readback_scope") != "observer_and_transaction" or (not executor.get("pixel_diagnostics") and row.get("transaction_readback_maps") != "0"):
                raise RuntimeError("cost measurement still contains diagnostic GPU readbacks")
            costs[kind] = row
            if kind=="restore" and (restore_reuse or repeat_seek):
                if any(r["run_id"]!=executor["run_id"] or r.get("transaction_readback_maps")!="0" for r in matches):
                    raise RuntimeError("reuse restore cost protocol mismatch")
                costs["restore_attempts"]=matches
    capture_owner={"result":"legacy_external_sequence"}
    owned_capture=re.findall(r"capture operation ready tick=(\d+) elapsed_us=(\d+) owned_bytes=(\d+) immutable=true gpu_complete=true",texts[1])
    if owned_capture:
        cancellation_capture=bool(executor.get("capture_cancel"))
        if [int(row[0]) for row in owned_capture]!=([170,205] if executor.get("checkpoint_fallback") else [205,206] if pair else [anchor]*(2 if cancellation_capture else 1)) or any(not 0<int(row[2])<=PRODUCTION_REPLAY_MEMORY_LIMIT or int(row[1])<=0 for row in owned_capture):
            raise RuntimeError("invalid host-owned capture evidence")
        capture_owner={"result":"pass","tick":anchor,"request_to_ready_us":int(owned_capture[-1][1]),
            "owned_bytes":int(owned_capture[-1][2]),"immutable_handle":True,"gpu_completion_before_ready":True,
            "cancellation_coverage":False,"multiple_capture_coverage":False}
        if pair:
            captured_pair=re.findall(r"checkpoint pair captured run_id=(\S+) first=205 second=206 distinct_gpu_owners=true first_storage_unchanged=true elapsed_us=(\d+) owned_bytes=(\d+)",texts[1])
            displaced=f"checkpoint pair displaced run_id={executor['run_id']} old=206 selected=205 caller_released=true host_retirement_required=true"
            advance=f"checkpoint pair advance requested run_id={executor['run_id']} origin=206 target=211 host_owned=true"
            original=f"checkpoint pair original held run_id={executor['run_id']} tick=211 application_idle=true pending_task=false"
            if len(captured_pair)!=1 or captured_pair[0][0]!=executor["run_id"] or any(texts[1].count(marker)!=1 for marker in (displaced,advance,original)):
                raise RuntimeError("checkpoint pair lacks immutable capture, owned advancement and deferred retirement evidence")
            if not texts[1].index("checkpoint pair captured run_id=")<texts[1].index(advance)<texts[1].index(original)<texts[1].index("host seek accepted run_id="):
                raise RuntimeError("checkpoint pair original boundary was not reached by owned advancement")
            capture_owner["multiple_capture_coverage"]="two distinct retained images used by independently compared seeks"
            capture_owner["captures"]=owned_capture
        if executor.get("checkpoint_fallback"):
            fallback=f"checkpoint fallback selected run_id={executor['run_id']} nearest=205 nearest_owners_live=false selected=170 selected_owners_live=true origin=220 target=208 explicit_checkpoint=false"
            pair_capture=f"checkpoint pair captured run_id={executor['run_id']} first=170 second=205 distinct_gpu_owners=true first_storage_unchanged=true"
            fallback_commit=repeat_seek and not cancellation and first_target==208 and exact_target==214
            fallback_cancel=not repeat_seek and cancellation in ("before","after") and exact_target==208
            if anchor!=170 or pair or not (fallback_commit or fallback_cancel) or texts[1].count(fallback)!=1 or texts[1].count(pair_capture)!=1:
                raise RuntimeError("automatic fallback lacks expired-nearest, live-earlier and null-selection evidence")
            second=rows(texts[1],"historical_second_target")
            control=[row for row in reference if row.get("frame")=="205" and row.get("round")=="0"]
            if len(second)!=1 or len(control)!=1 or values(second[0])!=values(control[0]):
                raise RuntimeError("fallback checkpoint differs from independent native tick205")
            capture_owner["automatic_fallback"]={"result":"pass","expired_nearest":205,"selected":170,"explicit_checkpoint":False}
            capture_owner["multiple_capture_coverage"]=("two retained images; expired nearest skipped, earlier image restored twice"
                if fallback_commit else "expired nearest retired; cancelled earlier-checkpoint request independently recovers B")
            capture_owner["captures"]=owned_capture
            capture_owner["request_to_ready_us"]=int(owned_capture[0][1])
            capture_owner["owned_bytes"]=int(owned_capture[0][2])
        if cancellation_capture:
            cancelled=re.findall(r"capture cancellation recovered point=(\w+) tick=205 native_boundaries_unchanged=true resources_retired=true",texts[1])
            if cancelled!=["before_submission","after_ready"]: raise RuntimeError("capture cancellation evidence missing")
            capture_owner["cancellation_coverage"]=cancelled
            capture_owner["multiple_capture_coverage"]="recapture at same held boundary after retirement"
    elif executor.get("capture_cancel"):
        raise RuntimeError("capture cancellation requested without host-owned capture evidence")
    costs["restore_plus_resimulation"] = {"result": "not_measured", "reason":
        "cancelled transaction; completed C work and recovery are reported separately" if cancellation else
        "return to original B lies beyond the validated target suffix; request-to-target cost is reported separately"}
    if not cancellation and advanced_tick<=continuation_end:
        from datetime import datetime
        def logged_time(line):
            stamp=re.match(r"\[([0-9-]+ [0-9:.]+)\]",line)
            if not stamp: raise RuntimeError("paced resimulation metric lacks timestamp")
            return datetime.fromisoformat(stamp[1])
        held_lines=[line for line in texts[1].splitlines() if publication_label+" run_id=" in line]
        catchup_lines=[line for line in suffix_text.splitlines() if "[ReplayQualification] trajectory ordinal=" in line
                       and re.search(rf" frame={advanced_tick} round=\d+ ",line)]
        if len(held_lines)!=restore_count or len(catchup_lines)!=1:
            raise RuntimeError("paced resimulation metric lacks its exact historical interval")
        commit_and_catchup=int((logged_time(catchup_lines[0])-logged_time(held_lines[-1])).total_seconds()*1_000_000)
        if commit_and_catchup<=0: raise RuntimeError("invalid historical resimulation timing order")
        costs["restore_plus_resimulation"]={"result":"measured", "resimulated_ticks":advanced_tick-anchor,
            "elapsed_us":int(costs["restore"]["elapsed_us"])+commit_and_catchup,
            "commit_release_and_paced_resimulation_us":commit_and_catchup,
            "includes_exact_target_validation_hold": exact_advance,
            "pacing":"native60Hz", "unpaced_rollback_cost":False,
            "endpoint":f"reproduced{advanced_tick} EngineTickPost; final application/render retirement not included",
            "excludes":("none; host seek has no deliberate A205 validation dwell" if host_seek else "deliberate A205 validation dwell only" if costs["restore"].get("includes_image_reopen") == "true"
                        else "deliberate A205 validation dwell and retained-image preparation before the restore timer"),
            "request_origin":costs["restore"].get("request_origin", "prepared_image_at_B"),
            "timing_source":"runtime restore timer and native log timestamps"}
    restore_owner={"result":"legacy_external_sequence"}
    request_accepted=re.findall(r"restore request accepted target=(\d+) original=(\d+) pending=true aliases_rejected=true",texts[1])
    request_prepared=re.findall(r"restore request prepared target=(\d+) original=(\d+) owned_bytes=(\d+) complete_B=true",texts[1])
    request_marker="restore request accepted target="
    seek_evidence={"result":"not_exercised"}
    if host_seek:
        request_marker=f"host seek accepted run_id={executor['run_id']} checkpoint={first_checkpoint} origin={advanced_tick} target={first_target if repeat_seek else exact_target} aliases_rejected=true"
        phases=re.findall(r"host seek phase run_id=(\S+) phase=(\d+) pending=(true|false) commit_decided=(true|false)",texts[1])
        expected_phases=([2,9,10] if cancellation=="before" else [2,3,4,9,10] if cancellation else [2,3,4,5,6,7,8])
        if repeat_seek:
            # Distinct checkpoint retirement runs before the next hold callback.
            # CompletingApplication is verified by Begin's returned witness in
            # the repeated marker, plus completed-tail retirement below; it may
            # not produce a separate callback while the old pin is retiring.
            completion_callback = [] if pair and "phase=1 pending=true commit_decided=false source=begin_return" not in texts[1] else [1]
            expected_phases = [2,3,4,5,6,7,8]+completion_callback+([2,11,9,10] if preparation_failure else [2,9,10] if cancellation=="before" else [2,3,4,9,10] if cancellation else [2,3,4,5,6,7,8])
        if owned_execution and not cancellation:
            expected_phases=[2,3,4,6,7,8]*(2 if repeat_seek else 1)
            if single_step: expected_phases += [7,8]
            if repeat_seek:
                starts=[m.start() for m in re.finditer("seek ownership state=BRetained ",texts[1])]
                if len(starts)!=2: raise RuntimeError("repeated seek lacks two complete B owners")
                validate_seek_commit_ownership(texts[1][:starts[1]],first_checkpoint,advanced_tick,first_target)
                validate_seek_commit_ownership(texts[1][starts[1]:],anchor,first_target,exact_target)
            else:
                validate_seek_commit_ownership(texts[1],anchor,advanced_tick,exact_target,single_step,bool(executor.get("completion_repeat")))
        if owned_execution and repeat_seek and first_target==anchor:
            # No traversal is manufactured for the first checkpoint landing.
            expected_phases.pop(4)
        if not cancellation and exact_target==anchor:
            # The final request lands directly on its checkpoint. It must not
            # manufacture an advance or a pending native task at that boundary.
            expected_phases.pop(len(expected_phases)-2)
        failed_wrapper={"result":"not_exercised"}
        if preparation_failure:
            failed=f"host seek preparation failure observed run_id={executor['run_id']} tick=220 unchanged_B=true before_publication=true"
            rejected=re.findall(r"lighting missing target primitive .* captured_id=(\d+) .*member_same_generation=false weak_alive=false",texts[1])
            if len(rejected)!=1 or texts[1].count(failed)!=1 or not texts[1].index("host seek repeated run_id=")<texts[1].index("lighting missing target primitive")<texts[1].index(failed)<texts[1].index(markers[1])<texts[1].index(markers[2]):
                raise RuntimeError("preparation recovery lacks the native lifetime rejection and ordered unchanged-B cancellation")
            failed_wrapper={"result":"pass","failure":"expired target primitive before publication","original_preserved":True,
                "recovered_tick":220,"independent_continuation_ticks":120,"requested_second_seek_result":"failed"}
            if second_undo_captured:
                undo_marker=f"restore undo captured target=205 original={first_target} gpu_complete=true before_target_preparation=true"
                if not texts[1].index("host seek repeated run_id=")<texts[1].index(undo_marker)<texts[1].index("lighting missing target primitive"):
                    raise RuntimeError("target preparation preceded complete B ownership")
                failed_wrapper["complete_B_before_target_preparation"]=True
            if "historical cancel control waiting run_id=" in texts[1]:
                failed_wrapper["ui_control"]=validate_cancel_control(texts[1],executor)
        if deferred_cancel:
            expected_phases=[2,3,4,6,7,8,9,10]
            validate_seek_recovery_ownership(texts[1],205,210,208,208,deferred=True)
        if executor.get("seek_publication_cancel"):
            if cancellation!="after" or anchor!=170 or undo_tick!=2504 or advance_failure or repeat_seek:
                raise RuntimeError("publication handoff cancellation has an invalid active-B plan")
            validate_publication_handoff_cancel(texts[1],executor["run_id"],anchor,undo_tick)
            expected_phases=[2,3,4,6,9,10]
        if executor.get("seek_observer_failure"):
            if cancellation!="after": raise RuntimeError("failed-wrapper test requires after-publication cancellation")
            expected_phases.insert(len(expected_phases)-2,11)
            injected=f"host seek observer failure injected run_id={executor['run_id']} target=205 after_publication=true"
            failed=f"host seek failed wrapper observed run_id={executor['run_id']} target=205 undo_tick={undo_tick} underlying_held=true complete_B=true"
            cancelled=f"historical cancellation injected run_id={executor['run_id']} point=after tick={anchor} failed_wrapper=true"
            if any(texts[1].count(m)!=1 for m in (injected,failed,cancelled)) or not texts[1].rindex("host seek publication observed run_id=")<texts[1].index(injected)<texts[1].index(failed)<texts[1].index(cancelled)<texts[1].index(markers[2]):
                raise RuntimeError("failed wrapper lacks ordered publication/failure/cancellation/B recovery")
            failed_wrapper={"result":"pass","failure":"observer exception after publication","underlying_transaction":"Held with complete B","recovered_tick":undo_tick,"independent_continuation_ticks":120}
        if advance_failure or consumer_mutation:
            expected_phases=[2,3,4,6,7,11,9,10]
            armed=f"host seek advance failure armed run_id={executor['run_id']} tick=208 target=214"
            injected="seek advance failure injected tick=208 target=214 B_retained=true external_hold=true"
            failed=f"host seek advance failure observed run_id={executor['run_id']} tick=208 undo_tick={undo_tick} pending_task=true complete_B=true"
            if settlement_failure:
                expected_phases=[2,3,4,6,7,8,11,9,10]
                armed=f"host seek settlement failure armed run_id={executor['run_id']} participants=11 target={exact_target}"
                injected=f"seek settlement failure injected tick={exact_target} participants=11 B_retained=true before_commit=true"
                if executor.get("seek_drained_cancel"):
                    armed=armed.replace("participants=11", "participants=0")
                    injected=f"seek drained cancellation boundary tick={exact_target} gpu_complete=true C_captured=true render_settled=false cpu_settled=0 B_retained=true"
                failed=f"host seek settlement failure observed run_id={executor['run_id']} tick={exact_target} undo_tick={undo_tick} pending_task=false complete_B=true"
            if lifetime_recovery:
                armed=f"host seek lifetime failure watching run_id={executor['run_id']} checkpoint=205 target=220 injection=false"
                injected="seek protected particle lifetime abort component="
                failed=f"host seek lifetime failure observed run_id={executor['run_id']} tick={lifetime_tick} undo_tick=210 pending_task=false complete_B=true injected=false"
                if "seek advance failure injected" in texts[1] or "seek settlement failure injected" in texts[1]:
                    raise RuntimeError("natural lifetime failure used an injected substitute")
            if consumer_mutation:
                armed=f"consumer mutation armed run_id={executor['run_id']} checkpoint=210 original=217 target=217"
                injected="consumer prerequisite mutation injected count=1 native_api=141D58ED0 production_entry_returned=true repair=false"
                failed=f"consumer mutation failure observed run_id={executor['run_id']} tick={consumer_tick} undo_tick=217 pending_task=false complete_B=true"
            ownership=validate_seek_recovery_ownership(texts[1],anchor,undo_tick,consumer_tick if consumer_mutation else lifetime_tick if lifetime_recovery else exact_target if settlement_failure else 208,exact_target,settlement_failure,bool(executor.get("seek_drained_cancel")),lifetime=bool(lifetime_recovery),consumer=consumer_mutation)
            if any(texts[1].count(m)!=1 for m in (armed,injected,failed)) or not texts[1].index(armed)<texts[1].index(injected)<texts[1].index(failed)<texts[1].index(markers[1])<texts[1].index(markers[2]):
                raise RuntimeError("advance failure lacks ordered native execution, external failure, cancellation and B recovery")
            failed_wrapper={"result":"pass","failure":"native prerequisite mutation detected before dependent consumption" if consumer_mutation else "native component lifecycle veto before callback/destruction" if lifetime_recovery else f"cancelled at C{exact_target} Render::Drained before render/CPU settlement" if executor.get("seek_drained_cancel") else f"injected after C{exact_target} CPU settlement before commit" if settlement_failure else "injected after external C208 during advance to214",
                "recovered_tick":undo_tick,"independent_continuation_ticks":120,"ownership":ownership}
        if texts[1].count(request_marker)!=1 or request_accepted or [int(row[1]) for row in phases]!=expected_phases or any(row[0]!=executor["run_id"] or row[2]!=("false" if row[1] in ("8","10") else "true") or row[3]!=("true" if not owned_execution and int(row[1]) in (5,6,7,8) else "false") for row in phases):
            raise RuntimeError("host seek lacks its complete owned phase evidence")
        all_ready=re.findall(r"host seek ready run_id=(\S+) target=(\d+) elapsed_us=(\d+) owned_bytes=(\d+) paused=true",texts[1])
        expected_targets=([first_target] if cancellation else [first_target,exact_target]) if repeat_seek else ([] if cancellation else [208,209] if single_step else [exact_target])
        if settlement_failure or deferred_cancel: expected_targets=[exact_target]
        if [int(r[1]) for r in all_ready]!=expected_targets: raise RuntimeError("seek target readiness sequence mismatch")
        if any(r[0]!=executor["run_id"] or not 0<int(r[2]) or not 0<int(r[3])<=PRODUCTION_REPLAY_MEMORY_LIMIT for r in all_ready):
            raise RuntimeError("seek sequence exceeds readiness or memory limit")
        ready=[(r[0],r[2],r[3]) for r in all_ready[-1:]]
        timing_pass=all(int(r[2])<=500000 for r in all_ready)
        if not timing_pass and anchor==205: raise RuntimeError("seek sequence exceeds500ms readiness limit")
        if cancellation:
            if ready and not repeat_seek and not settlement_failure and not deferred_cancel: raise RuntimeError("cancelled seek reported target readiness")
            seek_evidence={"result":"pass","requested_target":exact_target,"cancelled_at":cancellation,
                "recovered_tick":undo_tick,"repeated_backward_seek":repeat_seek and exact_target<first_target,"repeated_forward_seek":repeat_seek and exact_target>first_target,"cancellation_coverage":True,"failed_wrapper_recovery":failed_wrapper,"index_complete":False}
        else:
            if len(ready)!=1 or ready[0][0]!=executor["run_id"] or not 0<int(ready[0][1]) or not 0<int(ready[0][2])<=PRODUCTION_REPLAY_MEMORY_LIMIT:
                raise RuntimeError("host seek lacks bounded exact readiness within500ms")
            identical=f"host seek identical request run_id={executor['run_id']} target={exact_target} unchanged=true"
            if texts[1].count(identical)!=1 or texts[1].index(identical)>=texts[1].rindex("host seek ready run_id="):
                raise RuntimeError("host seek lacks its unchanged identical-request hold")
            seek_evidence={"result":"pass","requested_target":exact_target,"target":int(all_ready[-1][1]),"request_to_ready_us":int(ready[0][1]),
                "owned_bytes":int(ready[0][2]),"checkpoint_selection":"not_recorded_in_raw_protocol",
                "identical_request_preserves_hold":True,"repeated_backward_seek":repeat_seek and exact_target<first_target,"repeated_forward_seek":repeat_seek and exact_target>first_target,"ready_sequence":all_ready,
                "original_interior_request":repeat_seek and not owned_execution,"completion_phase_witness":"seek-wide completed target ownership" if owned_execution and repeat_seek else "Begin return and completed-application retirement" if pair else "progress callback" if repeat_seek else "completed origin",
                "cancellation_coverage":False,"index_complete":False}
        request_accepted=[(str(first_checkpoint),str(advanced_tick))]+([(str(anchor),str(first_target))] if repeat_seek and not preparation_failure else [])
        released=f"host seek released run_id={executor['run_id']} tick={exact_target if single_step and not owned_execution else held_tick} "
        if settlement_failure and executor.get("probe_interactive_controls"):
            released=f"replay UI resume dispatched tick={undo_tick} seek_released=true"
            seek_evidence["controls"]=validate_recovery_controls(texts[1],executor)
        retirement=re.findall(r"seek checkpoint retired tick=(\d+) application_idle=true",texts[1])
        seek_evidence["release"]=validate_seek_release_retirement(texts[1],released,held_tick,2 if pair else 1,
            completed_release_required=complete_seek_application)
        if pair and (retirement[0]!="210" or not texts[1].index("checkpoint pair displaced run_id=")<texts[1].index("seek checkpoint retired tick=")<texts[1].rindex("restore request prepared target=")):
            raise RuntimeError("displaced checkpoint did not retire after the completed original application")
    if preparation_cancel_run:
        if request_accepted!=[(str(anchor),str(advanced_tick))] or request_prepared or not callbacks.get("preparation_cancellation"):
            raise RuntimeError("cancelled preparation lacks its unique unpublished host request")
        restore_owner={"result":"pass","image_and_B_preparation":"host_owned",
            "cancellation_while_preparing_coverage":True,**callbacks["preparation_cancellation"]}
    elif request_accepted or request_prepared:
        if request_accepted!=([(str(first_checkpoint),str(advanced_tick)),(str(anchor),str(first_target))] if repeat_seek and not preparation_failure else [(str(anchor),str(advanced_tick))]*(restore_count-int(preparation_failure))) or len(request_prepared)!=restore_count-int(preparation_failure) or any(row[:2]!=expected or not 0<int(row[2])<=PRODUCTION_REPLAY_MEMORY_LIMIT for row,expected in zip(request_prepared,request_accepted)):
            raise RuntimeError("restore request ownership/completion evidence invalid")
        if texts[1].index(request_marker)>=texts[1].index("restore request prepared target="):
            raise RuntimeError("restore request completion precedes acceptance")
        restore_owner={"result":"pass","image_and_B_preparation":"host_owned","conflicting_requests_rejected":True,
            "owned_bytes_at_prepared":int(request_prepared[0][2]),"preparation_failure_coverage":preparation_failure,
            "cancellation_while_preparing_coverage":False,"multiple_retained_checkpoint_selection":pair}
        preparation_cancel = (f"historical cancellation injected run_id={executor['run_id']} point=before tick={undo_tick} "
                              "preparation_pending=true ")
        if preparation_cancel in texts[1]:
            pending_kind=re.findall(re.escape(preparation_cancel)+r"render_command_pending=(true|false) gpu_completion_pending=(true|false)",texts[1])
            if cancellation != "before" or texts[1].count(preparation_cancel)!=1 or not (
                    texts[1].index(request_marker) < texts[1].index(preparation_cancel)
                    < (texts[1].rindex("restore request prepared target=") if repeat_seek else texts[1].index("restore request prepared target=")) < texts[1].index(markers[2])):
                raise RuntimeError("preparation cancellation lacks pending submission and recovery ordering")
            if pending_kind not in ([("true","false")],[("false","true")]):
                raise RuntimeError("preparation cancellation lacks an unambiguous pending-work witness")
            restore_owner["cancellation_while_preparing_coverage"]=True
            restore_owner["cancelled_pending_work"]="render_command" if pending_kind[0][0]=="true" else "gpu_completion"
    if restore_reuse:
        raw=texts[1]
        reuse_marker=f"restore reuse ready target=205 original={advanced_tick} checkpoint_storage_unchanged=true prior_gpu_operation_released=true"
        cancel_marker=f"historical cancellation recovered run_id={executor['run_id']} point=after tick={advanced_tick}"
        if restore_owner["result"]!="pass" or raw.count(reuse_marker)!=1 or raw.count(cancel_marker)!=1 or not raw.index(cancel_marker)<raw.index(reuse_marker)<raw.rfind("restore request accepted target=")<raw.rfind("restore request prepared target=")<raw.index(markers[2]):
            raise RuntimeError("checkpoint reuse lacks cancellation, retained image and second request ordering")
        restore_owner["same_checkpoint_reopened_after_retirement"]=True
    application_pacing = re.findall(r"replay application pacing policy=physical_clock_fixed60_seek_v1 unpaced=(\d+) rejected=(\d+)", texts[1])
    if application_pacing:
        if len(application_pacing) != 1 or int(application_pacing[0][1]) != 0:
            raise RuntimeError("unpaced application admission rejected or retirement evidence duplicated")
        unpaced_count = int(application_pacing[0][0])
        if host_seek and not cancellation and exact_target > anchor and unpaced_count == 0:
            raise RuntimeError("seek did not exercise unpaced application advancement")
        seek_evidence["unpaced_applications"] = unpaced_count
        seek_evidence["application_clock_policy"] = "physical_clock_fixed60_seek_v1"
        if unpaced_count and not cancellation:
            costs["restore_plus_resimulation"]["pacing"] = "unpaced seek prefix; held validation and native60Hz suffix"
            costs["restore_plus_resimulation"]["unpaced_rollback_cost"] = False
    if completed_cost:
        if not application_pacing or int(application_pacing[0][0])==0:
            raise RuntimeError("completed application cost lacks actual unpaced execution")
        costs["restore_plus_completed_resimulation"]=completed_cost
    if poses["result"] != "pass":
        return {"result": "fail", "first_mismatch": "native_pose", "completion_repeat": completion_repeat, "native_poses": poses}
    if host_seek:
        costs["application_calls"]=seek_application_costs(texts[1],executor["run_id"])
    coherence_captures=coherence_capture_coverage(native,executor)
    qualification_result="fail" if (widget_clock.get("independent_continuation",{}).get("result")=="missing_coverage"
        or coherence_captures["result"]=="fail") else pacing["result"]
    seek_performance=combat_seek_performance(completed_cost,timing_pass if host_seek else False,host_seek and not cancellation,bool(executor.get("coherence_images_requested")))
    seek_performance.update(desktop_capture_timing_perturbed=bool(executor.get("desktop_capture_timing_perturbed")),diagnostic_gpu_readbacks=bool(executor.get("coherence_gpu_readbacks")))
    return {"result": qualification_result, "resume_performance":pacing, "seek_performance":seek_performance, "identities": executor["identities"], "native_control_dependencies": native_dependencies(native), "target_tick": anchor, "advanced_tick": advanced_tick, "particle_topology": ("births" if topology_birth else "unchanged") if topology_observed else "publication_not_exercised",
            "particle_birth_reproduction": {"result":"missing_coverage" if checkpoint_window else ("pass" if not cancellation or repeat_seek or pair or preparation_failure else "not_exercised"),
                "scope":"fixed tick210 birth witness is outside this checkpoint window" if checkpoint_window else "fixed combat birth witness; cancellation after birth does not reproduce it"},
            "continuation_ticks": 120, "native_callbacks_compared": len(expected_boundaries), "resume_timing": pacing,
            "cancellation": cancellation or None, "resumed_tick": held_tick, "advance_prefix_ticks": held_tick-resumed_tick,
            "exact_advance": {"result": "pass" if exact_advance else "not_exercised", "target_presentation": "unproven", "single_step": single_step,
                              "ui_step": validate_step_control(texts[1],executor) if single_step and "historical step control waiting" in texts[1] else {"result":"not_exercised"},
                              "completed_application":completed_application},
            "simulation_correctness": {"result": "pass", "continuation_ticks": 120,
                                       "changed_inputs": input_experiment, "scene_vector_fields": scene_fields, "callback_admission": callbacks, "mt": mt_coverage,"widget_clock":widget_clock},
            "visual_coherence": {"result": "incomplete", "native_poses": poses,"captures":coherence_captures,
                                 "held_output_ownership":validate_private_replay_output(texts[1]),
                                 "missing_coverage": ["actor/effect presence", "HUD freshness", "persistent visual corruption"]},
            "pixel_diagnostics": presentation,
            "capture_owner": capture_owner, "restore_owner":restore_owner,"seek_request":seek_evidence, "costs": costs, "native_display_boundaries": display_boundaries,
            "completion_repeat": completion_repeat, "native_poses": poses,
            "scope": "bounded independent gameplay, native callback and pose comparison under the recorded input policy; visual coherence and general seeking remain incomplete",
            "milestone_complete": False}


def require_resumable_baseline(args) -> None:
    report = json.loads(args.report.read_text(encoding="utf-8"))
    expected = current_identities(args)
    raw = report.get("raw_log", {})
    proof = report.get("loaded_runtime", {})
    mapped = (("observer", "loaded_observer"), ("framework", "loaded_framework"))
    if "ucrt" in expected: mapped += (("ucrt", "loaded_ucrt"),)
    absence_verified = (proof.get("verification") == "owned_process_runtime_absent"
        and all(report.get(field, {}).get("sha256") == expected[key]
            and report.get(field, {}).get("pid") == proof.get("pid")
            and report.get(field, {}).get("process_created") == proof.get("process_created")
            for key,field in mapped))
    if (report.get("result") != "captured" or report.get("mode") != "stock"
            or report.get("cleanup") != {"complete": True, "games_remaining": 0}
            or report.get("observations_requested") != args.watch_frames
            or bool(report.get("full_match")) != args.full_match
            or bool(report.get("include_setup")) != bool(args.include_setup)
            or bool(report.get("record_index")) != bool(getattr(args,"record_index",False))
            or bool(report.get("index_cancel")) != bool(getattr(args,"index_cancel",False))
            or bool(report.get("native_session_exit")) != bool(getattr(args,"native_session_exit",False))
            or bool(report.get("host_session")) != bool(getattr(args,"host_session",False))
            or report.get("probe_empty_interval") or report.get("probe_move_state") or report.get("probe_world_pause") or report.get("probe_interior_pause")
            or report.get("probe_application_pause")
            or bool(report.get("skip_intros"))!=bool(getattr(args,"skip_intros",False))
            or bool(report.get("native_fixed_seed"))!=bool(getattr(args,"native_fixed_seed",False))
            or bool(report.get("serial_particles"))!=bool(getattr(args,"serial_particles",False))
            or (args.full_match and not report.get("completion", {}).get("full_match"))
            or not absence_verified
            or any(report.get("identities", {}).get(key) != value for key, value in expected.items() if key != "runtime")
            or not raw.get("path") or sha256_file(Path(raw["path"])) != raw.get("sha256")):
        raise RuntimeError("first native capture is incomplete or stale; cannot resume baseline")
    if report.get("skip_intros"):
        validate_intro_skips(Path(raw["path"]).read_text(encoding="utf-8",errors="replace"),report["run_id"])
    # The normal pair comparison still verifies both complete raw captures,
    # loaded-module identities, outcomes, and independent run IDs afterward.


def require_bootstrap(args) -> None:
    path = OUTPUT / "bootstrap.json"
    if not path.is_file():
        raise RuntimeError("run bootstrap before baseline")
    report = json.loads(path.read_text(encoding="utf-8"))
    expected = current_identities(args)
    if (report.get("result") != "captured" or report.get("mode") != "runtime"
            or report.get("cleanup") != {"complete": True, "games_remaining": 0}
            or report.get("process_exit", {}).get("code") != 0
            or report.get("process_exit", {}).get("observed_before_owned_cleanup") is not True
            or any(report.get("identities", {}).get(key) != value for key, value in expected.items())
            or report.get("loaded_runtime", {}).get("sha256") != expected["runtime"]
            or report.get("loaded_runtime", {}).get("verification")
                != "owned_process_mapped_file_and_sha256"
            or report.get("loaded_framework", {}).get("sha256") != expected["framework"]
            or report.get("loaded_framework", {}).get("verification") != "owned_process_mapped_file_and_sha256"):
        raise RuntimeError("bootstrap is failed or stale; run bootstrap with this build and replay")
    if "ucrt" in expected:
        crt, runtime = report.get("loaded_ucrt", {}), report.get("loaded_runtime", {})
        if (crt.get("sha256") != expected["ucrt"] or crt.get("pid") != runtime.get("pid")
                or crt.get("process_created") != runtime.get("process_created")
                or crt.get("verification") != "owned_process_mapped_file_and_sha256"):
            raise RuntimeError("bootstrap lacks matching mapped native CRT identity")
    raw = report.get("raw_log", {})
    if not raw.get("path") or sha256_file(Path(raw["path"])) != raw.get("sha256"):
        raise RuntimeError("bootstrap log is missing or changed")
    timing = validate_trajectory_timing(Path(raw["path"]).read_text(encoding="utf-8", errors="replace"), report["run_id"])
    if timing != report.get("timing") or min(timing["viewport_fps_milli"], timing["tick_rate_milli"]) < 58000:
        raise RuntimeError("bootstrap cannot sustain 58 viewport updates and native ticks/s; fix basic pacing before longer runs")


def require_native_baseline(args) -> None:
    expected = current_identities(args)
    reference = OUTPUT / "baseline-a.json"
    candidate = OUTPUT / "baseline-b.json"
    result = compare_passive_controls(reference, candidate, candidate_mode="stock")
    if (result["result"] != "pass" or not result.get("completion", {}).get("full_match")
            or result.get("empty_interval_probe") or result.get("move_state_probe")
            or any(result["identities"].get(key) != value for key, value in expected.items()
                if key != "runtime" or not result.get("native_runtime_absence_verified"))):
        raise RuntimeError("a sealed, repeatable full native baseline from these exact binaries is required")
    if getattr(args, "include_setup", False) and not result.get("setup_observations_compared"):
        setup = compare_passive_controls(OUTPUT / "baseline-setup-a.json",
            OUTPUT / "baseline-setup-b.json", candidate_mode="stock")
        if (setup["result"] != "pass" or not setup.get("setup_observations_compared")
                or any(setup["identities"].get(key) != value for key, value in expected.items()
                    if key != "runtime" or not setup.get("native_runtime_absence_verified"))):
            raise RuntimeError("a repeatable native setup baseline from these exact binaries is required")


def executor_proof(report: dict, yield_every_tick: bool) -> dict:
    raw = report["raw_log"]
    path = Path(raw["path"])
    if sha256_file(path) != raw["sha256"]:
        raise RuntimeError("executor log changed")
    text = path.read_text(encoding="utf-8", errors="replace")
    if "outer-tick accounting failed" in text:
        raise RuntimeError("executor run reactivated failed legacy accounting, including after detach")
    starts = re.findall(r"executor started run_id=(\S+) native_frame=(\d+)", text)
    ends = re.findall(r"executor completed run_id=(\S+) ticks=(\d+) intervals=(\d+) "
                      r"publications=(\d+) native_frame=(\d+) disabled=true", text)
    if len(starts) != 1 or len(ends) != 1 or starts[0][0] != report["run_id"] or ends[0][0] != report["run_id"]:
        raise RuntimeError("executor lacks unique activation and completed-tail evidence")
    worlds = re.findall(r"world executor completed run_id=(\S+) worlds=(\d+) groups=(\d+) tasks=(\d+) "
        r"manager_tasks=(\d+) manager_yields=(\d+) idle=true disabled=true", text)
    if len(worlds) != 1 or worlds[0][0] != report["run_id"] or min(map(int, worlds[0][1:5])) <= 0:
        raise RuntimeError("executor lacks completed world-tail evidence")
    engines = re.findall(r"engine executor completed run_id=(\S+) intervals=(\d+) idle=true disabled=true", text)
    if len(engines) != 1 or engines[0][0] != report["run_id"] or int(engines[0][1]) <= 0:
        raise RuntimeError("executor lacks completed engine-tail evidence")
    applications = re.findall(r"application executor stopped intervals=(\d+) idle=true disabled=true", text)
    if len(applications) != 1 or int(applications[0]) != int(engines[0][1]):
        raise RuntimeError("executor lacks completed application-tail evidence")
    arenas = re.findall(r"world arena completed run_id=(\S+) scopes=(\d+) max_retained_bytes=(\d+) probe_checks=(\d+) empty=true", text)
    if (len(arenas) != 1 or arenas[0][0] != report["run_id"]
            or int(arenas[0][1]) < int(worlds[0][3])
            or (yield_every_tick and (int(arenas[0][3]) < 2 or int(arenas[0][2]) < 64))):
        raise RuntimeError("executor lacks released world-arena evidence")
    _, ticks, intervals, publications, frame = ends[0]
    post_replay_ticks = 0
    physical_frame = int(frame)
    if report.get("record_index") and report.get("index_cancel"):
        if report.get("full_match"):
            raise RuntimeError("cancelled index cannot claim full replay closure")
        validate_held_index_cancellation(text,report["run_id"])
    elif report.get("native_session_exit"):
        validate_native_session_exit(text,report["run_id"])
    elif report.get("record_index"):
        closure = re.findall(r"index closure run_id="+re.escape(report["run_id"])+r" replay_tick=(\d+) native_tick=(\d+) callbacks_retained=true", text)
        if len(closure)!=1 or int(closure[0][0])!=int(frame) or not 0<=int(closure[0][1])-int(frame)<=300:
            raise RuntimeError("executor lacks bounded post-replay closure accounting")
        physical_frame=int(closure[0][1]);post_replay_ticks=physical_frame-int(frame)
    indexed_seek=None
    if report.get("index_seek"):
        physical_rows,indexed_prefix,_,indexed_seek=indexed_seek_boundary_streams(text,report["run_id"],recovery=report.get("index_recovery",""))
        # Comparison ends at328; native execution continues through observer
        # detachment. Account those traversals from independent callee records.
        # Cancelled execution ends with discarded C callbacks, then restores B
        # without traversing B again. The independently checked recovery/exit
        # witnesses establish that coordinate; count discarded work separately.
        tail = indexed_prefix[-1] if report.get("index_recovery") else physical_rows[-1]
        physical_frame=int(tail["frame"])
        if tail["phase"]!="actor_tail" or not indexed_seek["last_tick"]<=physical_frame<=indexed_seek["last_tick"]+300:
            raise RuntimeError("indexed seek lacks bounded completed detachment tails")
    if int(ticks) != physical_frame - int(starts[0][1]) or min(int(ticks), int(intervals), int(publications)) <= 0:
        raise RuntimeError("executor traversal accounting disagrees with the native clock")
    boundaries = re.findall(r"executor boundaries run_id=(\S+) completed_intervals=(\d+) "
        r"zero_intervals=(\d+) multi_intervals=(\d+) repeat_requests=(\d+) move_state_ticks=(\d+) "
        r"yielded_boundaries=(\d+) yield_every_tick=(true|false)", text)
    if len(boundaries) != 1 or boundaries[0][0] != report["run_id"]:
        raise RuntimeError("executor lacks unique boundary coverage evidence")
    _, completed, zero, multi, repeats, move, yielded, requested = boundaries[0]
    restore = validate_small_restore(text, report["run_id"]) if report.get("probe_small_restore") else None
    trial_ticks = restore["trial_ticks"] if restore else 0
    historical_ticks = historical_intervals = historical_publications = 0
    rewind = re.findall(r"historical execution rewind run_id=(\S+) ticks=(\d+) intervals=(\d+)", text)
    if (not rewind and report.get("complete_seek_application")
            and report.get("seek_ownership_protocol")=="retained_B_through_C_v1"):
        rewind=[completed_seek_observed_rewind(text,report["run_id"],report["historical_anchor_tick"],
                                              report["historical_advanced_tick"],report["host_seek_target"])]
    if report.get("rolling_cycles"):
        rolling=validate_rolling_candidate(report,report['rolling_cycles'])
        receipt=re.findall(r'rolling completed run_id='+re.escape(report['run_id'])+r' first=210 last=\d+ cycles=\d+ resimulated_ticks=(\d+) resimulated_intervals=(\d+)',text)
        if rewind or len(receipt)!=1:raise RuntimeError('rolling execution has ambiguous rewind receipts')
        historical_ticks,historical_intervals=map(int,receipt[0])
        if historical_ticks!=rolling['resimulated_ticks']:raise RuntimeError('rolling execution count differs')
    elif report.get("probe_historical_restore") and (not report.get("historical_cancel") or report.get("host_seek_repeat") or report.get("seek_advance_failure") or report.get("consumer_mutation") or (report.get("completion_repeat") and report.get("historical_cancel"))):
        if len(rewind) != (2 if report.get("host_seek_repeat") and not report.get("historical_cancel") else 1) or any(row[0] != report["run_id"] or (min(map(int,row[1:]))<=0
                and not (index==1 and report.get("host_seek_repeat") and not report.get("historical_cancel")
                    and host_seek_first_target(report)==report.get("historical_anchor_tick",205)
                    and row[1:]==("0","0"))) for index,row in enumerate(rewind)):
            raise RuntimeError("historical commit lacks measured execution rewind")
        historical_ticks=sum(int(row[1]) for row in rewind)
        historical_intervals=sum(int(row[2]) for row in rewind)
        planned_ticks=(5+(0 if report.get("historical_cancel") else 5)) if report.get("checkpoint_pair") else report.get("historical_advanced_tick",210)-report.get("historical_anchor_tick",205)+((host_seek_first_target(report)-report.get("historical_anchor_tick",205)) if report.get("host_seek_repeat") and not report.get("historical_cancel") else 0)
        if report.get("completion_repeat") and report.get("historical_cancel")=="after":
            recovered=validate_seek_recovery_ownership(text,205,210,208,208,deferred=True)
            planned_ticks=recovered["executed_ticks"]
            if historical_intervals!=recovered["executed_intervals"]:
                raise RuntimeError("deferred cancellation lost completed native C interval accounting")
        if report.get("consumer_mutation"):
            from deterministic_qualification.replay_fidelity import consumer_mutation_abort_tick
            recovered=validate_seek_recovery_ownership(text,210,217,consumer_mutation_abort_tick(text),217,consumer=True)
            planned_ticks=recovered["executed_ticks"]
            if historical_intervals!=recovered["executed_intervals"]:
                raise RuntimeError("consumer recovery lost native C interval accounting")
        if report.get("seek_advance_failure"):
            from deterministic_qualification.replay_fidelity import particle_lifetime_recovery, particle_lifetime_abort_tick
            lifetime=particle_lifetime_recovery(report)
            recovered=validate_seek_recovery_ownership(text,report["historical_anchor_tick"],report["historical_advanced_tick"],particle_lifetime_abort_tick(text) if lifetime else report.get("host_seek_target",214) if report.get("seek_settlement_failure") else 208,report.get("host_seek_target",214),bool(report.get("seek_settlement_failure")),bool(report.get("seek_drained_cancel")),lifetime=bool(lifetime))
            planned_ticks=recovered["executed_ticks"]
            if historical_intervals!=recovered["executed_intervals"]:
                raise RuntimeError("discarded C intervals disagree with completed native recovery work")
        if execution_fallback_case(report):
            receipt=execution_fallback_receipt(text,report["run_id"])
            planned_ticks=40+receipt["discarded_ticks"]
        if historical_ticks != planned_ticks:
            raise RuntimeError("historical rewind disagrees with the bounded native targets")
    elif indexed_seek is not None and report.get("index_recovery"):
        if rewind:raise RuntimeError("indexed cancellation cannot use committed-seek rewind accounting")
        proof=indexed_seek["recovery"]
        historical_ticks=proof["executed_ticks"];historical_intervals=proof["executed_intervals"]
        historical_publications=sum(row["phase"]=="input_cache_publication" for row in
            indexed_seek_boundary_streams(text,report["run_id"],recovery=report["index_recovery"])[2])
    elif indexed_seek is not None and indexed_seek.get("sequence"):
        cases=indexed_seek['cases']
        if len(rewind)!=len(cases):raise RuntimeError("indexed sequence lacks every execution rewind")
        for row,case in zip(rewind,cases):
            anchor=re.findall(rf"index row tick={case['checkpoint']} [^\n]*interval=(\d+)",text)
            if (len(anchor)!=1 or row[0]!=report['run_id'] or int(row[1])!=case['origin']-case['checkpoint']
                    or int(row[2])!=case['interval']-int(anchor[0])):
                raise RuntimeError("indexed sequence logical rewind differs from native/index coordinates")
        historical_ticks=sum(int(row[1]) for row in rewind);historical_intervals=sum(int(row[2]) for row in rewind)
        historical_publications=sum(row['phase']=='input_cache_publication' for row in physical_rows)-int(publications)
        if historical_publications<0:raise RuntimeError("indexed sequence publication accounting is negative")
    elif indexed_seek is not None:
        if len(rewind)!=1 or rewind[0][0]!=report["run_id"] or int(rewind[0][1])!=indexed_seek["origin"]-indexed_seek["checkpoint"]:
            raise RuntimeError("indexed seek rewind disagrees with independently observed origin")
        anchor=re.findall(rf"index row tick={indexed_seek['checkpoint']} [^\n]*interval=(\d+)",text)
        if len(anchor)!=1 or int(rewind[0][2])!=indexed_seek["interval"]-int(anchor[0]):
            raise RuntimeError("indexed seek interval rewind disagrees with retained checkpoint mapping")
        historical_ticks=int(rewind[0][1]);historical_intervals=int(rewind[0][2])
        anchor_publications=re.findall(rf"index row tick={indexed_seek['checkpoint']} [^\n]*publications=(\d+)",text)
        if len(anchor_publications)!=1:
            raise RuntimeError("indexed seek lacks retained publication coordinate")
        historical_publications=sum(row["phase"]=="input_cache_publication" for row in indexed_prefix)-int(anchor_publications[0])
        if historical_publications<0:raise RuntimeError("indexed seek publication rewind is negative")
    elif rewind:
        raise RuntimeError("unexpected historical execution rewind")
    direct_yields = int(bool(report.get("probe_move_state")) and yield_every_tick)
    if (int(completed) != int(intervals) + historical_intervals or (requested == "true") != yield_every_tick
            or int(worlds[0][4]) + int(bool(report.get("probe_empty_interval")))
                + int(bool(report.get("probe_move_state"))) != int(completed)
            or int(worlds[0][5]) + direct_yields != int(yielded)
            or int(zero) + int(multi) > int(completed)
            or (yield_every_tick and int(yielded) != int(ticks) + trial_ticks + historical_ticks)
            or (not yield_every_tick and int(yielded) != 0)):
        raise RuntimeError("executor boundary coverage disagrees with the requested mode")
    return {"ticks": int(ticks), "intervals": int(intervals), "publications": int(publications),
            "physical_ticks":int(ticks)+historical_ticks,
            "physical_intervals":int(intervals)+historical_intervals,
            "physical_publications":int(publications)+historical_publications,
            "replay_endpoint_frame":int(frame), "physical_native_frame":physical_frame,"post_replay_ticks":post_replay_ticks,
            "completed_worlds": int(worlds[0][1]), "disabled_at_completed_world": True,
            "completed_engines": int(engines[0][1]), "disabled_at_completed_engine": True,
            "completed_applications": int(applications[0]), "disabled_at_completed_application": True,
            "completed_groups": int(worlds[0][2]), "dispatched_tasks": int(worlds[0][3]),
            "manager_tasks": int(worlds[0][4]), "manager_task_yields": int(worlds[0][5]),
            "direct_interval_yields": direct_yields,
            "arena_scopes": int(arenas[0][1]), "max_retained_arena_bytes": int(arenas[0][2]), "arena_empty": True,
            "arena_probe_checks": int(arenas[0][3]),
            "disabled_at_completed_interval": True, "zero_tick_intervals": int(zero),
            "multi_tick_intervals": int(multi), "repeat_requests": int(repeats),
            "restore_trial_ticks": trial_ticks,
            "historical_rewind_ticks": historical_ticks, "historical_rewind_intervals": historical_intervals,
            "move_state_ticks": int(move), "yielded_boundaries": int(yielded),
            "yield_every_tick": yield_every_tick}


def validate_interior_pause(raw: str, run_id: str) -> dict:
    targets = re.findall(r"interior pause started run_id=" + re.escape(run_id) + r" native_frame=(\d+) ", raw)
    if len(targets) != 1 or int(targets[0]) <= 0:
        raise RuntimeError("interior pause lacks a positive unique target")
    tick = int(targets[0])
    started = re.findall(rf"interior pause started run_id=(\S+) native_frame={tick} epoch=(\d+) pending_event=true", raw)
    held = re.findall(rf"interior pause held run_id=(\S+) native_frame={tick} epoch=(\d+) elapsed_us=(\d+) "
        r"application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) unchanged=true pending_event=true", raw)
    resumed = re.findall(rf"interior pause resumed run_id=(\S+) held_tick={tick} native_callbacks=(\d+)", raw)
    if (len(started) != 1 or len(held) != 1 or len(resumed) != 1
            or started[0] != held[0][:2] or started[0][0] != run_id or resumed[0][0] != run_id
            or int(held[0][2]) < 1_000_000 or min(map(int, held[0][3:5])) < 30
            or not 0 < int(held[0][5]) <= 32 * 1024 * 1024 or int(resumed[0][1]) <= 0
            or not raw.index("interior pause started") < raw.index("interior pause held") < raw.index("interior pause resumed")):
        raise RuntimeError("interior pause lacks independent held-state, pending-event, UI-frame, or resume evidence")
    # Read the independent native round-sequence record before suspension.
    # A setup/intro pause cannot supply active-combat coverage.
    boundaries = re.findall(rf"boundary ordinal=\d+ phase=round_sequence sample_version=[2345] frame={tick} [^\r\n]+", raw)
    combat = len(boundaries) == 1 and bool(re.search(
        r"cursor=[1-9]\d* .*source_active=true manager_phase=2 move_state=0 round_state=2 .*world_mode=2(?:\s|$)", boundaries[0]))
    if tick != 360 and (not combat or raw.index(boundaries[0]) > raw.index("interior pause started")):
        raise RuntimeError("combat interior pause lacks independent active-source and combat-state evidence")
    return {"independent_hold_verified": True, "active_combat_verified": combat,
            "tick": tick, "epoch": int(held[0][1]), "elapsed_us": int(held[0][2]),
            "application_updates": int(held[0][3]), "surface_frames": int(held[0][4]),
            "surface_bytes": int(held[0][5]), "resumed_native_callbacks": int(resumed[0][1]),
            "scope": "bounded pending-task suspension and render-side overlay updates; "
                "this witness alone does not prove target presentation, interactive controls or general window lifecycle"}



def validate_particle_copy(raw: str, run_id: str) -> dict:
    identity = re.escape(run_id)
    for target in (205, 210):
        boundary = re.findall(rf"boundary ordinal=\d+ phase=round_sequence sample_version=[2345] frame={target} [^\r\n]+", raw)
        marker = f"application pause started run_id={run_id} native_frame={target} "
        if (len(boundary) != 1 or not re.search(
                r"cursor=[1-9]\d* .*source_active=true manager_phase=2 move_state=0 round_state=2 .*world_mode=2(?:\s|$)", boundary[0])
                or marker not in raw or raw.index(boundary[0]) > raw.index(marker)):
            raise RuntimeError("particle copy lacks the fixed independent combat boundaries")
    captured = re.findall(rf"particle copy captured run_id={identity} tick=205 system=(\d+) pool=(\d+) resources=6 "
                         r"gpu_complete=true reserved_payload_and_metadata_bytes=(\d+)", raw)
    retained = re.findall(rf"particle copy survived run_id={identity} captured_tick=205 held_tick=210 system=(\d+) pool=(\d+) "
                         r"retained_matches=true changed_sources=([1-6]) reserved_payload_and_metadata_bytes=(\d+) gpu_complete=true", raw)
    released = f"particle copy released run_id={run_id} tick=210 in_flight=false"
    resumed = f"particle copy native playback resumed run_id={run_id} held_tick=210"
    retirement = (f"particle copy retirement run_id={run_id} tick=210 cancel_release_blocked=true "
                  "cancel_retired=true timeout_release_blocked=true timeout_retired=true")
    if (len(captured) != 1 or len(retained) != 1 or captured[0][:2] != retained[0][:2]
            or int(retained[0][3]) != int(captured[0][2]) + 56 * 1024 * 1024
            or not 0 < int(captured[0][2]) < int(retained[0][3]) <= PRODUCTION_REPLAY_MEMORY_LIMIT
            or released not in raw or resumed not in raw or raw.count(retirement) != 1
            or raw.index(retirement) > raw.index(released)
            or not raw.index("particle copy captured") < raw.index("particle copy survived") < raw.index(released) < raw.index(resumed)
            or f"particle copy failed run_id={run_id}" in raw):
        raise RuntimeError("particle copy lacks completed capture, witnessed native writes, retained image agreement or retirement")
    installed = (f"particle textures installed run_id={run_id} tick=210 captured_tick=205 images_match=true "
                 "gpu_complete=true release_blocked=true resume_blocked=true cpu_state=B target_presented=false")
    recovered = re.findall(rf"particle textures recovered run_id={identity} tick=210 images_match=true "
                           r"gpu_complete=true cancelled_after_publication=true uncommitted=false selection=([01])", raw)
    if (raw.count(installed) != 1 or len(recovered) != 1 or "historical cpu replacement prepared" not in raw
            or not raw.index("historical cpu replacement prepared") < raw.index(installed)
                < raw.index("particle textures recovered") < raw.index(released)):
        raise RuntimeError("particle transaction lacks completed texture publication, blocked release, or cancellation undo")
    published = re.findall(rf"cpu replacement published run_id={identity} tick=205 original=(\d+) replacement=(\d+) component=(\d+)", raw)
    undone = re.findall(rf"cpu replacement undone run_id={identity} tick=205 elapsed_us=(\d+) updates=(\d+) surface_frames=(\d+) bytes=(\d+) original_restored=true", raw)
    prepared = re.findall(rf"historical cpu replacement prepared run_id={identity} captured_tick=205 held_tick=210 original_retired=true address_reused=(?:true|false) independent_storage=true historical_publication=false bytes=(\d+)", raw)
    if (len(published) != 1 or len(undone) != 1 or len(prepared) != 1
            or min(map(int, published[0])) <= 0 or published[0][0] == published[0][1]
            or int(undone[0][0]) < 250000 or int(undone[0][1]) < 30 or int(undone[0][2]) < 10
            or not raw.index("cpu replacement published") < raw.index("cpu replacement undone") < raw.index("particle copy captured")
                < raw.index("historical cpu replacement prepared") < raw.index(released)):
        raise RuntimeError("particle diagnostic lacks CPU replacement, held undo, or historical preparation evidence")
    partitions = []
    object_leases = []
    for tick in (205, 210):
        partition = re.findall(rf"shared particle partition run_id={identity} tick={tick} owners=(\d+) allocated=(\d+) free=(\d+) complete=true disjoint=true", raw)
        owners = re.findall(rf"shared particle owner run_id={identity} tick={tick} component=(\d+) emitters=(\d+) tiles=(\d+) object=([^\r\n]+)", raw)
        if (len(partition) != 1 or not owners or len(owners) != int(partition[0][0])
                or len({row[0] for row in owners}) != len(owners)
                or sum(int(row[2]) for row in owners) != int(partition[0][1])
                or int(partition[0][1]) + int(partition[0][2]) != 65536):
            raise RuntimeError("shared particle allocation counts lack consistent owner evidence")
        partitions.append({"tick": tick, "owners": int(partition[0][0]), "allocated_tiles": int(partition[0][1]),
                           "free_tiles": int(partition[0][2]),
                           "free_list_disjoint": True,
                           "scope": "allocation partition only; lifetime retention and historical restoration unproven"})
        lease = re.findall(rf"particle object lease run_id={identity} tick={tick} objects=(\d+) bytes=(\d+) acquired=true code=0", raw)
        if len(lease) != 1 or int(lease[0][0]) < len(owners) or int(lease[0][1]) <= 0:
            raise RuntimeError("particle transaction lacks object lease acquisition")
        object_leases.append({"tick": tick, "objects": int(lease[0][0]), "bytes": int(lease[0][1])})
    leases_retired = f"particle object leases retired run_id={run_id} tick=210 registered=0"
    if raw.count(leases_retired) != 1 or not raw.index("particle object lease run_id=") < raw.index(leases_retired) < raw.index(released):
        raise RuntimeError("particle transaction lacks explicit object lease retirement before resume")
    return {"captured_tick": 205, "held_tick": 210, "changed_sources": int(retained[0][2]),
            "texture_publication_undone": True, "cancelled_after_publication": True,
            "historical_combat_restore": False, "target_presentation": False,
            "shared_pool_owner_counts": partitions,
            "object_leases": object_leases, "object_leases_retired": True,
            "object_lease_scope": "registration, live-binding validation and removal only; collection-time retention and explicit component destruction unproven",
            "current_boundary_cpu_replacement_undo": True, "historical_cpu_preparation": True,
            "cancel_and_timeout_retirement": True,
            "reserved_payload_and_metadata_bytes": int(retained[0][3]),
            "scope": "GPU image survival, texture publication/cancellation undo and current-boundary CPU replacement/undo; coherent historical installation, target presentation, complete owned-memory accounting and independent replay equivalence unproven"}


def validate_application_pause(raw: str, run_id: str, *, consumer_task: bool = False) -> dict:
    targets = re.findall(r"application pause started run_id=" + re.escape(run_id) + r" native_frame=(\d+) ", raw)
    if (len(targets) != 2 or int(targets[0]) <= 0
            or (tuple(map(int, targets)) != (210, 217) if consumer_task else int(targets[1]) != int(targets[0]) + 5)):
        raise RuntimeError("application pause lacks the required ordered combat targets")
    tick, reentry_tick = map(int, targets)
    for target in (tick, reentry_tick):
        boundary = re.findall(rf"boundary ordinal=\d+ phase=round_sequence sample_version=[2345] frame={target} [^\r\n]+", raw)
        if (len(boundary) != 1 or not re.search(
                r"cursor=[1-9]\d* .*source_active=true manager_phase=2 move_state=0 round_state=2 .*world_mode=2(?:\s|$)", boundary[0])
                or raw.index(boundary[0]) > raw.index(f"application pause started run_id={run_id} native_frame={target} ")):
            raise RuntimeError("application pause lacks independent active-combat evidence")
    started = re.findall(rf"application pause started run_id=(\S+) native_frame={tick} epoch=(\d+) applications=(\d+) task_retired=true", raw)
    held = re.findall(rf"application pause held run_id=(\S+) native_frame={tick} epoch=(\d+) elapsed_us=(\d+) "
        rf"application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) applications=(\d+) unchanged=true task_retired=true", raw)
    resumed = re.findall(rf"application pause resumed run_id=(\S+) held_tick={tick} native_callbacks=(\d+)", raw)
    captured = re.findall(rf"application checkpoint captured run_id=(\S+) native_frame={tick} bytes=(\d+) timers=(\d+) borrowed_task=false", raw)
    retained = re.findall(rf"application checkpoint retained run_id=(\S+) captured_tick={tick} resumed_tick=(\d+) "
        rf"epoch_advanced=true storage_unchanged=true borrowed_task=false", raw)
    if (len(started) != 1 or len(held) != 1 or len(resumed) != 1
            or started[0][0] != run_id or held[0][0] != run_id or resumed[0][0] != run_id
            or started[0][1] != held[0][1] or started[0][2] != held[0][6]
            or int(started[0][2]) <= 0 or int(held[0][2]) < 1_000_000
            or min(map(int, held[0][3:5])) < 30 or not 0 < int(held[0][5]) <= 32 * 1024 * 1024
            or int(resumed[0][1]) <= 0
            or not raw.index("application pause started") < raw.index("application pause held") < raw.index("application pause resumed")):
        raise RuntimeError("application pause lacks unchanged-state, completed-task, surface, or continuation evidence")
    if (len(captured) != 1 or len(retained) != 1 or captured[0][0] != run_id or retained[0][0] != run_id
            or min(map(int, captured[0][1:])) <= 0 or int(retained[0][1]) <= tick
            or not raw.index("application pause started") < raw.index("application checkpoint captured")
                < raw.index("application pause held") < raw.index("application checkpoint retained")
                < raw.index("application pause resumed")):
        raise RuntimeError("application checkpoint lacks capture and retained owned-storage evidence")
    inventory = re.findall(rf"scheduler inventory run_id=(\S+) levels=(\d+) ticks=(\d+) kinds=(\d+) active=(\d+) disabled=(\d+) "
        rf"cooldown=(\d+) newly_spawned=(\d+) prerequisites=(\d+) outside_prerequisites=(\d+) pending_tasks=(\d+)", raw)
    kinds = re.findall(rf"scheduler kind run_id=(\S+) vtable_rva=(\d+) ticks=(\d+) interval_ticks=(\d+) prerequisites=(\d+) pending_tasks=(\d+)", raw)
    if (len(inventory) != 1 or inventory[0][0] != run_id or min(map(int, inventory[0][1:4])) <= 0
            or len(kinds) != int(inventory[0][3]) or any(row[0] != run_id for row in kinds)
            or len({int(row[1]) for row in kinds}) != len(kinds)
            or sum(int(row[2]) for row in kinds) != int(inventory[0][2])
            or sum(int(row[4]) for row in kinds) != int(inventory[0][8])
            or sum(int(row[5]) for row in kinds) != int(inventory[0][10])):
        raise RuntimeError("scheduler inventory lacks consistent native type and membership counts")
    scheduler = dict(zip(("levels", "ticks", "kinds", "active", "disabled", "cooldown", "newly_spawned",
                          "prerequisites", "outside_prerequisites", "pending_tasks"), map(int, inventory[0][1:])))
    scheduler["types"] = [dict(zip(("vtable_rva", "ticks", "interval_ticks", "prerequisites", "pending_tasks"),
                                   map(int, row[1:]))) for row in kinds]
    owned = re.findall(rf"scheduler captured run_id=(\S+) levels=(\d+) ticks=(\d+) prerequisites=(\d+) bytes=(\d+) zero_budget_rejected=true", raw)
    if (len(owned) != 1 or owned[0][0] != run_id
            or tuple(map(int, owned[0][1:4])) != (scheduler["levels"], scheduler["ticks"], scheduler["prerequisites"])
            or not 0 < int(owned[0][4]) <= PRODUCTION_REPLAY_MEMORY_LIMIT
            or not raw.index("scheduler inventory") < raw.index("scheduler captured") < raw.index("application checkpoint captured")):
        raise RuntimeError("scheduler capture lacks independent coverage and capacity-rejection evidence")
    scheduler["owned_bytes"] = int(owned[0][4])
    vfx = re.findall(rf"vfx requests captured run_id=(\S+) native_frame={tick} particles=(\d+) debris=(\d+) bytes=(\d+) "
        r"independent_storage=true zero_budget_rejected=true", raw)
    if (len(vfx) != 1 or vfx[0][0] != run_id
            or int(vfx[0][3]) < (int(vfx[0][1]) + int(vfx[0][2])) * 208
            or int(vfx[0][3]) > PRODUCTION_REPLAY_MEMORY_LIMIT - scheduler["owned_bytes"]
            or not raw.index("application pause started") < raw.index("vfx requests captured")
                < raw.index("application checkpoint captured")):
        raise RuntimeError("VFX request storage lacks bounded capture and rejection evidence")
    scheduler["vfx_requests"] = {"particles": int(vfx[0][1]), "debris": int(vfx[0][2]), "bytes": int(vfx[0][3]),
        "scope": "owned request storage only; component/emitter and historical lifecycle restoration remain unproven"}
    bindings = re.findall(rf"scheduler bindings validated run_id=(\S+) native_frame=(\d+) strong=(\d+) weak=(\d+) foreign_bindings_rejected=true", raw)
    lease_released = re.findall(rf"scheduler scene lease released run_id=(\S+) strong=(\d+) weak=(\d+)", raw)
    if (len(bindings) != 2 or any(row[0] != run_id for row in bindings)
            or [int(row[1]) for row in bindings] != [tick, reentry_tick]
            or bindings[0][2:] != bindings[1][2:] or min(map(int, bindings[0][2:])) <= 0
            or len(lease_released) != 1 or lease_released[0][0] != run_id
            or lease_released[0][1] != bindings[0][2]
            or int(lease_released[0][2]) <= 0 or int(lease_released[0][2]) + 1 != int(bindings[0][3])
            or "application reentry resumed" not in raw
            or raw.index("scheduler scene lease released") < raw.index("application reentry resumed")):
        raise RuntimeError("scheduler bindings lack repeated preflight and balanced weak-reference ownership evidence")
    scheduler["weak_scene_lease_released"] = True
    prepared = re.findall(rf"scheduler prepared run_id=(\S+) patches=(\d+) allocations=(\d+) bytes=(\d+) "
        rf"zero_budget_rejected=true live_unchanged=true never_installed=true", raw)
    prepared_released = re.findall(rf"scheduler prepared storage released run_id=(\S+) unchanged=true never_installed=true", raw)
    if (len(prepared) != 1 or prepared[0][0] != run_id
            or int(prepared[0][1]) != scheduler["levels"] * 4 + scheduler["ticks"]
            or int(prepared[0][2]) <= 0 or not 0 < int(prepared[0][3]) <= PRODUCTION_REPLAY_MEMORY_LIMIT - scheduler["owned_bytes"]
            or len(prepared_released) != 1 or prepared_released[0] != run_id
            or not raw.index("application pause started") < raw.index("scheduler prepared run_id=")
                < raw.index("application checkpoint captured") < raw.index("application reentry resumed")
                < raw.index("scheduler prepared storage released") < raw.index("scheduler scene lease released")):
        raise RuntimeError("scheduler preparation lacks bounded storage, rejection and release evidence")
    scheduler["prepared_storage"] = {"patches": int(prepared[0][1]), "allocations": int(prepared[0][2]),
                                     "bytes": int(prepared[0][3]), "retained_and_released_without_installation": True}
    replaced = re.findall(rf"scheduler backing replaced run_id=(\S+) native_frame=(\d+) epoch=(\d+) bytes=(\d+) elapsed_us=(\d+) "
        rf"capacity_rejected=true cancellation_undone=true committed=true unchanged=true", raw)
    if (len(replaced) != 1 or replaced[0][0] != run_id or int(replaced[0][1]) != tick
            or replaced[0][2] != started[0][1]
            or not 0 < int(replaced[0][3]) <= PRODUCTION_REPLAY_MEMORY_LIMIT - scheduler["owned_bytes"] - int(prepared[0][3])
            or not raw.index("scheduler prepared run_id=") < raw.index("scheduler backing replaced")
                < raw.index("application checkpoint captured")):
        raise RuntimeError("scheduler replacement lacks current-boundary commit, cancellation undo and capacity-rejection evidence")
    scheduler["backing_replacement"] = {"tick": tick, "epoch": int(replaced[0][2]), "bytes": int(replaced[0][3]),
        "elapsed_us": int(replaced[0][4]), "cancellation_undone": True, "scope": "current-boundary storage replacement only"}
    reentry_started = re.findall(rf"application pause started run_id=(\S+) native_frame={reentry_tick} epoch=(\d+) applications=(\d+) task_retired=true", raw)
    reentry_held = re.findall(rf"application pause held run_id=(\S+) native_frame={reentry_tick} epoch=(\d+) elapsed_us=(\d+) "
        rf"application_updates=(\d+) surface_frames=(\d+) surface_bytes=(\d+) applications=(\d+) unchanged=true task_retired=true", raw)
    reentry_resumed = re.findall(rf"application reentry resumed run_id=(\S+) held_tick={reentry_tick} native_callbacks=(\d+) storage_unchanged=true", raw)
    rejected = re.findall(rf"application historical restore rejected run_id=(\S+) captured_tick={tick} held_tick={reentry_tick} "
        rf"old_epoch=(\d+) current_epoch=(\d+) unchanged=true", raw)
    if (any(len(rows) != 1 or rows[0][0] != run_id for rows in (reentry_started, reentry_held, reentry_resumed, rejected))
            or reentry_started[0][1] != reentry_held[0][1] or reentry_started[0][2] != reentry_held[0][6]
            or rejected[0][1] != started[0][1] or rejected[0][2] != reentry_started[0][1]
            or int(reentry_started[0][1]) <= int(started[0][1]) or int(reentry_started[0][2]) <= int(started[0][2])
            or int(reentry_held[0][2]) < 1_000_000 or min(map(int, reentry_held[0][3:5])) < 30
            or not 0 < int(reentry_held[0][5]) <= 32 * 1024 * 1024 or int(reentry_resumed[0][1]) <= 0
            or not raw.index("application pause resumed") < raw.index(f"native_frame={reentry_tick} epoch=")
                < raw.index("application historical restore rejected") < raw.index("application pause held run_id=" + run_id + f" native_frame={reentry_tick}")
                < raw.index("application reentry resumed")):
        raise RuntimeError("application reentry lacks retired work, unchanged held state, historical rejection or resumed continuation evidence")
    historical = re.findall(rf"scheduler historical install cancelled run_id=(\S+) captured_tick={tick} held_tick={reentry_tick} "
        rf"old_epoch=(\d+) current_epoch=(\d+) elapsed_us=(\d+) changed_before=true target_observed=true current_recovered=true epoch_preserved=true", raw)
    if (len(historical) != 1 or historical[0][0] != run_id
            or historical[0][1:3] != rejected[0][1:3]
            or not raw.index("application historical restore rejected") < raw.index("scheduler historical install cancelled")
                < raw.index("application pause held run_id=" + run_id + f" native_frame={reentry_tick}")):
        raise RuntimeError("historical scheduler lacks independent target observation, preserved epoch and current-state undo evidence")
    scheduler["historical_cancelled_install"] = {"captured_tick": tick, "held_tick": reentry_tick,
        "elapsed_us": int(historical[0][3]), "scope": "scheduler component installation and undo only"}
    return {"tick": tick, "epoch": int(held[0][1]), "completed_applications": int(held[0][6]),
            "elapsed_us": int(held[0][2]), "application_updates": int(held[0][3]), "surface_frames": int(held[0][4]),
            "checkpoint_retained_after_resume": True, "encoded_bytes": int(captured[0][1]), "timers": int(captured[0][2]),
            "scheduler_inventory": scheduler,
            "reentry": {"tick": reentry_tick, "epoch": int(reentry_started[0][1]), "elapsed_us": int(reentry_held[0][2]),
                        "application_updates": int(reentry_held[0][3]), "surface_frames": int(reentry_held[0][4]),
                        "historical_restore_rejected_without_observed_change": True},
            "scope": "completed-application holds and current-boundary backing replacement; historical restoration and target presentation remain unproven"}


def validate_complete_B_target_preparation(raw: str, run_id: str) -> None:
    # This experiment specifically needs complete B before the expired A
    # owner is rejected. A different early failure must not launch a control.
    markers=("host seek repeated run_id="+run_id,
        "restore undo captured target=205 original=220 gpu_complete=true before_target_preparation=true",
        "lighting missing target primitive",
        f"host seek preparation failure observed run_id={run_id} tick=220 unchanged_B=true before_publication=true")
    positions=[raw.find(marker) for marker in markers]
    if any(position<0 for position in positions) or positions!=sorted(positions):
        failures=re.findall(r"(?:checkpoint component failed|motion skeleton capture rejected|checkpoint capture failed) [^\r\n]+",raw)
        detail=failures[0] if failures else "complete B / expired-target preparation sequence missing"
        raise RuntimeError(f"{detail}; intended target-preparation failure not reached, native control deferred")


def validate_private_replay_output(raw: str) -> dict:
    if 'native replay viewport prepared ' in raw:
        if 'private replay output prepared ' in raw:
            raise RuntimeError('original viewport run also created a private output')
        prepared=re.findall(r'native replay viewport prepared hwnd=(\d+) bytes=(\d+) swap_effect=(\d+) buffers=(\d+)',raw)
        windows=set(re.findall(r'DX11 overlay initialised \(hwnd=(\d+),',raw))
        displayed=re.findall(r'native replay viewport displayed tick=(\d+) hwnd=(\d+) backbuffer_restored=true gpu_complete=true',raw)
        retired=re.findall(r'native replay viewport retired hwnd=(\d+) gpu_complete=true',raw)
        if (len(windows)!=1 or not prepared or not displayed or [p[0] for p in prepared]!=retired
                or any(p[0] not in windows or p[2]!='0' or int(p[3])<1 or not 0<int(p[1])<=32*1024*1024 for p in prepared)
                or any(p[1] not in windows for p in displayed)
                or 'nested Present detected' in raw or 'native replay viewport completion failed' in raw):
            raise RuntimeError('original viewport lacks native identity or completed backbuffer/GPU restoration')
        events=re.findall(r'native replay viewport (prepared|displayed|retired) (?:tick=\d+ )?hwnd=(\d+)',raw)
        active=None
        for kind,window in events:
            if kind=='prepared':
                if active is not None:raise RuntimeError('original viewport replaced an owned output')
                active=window
            elif active!=window:raise RuntimeError('original viewport used a retired or unknown output')
            elif kind=='retired':active=None
        return {'result':'pass','original_window':True,'held_ticks':[int(p[0]) for p in displayed],
                'backbuffer_restored':True,'gpu_retired':True,
                'scope':'original-window ownership and completed copy receipts; scene coherence and displayed FPS require separate evidence'}
    prepared=re.findall(r"private replay output prepared hwnd=(\d+) bytes=(\d+) native_target_untouched=true",raw)
    if not prepared: return {"result":"not_exercised"}
    retired=re.findall(r"private replay output retired hwnd=(\d+) gpu_complete=true",raw)
    displayed=re.findall(r"private replay output displayed tick=(\d+) hwnd=(\d+) native_target_untouched=true",raw)
    if [row[0] for row in prepared]!=retired or not displayed:
        raise RuntimeError("private replay output lacks ordered window/thread/GPU retirement")
    if any(not 0<int(row[1])<=32*1024*1024 for row in prepared):
        raise RuntimeError("private output ownership exceeds its admitted budget")
    if "nested Present detected" in raw or "private replay output completion failed" in raw:
        raise RuntimeError("private output lost presentation or GPU completion")
    events=re.findall(r"private replay output (prepared|displayed|retired) (?:tick=\d+ )?hwnd=(\d+)",raw)
    active=None
    for kind,window in events:
        if kind=="prepared":
            if active is not None: raise RuntimeError("private output replaced a live window")
            active=window
        elif active!=window: raise RuntimeError("private output used a retired or unknown window")
        elif kind=="retired": active=None
    return {"result":"pass","windows":len(prepared),"held_ticks":[int(row[0]) for row in displayed],
            "native_target_untouched":True,"window_thread_and_gpu_retired":True,
            "scope":"ownership and submission evidence; desktop review and input tests are separate"}


def validate_cancel_control(raw: str, report: dict) -> dict:
    waiting=f"historical cancel control waiting run_id={report['run_id']} tick=220"
    dispatched="replay UI cancel dispatched tick=220 accepted=true phase=9"
    controls=re.findall(r"replay cancel control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)",raw)
    activations=re.findall(r"replay cancel activated request=(\d+) surface_frame=(\d+)",raw)
    sent=report.get("ui_cancel_input",{})
    if (raw.count(waiting)!=1 or raw.count(dispatched)!=1 or len(activations)!=1 or activations[0][0]!="1"
            or int(activations[0][1])<1 or not controls
            or sent.get("method")!="Windows SendInput" or not sent.get("button_released")
            or not sent.get("pid") or sent["pid"]!=report.get("loaded_observer",{}).get("pid")
            or tuple(sent.get(k) for k in ("hwnd","x","y")) not in [tuple(map(int,row)) for row in controls]
            or not raw.index(waiting)<raw.index("replay cancel activated")<raw.index(dispatched)):
        raise RuntimeError("seek cancellation lacks owned UI input and accepted host dispatch")
    return {"result":"pass","requests":1,"input":"Windows SendInput","tick":220,"dispatch":"host_ui"}


def validate_recovery_controls(raw: str, report: dict) -> dict:
    run=report["run_id"]
    original=report.get("historical_advanced_tick",220)
    if original not in (220,2504):raise RuntimeError("unsupported recovery control origin")
    for action in ("cancel","resume"):
        controls=re.findall(rf"replay {action} control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)",raw)
        activations=re.findall(rf"replay {action} activated request=(\d+) surface_frame=(\d+)",raw)
        sent=report.get(f"ui_{action}_input",{})
        if (len(activations)!=1 or activations[0][0]!="1" or int(activations[0][1])<1
                or sent.get("method")!="Windows SendInput" or not sent.get("button_released")
                or not sent.get("pid") or sent["pid"]!=report.get("loaded_observer",{}).get("pid")
                or tuple(sent.get(k) for k in ("hwnd","x","y")) not in [tuple(map(int,row)) for row in controls]):
            raise RuntimeError("recovery controls lack owned Windows input")
    markers=[f"historical cancel control waiting run_id={run} tick=214",
             "replay cancel activated", "replay UI cancel dispatched tick=214 accepted=true phase=9",
             f"historical cancellation recovered run_id={run} point=after tick={original}",
             f"historical resume control waiting run_id={run} tick={original} recovered_B=true",
             "replay resume activated", f"historical resume control verified run_id={run} tick={original} requests=1 recovered_B=true",
             f"replay UI resume dispatched tick={original} seek_released=true", f"seek checkpoint retired tick={original} application_idle=true"]
    if "seek_release_result=completed" in raw:markers[-2:]=reversed(markers[-2:])
    if any(raw.count(m)!=1 for m in markers) or [raw.index(m) for m in markers]!=sorted(raw.index(m) for m in markers):
        raise RuntimeError("UI cancellation/resume did not follow verified B recovery and host retirement")
    return {"result":"pass","input":"Windows SendInput","cancel_tick":214,"resume_tick":original,
            "dispatch":"host","observer":"read-only recovery pause monitor retired before dispatch"}


def validate_step_control(raw: str, report: dict) -> dict:
    waiting = f"historical step control waiting run_id={report['run_id']} tick=208 pending_event=true unchanged=true"
    verified = f"historical step control verified run_id={report['run_id']} tick=208 requests=1 pending_event=true unchanged=true"
    controls = re.findall(r"replay step control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)", raw)
    activations = re.findall(r"replay step activated request=(\d+) surface_frame=(\d+)", raw)
    sent = report.get("ui_step_input", {})
    if (raw.count(waiting)!=1 or raw.count(verified)!=1 or len(activations)!=1 or activations[0][0]!="1"
            or int(activations[0][1])<1 or not controls
            or sent.get("method")!="Windows SendInput" or not sent.get("button_released")
            or not sent.get("pid") or sent["pid"]!=report.get("loaded_observer",{}).get("pid")
            or tuple(sent.get(k) for k in ("hwnd","x","y")) not in [tuple(map(int,row)) for row in controls]
            or not raw.index(waiting)<raw.index("replay step activated")<raw.index(verified)<raw.index("historical single step requested")):
        raise RuntimeError("single step lacks owned UI input and independent pending-state activation")
    dispatch="diagnostic_api"
    if report.get("seek_ownership_protocol")=="retained_B_through_C_v1" and "target=209 dispatch=host_ui" in raw:
        host="host UI step accepted origin=208 target=209 B_retained=true pending_tails_preserved=true"
        if raw.count(host)!=1 or not raw.index("replay step activated")<raw.index(host)<raw.index(verified):
            raise RuntimeError("single step lacks retained-B host dispatch")
        dispatch="host_ui_retained_B"
    elif "target=209 dispatch=host_ui" in raw:
        host=re.findall(r"replay UI step dispatched origin=208 target=209 phase=(\d+) observer=false",raw)
        retired=f"host step monitor retired run_id={report['run_id']} tick=209"
        if host!=["6"] or raw.count(retired)!=1 or not (raw.index(verified)<raw.index("replay UI step dispatched")<raw.index(retired)):
            raise RuntimeError("single step lacks host dispatch and retired independent pause monitor")
        dispatch="host_ui"
    return {"result":"pass","requests":1,"input":"Windows SendInput","origin":208,"target":209,"dispatch":dispatch}


def validate_interior_control(raw: str, report: dict) -> dict:
    run_id = report["run_id"]
    tick = validate_interior_pause(raw, run_id)["tick"]
    controls = re.findall(r"replay resume control hwnd=(\d+) screen_x=(-?\d+) screen_y=(-?\d+)", raw)
    activations = re.findall(r"replay resume activated request=(\d+) surface_frame=(\d+)", raw)
    witnesses = re.findall(rf"interior control verified run_id=(\S+) native_frame={tick} requests=1 "
                          r"application_updates=(\d+) pending_event=true unchanged=true", raw)
    sent = report.get("ui_input", {})
    if (len(controls) != 1 or len(activations) != 1 or len(witnesses) != 1
            or witnesses[0][0] != run_id or int(witnesses[0][1]) < 30
            or activations[0][0] != "1" or int(activations[0][1]) < 1
            or sent.get("method") != "Windows SendInput" or not sent.get("button_released")
            or not sent.get("pid") or sent["pid"] != report.get("loaded_observer", {}).get("pid")
            or tuple(map(int, controls[0])) != tuple(sent.get(k) for k in ("hwnd", "x", "y"))
            or not raw.index("interior pause started") < raw.index("replay resume control")
                < raw.index("replay resume activated") < raw.index("interior control verified")
                < raw.index("interior pause held") < raw.index("interior pause resumed")):
        raise RuntimeError("interior control lacks owned input, activation, or pending-state evidence")
    return {"independent_activation_verified": True, "requests": 1,
            "scope": "one Windows mouse activation while the pending native task remains incomplete; "
                     "general window lifecycle and other input devices remain unproven"}


def validate_small_restore(raw: str, run_id: str) -> dict:
    starts = re.findall(r"small restore started run_id=(\S+) from_tick=360 to_tick=361", raw)
    ends = re.findall(r"small restore completed run_id=(\S+) from_tick=360 to_tick=361 trial_ticks=1 "
        r"trial_boundaries=2 native_equal=true hash_equal=true components_equal=true canonical_hash=([0-9a-f]{64}) elapsed_us=(\d+)", raw)
    trial = re.findall(r"restore_trial ordinal=(\d+) phase=(\w+) sample_version=[2345] frame=(\d+) ", raw)
    rejections = re.findall(r"small restore metadata rejected run_id=(\S+) field=(\w+) code=(\d+) "
                           r"unchanged=true pending_event=true", raw)
    native_crt = re.findall(r"small restore native CRT verified run_id=(\S+) draws=2 native_equal=true restored=true", raw)
    world = re.findall(r"small restore world captured run_id=(\S+) timers=(\d+) owned_bytes=(\d+) "
        r"independent_callbacks=true capacity_rejected=true retained=true", raw)
    rebuilt = re.findall(r"small restore world rebuilt run_id=(\S+) old_storage=(\d+) new_storage=(\d+) "
        r"restore_capacity_rejected=true", raw)
    cancelled = re.findall(r"small restore cancelled run_id=(\S+) phase=(\w+) original_tick=361 "
        r"target_tick=360 queries=(\d+) target_observed=(true|false) original_restored=true nested_rejected=true pending_event=true", raw)
    if (starts != [run_id] or len(ends) != 1 or ends[0][0] != run_id
            or native_crt != [run_id]
            or len(world) != 1 or world[0][0] != run_id or int(world[0][1]) <= 0 or int(world[0][2]) <= 0
            or len(rebuilt) != 1 or rebuilt[0][0] != run_id or min(map(int, rebuilt[0][1:])) <= 0
            or int(rebuilt[0][1]) == int(rebuilt[0][2])
            or cancelled != [(run_id, "before_writes", "1", "false"), (run_id, "before_commit", "2", "true")]
            or trial != [("1", "callback_a30", "361"), ("2", "round_sequence", "361")]
            or [(row[0], row[1]) for row in rejections] != [(run_id, field) for field in ("coordinate", "ucrt", "task")]
            or any(int(row[2]) == 0 for row in rejections)
            or "small restore failed" in raw or int(ends[0][2]) <= 0
            or not raw.index("small restore started") < raw.index("small restore world captured")
                < raw.index("small restore world rebuilt")
                < raw.index("small restore native CRT verified")
                < raw.index("small restore metadata rejected")
            or not raw.index("small restore started") < raw.index("small restore metadata rejected")
                <= raw.rindex("small restore metadata rejected") < raw.index("restore_trial ordinal=1")
            or not raw.index("small restore started") < raw.index("restore_trial ordinal=1")
                < raw.index("restore_trial ordinal=2") < raw.index("small restore cancelled")
                <= raw.rindex("small restore cancelled") < raw.index("small restore completed")):
        raise RuntimeError("small restore lacks independently reproduced native suffix and component evidence")
    return {"from_tick": 360, "to_tick": 361, "trial_ticks": 1, "trial_boundaries": 2,
            "canonical_hash": ends[0][1], "elapsed_us": int(ends[0][2]),
            "prewrite_rejections": [row[1] for row in rejections],
            "native_crt_draw_reproduction": True,
            "world_capture": {"timers": int(world[0][1]), "owned_bytes": int(world[0][2]),
                "independent_callbacks": True, "capacity_rejection_retains_snapshot": True,
                "native_storage_reconstructed": True, "restore_capacity_rejection_preserves_storage": True},
            "cancellation_recovery": [row[1] for row in cancelled],
            "scope": "same live pending task, one captured repeat; full restoration gate and expired-task reconstruction remain unproven"}


def require_interior_equivalence(args) -> None:
    """Recheck raw evidence, not just a previously written pass flag."""
    summary = json.loads((OUTPUT / "equivalence-stage.json").read_text(encoding="utf-8"))
    executor_path = OUTPUT / "equivalence-executor.json"
    report = json.loads(executor_path.read_text(encoding="utf-8"))
    expected = current_identities(args)
    if (summary.get("result") != "pass" or not report.get("probe_interior_pause")
            or report.get("probe_small_restore")
            or any(report.get("identities", {}).get(key) != value for key, value in expected.items())):
        raise RuntimeError("restore requires passing external interior equivalence from these exact binaries")
    comparison = compare_passive_controls(Path(summary["native_control_report"]), executor_path)
    if comparison["result"] != "pass":
        raise RuntimeError("the prerequisite interior continuation no longer matches native execution")
    executor_proof(report, True)
    raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
    validate_interior_pause(raw, report["run_id"])
    if report.get("probe_interactive_controls"):
        validate_interior_control(raw, report)
    if interior_resume_timing(raw, report["run_id"])["tick_rate_milli"] < 58000:
        raise RuntimeError("prerequisite interior continuation is below 58 native ticks/s")


def interior_resume_timing(raw: str, run_id: str, *, held_tick: int | None = None, marker: str = "interior pause held") -> dict:
    if held_tick is None:
        held_tick = validate_interior_pause(raw, run_id)["tick"]
    starts = re.findall(r"\[([0-9-]+ [0-9:.]+)\] \[ReplayQualification\] " + re.escape(marker) + r" run_id="
        + re.escape(run_id) + rf" native_frame={held_tick} ", raw)
    if len(starts) != 1:
        raise RuntimeError("interior resume timing lacks a unique release request")
    start = previous = datetime.fromisoformat(starts[0])
    gaps = []
    for stamp, frame_text in re.findall(r"\[([0-9-]+ [0-9:.]+)\] \[ReplayQualification\] "
            r"(?:setup|trajectory) ordinal=\d+ phase=engine_post sample_version=[2345] frame=(\d+) ", raw):
        frame = int(frame_text)
        if frame <= held_tick:
            continue
        now = datetime.fromisoformat(stamp)
        gap = round((now - previous).total_seconds() * 1_000_000)
        if gap < 0 or frame > held_tick + 120:
            raise RuntimeError("interior resume timing has nonmonotonic or missing endpoint evidence")
        gaps.append(gap)
        previous = now
        if frame == held_tick + 120:
            elapsed = round((now - start).total_seconds() * 1_000_000)
            if elapsed <= 0:
                break
            return {"native_ticks": 120, "elapsed_us": elapsed,
                    "tick_rate_milli": 120_000_000_000 // elapsed,
                    "max_observation_gap_us": max(gaps),
                    "gaps_over_20ms": sum(gap > 20_000 for gap in gaps),
                    "scope": "release request through 120 native ticks, including first resumed tick delay; not presentation FPS"}
    raise RuntimeError("interior resume timing lacks 120 subsequent native ticks")


def execution_gate_coverage(summary: dict) -> dict:
    """Coverage of this identity-bound experiment, not an aggregate campaign.

    External suspension needs validated native hold witnesses followed by
    matching independent continuation. A completed-world pause is insufficient.
    """
    executor = summary.get("executor", {})
    native = summary.get("native_execution_coverage", {})
    matched = summary.get("result") == "pass" and summary.get("native_boundaries_compared", 0) > 0
    yielded = matched and executor.get("yield_every_tick", False)
    demonstrated = {
        "zero_tick_interval": matched and native.get("zero_tick_intervals", 0) > 0
            and executor.get("zero_tick_intervals", 0) > 0,
        "multiple_ticks_per_interval": yielded and native.get("multi_tick_intervals", 0) > 0
            and executor.get("multi_tick_intervals", 0) > 0,
        "repeated_ticks_sharing_input": yielded and bool(native.get("publication_observer_present"))
            and native.get("repeated_ticks_without_new_publication", 0) > 0 and executor.get("repeat_requests", 0) > 0,
        "move_state_3_traversal": yielded and native.get("move_state_3_ticks", 0) > 0
            and executor.get("move_state_ticks", 0) > 0,
        "round_transitions_and_final_tail": matched and bool(summary.get("completion", {}).get("full_match")),
        "external_interior_pause_and_continuation": matched
            and summary.get("interior_pause", {}).get("independent_hold_verified", False),
        "active_combat_interior_pause_and_continuation": matched
            and summary.get("interior_pause", {}).get("independent_hold_verified", False)
            and summary.get("interior_pause", {}).get("active_combat_verified", False),
        # Supplied only by revalidating the owned Windows input and ordered
        # widget/hold witnesses; frame counts and report checkboxes do not count.
        "interactive_controls_during_interior_pause": matched
            and summary.get("interior_pause", {}).get("independent_hold_verified", False)
            and summary.get("interactive_control", {}).get("independent_activation_verified", False),
    }
    missing = [name for name, passed in demonstrated.items() if not passed]
    return {"result": "incomplete" if missing else "pass", "complete": not missing,
            "demonstrated": demonstrated, "missing": missing,
            "scope": "this experiment and its exact binary identities; earlier builds do not fill coverage",
            "external_pause_requirement": "pending simulation/interval work held across application updates, "
                "independent unchanged-state and incomplete-event witnesses, responsive UI, "
                "then native-matching continuation with exactly-once tails"}


def verify_executor_native_counts(summary: dict) -> None:
    executor = summary["executor"]
    if summary.get("native_boundary_counts", {}).get("actor_tail") != executor.get("physical_intervals",executor["intervals"]):
        raise RuntimeError("executor intervals disagree with independently observed native actor tails")
    native = summary["native_execution_coverage"]
    for native_key, executor_key in (
            ("tick_callbacks", "ticks"), ("zero_tick_intervals", "zero_tick_intervals"),
            ("multi_tick_intervals", "multi_tick_intervals"), ("move_state_3_ticks", "move_state_ticks"),
            ("input_cache_publications", "publications"), ("repeated_ticks_without_new_publication", "repeat_requests")):
        trial = executor["restore_trial_ticks"] if executor_key == "repeat_requests" else 0
        expected=executor.get("physical_"+executor_key,executor[executor_key])
        if native[native_key] is not None and native[native_key] + trial != expected:
            raise RuntimeError(f"executor {executor_key} disagrees with independent native boundary evidence")


def collect_execution_gate(args) -> dict:
    """Revalidate the bounded retained experiments before combining coverage."""
    require_bootstrap(args)
    require_native_baseline(args)
    expected = current_identities(args)
    gate = execution_gate_coverage({})
    gate["scope"] = "revalidated retained experiments with identical binaries, observer, configuration and replay"
    gate["evidence"] = []
    gate["excluded"] = []
    config_identity = json.loads((OUTPUT / "baseline-a.json").read_text(encoding="utf-8"))["identities"]["config"]
    for name in ("equivalence", "equivalence-empty", "equivalence-full", "equivalence-move"):
        stage_path = OUTPUT / f"{name}-stage.json"
        report_path = OUTPUT / f"{name}-executor.json"
        if not stage_path.is_file() or not report_path.is_file():
            gate["excluded"].append({"experiment": name, "reason": "missing report"})
            continue
        stage = json.loads(stage_path.read_text(encoding="utf-8"))
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if (stage.get("result") != "pass"
                or any(report.get("identities", {}).get(k) != v for k, v in expected.items())):
            gate["excluded"].append({"experiment": name, "reason": "failed or stale identities"})
            continue
        native_path = Path(stage["native_control_report"])
        comparison = compare_passive_controls(native_path, report_path)
        if comparison["result"] != "pass":
            raise RuntimeError(f"{name}: retained continuation no longer matches native execution")
        config = comparison["identities"]["config"]
        if config_identity is not None and config_identity != config:
            raise RuntimeError("execution experiments used different configurations")
        config_identity = config
        comparison["executor"] = executor_proof(report, True)
        verify_executor_native_counts(comparison)
        if report.get("probe_interior_pause"):
            raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
            comparison["interior_pause"] = validate_interior_pause(raw, report["run_id"])
            if report.get("probe_interactive_controls"):
                comparison["interactive_control"] = validate_interior_control(raw, report)
            if interior_resume_timing(raw, report["run_id"])["tick_rate_milli"] < 58000:
                raise RuntimeError(f"{name}: interior continuation is below 58 ticks/s")
        demonstrated = execution_gate_coverage(comparison)["demonstrated"]
        for category, covered in demonstrated.items():
            gate["demonstrated"][category] |= covered
        gate["evidence"].append({"experiment": name, "run_id": report["run_id"],
            "native_report_sha256": sha256_file(native_path), "executor_report_sha256": sha256_file(report_path),
            "raw_log_sha256": report["raw_log"]["sha256"], "demonstrated": demonstrated})
    gate["identities"] = dict(expected, config=config_identity)
    gate["missing"] = [name for name, covered in gate["demonstrated"].items() if not covered]
    gate["complete"] = not gate["missing"]
    gate["result"] = "pass" if gate["complete"] else "incomplete"
    return gate


def live_preflight(options):
    from deterministic_qualification.replay_preflight import inspect_preflight, inspect_steam
    deployed = GAME_ROOT / "ue4ss/Mods/HorseMod/dlls/main.dll"
    result=inspect_preflight(deployed, [GAME_ROOT / "SoulcaliburVI.exe", options.replay,
        deployed.with_name("rollback.ini")], space_paths=[BUILD], minimum_free_bytes=2 * 1024**3, steam=inspect_steam,
        loader_executable=GAME_ROOT / "SoulcaliburVI.exe")
    required=(ACTIVE_PROFILE or {}).get('definition',{}).get('required_gates',[])
    if required:
        from deterministic_qualification.replay_readiness import readiness,current_context
        from deterministic_qualification.replay_run import inspect_deployment
        ready=readiness(OUTPUT/'evidence',current_context(ROOT,OUTPUT,replay=options.replay),inspect_deployment(deployed))
        for gate in required:
            passed=ready['readiness'][gate]['result']=='pass'
            result['checks'].append(dict(check='profile prerequisite '+gate,result='pass' if passed else 'blocked',detail='explicit compatible gate evidence required'))
            if not passed:result['result']='blocked'
    return result


@reported_cli(output_root=lambda: OUTPUT)
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("stage", choices=("inspect", "status", "preflight", "local", "evidence", "certify-g0", "build", "bootstrap", "baseline", "consumer-census", "c18-diagnostic", "equivalence", "execution-gate", "index-proof", "index-cancel", "session-exit", "restore", "interior-probe", "restore-probe", "move-probe", "application-probe", "gpu-copy", "combat-restore", "seek-recovery-contract"))
    parser.add_argument("--output", choices=("compact", "json", "full"), default="compact", help="console format only; complete evidence is always retained")
    parser.add_argument("--observer-overhead",type=Path,nargs=2,help="compare immutable core and optional-witness manifests")
    parser.add_argument("--generate-distributed",type=Path,help="verified independent native report with sample consumption receipts")
    parser.add_argument("--distributed-cycles",type=int,choices=(30,600),default=30)
    parser.add_argument("--seed-source",type=Path,help="verify and seed one retained source receipt into the shared payload store")
    parser.add_argument("--export-source", type=Path, help="evidence only: source receipt JSON to export")
    parser.add_argument("--destination", type=Path, help="evidence only: portable source ZIP destination")
    parser.add_argument("--rebuild-index", action="store_true", help="evidence only: rebuild readiness index offline")
    parser.add_argument("--report", type=Path, help="status/inspect only: read this report without launching or recovering")
    parser.add_argument("--participant", help="inspect only: exact participant filter")
    parser.add_argument("--tick", type=int, help="inspect only: recorded tick filter")
    parser.add_argument("--offset", type=int, default=0, help="inspect only: continuation byte offset")
    parser.add_argument("--since", type=Path, help="status only: previous JSON status snapshot")
    parser.add_argument("--replay", type=Path, default=DEFAULT_REPLAY)
    parser.add_argument("--samples", type=int, default=1200,
                        help="bounded observations (120..36000; default: bootstrap 360, otherwise 1200)")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--full-match", action="store_true",
                        help="baseline/equivalence: capture entire matches, bounded to 36000 observations")
    parser.add_argument("--seek-observer-failure", action="store_true", help="combat-restore after: fail the published seek observer, then prove Failed-state B recovery")
    parser.add_argument("--seek-preparation-failure", action="store_true", help="220-to-208 repeated seek: cancel the known prepublication lifetime failure and compare unchanged B continuation")
    parser.add_argument("--index-preparation-fallback", action="store_true", help="cancel one preparation retry, then require earlier fallback and independent continuation on a fresh seek")
    parser.add_argument("--index-recovery",choices=("published","drained","interior"),default="",help="recover retained B after publication, GPU drain, or queued interior replacement; no authored ticks remain at B")
    parser.add_argument("--record-index", action="store_true", help="equivalence: record and verify the full native tick index")
    parser.add_argument("--host-session", action="store_true", help="production native entry and checkpoint placement for session-exit or full indexed seeking; observer supplies no enable/index/arm requests")
    parser.add_argument("--index-seek", action="store_true", help="assembled full-index to retained A170 to exact208 and120 independent continuation ticks")
    parser.add_argument("--index-extra-checkpoint", type=int, default=0, help="retain one additional indexed combat checkpoint alongside170; qualify selection/fallback and memory")
    parser.add_argument("--index-sequence",choices=("early","late"),default="",help="bounded multiple-target retained-session qualification; one independent control covers both sequences")
    parser.add_argument("--index-seek-target",type=int,default=208)
    parser.add_argument("--index-seek-continuation",type=int,default=120)
    parser.add_argument("--index-checkpoint", action="store_true", help="index integration: retain the verified combat170 checkpoint through the full map and drain its owners on release")
    parser.add_argument("--yield-every-tick", action="store_true",
                        help="equivalence: return and resume at every executor tick boundary")
    parser.add_argument("--include-setup", action="store_true",
                        help="baseline/equivalence: observe verified replay setup before tracker activation")
    parser.add_argument("--empty-interval", action="store_true",
                        help="equivalence: invoke one native scheduling interval without new input")
    parser.add_argument("--move-state", action="store_true",
                        help="equivalence: compare one labelled native-setter move-state-3 intervention")
    parser.add_argument("--world-pause", action="store_true",
                        help="equivalence: hold a completed world boundary for one second, then compare continuation")
    parser.add_argument("--interior-pause", action="store_true",
                        help="equivalence: hold a tick selected from active combat with pending native work across application returns")
    parser.add_argument("--application-pause", action="store_true",
                        help="equivalence: compare two completed-application holds and transactional scheduler backing replacement")
    parser.add_argument("--consumer-task", action="store_true", help="equivalence application-pause: bounded untouched LuxTrace task suspension diagnostic")
    parser.add_argument("--consumer-mutation", action="store_true", help="A210/B217 native prerequisite enforcement and complete-B recovery diagnostic")
    parser.add_argument("--interactive-controls", action="store_true",
                        help="interior-probe/equivalence: activate held Resume through owned Windows input")
    parser.add_argument("--resume-baseline", action="store_true",
                        help="baseline: retain the verified first capture and run the second after an automation fix")
    parser.add_argument("--resume-native-control", action="store_true",
                        help="equivalence: reuse the verified uninterrupted native control after an automation fix")
    parser.add_argument("--resume-candidate", action="store_true",
                        help="revalidate retained completed advance-failure capture after a parser-only fix")
    parser.add_argument("--seek-publication-cancel", action="store_true", help="cancel A170 after execution handoff and independently recover active B2504")
    parser.add_argument("--seek-drained-cancel", action="store_true", help="Cancel after GPU drain before render settlement")
    parser.add_argument("--seek-settlement-failure", action="store_true",
                        help="extend seek-advance-failure to fail after all C CPU owners settle, before commit")
    parser.add_argument("--seek-advance-failure", action="store_true",
                        help="bounded A170/B220 or B2504 seek: fail after external C208 and verify B recovery")
    parser.add_argument("--combat-case", choices=("all", "before", "after", "commit"), default="all",
                        help="combat-restore: isolate a failed assembled transaction; an individual case cannot complete the milestone")
    parser.add_argument("--checkpoint-fallback", action="store_true", help="retain170 and205; at220 automatically skip expired205 and restore170")
    parser.add_argument("--checkpoint-pair", action="store_true", help="retain205 and206, seek211 through206 to210, then through205 to208")
    parser.add_argument("--authored-prefix",default="",help="frozen corrected prefix for the single A617/C624 diagnostic")
    parser.add_argument("--rolling-corrections",default="",help="bounded arrival:revision:round:sample:players:raw0:raw1 rows separated by semicolons")
    parser.add_argument("--rolling-cycles",type=int,choices=(0,1,2,7,30,48,54,174,252,407,408,600),default=0,help="bounded every-tick depth-seven campaign from210;2 isolates overlapping corrections,48 isolates cycle42 RNG,54 isolates display-copy capacity,174 captures and restores GPU SpawnPerUnit T383,407 reaches C623;408 restores active ground boundary A617 at C624")
    parser.add_argument("--host-seek-repeat", action="store_true", help="seek209 then208 without releasing the retained checkpoint")
    parser.add_argument("--host-seek-target", type=int, default=208)
    parser.add_argument("--host-seek-first-target", type=int, choices=range(205,221), default=0)
    parser.add_argument("--host-seek", action="store_true", help="exercise the host-owned seek request to208 from retained205")
    parser.add_argument("--restore-reuse", action="store_true", help="cancel one restore, reuse the retained checkpoint after GPU retirement, then commit")
    parser.add_argument("--capture-cancel", action="store_true", help="cancel host capture before submission and after readiness, then complete the assembled restore")
    parser.add_argument("--particle-owner-copy", action="store_true", help="bounded cold-component constructor/retirement at early A205 or GPU regression A6415; not rollback proof")
    parser.add_argument("--particle-owner-registration", action="store_true", help="extend the cold-owner check through native inactive registration/retirement; no emitter or GPU publication")
    parser.add_argument("--particle-owner-gpu", action="store_true", help="construct an independent unused GPU owner and verify ordered retirement completion")
    parser.add_argument("--source-revision", action="store_true", help="compare core source revision with independently changed authored native samples")
    parser.add_argument("--completion-repeat", action="store_true", help="bounded native repeat at208: prove paused completion defers before explicit step209")
    parser.add_argument("--combat-single-step", action="store_true", help="after external208 hold, single-step and externally hold209 before continuation")
    parser.add_argument("--combat-exact-advance", action="store_true", help="commit A205 then externally hold exact interior208 before120 resumed ticks")
    parser.add_argument("--complete-seek-application", action="store_true", help="measure restore plus unpaced seek through an exact completed application before validation dwell")
    parser.add_argument("--combat-advanced-tick", type=int, default=210,
                        help="bounded combat B:209 admits unchanged particle membership;210 retains the original birth experiment")
    parser.add_argument("--combat-anchor-tick", type=int, default=205,
                        help="170: completed combat anchor before hit birth177, B220, exact host seek through native births")
    parser.add_argument("--corrected-inputs", action="store_true", help="commit only: original B uses authored inputs; restored A changes samples161-190, including the previously traversed suffix")
    parser.add_argument("--changed-inputs", action="store_true", help="bounded combat continuation with scripted P1 raw cache samples166-195; native and restored runs use identical input policy")
    parser.add_argument("--pixel-diagnostics", action="store_true", help="optional native full-image comparison; does not gate simulation correctness")
    parser.add_argument("--coherence-images", action="store_true", help="bounded visible-client images for actor/effect/HUD review; timings include desktop capture overhead")
    parser.add_argument("--same-process-render", action="store_true",
                        help="combat-restore --combat-case commit: causal original/restored render diagnostic; no independent-control or milestone claim")
    parser.add_argument("--candidate-only", action="store_true",
                        help="bounded committed host transaction diagnostic; skips independent control and never qualifies continuation")
    parser.add_argument("--replay-budget-gib", type=int, choices=(1,2), default=1, help="1 GiB production; 2 GiB bounded host-session diagnostic only")
    parser.add_argument("--ground-motion-perturb", action="store_true", help="diagnostic: change admitted chunk poses after rolling407, then compare120 native continuation ticks")
    parser.add_argument("-j", "--jobs", type=int, default=4)
    parser.add_argument("--diagnostic-stack-depth", type=int, choices=(0,8,16), default=0)
    parser.add_argument("--diagnostic-mask", type=int, choices=range(16), default=0, help="audio=1, RNG=2, particles=4, presentation=8")
    parser.add_argument("--diagnostic-first-tick", type=int, default=0)
    parser.add_argument("--diagnostic-last-tick", type=int, default=36000)
    parser.add_argument("--diagnostic-byte-limit", type=int, default=1048576)
    parser.add_argument("--profile", help="checked-in experiment name or versioned JSON path")
    parser.add_argument("--flush-startup-loading",action="store_true",help="native startup loading drain; identical in candidate and stock, profile-owned when selected")
    parser.add_argument("--no-async-loading-thread",action="store_true",help="native loading diagnostic; identical in candidate and stock, profile-owned when selected")
    from deterministic_qualification.replay_local import MAP, LAYERS
    parser.add_argument("--group", choices=("auto", "all", *json.loads(MAP.read_bytes())['groups']), default="auto")
    parser.add_argument("--test", action="append", default=[], help="local only: exact test file or pytest node; repeat to combine")
    parser.add_argument("--layer", choices=LAYERS, default="all", help="local only: restrict selected tests by layer")
    parser.add_argument("--changed-path", action="append", default=[])
    parser.add_argument("--list-tests", action="store_true")
    parser.add_argument("--evidence-report", type=Path, action="append", default=[])
    from deterministic_qualification.replay_profiles import parse_profile_args
    options, profile = parse_profile_args(parser, sys.argv[1:])
    # Bootstrap keeps its shorter default; explicit --samples also accepts =N.
    if options.stage == 'bootstrap' and not any(token.split('=', 1)[0] == '--samples' for token in sys.argv[1:]):
        options.samples = 360
    if options.stage == 'certify-g0':
        if profile or any(getattr(options, action.dest, action.default) != action.default
                          for action in parser._actions if action.dest not in {'stage', 'output'}):
            parser.error('certify-g0 accepts only --output; it verifies existing evidence without running tests or deploying')
    if options.stage == 'c18-diagnostic':
        allowed={'stage','replay','timeout','jobs','output','no_async_loading_thread','flush_startup_loading',
                 'diagnostic_mask','diagnostic_stack_depth','diagnostic_first_tick','diagnostic_last_tick','diagnostic_byte_limit'}
        if profile or any(getattr(options, action.dest, action.default) != action.default
                          for action in parser._actions if action.dest not in allowed):
            parser.error('c18-diagnostic accepts only replay, timeout, jobs, output, startup loading and bounded diagnostic options')
    if options.offset < 0:
        parser.error("--offset must be nonnegative")
    if options.stage != "inspect" and (options.participant is not None or options.tick is not None or options.offset):
        parser.error("--participant, --tick and --offset are only valid for inspect")
    if options.since is not None and options.stage != "status":
        parser.error("--since is only valid for status")
    if (options.test or options.layer != 'all') and options.stage != 'local':
        parser.error('--test and --layer require local')
    if options.test and (options.group != 'auto' or options.changed_path):
        parser.error('--test cannot be combined with --group or --changed-path')
    if options.stage == "inspect":
        publish({"stage": "inspect", "result": "unknown"}, readonly=True)
        if options.report is None: parser.error("inspect requires --report")
        summary = inspect_report(options.report, participant=options.participant, tick=options.tick, offset=options.offset)
        publish(summary, report_path=options.report, readonly=True)
        print(render(summary, mode=options.output, report_path=options.report), end="")
        return 0 if summary['result'] == 'pass' else 1
    if options.stage == "status":
        publish({"stage": "status", "result": "unknown"}, readonly=True)
        from deterministic_qualification.replay_run import inspect_deployment
        deployed = GAME_ROOT / "ue4ss/Mods/HorseMod/dlls/main.dll"
        deployment = inspect_deployment(deployed)
        path = options.report
        if path is None:
            from deterministic_qualification.replay_readiness import readiness, current_context
            summary = readiness(OUTPUT / 'evidence', current_context(ROOT, OUTPUT,replay=options.replay), deployment)
        else:
            summary = read_status(path)
        if options.since is not None:
            from deterministic_qualification.replay_reporting import read_json
            summary = status_difference(compact_document(summary, receipt=summary.get("retained_evidence")), read_json(options.since))
        publish(summary, summary.get("retained_evidence"), path, readonly=True)
        print(render(summary, mode=options.output, report_path=path,
                     receipt=summary.get("retained_evidence")), end="")
        return 0 if path is not None or summary.get("latest") else 1
    if (options.export_source or options.seed_source or options.generate_distributed or options.observer_overhead or options.destination) and options.stage != "evidence": parser.error("source export requires evidence")
    if options.rebuild_index and options.stage != "evidence": parser.error("--rebuild-index requires evidence")
    if options.report is not None:
        parser.error("--report is only valid for status/inspect")
    global ACTIVE_PROFILE, ACTIVE_CAPTURE_SOURCES, ACTIVE_DIAGNOSTICS, ACTIVE_STARTUP_LOADING
    ACTIVE_PROFILE = profile
    ACTIVE_STARTUP_LOADING = {name:bool(getattr(options,name)) for name in ('no_async_loading_thread','flush_startup_loading')}
    ACTIVE_CAPTURE_SOURCES = None
    if not (0 <= options.diagnostic_first_tick <= options.diagnostic_last_tick <= 36000
            and 4096 <= options.diagnostic_byte_limit <= 16 * 1024 * 1024):
        parser.error("diagnostics require ordered ticks 0..36000 and byte limit 4096..16777216")
    ACTIVE_DIAGNOSTICS = dict(stack_depth=options.diagnostic_stack_depth, mask=options.diagnostic_mask,
        first_tick=options.diagnostic_first_tick, last_tick=options.diagnostic_last_tick,
        byte_limit=options.diagnostic_byte_limit)
    CAPTURE_REPORTS.clear()
    if options.stage in ("preflight", "local", "evidence", "certify-g0"):
        from deterministic_qualification.replay_evidence import retain_run
        if options.stage == "certify-g0":
            from deterministic_qualification.replay_baseline import certify_g0
            summary = certify_g0(ROOT, OUTPUT, build_binary_paths(), GAME_ROOT / "ue4ss/Mods/HorseMod/dlls/main.dll")
        elif options.stage == "preflight":
            summary = live_preflight(options)
        elif options.stage == "local":
            from deterministic_qualification.replay_local import run_local
            summary = run_local(ROOT, OUTPUT, options.group, options.changed_path, options.list_tests,
                                test_ids=options.test, layer=options.layer)
        elif options.observer_overhead:
            from deterministic_qualification.replay_overhead import compare
            try:summary=compare(*options.observer_overhead)
            except (OSError,ValueError,KeyError) as error:summary=dict(result='blocked',failure=str(error))
        elif options.generate_distributed:
            if not options.destination:parser.error('--generate-distributed requires --destination profile.json')
            from deterministic_qualification.replay_schedule import generate
            try:summary=dict(result='pass',distributed_schedule=generate(options.generate_distributed,options.distributed_cycles,options.destination,OUTPUT/'evidence/objects'))
            except (OSError,ValueError,KeyError) as error:summary=dict(result='blocked',failure=str(error),scope='schedule was not generated or launched')
        elif options.seed_source:
            from deterministic_qualification.source_retention import seed_sources
            source_document=json.loads(options.seed_source.read_bytes())
            summary=dict(result='pass',source_seed=seed_sources(source_document.get('source_retention',source_document),OUTPUT/'source-archives'))
        elif options.export_source:
            from deterministic_qualification.source_retention import export_sources
            if not options.destination: parser.error('--export-source requires --destination')
            source_document = json.loads(options.export_source.read_bytes())
            source_receipt = source_document.get('source_retention', source_document)
            summary = dict(result='pass', source_export=export_sources(source_receipt, options.destination))
        elif options.rebuild_index:
            from deterministic_qualification.replay_readiness import rebuild_index
            summary = rebuild_index(OUTPUT / 'evidence')
        else:
            if not options.evidence_report:
                parser.error("evidence requires --evidence-report")
            summary = json.loads(options.evidence_report[0].read_bytes())
        summary.update(stage=options.stage, profile=profile)
        receipt = retain_run(OUTPUT / "evidence", summary, options.evidence_report)
        publish(summary, receipt)
        print(json.dumps(dict(summary=summary, evidence=receipt), indent=2, default=str))
        return 0 if summary["result"] in ("ready", "pass", "selected") else 1
    if options.particle_owner_registration and not options.particle_owner_copy:
        parser.error("particle owner registration requires particle owner copy")
    if options.particle_owner_gpu and (not options.particle_owner_registration or options.combat_anchor_tick!=205):
        parser.error("particle GPU owner check requires inactive registration at205")
    if options.particle_owner_copy and (options.stage!="combat-restore" or options.combat_case!="before"
            or not options.host_seek or not (
                (options.combat_anchor_tick==6415 and options.combat_advanced_tick==6416
                 and options.host_seek_target==6415 and options.combat_exact_advance)
                or (options.combat_anchor_tick==205 and options.combat_advanced_tick==210
                    and options.host_seek_target==208 and not options.combat_exact_advance))
            or options.changed_inputs or options.corrected_inputs or options.capture_cancel or options.restore_reuse
            or options.host_seek_repeat or options.checkpoint_pair or options.pixel_diagnostics
            or options.seek_advance_failure or options.seek_observer_failure or options.seek_preparation_failure
            or options.completion_repeat):
        parser.error("particle owner copy requires unchanged early A205/B210 host target208 or exact A6415/B6416 target6415, before-publication cancellation")
    if options.authored_prefix:
        from deterministic_qualification.replay_control import authored_prefix_request
        try: authored_prefix_request(options.authored_prefix)
        except ValueError as error: parser.error(str(error))
        if (options.stage != 'combat-restore' or options.combat_case != 'commit' or not options.host_seek
            or not options.combat_exact_advance or (options.combat_anchor_tick,options.combat_advanced_tick,options.host_seek_target)!=(617,624,624)
            or options.rolling_cycles or options.rolling_corrections or options.changed_inputs or options.corrected_inputs
            or options.source_revision or options.consumer_mutation or options.probe_consumer_task):
            parser.error('--authored-prefix requires the exclusive single A617/C624 committed transaction')
    if options.rolling_corrections:
        try:rolling_revision_assignments(options.rolling_corrections,options.rolling_cycles,options.combat_anchor_tick)
        except ValueError as error:parser.error(str(error))
    elif options.rolling_cycles in (1,2,7):parser.error("one/two/seven-cycle rolling requires an explicit correction schedule")
    if options.rolling_cycles and (options.stage!="combat-restore" or options.combat_case!="commit"
            or not options.host_seek or not options.combat_exact_advance
            or not ((options.combat_anchor_tick,options.combat_advanced_tick,options.host_seek_target)==(210,217,217)
                or (options.rolling_cycles==30 and (options.combat_anchor_tick,options.combat_advanced_tick,options.host_seek_target)==(332,339,339)))
            or options.host_seek_repeat or options.changed_inputs or options.corrected_inputs
            or (options.coherence_images and options.rolling_cycles!=30) or options.complete_seek_application or options.capture_cancel):
        parser.error("rolling requires unchanged exact210/217, or bounded30-cycle332/339, committed host execution")
    if options.ground_motion_perturb and (options.stage!="combat-restore" or options.combat_case!="commit" or options.rolling_cycles!=407 or options.candidate_only):
        parser.error("ground motion diagnostic requires independently compared rolling407 commit")
    if options.replay_budget_gib==2 and not options.rolling_cycles and (options.stage!="equivalence" or not options.host_session or not options.index_seek):
        parser.error("2 GiB requires bounded indexed host-session equivalence")
    if options.index_sequence:
        if (options.stage!="equivalence" or not options.host_session or not options.full_match or not options.index_seek
                or options.index_recovery or options.index_preparation_fallback or options.interactive_controls
                or options.index_seek_target!=208 or options.index_seek_continuation!=120
                or options.corrected_inputs or options.changed_inputs or options.pixel_diagnostics or options.coherence_images):
            parser.error("index sequence requires unchanged full host indexing and default target/window without other probes")
        options.index_seek_target=208 if options.index_sequence=="early" else 5574

    checkpoint_window=bounded_checkpoint_window(dict(vars(options),
        historical_anchor_tick=options.combat_anchor_tick,historical_advanced_tick=options.combat_advanced_tick,
        historical_exact_advance=options.combat_exact_advance,historical_single_step=options.combat_single_step,
        historical_cancel="" if options.combat_case=="commit" else options.combat_case))
    if options.combat_anchor_tick not in (0,170,205):
        if options.stage!="combat-restore" or not checkpoint_window:
            parser.error("a new checkpoint window requires one unchanged exact host transaction: A210/B217 or 221<=A<B<=35000, B-A<=120, A<=target<=B; commit or publication cancellation")
    elif (options.host_seek_target not in (*range(205,221),300,2510,5750,11000)
            or options.combat_advanced_tick not in (209,210,211,220,300,329,2504,5695,6538)):
        parser.error("unsupported legacy combat coordinates")

    if options.combat_anchor_tick==0 and (options.stage!="combat-restore" or not options.candidate_only
            or not options.host_seek or not options.combat_exact_advance or options.combat_case!="commit"
            or options.combat_advanced_tick!=210 or options.host_seek_target!=208
            or options.host_seek_repeat or options.checkpoint_pair or options.corrected_inputs or options.changed_inputs
            or options.source_revision or options.capture_cancel or options.restore_reuse or options.pixel_diagnostics
            or options.coherence_images or options.seek_advance_failure or options.seek_settlement_failure):
        parser.error("baseline0 currently requires the bounded candidate-only0-to210-to208 transaction")
    if options.candidate_only and (options.stage != "combat-restore" or not options.host_seek
            or options.combat_case != "commit" or options.resume_native_control or options.same_process_render):
        parser.error("--candidate-only requires committed host combat restore without native-control reuse or pixel campaign")
    if options.complete_seek_application and (options.stage!="combat-restore" or options.combat_case!="commit"
            or not options.host_seek or not options.combat_exact_advance or options.host_seek_repeat
            or options.combat_single_step or options.pixel_diagnostics or options.coherence_images
            or options.host_seek_target<=options.combat_anchor_tick):
        parser.error("completed-application cost requires a committed host seek above its anchor without step/repeat/images")
    execution_fallback=(options.stage=="combat-restore" and options.checkpoint_fallback and options.host_seek
        and options.combat_anchor_tick==170 and options.combat_advanced_tick==210 and options.host_seek_target==220
        and options.combat_case=="commit" and options.combat_exact_advance and not options.host_seek_repeat
        and not options.corrected_inputs and not options.changed_inputs and not options.seek_advance_failure
        and not options.seek_settlement_failure)
    if options.checkpoint_fallback and not execution_fallback and (options.stage!="combat-restore" or options.combat_anchor_tick!=170
            or options.checkpoint_pair or options.corrected_inputs or options.changed_inputs
            or not ((options.host_seek_repeat and options.host_seek_first_target==208 and options.host_seek_target==214 and options.combat_case=="commit")
                or (not options.host_seek_repeat and not options.host_seek_first_target and options.host_seek_target==208 and options.combat_case in ("before","after")))):
        parser.error("checkpoint fallback requires repeated208/214 commit or single208 before/after cancellation from170/220")
    if options.coherence_images and (options.stage!="combat-restore" or options.combat_case!="commit" or not options.host_seek):
        parser.error("coherence images require a committed host seek")
    if options.combat_anchor_tick==170:
        if (options.stage!="combat-restore" or not options.host_seek
                or (options.host_seek_repeat and (options.combat_case!="commit" or options.corrected_inputs or options.changed_inputs))
                or options.checkpoint_pair or options.combat_case not in ("commit","before","after")
                or options.pixel_diagnostics
                or ((options.corrected_inputs or options.changed_inputs)
                    and (not options.corrected_inputs or not options.source_revision or options.combat_case!="commit"))
                or options.capture_cancel or options.restore_reuse):
            parser.error("anchor170 requires one host seek, with unchanged inputs or committed authored source correction")
        if options.combat_advanced_tick not in (300,329,2504,5695,6538) and not execution_fallback:options.combat_advanced_tick=220
    elif options.combat_advanced_tick in (220,2504):
        parser.error("B220 requires the bounded anchor170 protocol")
    if options.seek_drained_cancel:
        if options.interactive_controls: parser.error("drained cancellation requires API cancellation")
        options.seek_settlement_failure=True
    midpoint_recovery=(options.stage=="combat-restore" and options.combat_anchor_tick==170
        and options.combat_advanced_tick==2504 and options.host_seek and options.host_seek_target in (5750,11000)
        and options.seek_settlement_failure and options.combat_case=="after" and not options.interactive_controls)
    if options.host_seek_target in (5750,11000) and not midpoint_recovery:
        parser.error("targets5750/11000 require the bounded A170/B2504 settlement-failure recovery")
    guard_resimulation=(options.stage=="combat-restore" and options.host_seek_target==300 and options.host_seek
        and options.combat_anchor_tick==170 and options.combat_advanced_tick in (300,2504)
        and options.corrected_inputs and options.source_revision and options.combat_case=="commit"
        and not options.host_seek_repeat and not options.combat_single_step and not options.seek_settlement_failure
        and not options.seek_advance_failure)
    private_hud_drained=(options.stage=="combat-restore" and options.combat_anchor_tick==170
        and options.combat_advanced_tick==300 and options.host_seek and options.host_seek_target==300
        and options.combat_case=="after" and options.seek_drained_cancel
        and not any((options.corrected_inputs,options.changed_inputs,options.source_revision,options.host_seek_repeat,
            options.seek_observer_failure,options.seek_preparation_failure)))
    trace_render_recovery=(options.stage=="combat-restore" and options.combat_anchor_tick==170
        and options.combat_advanced_tick in (329,5695,6538) and options.host_seek and options.host_seek_target==2510
        and options.combat_case=="after" and options.seek_drained_cancel
        and not any((options.corrected_inputs,options.changed_inputs,options.source_revision,options.host_seek_repeat,
            options.seek_observer_failure,options.seek_preparation_failure)))
    ground_commit=(options.stage=="combat-restore" and options.combat_case=="commit"
        and ground_commit_case(dict(historical_anchor_tick=options.combat_anchor_tick,
            historical_advanced_tick=options.combat_advanced_tick,host_seek_target=options.host_seek_target,
            host_seek=options.host_seek,**{k:getattr(options,k) for k in (
                "seek_advance_failure","seek_settlement_failure","seek_drained_cancel","corrected_inputs",
                "changed_inputs","source_revision","host_seek_repeat","seek_observer_failure","seek_preparation_failure")})))
    if (options.combat_advanced_tick in (329,5695,6538) or options.host_seek_target==2510) and not trace_render_recovery and not ground_commit:
        parser.error("B329/B5695/B6538 to C2510 requires the bounded unchanged trace Render::Drained recovery")
    if options.host_seek_target==300 and not guard_resimulation and not private_hud_drained:
        parser.error("target300 requires committed A170/B2504 authored guard resimulation")
    private_hud_recovery=(options.stage=="combat-restore" and options.combat_anchor_tick==170
        and options.combat_advanced_tick==300 and options.host_seek and options.host_seek_target==208
        and options.combat_case in ("before","after") and not options.host_seek_repeat
        and not options.corrected_inputs and not options.changed_inputs and not options.source_revision
        and not options.seek_advance_failure and not options.seek_settlement_failure)
    if options.combat_advanced_tick==300 and not (guard_resimulation or private_hud_recovery or private_hud_drained):
        parser.error("B300 requires committed authored guard resimulation or bounded unchanged HUD cancellation")
    if options.combat_advanced_tick==2504 and (options.combat_case=="all" or options.host_seek_repeat or options.checkpoint_fallback
            or options.changed_inputs
            or (not midpoint_recovery and not guard_resimulation and options.host_seek_target!=(214 if options.seek_advance_failure or options.seek_settlement_failure else 208))
            or (options.interactive_controls and not options.seek_settlement_failure)):
        parser.error("cross-round2504 requires one170-to-208 case (unchanged or committed source correction), or explicit214 recovery")
    if options.checkpoint_pair:
        if options.stage!="combat-restore" or options.combat_case not in ("commit","before","after") or options.corrected_inputs or options.changed_inputs or options.same_process_render or options.capture_cancel or options.restore_reuse:
            parser.error("--checkpoint-pair requires a bounded unchanged-input combat restore case")
        options.host_seek=options.host_seek_repeat=options.combat_exact_advance=True
        options.combat_advanced_tick=211;options.host_seek_target=208
    elif options.combat_advanced_tick==211:
        parser.error("tick211 is admitted only by --checkpoint-pair")
    if options.index_preparation_fallback and (not options.index_seek or not options.index_extra_checkpoint):
        parser.error("--index-preparation-fallback requires indexed seeking with an extra checkpoint")
    if options.index_extra_checkpoint and (not options.index_seek or not 170<options.index_extra_checkpoint<options.index_seek_target):
        parser.error("--index-extra-checkpoint requires an indexed seek and170 < checkpoint < target")
    if options.index_recovery and (not options.index_seek or options.index_preparation_fallback
            or options.index_seek_target<=max(170,options.index_extra_checkpoint)):
        parser.error("indexed recovery requires an advancing indexed seek without a preparation-fallback trial")
    if not (170<=options.index_seek_target<=36000-options.index_seek_continuation and 120<=options.index_seek_continuation<=600):
        parser.error("indexed seek requires target>=170 and120..600 continuation ticks within36000")
    if not options.index_seek and (options.index_seek_target!=208 or options.index_seek_continuation!=120):
        parser.error("indexed seek window requires --index-seek")
    if options.index_seek:
        if options.stage!="equivalence" or not options.full_match:
            parser.error("--index-seek requires equivalence --full-match")
        options.record_index=options.index_checkpoint=options.include_setup=options.yield_every_tick=True
    if options.host_session and ((options.stage!="session-exit"
        and not (options.stage=="equivalence" and options.full_match and options.record_index and options.index_checkpoint and options.index_seek))):
        parser.error("--host-session requires session-exit or full indexed seeking")
    if options.host_session and options.index_extra_checkpoint not in (0,2504):
        parser.error("managed additional-checkpoint observation expects the verified next-round boundary2504")
    if options.stage in ("index-cancel","session-exit"):
        if options.full_match:parser.error("index-cancel is a bounded cancellation experiment")
        options.record_index=options.index_checkpoint=options.include_setup=options.yield_every_tick=True
        options.samples=max(360,options.samples)
    if options.record_index and options.stage not in ("index-cancel","session-exit") and (options.stage!="equivalence" or not options.full_match or not options.include_setup
            or options.empty_interval or options.move_state or options.world_pause or options.interior_pause or options.application_pause):
        parser.error("--record-index requires uninterrupted equivalence --full-match --include-setup")
    if options.index_checkpoint and options.stage!="index-proof" and (not options.record_index or not options.yield_every_tick):
        parser.error("--index-checkpoint requires --record-index --yield-every-tick")

    if options.seek_preparation_failure and (options.stage!="combat-restore" or not options.host_seek_repeat
            or options.host_seek_first_target!=220 or options.host_seek_target!=208 or options.combat_case!="after"
            or options.seek_observer_failure or options.checkpoint_pair or options.pixel_diagnostics):
        parser.error("preparation-failure recovery requires repeated220-to-208 host seek, after cancellation and ordinary diagnostics")
    if options.seek_settlement_failure: options.seek_advance_failure=True
    if options.resume_candidate and (options.stage!="combat-restore" or not (options.particle_owner_copy or checkpoint_window or options.seek_advance_failure or (options.completion_repeat and options.combat_case=="after"))):
        parser.error("--resume-candidate requires a bounded completed checkpoint window or seek recovery protocol")
    emitter_recovery=options.seek_settlement_failure and options.combat_anchor_tick==205 and options.combat_advanced_tick==210 and options.host_seek_target==208
    lifetime_recovery=options.combat_anchor_tick==205 and options.combat_advanced_tick==210 and options.host_seek_target==220 and not options.seek_settlement_failure
    if options.seek_advance_failure and (options.stage!="combat-restore" or not options.host_seek
            or options.combat_case!="after" or (not emitter_recovery and not lifetime_recovery and not private_hud_drained and not trace_render_recovery and (options.combat_anchor_tick!=170 or options.combat_advanced_tick not in (220,2504) or (not midpoint_recovery and options.host_seek_target!=214)))
            or options.host_seek_repeat or options.seek_observer_failure or options.seek_preparation_failure
            or options.pixel_diagnostics or options.corrected_inputs or options.changed_inputs or options.combat_exact_advance):
        parser.error("advance failure requires one unchanged host seek A170/B220 or B2504, combat-case after, without other interventions")
    if options.seek_publication_cancel and (options.stage!="combat-restore" or not options.host_seek
            or options.combat_case!="after" or options.combat_anchor_tick!=170 or options.combat_advanced_tick!=2504
            or options.seek_advance_failure or options.seek_observer_failure or options.seek_preparation_failure
            or options.host_seek_repeat or options.changed_inputs or options.corrected_inputs or options.pixel_diagnostics):
        parser.error("publication-handoff cancellation requires unchanged A170/B2504 host recovery")
    if options.seek_observer_failure and (options.stage!="combat-restore" or not options.host_seek or options.combat_case!="after" or options.pixel_diagnostics or options.corrected_inputs or options.changed_inputs):
        parser.error("--seek-observer-failure requires unchanged-input host seek after-publication cancellation")
    if options.source_revision and not (options.corrected_inputs or options.changed_inputs): parser.error("--source-revision requires --corrected-inputs or --changed-inputs")
    if options.source_revision and not options.corrected_inputs and (options.combat_case not in ("before", "after") or options.combat_advanced_tick != 210):
        parser.error("future source-revision recovery requires combat-case before/after at B210")
    if options.host_seek_repeat and (not options.host_seek or options.combat_case not in (("commit","before","after") if options.checkpoint_pair else ("commit","after")) or (options.combat_case!="commit" and options.host_seek_target!=208) or options.corrected_inputs or options.changed_inputs or (options.pixel_diagnostics and not options.checkpoint_pair)):
        parser.error("--host-seek-repeat requires unchanged host seek commit or second-publication cancellation to208 without pixel diagnostics")
    if options.host_seek and (options.stage!="combat-restore" or options.combat_case not in ("commit","before","after") or (options.combat_case=="commit" and not options.combat_exact_advance) or options.restore_reuse or options.capture_cancel or options.same_process_render):
        parser.error("--host-seek requires independent commit with exact advance, or before/after cancellation, without single-step, capture/reuse or per-draw diagnostics")
    if options.restore_reuse and (options.stage!="combat-restore" or options.combat_case!="commit" or options.pixel_diagnostics or options.same_process_render):
        parser.error("--restore-reuse requires independent commit without pixel diagnostics")
    if options.capture_cancel and (options.stage!="combat-restore" or options.combat_case!="commit" or options.pixel_diagnostics or options.same_process_render):
        parser.error("--capture-cancel requires the independent combat commit without pixel diagnostics")
    if options.host_seek_first_target and (not options.host_seek_repeat or options.checkpoint_pair or options.host_seek_first_target==options.host_seek_target):
        parser.error("a distinct first target requires repeated host seek without checkpoint-pair mode")
    indexed_repeat=(options.stage=="equivalence" and options.index_seek and options.host_session
        and options.index_recovery=="interior" and options.index_seek_target==208
        and not options.index_extra_checkpoint and not options.index_preparation_fallback)
    if options.index_recovery=="interior" and not (indexed_repeat and options.completion_repeat):
        parser.error("interior indexed recovery requires the bounded host-session208 completion-repeat protocol")
    if options.completion_repeat and not indexed_repeat and (options.stage!="combat-restore" or options.combat_case not in ("commit","after")
            or not options.host_seek or options.host_seek_target!=208 or options.host_seek_repeat
            or (not options.combat_single_step if options.combat_case=="commit" else options.combat_single_step or options.combat_exact_advance) or options.combat_anchor_tick!=205
            or options.combat_advanced_tick!=210 or options.corrected_inputs or options.changed_inputs
            or options.complete_seek_application):
        parser.error("--completion-repeat requires bounded205/210 host step commit or deferred cancellation")
    if options.combat_single_step and (options.host_seek_repeat or options.host_seek_target!=208 or options.combat_case!="commit"):
        parser.error("--combat-single-step requires one committed seek to208")
    if options.combat_single_step and not options.combat_exact_advance: parser.error("--combat-single-step requires --combat-exact-advance")
    if options.combat_exact_advance and (options.stage != "combat-restore" or (options.combat_case != "commit" and not options.host_seek_repeat and not checkpoint_window) or options.same_process_render or (options.pixel_diagnostics and not options.host_seek)):
        parser.error("--combat-exact-advance requires commit without pixel diagnostics")
    if options.combat_advanced_tick != 210 and (options.stage != "combat-restore" or options.same_process_render):
        parser.error("--combat-advanced-tick209 requires independent combat-restore")
    if options.corrected_inputs:
        if options.stage != "combat-restore" or options.combat_case != "commit" or options.same_process_render or options.changed_inputs:
            parser.error("--corrected-inputs requires independent combat-restore --combat-case commit, without --changed-inputs")
        options.changed_inputs = True
    if options.changed_inputs and (options.stage != "combat-restore" or options.same_process_render):
        parser.error("--changed-inputs requires independent combat-restore")
    if options.same_process_render and (options.stage != "combat-restore" or options.combat_case != "commit" or options.resume_native_control):
        parser.error("--same-process-render requires combat-restore --combat-case commit without --resume-native-control")
    if options.combat_case != "all" and options.stage != "combat-restore":
        parser.error("--combat-case requires combat-restore")
    if options.stage in ("restore", "restore-probe"):
        parser.error("the legacy 360/361 restore experiment pauses during character intros and is retired; "
                     "use combat-restore for the bounded historical combat transaction. "
                     "Retained reports remain readable; application-probe diagnoses the current combat restore boundary")
    if options.interactive_controls and not (options.stage == "interior-probe"
            or (options.stage == "combat-restore" and options.seek_settlement_failure)
            or (options.stage == "equivalence" and (options.interior_pause or options.index_seek))):
        parser.error("--interactive-controls requires interior-probe or equivalence --interior-pause")
    if options.interactive_controls and options.index_seek and options.index_recovery!="published" and (options.index_seek_continuation<240 or options.index_recovery or options.index_preparation_fallback):
        parser.error("indexed playback controls require at least240 continuation ticks and no recovery fault")
    if options.stage == "restore":
        options.include_setup = options.yield_every_tick = options.interior_pause = True
    if options.resume_baseline and options.stage != "baseline":
        parser.error("--resume-baseline requires baseline")
    if options.resume_native_control and (options.stage not in ("equivalence", "combat-restore", "session-exit") or options.empty_interval or options.move_state):
        parser.error("--resume-native-control requires equivalence with an uninterrupted native control")
    if options.jobs < 1 or not 120 <= options.samples <= 36000 or options.timeout <= 0:
        parser.error("jobs and timeout must be positive; samples must be 120..36000")
    if options.yield_every_tick and options.stage not in ("equivalence", "restore", "index-cancel", "session-exit"):
        parser.error("--yield-every-tick requires equivalence")
    if options.include_setup and options.stage not in ("baseline", "equivalence", "restore", "index-cancel", "session-exit"):
        parser.error("--include-setup requires baseline or equivalence")
    if options.empty_interval and options.stage != "equivalence":
        parser.error("--empty-interval requires equivalence; native baselines stay uninterrupted")
    if options.move_state and (options.stage != "equivalence" or options.samples < 240 or options.empty_interval
            or options.world_pause or options.interior_pause):
        parser.error("--move-state requires equivalence, at least 240 samples, and no other intervention")
    if options.world_pause and (options.stage != "equivalence" or options.samples < 240 or options.empty_interval):
        parser.error("--world-pause requires equivalence, at least 240 samples, and no empty-interval probe")
    if options.interior_pause and (options.stage not in ("equivalence", "restore") or not options.include_setup
            or not options.yield_every_tick or options.world_pause or options.empty_interval):
        parser.error("--interior-pause requires equivalence, include-setup, yield-every-tick, and no other pause probe")
    if options.stage == "equivalence" and options.interior_pause and not options.full_match and options.samples < 300:
        parser.error("combat interior pause requires at least 300 samples to observe 120 ticks after the combat hold")
    if options.consumer_mutation and (options.stage!="combat-restore" or not options.host_seek
            or (options.combat_anchor_tick,options.combat_advanced_tick,options.host_seek_target)!=(210,217,217)
            or options.combat_case!="after" or not options.combat_exact_advance or options.consumer_task
            or options.seek_advance_failure or options.seek_settlement_failure):
        parser.error("--consumer-mutation requires combat-restore --host-seek A210/B217 target217 --after --combat-exact-advance")
    if options.consumer_task and options.stage!="application-probe" and (options.stage!="equivalence" or not options.application_pause):
        parser.error("--consumer-task requires application-probe or equivalence --application-pause")
    if options.application_pause and (options.stage != "equivalence" or not options.include_setup
            or not options.yield_every_tick or options.world_pause or options.interior_pause
            or options.empty_interval or options.move_state or options.interactive_controls):
        parser.error("--application-pause requires equivalence, include-setup, yield-every-tick, and no other intervention")
    if options.application_pause and not options.full_match and options.samples < 300:
        parser.error("combat application pause requires at least 300 samples")
    started = time.monotonic()
    experiment_name = options.stage
    if options.stage=="combat-restore" and options.combat_advanced_tick==2504:
        experiment_name="combat-round-"+options.combat_case
    if options.stage=="index-proof" and options.index_checkpoint:experiment_name="index-owned-proof"
    if options.stage == "equivalence":
        if options.application_pause:
            experiment_name += "-application"
        elif options.world_pause:
            experiment_name += "-world"
        elif options.empty_interval:
            experiment_name += "-empty"
        elif options.move_state:
            experiment_name += "-move"
        elif options.full_match:
            experiment_name += "-full"
        if options.index_checkpoint:
            experiment_name += "-index-checkpoint"
    summary = {"stage": options.stage, "result": "fail", "certifying": False, "profile": profile}
    OUTPUT.mkdir(parents=True, exist_ok=True)
    try:
        if options.stage not in ("build", "execution-gate", "index-proof", "seek-recovery-contract"):
            from deterministic_qualification.replay_preflight import require_ready
            summary["preflight"] = live_preflight(options)
            require_ready(summary["preflight"])
        if profile and profile["definition"]["purpose"] == "qualification":
            from deterministic_qualification.replay_local import run_integration
            summary["integration_tests"] = run_integration(ROOT, OUTPUT)
            if summary["integration_tests"]["result"] != "pass":
                raise RuntimeError("integration tests failed; qualification prohibited")
        if options.stage not in ("execution-gate", "index-proof"):
            summary["build_provenance"] = build(options.jobs) if options.stage=="build" else ensure_build(options.jobs)
            ACTIVE_CAPTURE_SOURCES = (summary["build_provenance"]["source_retention"] if options.stage == "build"
                                      else retain_sources(ROOT, OUTPUT / "source-archives", BUILD))
            summary["capture_source_retention"] = ACTIVE_CAPTURE_SOURCES
            summary["workspace_fingerprint"] = workspace_fingerprint(ROOT)
        summary["build_identities"] = {name: sha256_file(path) for name, path in (
            ("runtime", BUILD / "HorseMod" / "HorseMod.dll"),
            ("observer", BUILD / "HorseMod" / "ReplayQualificationMod.dll"),
            ("framework", BUILD / "LessEqual421__Shipping__Win64" / "bin" / "UE4SS.dll"))}
        if profile and profile['definition']['name'] == 'rolling600-qualification':
            from deterministic_qualification.replay_profiles import require_rolling30
            require_rolling30(OUTPUT, summary['build_identities'])
        if options.stage == "build":
            summary["test_evidence"] = run_local_regressions(summary["build_provenance"])
        if options.stage == "build":
            pass
        elif options.stage == "seek-recovery-contract":
            executable=BUILD / "HorseMod" / "DeterministicCoreSelfTest.exe"
            log=OUTPUT / "seek-recovery-contract.log"
            result=subprocess.run([str(executable),"--seek-advance-recovery"],cwd=ROOT,capture_output=True,text=True)
            log.write_text(result.stdout+result.stderr,encoding="utf-8")
            retained_log=OUTPUT / "source-archives" / ("seek-recovery-contract-"+sha256_file(log)+".log")
            if not retained_log.exists(): retained_log.write_bytes(log.read_bytes())
            if sha256_file(retained_log)!=sha256_file(log): raise RuntimeError("retained contract log corruption")
            summary.update(scope="production orchestration with controlled asynchronous owners; not live B recovery",
                test_binary_sha256=sha256_file(executable),raw_log={"path":str(retained_log),"sha256":sha256_file(retained_log)},
                cleanup={"complete":True,"game_launched":False})
            if result.returncode: raise RuntimeError("post-traversal failure cannot recover B; "+str(log))
        elif options.stage == "index-proof":
            summary.update(qualify_retained_index(options.index_checkpoint))
        elif options.stage == "execution-gate":
            args = control_args(options.replay.resolve(), "runtime", "execution-gate", 120, options.timeout)
            args.include_setup = True
            summary["execution_gate"] = collect_execution_gate(args)
        elif options.stage == "bootstrap":
            subprocess.run([sys.executable, "-m", "pytest", "-q",
                "tools/deterministic_qualification/tests/test_replay_run.py",
                "tools/deterministic_qualification/tests/test_process_control.py"],
                cwd=ROOT, check=True)
            # Intro-skipped presentation observes170..340 and requires120
            # tagged frames. A120-sample bootstrap can finish before170.
            bootstrap = control_args(options.replay.resolve(), "runtime", "bootstrap", options.samples, options.timeout)
            bootstrap.skip_intros = True
            bootstrap.include_setup = True
            capture(bootstrap)
            require_bootstrap(bootstrap)
        elif options.stage == "c18-diagnostic":
            probe = control_args(options.replay.resolve(), "runtime", "c18-diagnostic", 360, options.timeout)
            probe.c18_diagnostic = probe.skip_intros = probe.include_setup = True
            # Match the retained consumer-task diagnostic startup; the request
            # remains ordinary trajectory and never asks for scheduler prepare.
            probe.single_game_thread = probe.no_async_loading_thread = True
            report = capture(probe)
            if report.get('result') != 'diagnostic_observed' or not report.get('c18_diagnostic_stop'):
                raise RuntimeError('selected C18 occurrence was not retained')
            summary.update(scope='ordinary-forward C18 diagnostic only; no recovery or ownership proof',
                           c18_diagnostic_stop=report['c18_diagnostic_stop'], capture_report=str(probe.report))
        elif options.stage == "consumer-census":
            # One stock observation, no correction/hold/publication. The
            # existing observer records concrete task owners in205..224.
            #240 observations produced only113 tagged presentation frames;
            # preserve the existing120-frame completion gate with360 samples.
            probe = control_args(options.replay.resolve(), "stock", "consumer-census", 360, options.timeout)
            probe.skip_intros = probe.include_setup = True
            report = capture(probe)
            raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
            summary["intro_skips"] = validate_intro_skips(raw, report["run_id"])
            rows = [line for line in raw.splitlines() if "[ReplayQualification] component task owner " in line]
            if not rows:
                raise RuntimeError("no concrete component task observed in205..224; do not infer a supported family")
            root_rows = [line for line in raw.splitlines() if "[ReplayQualification] trace retained root " in line]
            root_keys = [tuple(match) for line in root_rows for match in re.findall(r"component_index=(\d+) role=(chara|scene) ",line)]
            if (len(root_keys)!=4 or len(set(root_keys))!=4
                    or {role for _,role in root_keys}!={"chara","scene"}
                    or len({owner for owner,_ in root_keys})!=2):
                raise RuntimeError("selected trace chara/scene family census missing or incomplete")
            cri_rows = [line for line in raw.splitlines() if "[ReplayQualification] cri callback census " in line]
            cri_entries = [line for line in raw.splitlines() if "[ReplayQualification] cri callback entry " in line]
            if not cri_rows or any("valid=true " not in line for line in cri_rows):
                raise RuntimeError("CRI callback census missing or incomplete; no callback-closure inference is permitted")
            summary.update(scope="stock component-owner census only; no suspension, recovery or equivalence proof",
                           component_owners=rows, cri_callback_snapshots=cri_rows,
                           trace_retained_roots=root_rows,
                           cri_callback_entries=cri_entries, capture_report=str(probe.report))
        elif options.stage == "baseline":
            samples = 36000 if options.full_match else options.samples
            timeout = max(options.timeout, 600) if options.full_match else options.timeout
            prefix = "baseline-setup" if options.include_setup and not options.full_match else "baseline"
            first = control_args(options.replay.resolve(), "stock", f"{prefix}-a", samples, timeout, options.full_match)
            second = control_args(options.replay.resolve(), "stock", f"{prefix}-b", samples, timeout, options.full_match)
            first.include_setup = second.include_setup = options.include_setup
            require_bootstrap(first)
            if options.resume_baseline:
                require_resumable_baseline(first)
                print(f"baseline: retaining verified first capture {first.report}", flush=True)
            else:
                capture(first)
            capture(second)
            summary.update(compare_passive_controls(first.report, second.report, candidate_mode="stock"))
            if summary["result"] != "pass":
                raise RuntimeError("independent stock runs diverged; inspect first mismatch in stage report")
        elif options.stage == "combat-restore":
            combat_prefix="combat-round" if options.combat_advanced_tick==2504 else "combat-restore"
            combat_samples=options.combat_advanced_tick+160 if checkpoint_window else max(2670,options.combat_advanced_tick+160) if trace_render_recovery or ground_commit else 2664 if options.combat_advanced_tick==2504 else 460 if options.combat_advanced_tick==300 else 360
            if options.rolling_cycles:combat_samples=options.combat_anchor_tick+6+options.rolling_cycles+160
            native = control_args(options.replay.resolve(), "stock", combat_prefix+"-native", combat_samples, options.timeout)
            # Observations count physical traversals, including replayed prefixes.
            # Two A170 restores consume88 observations before the final suffix;
            # the former fixed360 budget stopped before target214+120. Keep the
            # native control and final120 comparison unchanged, but budget every
            # repeated traversal in the candidate capture.
            repeated_prefix = 0
            if options.seek_advance_failure or ground_commit or checkpoint_window:
                # Original B, the discarded combat prefix, and B+120 must all
                # fit. Leave bounded room for completing the failed interval.
                repeated_prefix = max(64,options.host_seek_target-options.combat_anchor_tick+40)
            if options.host_seek_repeat:
                first_target = options.host_seek_first_target or (210 if options.checkpoint_pair
                    else 209 if options.host_seek_target == 208 else 208)
                repeated_prefix = (10 if options.checkpoint_pair else
                    options.combat_advanced_tick - options.combat_anchor_tick
                    + max(0, first_target - options.combat_anchor_tick))
            if options.rolling_cycles:repeated_prefix=7*options.rolling_cycles+64
            executor = control_args(options.replay.resolve(), "runtime", combat_prefix+"-executor",
                                    combat_samples + repeated_prefix + (92 if guard_resimulation else 0), options.timeout)
            executor.rolling_cycles=options.rolling_cycles
            executor.ground_motion_perturb=options.ground_motion_perturb
            executor.rolling_corrections=options.rolling_corrections
            native.authored_prefix=executor.authored_prefix=options.authored_prefix
            executor.replay_budget_gib=options.replay_budget_gib
            native.include_setup = executor.include_setup = True
            native.skip_intros = executor.skip_intros = True
            native.observation_target = executor.observation_target = (options.combat_advanced_tick if options.combat_case!="commit" else options.host_seek_target) if checkpoint_window else options.combat_advanced_tick if trace_render_recovery else 2510 if ground_commit else 0
            if options.rolling_cycles:
                native.observation_target = executor.observation_target = options.combat_anchor_tick+6+options.rolling_cycles
            native.changed_inputs = executor.changed_inputs = options.changed_inputs
            native.corrected_inputs = executor.corrected_inputs = options.corrected_inputs
            native.source_revision = executor.source_revision = options.source_revision
            native.source_revision_profile = executor.source_revision_profile = (
                "guard201" if options.combat_advanced_tick in (300,2504) and options.corrected_inputs and options.source_revision else "historical")
            input_suffix = "-corrected" if options.corrected_inputs else "-changed"
            if options.changed_inputs: native.report = OUTPUT / ("combat-restore-native" + input_suffix + ".json")
            native.pixel_diagnostics = executor.pixel_diagnostics = options.pixel_diagnostics or options.same_process_render
            native.coherence_images = executor.coherence_images = options.coherence_images
            native.pass_diagnostics = executor.pass_diagnostics = options.same_process_render
            native.physx = executor.physx = GAME_ROOT.parents[2] / "Engine/Binaries/ThirdParty/PhysX/Win64/VS2015/PhysX3_x64.dll"
            executor.executor = executor.yield_every_tick = True
            executor.probe_application_pause = executor.probe_particle_copy = executor.probe_historical_restore = True
            executor.historical_advanced_tick = options.combat_advanced_tick
            executor.historical_anchor_tick = options.combat_anchor_tick
            executor.historical_exact_advance = options.combat_exact_advance
            executor.complete_seek_application = options.complete_seek_application
            executor.historical_single_step = options.combat_single_step
            native.completion_repeat = executor.completion_repeat = options.completion_repeat
            executor.capture_cancel = options.capture_cancel
            executor.particle_owner_copy = options.particle_owner_copy
            executor.particle_owner_registration = options.particle_owner_registration
            executor.particle_owner_gpu = options.particle_owner_gpu
            executor.restore_reuse = options.restore_reuse
            executor.host_seek_repeat = options.host_seek_repeat
            executor.checkpoint_pair = options.checkpoint_pair
            executor.checkpoint_fallback = options.checkpoint_fallback
            executor.seek_publication_cancel = options.seek_publication_cancel
            executor.seek_observer_failure = options.seek_observer_failure
            executor.consumer_mutation = options.consumer_mutation
            executor.seek_advance_failure = options.seek_advance_failure
            executor.seek_drained_cancel = options.seek_drained_cancel
            executor.seek_settlement_failure = options.seek_settlement_failure
            executor.probe_interactive_controls = options.interactive_controls
            executor.seek_preparation_failure = options.seek_preparation_failure
            executor.host_seek = options.host_seek
            executor.host_seek_target = options.host_seek_target
            executor.host_seek_first_target = options.host_seek_first_target
            # Prove reversible recovery before allowing destructive commit.
            # Each variant gets a fresh replay and compares against the same
            # independently executed native control, never expected inputs.
            first_case = "" if options.combat_case == "commit" else "after" if options.combat_case == "after" else "before"
            executor.historical_cancel = first_case
            executor.report = OUTPUT / (f"{combat_prefix}-cancel-{first_case}.json" if first_case else f"{combat_prefix}-executor.json")
            if options.changed_inputs: executor.report = executor.report.with_stem(executor.report.stem + input_suffix)
            # This is the bounded mechanism diagnostic, not the execution
            # qualification gate. Investigate its first failure before paying
            # for another control; comparison still requires a current,
            # independent stock capture before any success is reported.
            if options.resume_candidate:
                from deterministic_qualification.replay_profiles import validate_resumed_profile
                if ACTIVE_PROFILE is not None:
                    validate_resumed_profile(json.loads(executor.report.read_text(encoding="utf-8")),
                                             ACTIVE_PROFILE, ACTIVE_DIAGNOSTICS)
                revalidate_advance_failure_capture(executor)
                CAPTURE_REPORTS.append(executor.report)
            else:
                capture(executor)
            candidate_report = json.loads(executor.report.read_text(encoding="utf-8"))
            if options.rolling_cycles:
                summary['rolling_candidate']=validate_rolling_candidate(candidate_report,options.rolling_cycles)
                summary['observed_coverage']=summary['rolling_candidate']['observed_coverage']
                if options.rolling_corrections:
                    from copy import copy
                    from deterministic_qualification.replay_control import rolling_correction_prefix
                    controls={}
                    for revision in sorted(set(rolling_revision_assignments(options.rolling_corrections,options.rolling_cycles,options.combat_anchor_tick).values())):
                        control=copy(native)
                        control.rolling_corrections=rolling_correction_prefix(options.rolling_corrections,revision)
                        control.report=native.report.with_stem(native.report.stem+f'-revision{revision}')
                        controls[revision]=independent_control(control,candidate_report,options.combat_anchor_tick)
                    summary['independent_controls']={revision:{'run_id':report['run_id'],'reuse':report.get('reuse')} for revision,report in controls.items()}
                    summary.update(compare_scheduled_rolling_callbacks(controls,candidate_report,options.rolling_cycles))
                else:
                    native_report=independent_control(native, candidate_report, options.combat_anchor_tick)
                    from deterministic_qualification.replay_controls_catalog import require_startup_compatible
                    summary['control_startup'] = require_startup_compatible(native_report, candidate_report, options.combat_anchor_tick)
                    summary['independent_control'] = {'run_id': native_report['run_id'], 'reuse': native_report.get('reuse')}
                    summary.update(compare_rolling_callbacks(native_report,candidate_report,options.rolling_cycles))
                summary['acceptance_results'] = {'observer_validity': 'pass', 'simulation': 'pass'}
                summary['execution_gate']=execution_gate_coverage({})
            else:
                if options.combat_single_step:
                    candidate_raw=Path(candidate_report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace")
                    if "historical application completed held run_id=" not in candidate_raw and "host step completed run_id=" not in candidate_raw:
                        raise RuntimeError("interior application completion was not exercised; native control deferred")
                if first_case=="before" and not executor.pixel_diagnostics:
                    candidate_raw=Path(candidate_report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace")
                    if not re.search(r"preparation_pending=true render_command_pending=(?:true gpu_completion_pending=false|false gpu_completion_pending=true)",candidate_raw):
                        raise RuntimeError("pending preparation cancellation was not exercised; native control deferred")
                candidate_raw=Path(candidate_report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
                if (options.host_seek and options.combat_case=="commit" and not options.host_seek_repeat
                        and not options.changed_inputs and not options.corrected_inputs
                        and 170<=options.combat_anchor_tick<options.host_seek_target<=230):
                    summary["particle_seed_reproduction"]=compare_repeated_particle_seeds(
                        candidate_raw,candidate_report["run_id"],options.combat_anchor_tick,options.host_seek_target)
                    if summary["particle_seed_reproduction"]["result"]=="fail":
                        raise RuntimeError("native particle seed traversal diverged; native control deferred")
                    summary['particle_lifecycle_reproduction']=compare_particle_lifecycle(
                        candidate_report,candidate_report,options.combat_anchor_tick,options.host_seek_target,original_traversal=True)
                    if summary['particle_lifecycle_reproduction']['result']=='fail':
                        raise RuntimeError('native particle lifecycle traversal diverged; native control deferred')
                if options.particle_owner_copy:
                    summary["particle_owner_copy"] = validate_particle_owner_copy(candidate_raw,candidate_report["run_id"],options.combat_anchor_tick,options.particle_owner_registration)
                    if options.particle_owner_gpu:
                        summary["particle_gpu_owner"] = validate_particle_gpu_owner(candidate_raw,candidate_report["run_id"],options.combat_anchor_tick)
                if options.seek_preparation_failure:
                    validate_complete_B_target_preparation(candidate_raw,candidate_report["run_id"])
                second_undo_captured=(f"restore undo captured target=205 original={host_seek_first_target(candidate_report)} gpu_complete=true before_target_preparation=true" in candidate_raw)
                summary["callback_admission"] = validate_callback_admission(
                    candidate_raw, options.combat_advanced_tick, options.capture_cancel, options.restore_reuse, options.host_seek_repeat and (not options.seek_preparation_failure or second_undo_captured), host_seek_first_target(candidate_report), options.checkpoint_pair, options.combat_anchor_tick, options.checkpoint_fallback,
                    preparation_cancel_run=(candidate_report['run_id'] if options.host_seek and first_case=='before'
                        and not options.host_seek_repeat and f"historical cancellation injected run_id={candidate_report['run_id']} point=before tick={options.combat_advanced_tick} preparation_pending=true " in candidate_raw else None))
                summary["trace_source"] = compare_same_process_trace(executor.report)
                if not options.same_process_render and summary["trace_source"]["result"] == "fail":
                    summary["simulation_correctness"] = {"result":"fail", "participant":"trace_source",
                        "first_mismatch":summary["trace_source"]["first_mismatch"]}
                    raise RuntimeError("same-process trace source diverged; native control deferred until this mechanism passes")
                if options.candidate_only:
                    summary["scope"] = "candidate-only causal diagnostic; independent continuation and seeker qualification unproven"
                    summary["independent_continuation"] = {"result": "not_run"}
                    summary["execution_gate"] = execution_gate_coverage({})
                elif options.same_process_render:
                    summary.update(compare_same_process_render(executor.report))
                    if summary["result"] != "pass":
                        raise RuntimeError("same-process render divergence; inspect the earliest selected output")
                else:
                    if options.resume_native_control:
                        retained = json.loads(native.report.read_text(encoding="utf-8"))
                        identities = current_identities(native)
                        if (retained.get("result") != "captured" or retained.get("mode") != "stock"
                                or not retained.get("skip_intros") or not retained.get("native_fixed_seed") or not retained.get("serial_particles") or not retained.get("include_setup")
                                or retained.get("corrected_inputs", False) != native.corrected_inputs
                                or retained.get("source_revision", False) != native.source_revision
                                or retained.get("source_revision_profile","historical") != native.source_revision_profile
                                or retained.get("changed_inputs", False) != native.changed_inputs
                                or retained.get("pixel_diagnostics", False) != native.pixel_diagnostics
                                or retained.get("pass_diagnostics", False) != native.pass_diagnostics
                                or retained.get("observations_requested") != combat_samples or retained.get("full_match")
                                or retained.get("cleanup") != {"complete": True, "games_remaining": 0}
                                or retained.get("loaded_runtime", {}).get("verification") != "owned_process_runtime_absent"
                                or any(retained.get("identities", {}).get(key) != value for key, value in identities.items() if key != "runtime")
                                or sha256_file(Path(retained["raw_log"]["path"])) != retained["raw_log"]["sha256"]):
                            raise RuntimeError("retained combat native control has stale dependencies/protocol or incomplete evidence")
                        print("reusing verified runtime-absent combat control", flush=True)
                    else:
                        native_report = independent_control(native, candidate_report, options.combat_anchor_tick)
                        # Existing comparator takes paths. A working alias is not evidence;
                        # the catalog and stage manifest keep immutable originals.
                        native.report.write_text(json.dumps(native_report, indent=2) + "\n")
                    summary["experiments"] = []
                    comparison = compare_historical_combat(native.report, executor.report)
                    summary["experiments"].append(comparison)
                    summary.update(comparison)
                    if comparison["result"] != "pass":
                        from deterministic_qualification.replay_outcomes import failure_message
                        raise RuntimeError(failure_message(comparison))
                    if 'particle_seed_reproduction' in summary:
                        native_report=json.loads(native.report.read_text(encoding='utf-8'))
                        summary['particle_lifecycle_independent']=[]
                        for tail in (options.host_seek_target,options.host_seek_target+120):
                            lifecycle=compare_particle_lifecycle(native_report,candidate_report,options.combat_anchor_tick,tail)
                            summary['particle_lifecycle_independent'].append(lifecycle)
                            if lifecycle['result']!='pass':raise RuntimeError('independent particle lifecycle or RNG divergence')
                    remaining_cases = (("after", "combat-restore-cancel-after"), ("", "combat-restore-executor")) if options.combat_case == "all" else ()
                    for cancellation, name in remaining_cases:
                        executor.historical_cancel = cancellation
                        executor.report = OUTPUT / (name + ("-changed" if options.changed_inputs else "") + ".json")
                        capture(executor)
                        comparison = compare_historical_combat(native.report, executor.report)
                        summary["experiments"].append(comparison)
                        summary.update(comparison)
                        if comparison["result"] != "pass":
                            raise RuntimeError("historical combat continuation diverged; inspect first mismatch")
                    summary["execution_gate"] = execution_gate_coverage({})
                    if summary["result"] != "pass":
                        raise RuntimeError("historical combat continuation diverged; inspect first mismatch")
        elif options.stage in ("interior-probe", "restore-probe", "move-probe", "application-probe", "gpu-copy"):
            # Bounded debugging after a failed admission, not a correctness
            # gate. Do not run two full matches merely to identify a rejected
            # guard. Equivalence still requires exact-binary prerequisites.
            summary["scope"] = "bounded mechanism diagnostic; native equivalence and execution gate are unproven"
            summary["execution_gate"] = execution_gate_coverage({})
            probe = control_args(options.replay.resolve(), "runtime", options.stage,
                300 if options.stage in ("interior-probe", "application-probe", "gpu-copy") else 240 if options.stage == "move-probe" else 120, options.timeout)
            probe.executor = probe.yield_every_tick = probe.include_setup = True
            probe.probe_interior_pause = options.stage in ("interior-probe", "restore-probe")
            probe.probe_application_pause = options.stage in ("application-probe", "gpu-copy")
            probe.probe_consumer_task=options.consumer_task
            if options.consumer_task:
                probe.single_game_thread=probe.no_async_loading_thread=True
            probe.probe_particle_copy = options.stage == "gpu-copy"
            probe.probe_interactive_controls = options.interactive_controls
            probe.probe_move_state = options.stage == "move-probe"
            probe.probe_small_restore = options.stage == "restore-probe"
            probe.skip_intros = options.stage in ("interior-probe", "application-probe", "gpu-copy")
            report = capture(probe)
            summary["executor"] = executor_proof(report, True)
            raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
            if probe.skip_intros:
                summary["intro_skips"] = validate_intro_skips(raw, report["run_id"])
            if probe.probe_move_state:
                summary["move_state_probe"] = validate_move_state_interval(raw, report["run_id"], True)
            elif probe.probe_particle_copy:
                summary["particle_copy"] = validate_particle_copy(raw, report["run_id"])
            elif probe.probe_application_pause:
                summary["application_pause"] = validate_application_pause(raw, report["run_id"], consumer_task=options.consumer_task)
            else:
                summary["interior_pause"] = validate_interior_pause(raw, report["run_id"])
                summary["interior_pause"]["resume_timing"] = interior_resume_timing(raw, report["run_id"])
                if summary["interior_pause"]["resume_timing"]["tick_rate_milli"] < 58000:
                    raise RuntimeError("post-interior-pause playback is below 58 native ticks per second")
            if probe.probe_consumer_task:
                summary["consumer_suspension"]=validate_consumer_suspension(raw)
            if probe.probe_small_restore:
                summary["small_restore"] = validate_small_restore(raw, report["run_id"])
            if probe.probe_interactive_controls:
                summary["interactive_control"] = validate_interior_control(raw, report)
            summary["scope"] = "bounded mechanism diagnostic; native equivalence and execution gate are unproven"
            summary["execution_gate"] = execution_gate_coverage({})
        elif options.stage in ("equivalence", "restore", "index-cancel", "session-exit"):
            samples = 36000 if options.full_match else options.samples
            timeout = max(options.timeout, 600) if options.full_match else options.timeout
            native = control_args(options.replay.resolve(), "stock", f"{experiment_name}-native", samples, timeout, options.full_match)
            executor = control_args(options.replay.resolve(), "runtime", f"{experiment_name}-executor", samples, timeout, options.full_match)
            executor.replay_budget_gib=options.replay_budget_gib
            executor.executor = True
            executor.yield_every_tick = options.yield_every_tick
            executor.probe_world_pause = options.world_pause
            executor.probe_interior_pause = options.interior_pause
            executor.probe_application_pause = options.application_pause
            executor.probe_consumer_task = options.consumer_task
            if options.consumer_task:
                native.single_game_thread=executor.single_game_thread=True
                native.no_async_loading_thread=executor.no_async_loading_thread=True
                native.skip_intros=executor.skip_intros=True
                native.serial_particles=executor.serial_particles=True
            executor.probe_interactive_controls = options.interactive_controls
            executor.probe_small_restore = options.stage == "restore"
            native.include_setup = executor.include_setup = options.include_setup
            native.record_index = executor.record_index = options.record_index
            executor.index_checkpoint=options.index_checkpoint
            executor.index_extra_checkpoint=options.index_extra_checkpoint
            executor.index_preparation_fallback=options.index_preparation_fallback
            executor.index_recovery=options.index_recovery
            native.completion_repeat=executor.completion_repeat=options.completion_repeat
            executor.index_seek=options.index_seek
            native.index_seek_target=executor.index_seek_target=options.index_seek_target
            native.index_sequence=executor.index_sequence=options.index_sequence
            native.index_seek_continuation=executor.index_seek_continuation=options.index_seek_continuation
            native.index_cancel=executor.index_cancel=options.stage=="index-cancel"
            native.native_session_exit=executor.native_session_exit=options.stage=="session-exit"
            native.host_session=executor.host_session=options.host_session
            if options.index_checkpoint:
                native.skip_intros=executor.skip_intros=True
                native.native_fixed_seed=executor.native_fixed_seed=True
                native.serial_particles=executor.serial_particles=True
            native.probe_empty_interval = executor.probe_empty_interval = options.empty_interval
            native.probe_move_state = executor.probe_move_state = options.move_state
            require_bootstrap(executor)
            if not options.index_checkpoint:
                require_native_baseline(native)
            # This assembled integration has its own independent full native
            # control below, after candidate completion. Do not precede it with
            # another full baseline campaign solely because observer code changed.
            if options.stage == "restore":
                require_interior_equivalence(executor)
            # The completed baseline pair is already an independent native
            # control. Reuse it only for the identical observer/build and
            # uninterrupted full-match protocol; special probes need a fresh run.
            baseline_path = OUTPUT / "baseline-a.json"
            baseline_report = json.loads(baseline_path.read_text(encoding="utf-8"))
            reuse = (options.full_match and not options.record_index and not options.empty_interval and not options.move_state
                and bool(baseline_report.get("include_setup")) == bool(options.include_setup)
                and all(baseline_report["identities"].get(key) == sha256_file(path)
                    for key, path in (("runtime", native.dll), ("observer", native.replay_mod), ("framework", native.framework))))
            if options.resume_native_control:
                require_resumable_baseline(native)
                print("equivalence: retaining verified native control after automation fix", flush=True)
                reuse = True
            elif reuse:
                native.report = baseline_path
                print("equivalence: reusing sealed native baseline from this build", flush=True)
            elif not options.record_index:
                capture(native)
            summary["native_control_report"] = str(native.report) if reuse or not options.record_index else None
            summary["native_control_reused"] = reuse
            report = capture(executor)
            if options.index_seek and options.interactive_controls:
                from deterministic_qualification.replay_controls import validate_indexed_controls
                summary["indexed_playback_controls"]=validate_indexed_controls(report,
                    Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"))
            if report.get("host_index_checkpoint"):
                summary["host_index_checkpoint"]=validate_host_index_checkpoint(
                    Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),report["run_id"],options.stage=="index-cancel",
                    bool(report.get("host_index_checkpoint_arming")))
            if options.host_session:
                summary["host_session"]=validate_host_session_entry(Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"))
            if options.stage=="session-exit":
                summary["native_session_exit"]=validate_native_session_exit(
                    Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),report["run_id"])
            if options.index_seek and not options.index_recovery:
                # The indexed original is already available. Reject its first
                # restoration defect before spending another live native run.
                summary["same_process_hud"]=validate_indexed_same_process_hud(report)
            if options.index_preparation_fallback:
                summary["preparation_fallback"]=validate_index_preparation_fallback(
                    Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),report["run_id"],require_cancellation=True)
            # The sealed native baseline already established repeatability.
            # Do not launch another matched control if index admission or
            # completion fails in the candidate. Both still need to pass.
            if options.record_index and not reuse:
                capture(native)
                summary["native_control_report"] = str(native.report)
            if options.index_checkpoint:
                policies=[]
                for path in (native.report,executor.report):
                    captured=json.loads(path.read_text(encoding="utf-8"))
                    policies.append(validate_intro_skips(Path(captured["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),captured["run_id"]))
                if policies[0]!=policies[1]:raise RuntimeError("index checkpoint controls used different intro interventions")
                summary["intro_skip_policy"]=policies[0]
            summary.update(compare_passive_controls(native.report, executor.report,index_checkpoint=options.index_checkpoint))
            if options.record_index and options.stage not in ("index-cancel","session-exit"):
                native_report=json.loads(native.report.read_text(encoding="utf-8"))
                summary["tick_index"]=validate_tick_index(
                    Path(native_report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),
                    Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),
                    native_report["run_id"],report["run_id"],index_seek=options.index_seek,index_recovery=options.index_recovery)
                if options.index_recovery:
                    summary["indexed_recovery"]=compare_indexed_recovery(native.report,executor.report)
                elif options.index_seek:
                    summary["indexed_seek"]=compare_indexed_seek_suffix(native.report,executor.report)
                    if summary["indexed_seek"]["result"]!="pass":
                        raise RuntimeError("indexed seek resumed playback is below 58 native ticks per second")
                if options.index_checkpoint:
                    summary["index_checkpoint"]=validate_index_checkpoint_retention(
                        Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),report["run_id"],options.index_extra_checkpoint,options.index_preparation_fallback)

            if options.stage=="index-cancel":
                summary["index_cancellation"]=validate_held_index_cancellation(
                    Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),report["run_id"])

            summary["executor"] = executor_proof(report, options.yield_every_tick)
            if options.application_pause:
                raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
                summary["application_pause"] = validate_application_pause(raw, report["run_id"], consumer_task=options.consumer_task)
                summary["application_pause"]["resume_timing"] = interior_resume_timing(
                    raw, report["run_id"], held_tick=summary["application_pause"]["reentry"]["tick"], marker="application pause held")
                if summary["application_pause"]["resume_timing"]["tick_rate_milli"] < 58000:
                    raise RuntimeError("post-application-pause playback is below 58 native ticks per second")
            if options.consumer_task:
                raw=Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace")
                summary["consumer_suspension"]=validate_consumer_suspension(raw)
            if options.stage == "restore":
                raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
                summary["small_restore"] = validate_small_restore(raw, report["run_id"])
            if options.interior_pause:
                raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
                summary["interior_pause"] = validate_interior_pause(raw, report["run_id"])
                summary["interior_pause"]["resume_timing"] = interior_resume_timing(raw, report["run_id"])
                if options.interactive_controls:
                    summary["interactive_control"] = validate_interior_control(raw, report)
                if summary["interior_pause"]["resume_timing"]["tick_rate_milli"] < 58000:
                    raise RuntimeError("post-interior-pause playback is below 58 native ticks per second")
            if options.world_pause:
                raw = Path(report["raw_log"]["path"]).read_text(encoding="utf-8", errors="replace")
                started_pause = re.findall(r"world pause started run_id=(\S+) native_frame=(\d+)", raw)
                ended_pause = re.findall(r"world pause completed run_id=(\S+) native_frame=(\d+) elapsed_us=(\d+) "
                    r"viewport_frames=(\d+) held_updates=(\d+) unchanged=true resumed=true", raw)
                if (len(started_pause) != 1 or len(ended_pause) != 1
                        or started_pause[0] != ended_pause[0][:2] or started_pause[0][0] != report["run_id"]
                        or int(ended_pause[0][2]) < 1_000_000 or min(map(int, ended_pause[0][3:])) < 30):
                    raise RuntimeError("completed-world pause lacks stable state, responsive viewport, or resume evidence")
                summary["world_pause"] = {"elapsed_us": int(ended_pause[0][2]),
                    "viewport_frames": int(ended_pause[0][3]), "held_updates": int(ended_pause[0][4]),
                    "scope": "completed world boundary; interior world continuation remains unverified"}
                summary["world_pause"]["resume_timing"] = validate_world_resume_timing(raw, report["run_id"])
                if min(summary["world_pause"]["resume_timing"][key]
                       for key in ("tick_rate_milli", "viewport_fps_milli")) < 58000:
                    raise RuntimeError("post-pause playback is below 58 native ticks or viewport updates per second")
            if options.index_checkpoint:
                from deterministic_qualification.replay_fidelity import summarize_native_execution
                physical_text=Path(report["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace")
                physical_rows=(indexed_seek_boundary_streams(physical_text,report["run_id"],recovery=report.get("index_recovery",""))[0] if options.index_seek
                               else validate_boundary_capture(physical_text,report["run_id"]))
                physical={"executor":summary["executor"],
                          "native_boundary_counts":{"actor_tail":sum(row["phase"]=="actor_tail" for row in physical_rows)},
                          "native_execution_coverage":summarize_native_execution(physical_rows)}
                verify_executor_native_counts(physical)
                summary["executor_physical_tail_accounting"]=physical
            else:
                verify_executor_native_counts(summary)
            if options.empty_interval and (not summary.get("empty_interval_probe")
                    or summary["executor"]["zero_tick_intervals"] < 1):
                raise RuntimeError("empty-interval experiment lacks native and executor evidence")
            if options.move_state and (not summary.get("move_state_probe")
                    or summary["executor"]["move_state_ticks"] < 1):
                raise RuntimeError("move-state experiment lacks native and executor evidence")
            summary["scope"] = "replacement_executor_equivalence"
            if options.index_recovery:
                summary["scope"]="indexed cancellation with independent original/discarded execution; post-replay B has no authored continuation"
            elif options.stage == "index-cancel":
                summary["scope"] = "held index cancellation and independent resumed continuation; not complete indexing or seeking"
            elif options.stage == "session-exit":
                summary["scope"] = "retained-index native exit and independent observed prefix; not historical cancellation, full indexing or re-entry"
            elif options.stage == "restore":
                summary["scope"] = "native comparison after transactional restore of one repeat within the same pending task"
            elif options.application_pause:
                summary["scope"] = "native comparison after current-boundary scheduler backing replacement; historical restoration is unproven"
            summary["coverage"] = "demonstrated categories and remaining gaps are listed separately in execution_gate"
            if summary["result"] != "pass":
                raise RuntimeError("executor diverged from native playback; inspect the first mismatch")
        summary["result"] = ("captured" if options.candidate_only else
                             summary["execution_gate"]["result"] if options.stage == "execution-gate" else "pass")
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        summary.update(result="blocked" if summary.get("preflight", {}).get("result") in ("blocked", "unknown") else "fail", failure=str(error))
    finally:
        if options.stage in ("equivalence", "restore"):
            summary["experiment_result"] = summary["result"]
            summary["execution_gate"] = execution_gate_coverage(summary)
        summary["elapsed_seconds"] = round(time.monotonic() - started, 3)
        from deterministic_qualification.replay_evidence import retain_run
        summary["retained_evidence"] = retain_run(OUTPUT / "evidence", summary, CAPTURE_REPORTS)
        if (profile and profile['definition']['name'] == 'rolling30-gate'
                and summary['result'] == 'pass' and summary.get('acceptance_results', {}).get('simulation') == 'pass'):
            from deterministic_qualification.run_journal import atomic_json
            atomic_json(OUTPUT / 'rolling30-gate-receipt.json', summary['retained_evidence'])
        path = OUTPUT / f"{experiment_name}-stage.json"
        path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        publish(summary, summary["retained_evidence"], path)
    print(f"{options.stage}: {summary['result']} ({summary['elapsed_seconds']}s); {path}", flush=True)
    if "execution_gate" in summary:
        print("execution gate: " + summary["execution_gate"]["result"] + "; missing: "
              + ", ".join(summary["execution_gate"]["missing"]), flush=True)
    if "failure" in summary:
        print(summary["failure"], file=sys.stderr)
    return 0 if summary["result"] == "pass" or (options.candidate_only and summary["result"] == "captured") else 1


if __name__ == "__main__":
    raise SystemExit(main())
