"""Compare sampled native replay trajectories against uninterrupted playback.

This establishes seek neutrality at measured source frames. It does not turn a
mod-observed uninterrupted run into an independent stock-game oracle.
"""
from __future__ import annotations

import json
from pathlib import Path
import re

from .artifacts import sha256_file
from .report import write_report


TRAJECTORY_FIELDS = ("producer_inputs", "resolved_hit_calls", "p0_sim", "p0_step", "p0_render",
                     "p1_sim", "p1_step", "p1_render")


def valid_ground_lifecycle(row):
    return (re.fullmatch(r"[0-9a-f]{16}",row.get("ground_lifecycle","")) is not None
            and row.get("ground_roots","").isdigit() and int(row["ground_roots"])<=64
            and row.get("ground_next_id","").isdigit() and int(row["ground_next_id"])<=0x7fffffff)


def validate_payload_handoff(raw_text: str, run_id: str):
    lines = [line for line in raw_text.splitlines()
             if "[ReplayQualification] payload handoff " in line]
    if len(lines) != 1:
        raise RuntimeError("payload handoff needs exactly one observation")
    pairs = re.findall(r"\b(\w+)=([^\s]+)", lines[0])
    row = dict(pairs)
    hashes = ("battle_sha256", "expected_recording_sha256", "actual_recording_sha256")
    if (len(row) != len(pairs) or row.get("run_id") != run_id
            or row.get("source_equal") != "true"
            or any(re.fullmatch(r"[0-9a-f]{64}", row.get(key, "")) is None for key in hashes)
            or row["expected_recording_sha256"] != row["actual_recording_sha256"]
            or row.get("round") != "0" or row.get("cursor") != "0"):
        raise RuntimeError("payload handoff lacks matching source identity at playback entry")
    return {"battle_sha256": row["battle_sha256"],
            "recording_sha256": row["actual_recording_sha256"]}


def validate_recorded_outcome(raw_text: str, run_id: str, simulated_winner: int):
    lines = [line for line in raw_text.splitlines()
             if "[ReplayQualification] recorded match outcome " in line]
    if len(lines) != 1:
        raise RuntimeError("recorded outcome needs exactly one observation")
    pairs = re.findall(r"\b(\w+)=([^\s]+)", lines[0])
    row = dict(pairs)
    if (len(row) != len(pairs) or row.get("run_id") != run_id or row.get("equal") != "true"
            or row.get("recorded") not in ("0", "1")
            or row.get("simulated") != row["recorded"]
            or str(simulated_winner) != row["recorded"]):
        raise RuntimeError("recorded outcome disagrees with source or request")
    return {"recorded_match_winner": simulated_winner,
            "verification": "native_replay_summary_matches_simulated_result"}


def validate_source_extents(raw_text: str, run_id: str):
    rows = [dict(re.findall(r"\b(\w+)=([^\s]+)", line)) for line in raw_text.splitlines()
            if "[ReplayQualification] source extent " in line]
    if not rows:
        raise RuntimeError("native source extent evidence is missing")
    rounds = []
    try:
        count = int(rows[0]["rounds"])
        if not 1 <= count <= 16:
            raise ValueError()
        for row in rows:
            round_index, slot = int(row["round"]), int(row["slot"])
            samples, recorded = int(row["source_samples"]), int(row["recorded_time"])
            if (row["run_id"] != run_id or int(row["rounds"]) != count
                    or not 0 <= round_index < count or not 0 <= samples <= 16 * 1024 * 1024
                    or recorded not in (0, samples)):
                raise ValueError()
            if round_index == len(rounds):
                rounds.append([])
            if round_index != len(rounds) - 1 or slot != len(rounds[-1]) or slot >= 8:
                raise ValueError()
            rounds[-1].append(samples)
        if len(rounds) != count:
            raise ValueError()
    except (ValueError, KeyError, IndexError) as error:
        raise RuntimeError("native source extents are incomplete or invalid") from error
    return rounds


def validate_trajectory_timing(raw_text: str, run_id: str):
    rows = [dict(re.findall(r"\b(\w+)=([^\s]+)", line)) for line in raw_text.splitlines()
            if "[ReplayQualification] trajectory timing " in line]
    if len(rows) != 1 or rows[0].get("run_id") != run_id:
        raise RuntimeError("trajectory timing witness is missing or duplicated")
    try:
        result = {key: int(rows[0][key]) for key in ("viewport_frames", "native_ticks", "elapsed_us",
                  "viewport_fps_milli", "tick_rate_milli", "max_observation_gap_us", "gaps_over_20ms")}
        if result["elapsed_us"] <= 0 or any(value < 0 for value in result.values()):
            raise ValueError()
        for count, rate in (("viewport_frames", "viewport_fps_milli"), ("native_ticks", "tick_rate_milli")):
            if result[rate] != result[count] * 1_000_000_000 // result["elapsed_us"]:
                raise ValueError()
    except (KeyError, ValueError) as error:
        raise RuntimeError("invalid trajectory timing witness") from error
    return result


def validate_world_resume_timing(raw_text: str, run_id: str):
    lines = [line for line in raw_text.splitlines()
             if "[ReplayQualification] world resume timing " in line]
    if len(lines) != 1:
        raise RuntimeError("world resume needs exactly one timing witness")
    pairs = re.findall(r"\b(\w+)=([^\s]+)", lines[0])
    row = dict(pairs)
    try:
        result = {key: int(row[key]) for key in ("native_ticks", "elapsed_us", "viewport_frames",
                  "max_observation_gap_us", "gaps_over_20ms")}
        if (len(pairs) != len(row) or row.get("run_id") != run_id
                or result["native_ticks"] < 120 or result["elapsed_us"] <= 0
                or any(value < 0 for value in result.values())
                or result["max_observation_gap_us"] > result["elapsed_us"]):
            raise ValueError()
    except (KeyError, ValueError) as error:
        raise RuntimeError("invalid world resume timing witness") from error
    result["tick_rate_milli"] = result["native_ticks"] * 1_000_000_000 // result["elapsed_us"]
    result["viewport_fps_milli"] = result["viewport_frames"] * 1_000_000_000 // result["elapsed_us"]
    return result


def validate_initial_snapshot(raw_text: str, run_id: str):
    lines = [line for line in raw_text.splitlines()
             if "[ReplayQualification] initial round snapshot " in line]
    if len(lines) != 1:
        raise RuntimeError("initial snapshot needs exactly one observation")
    pairs = re.findall(r"\b(\w+)=([^\s]+)", lines[0])
    row = dict(pairs)
    # The first consumed-field capture predates the explicit version label;
    # its distinct consumed_equal/count fields carry the same complete data.
    if (len(row) != len(pairs) or row.get("run_id") != run_id or row.get("version", "1") != "1"
            or row.get("round") != "0" or row.get("cursor") != "0"
            or row.get("available") != "true" or row.get("consumed_equal") != "true"
            or row.get("raw_equal") not in ("true", "false")
            or row.get("expected_motion_count") not in tuple(map(str, range(17)))
            or row["expected_motion_count"] != row.get("actual_motion_count")
            or row.get("consumed_difference") != str(2**32 - 1)):
        raise RuntimeError("initial snapshot lacks bounded recorded/native round-start agreement")
    return {"scope": "cached_round_start_restore_fields", "version": 1,
            "raw_equal": row["raw_equal"] == "true",
            "motion_count": int(row["expected_motion_count"])}


def retained_source_range(text: str, identity: str):
    """Validate the distinct, explicitly incomplete native-tail protocol."""
    if "index supported range" not in text and "retained source stop complete" not in text:
        return None
    published=re.findall(r"index supported range run_id=(\S+) source_tick=(\d+) last_tick=(\d+) tail_ticks=(\d+) all_retained_ticks_indexed=true unsupported_native_tail=true",text)
    host=re.findall(r"retained source stop complete source_tick=(\d+) last_tick=(\d+) entries=(\d+) intervals=(\d+) empty=(\d+) multi=(\d+) bytes=(\d+) native_finish=false application_idle=true bindings_valid=true unsupported_native_tail=true",text)
    complete=re.findall(r"index supported complete run_id=(\S+) entries=(\d+) bytes=(\d+) intervals=(\d+) empty=(\d+) multi=(\d+) native_finish=false unsupported_native_tail=true",text)
    if (len(published)!=1 or len(host)!=1 or len(complete)!=1 or published[0][0]!=identity or complete[0][0]!=identity
            or published[0][1:3]!=host[0][:2] or complete[0][1:]!=(host[0][2],host[0][6],*host[0][3:6])
            or int(host[0][2])!=int(host[0][1])+1 or not 170<int(host[0][0])<int(host[0][1])<=int(host[0][0])+300
            or int(published[0][3])!=int(host[0][1])-int(host[0][0])
            or "native match finished run_id=" in text or "all_native_ticks_indexed=true" in text):
        raise RuntimeError("retained source range lacks consistent ownership and explicit unsupported tail")
    return {"source_tick":int(host[0][0]),"last_tick":int(host[0][1]),"entries":int(host[0][2]),
            "unsupported_native_tail":True,"full_native_replay":False}


def validate_trajectory_completion(raw_text: str, run_id: str, limit: int, full_match: bool):
    supported=retained_source_range(raw_text,run_id)
    rows = re.findall(r"trajectory completed run_id=(\S+) observations=(\d+) full_match=(true|false)", raw_text)
    if (len(rows) != 1 or rows[0][0] != run_id or rows[0][2] != str(full_match and not supported).lower()
            or not 120 <= int(rows[0][1]) <= limit
            or (not full_match and int(rows[0][1]) != limit)):
        raise RuntimeError("trajectory lacks matching bounded completion")
    result = {"observations": int(rows[0][1]), "full_match": full_match and not bool(supported)}
    if supported:
        endpoints=re.findall(r"retained replay endpoint run_id=(\S+) native_frame=(\d+) round=(\d+) cursor=(\d+) world_mode=5 round_state=5 source_active=false native_finish=false unsupported_native_tail=true",raw_text)
        if not full_match or len(endpoints)!=1 or endpoints[0][0]!=run_id or int(endpoints[0][1])!=supported["source_tick"]:
            raise RuntimeError("retained trajectory lacks its observed source-stop endpoint")
        outcomes=re.findall(r"ordered round outcomes verified rounds=(\d+) match_winner=([01]) winners=([01,]+)",raw_text)
        if len(outcomes)!=1 or len(outcomes[0][2].split(","))!=int(outcomes[0][0]) or int(endpoints[0][2])+1!=int(outcomes[0][0]):
            raise RuntimeError("retained trajectory lacks all recorded round outcomes")
        result.update(validate_recorded_outcome(raw_text,run_id,int(outcomes[0][1])))
        result.update(retained_range=supported,round_winners=list(map(int,outcomes[0][2].split(","))),
            retained_endpoint=dict(zip(("frame","round","cursor"),map(int,endpoints[0][1:]))),native_match_finished=False)
        return result
    if full_match:
        finished = re.findall(r"native match finished run_id=(\S+) boundary=ReplayBattleScene.OnFinishMatch.post", raw_text)
        if finished != [run_id]:
            raise RuntimeError("full trajectory lacks the native match-finish boundary; winner-known is insufficient")
        endpoints = re.findall(r"native replay endpoint run_id=(\S+) native_frame=(\d+) round=(\d+) cursor=(\d+) "
            r"world_mode=10 round_state=10 source_active=false outer_complete=true", raw_text)
        if len(endpoints) != 1 or endpoints[0][0] != run_id:
            raise RuntimeError("full trajectory lacks a completed native endpoint independent of UI timing")
        result["native_endpoint"] = dict(zip(("frame", "round", "cursor"), map(int, endpoints[0][1:])))
        result["native_match_finished"] = True
        outcomes = re.findall(r"ordered round outcomes verified rounds=(\d+) match_winner=([01]) winners=([01,]+)", raw_text)
        if len(outcomes) != 1 or len(outcomes[0][2].split(",")) != int(outcomes[0][0]):
            raise RuntimeError("full trajectory lacks ordered round outcomes")
        result.update(validate_recorded_outcome(raw_text, run_id, int(outcomes[0][1])))
        result["round_winners"] = list(map(int, outcomes[0][2].split(",")))
    return result


def validate_setup_capture(raw_text: str, run_id: str):
    starts = re.findall(r"setup started run_id=(\S+) native_frame=(\d+) source_equal=true", raw_text)
    ends = re.findall(r"setup completed run_id=(\S+) observations=(\d+) native_frame=(\d+)", raw_text)
    if len(starts) != 1 or len(ends) != 1 or starts[0][0] != run_id or ends[0][0] != run_id:
        raise RuntimeError("setup capture lacks unique identity and completion boundaries")
    rows = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in raw_text.splitlines()
            if "[ReplayQualification] setup ordinal=" in line]
    if not rows or len(rows) != int(ends[0][1]) or len(rows) > 36000:
        raise RuntimeError("setup capture has invalid observation bounds")
    required = ("frame", "round", "cursor", "input_round", "input_time", "round_frame", "inputs",
                "p0_sim", "p0_step", "p0_render", "p1_sim", "p1_step", "p1_render",
                "p0_vital", "p1_vital", "p0_moves", "p1_moves", "manager_phase", "move_state", "round_state")
    for ordinal, row in enumerate(rows, 1):
        if (row.get("ordinal") != str(ordinal) or row.get("phase") != "engine_post"
                or row.get("source_active") != "false" or row.get("sample_version") not in ("2", "3", "4", "5")
                or (row.get("sample_version") in ("3", "4", "5") and not {"mt_cursor", "mt_hash"} <= row.keys())
                or (row.get("sample_version") in ("4", "5") and not re.fullmatch(r"[0-9a-f]{8}", row.get("crt_state", "")))
                or (row.get("sample_version") == "5" and not valid_ground_lifecycle(row))
                or any(key not in row for key in required)):
            raise RuntimeError("setup capture has a missing or invalid observation")
    if rows[0]["frame"] != starts[0][1] or int(rows[-1]["frame"]) >= int(ends[0][2]):
        raise RuntimeError("setup capture clocks disagree with its boundaries")
    return rows


def particle_lifetime_recovery(report):
    return (report.get("seek_advance_failure") and not report.get("seek_settlement_failure")
            and report.get("historical_anchor_tick",205)==205 and report.get("historical_advanced_tick",210)==210
            and report.get("host_seek_target")==220 and report.get("historical_cancel")=="after")


def particle_lifetime_abort_tick(text):
    rows=re.findall(r"seek protected particle lifetime abort component=([0-9a-f]+) routes=(\d+) tick=(\d+) target=220 B=210 before_native_lifecycle=true B_retained=true commit_decided=false",text)
    if len(rows)!=1 or not int(rows[0][0],16) or int(rows[0][1]) not in (1,2,3) or not 205<int(rows[0][2])<=220:
        raise RuntimeError("lifetime recovery lacks one bounded pre-native veto receipt")
    return int(rows[0][2])


def expected_recovery_rewind(report, raw_text=None):
    if report.get("consumer_mutation"):
        consumer_mutation_abort_tick(raw_text or "")
        return None  # C<=B, so B restoration does not rewind simulation time.
    if particle_lifetime_recovery(report):
        end=particle_lifetime_abort_tick(raw_text or "")
        return (end,210,220) if end>210 else None
    origin=report.get("historical_advanced_tick",210)
    target=report.get("host_seek_target",208)
    if (report.get("seek_settlement_failure") and report.get("seek_advance_failure")
            and report.get("historical_cancel")=="after" and target>origin):
        return target,origin
    return None


def private_hud_drained_case(report):
    return (report.get("historical_anchor_tick")==170 and report.get("historical_advanced_tick")==300
            and report.get("host_seek_target")==300 and report.get("host_seek")
            and report.get("historical_cancel")=="after" and report.get("seek_advance_failure")
            and report.get("seek_settlement_failure") and report.get("seek_drained_cancel")
            and not any(report.get(k) for k in ("corrected_inputs","changed_inputs","source_revision",
                "host_seek_repeat","seek_observer_failure","seek_preparation_failure")))


def trace_render_recovery_case(report):
    return (report.get("historical_anchor_tick")==170 and report.get("historical_advanced_tick") in (329,5695,6538)
            and report.get("host_seek_target")==2510 and report.get("host_seek")
            and report.get("historical_cancel")=="after" and report.get("seek_advance_failure")
            and report.get("seek_settlement_failure") and report.get("seek_drained_cancel")
            and not any(report.get(k) for k in ("corrected_inputs","changed_inputs","source_revision",
                "host_seek_repeat","seek_observer_failure","seek_preparation_failure")))


def bounded_checkpoint_window(report):
    """Short unchanged-input transaction, optionally cancelled before/after publication."""
    a=report.get("historical_anchor_tick",0); b=report.get("historical_advanced_tick",0)
    target=report.get("host_seek_target",0)
    return ((not report.get("consumer_mutation") or (a,b,target,report.get("historical_cancel"))==(210,217,217,"after"))
        and all(type(x) is int for x in (a,b,target)) and ((a==210 and b==217) or 221<=a<b<=35000)
        and b-a<=120 and a<=target<=b and report.get("host_seek")
        and report.get("historical_exact_advance")
        and report.get("historical_cancel", "") in ("", "before", "after")
        and not any(report.get(k) for k in ("seek_advance_failure",
            "seek_settlement_failure","seek_drained_cancel","corrected_inputs","changed_inputs",
            "source_revision","host_seek_repeat","host_seek_first_target","checkpoint_pair",
            "checkpoint_fallback","capture_cancel","restore_reuse","historical_single_step",
            "completion_repeat","seek_observer_failure","seek_preparation_failure",
            "pixel_diagnostics","pass_diagnostics","same_process_render")))


def consumer_mutation_abort_tick(text):
    injected="consumer prerequisite mutation injected count=1 native_api=141D58ED0 production_entry_returned=true repair=false"
    rows=list(re.finditer(r"consumer prerequisite changed tick=(\d+) epoch=(\d+) owner_index=(\d+) owner_generation=(\d+) native_completed=true application_complete=true B_retained=true recovery=false",text))
    observed=list(re.finditer(r"consumer mutation failure observed run_id=\S+ tick=(\d+) undo_tick=217 pending_task=false complete_B=true",text))
    if (text.count(injected)!=1 or len(rows)!=1 or len(observed)!=1
            or not 210<int(rows[0][1])<=217 or any(int(rows[0][i])<=0 for i in (2,4))
            or rows[0][1]!=observed[0][1] or not text.index(injected)<rows[0].start()<observed[0].start()):
        raise RuntimeError("consumer recovery lacks unique native mutation, completed task and application witnesses")
    return int(rows[0][1])


def ground_commit_case(report):
    return (report.get("historical_anchor_tick")==170 and report.get("historical_advanced_tick")==6538
            and report.get("host_seek_target")==2510 and report.get("host_seek")
            and not any(report.get(k) for k in ("historical_cancel","seek_advance_failure",
                "seek_settlement_failure","seek_drained_cancel","corrected_inputs","changed_inputs",
                "source_revision","host_seek_repeat","seek_observer_failure","seek_preparation_failure")))


def execution_fallback_case(report):
    return (report.get("checkpoint_fallback") and report.get("historical_anchor_tick")==170
            and report.get("historical_advanced_tick")==210 and report.get("host_seek_target")==220
            and not any(report.get(k) for k in ("historical_cancel","host_seek_repeat","seek_advance_failure",
                                               "seek_settlement_failure","corrected_inputs","changed_inputs")))


def execution_fallback_receipt(text, identity):
    end=particle_lifetime_abort_tick(text)
    if not 210<end<=220:
        raise RuntimeError("execution fallback lacks a bounded native lifetime failure")
    pattern=(r"seek execution fallback rejected=205 selected=170 target=220 B=210 discarded_ticks=(\d+) "
             r"discarded_intervals=(\d+) B_recovered=true transaction_released=true publication=true unchanged_input_history=true")
    switches=list(re.finditer(pattern,text))
    if len(switches)!=1 or int(switches[0][1])!=end-205 or int(switches[0][2])<=0:
        raise RuntimeError("execution fallback lost discarded native work or complete B release")
    observed=(f"execution fallback B verified run_id={identity} tick=210 checkpoint=170 target=220 "
              f"discarded_ticks={end-205} discarded_intervals={switches[0][2]} independent_original_equal=true commit_decided=false")
    owners=(f"seek ownership state=Recovered B_tick=210 target=220 current_tick=210 completed_tail_tick={end} "
            f"B_retained=false commit_decided=false executed_ticks={end-205} executed_intervals={switches[0][2]}")
    if text.count(observed)!=1 or text.count(owners)!=1 or not text.index(owners)<switches[0].start()<text.index(observed):
        raise RuntimeError("execution fallback lacks ordered ownership and independent B observation")
    return {"completed_tick":end,"discarded_ticks":end-205,"discarded_intervals":int(switches[0][2]),
            "switch_position":switches[0].start(),"recovery_marker":observed}


def validate_boundary_capture(raw_text: str, run_id: str, *, historical_restore=False,
                              historical_advanced_tick=210, expected_rewinds=None, retained_execution=False,
                              recovery_rewind=None, execution_retry=False):
    # The caller supplies the experiment's exact traversal plan. Never infer
    # permitted rewinds from the candidate's own commit log.
    rewinds = ([(historical_advanced_tick, 205)] if historical_restore else []) if expected_rewinds is None else list(expected_rewinds)
    if (bool(rewinds) != bool(historical_restore) or len(rewinds) > 1024
            or any(not isinstance(origin, int) or not isinstance(target, int)
                   or not 0 <= target <= origin for origin, target in rewinds)):
        raise RuntimeError("invalid expected historical traversal plan")
    starts = re.findall(r"boundaries started run_id=(\S+) native_frame=(\d+)", raw_text)
    ends = re.findall(r"boundaries completed run_id=(\S+) observations=(\d+) detached=true", raw_text)
    if not starts and not ends:
        if historical_restore:
            raise RuntimeError("historical restoration requires independent native boundary observations")
        return []  # Historical EngineTickPost-only evidence remains readable.
    if len(starts) != 1 or len(ends) != 1 or starts[0][0] != run_id or ends[0][0] != run_id:
        raise RuntimeError("native boundaries lack unique activation and cleanup")
    positioned_rows = [(position, dict(re.findall(r"(\w+)=([^\s]+)", line)))
                       for position, line in enumerate(raw_text.splitlines())
                       if "[ReplayQualification] boundary ordinal=" in line]
    rows = [row for _, row in positioned_rows]
    if not 0 < len(rows) <= 200000 or len(rows) != int(ends[0][1]):
        raise RuntimeError("native boundary capture has invalid observation bounds")
    phases = {"callback_720", "callback_870", "callback_8e0", "callback_950",
              "callback_a30", "callback_b80", "callback_f70", "round_sequence", "actor_tail",
              "input_cache_publication"}
    required = {"frame", "round", "cursor", "input_round", "input_time", "round_frame", "inputs",
                "p0_sim", "p0_step", "p0_render", "p1_sim", "p1_step", "p1_render",
                "p0_vital", "p1_vital", "p0_moves", "p1_moves", "source_active",
                "manager_phase", "move_state", "round_state", "published_count", "published_pairs"}
    previous_frame = int(starts[0][1])
    rewind_count = 0
    marker = (r"historical combat execution admitted run_id=(\S+) from_tick=(\d+) to_tick=(\d+) B_retained=true"
              if retained_execution else r"historical combat committed run_id=(\S+) from_tick=(\d+) to_tick=(\d+)")
    commits = [(position, match.groups()) for position, line in enumerate(raw_text.splitlines())
               if (match := re.search(marker, line))]
    if ([(int(fields[1]), int(fields[2])) for _, fields in commits] != rewinds
            or any(fields[0] != run_id for _, fields in commits)):
        raise RuntimeError("historical boundary capture lacks the exact admitted transaction sequence")
    # A zero-distance transaction after a zero-traversal landing is explicit
    # ownership evidence, not another native callback rewind. Require the same
    # target and no intervening callback before collapsing that marker pair.
    for index in reversed(range(len(rewinds))):
        origin,target=rewinds[index]
        if origin!=target: continue
        if (not retained_execution or index==0 or rewinds[index-1][1]!=target
                or any(commits[index-1][0]<position<commits[index][0] for position,_ in positioned_rows)):
            raise RuntimeError("equal-tick restoration lacks a zero-traversal preceding landing")
        del rewinds[index]
        del commits[index]
    if recovery_rewind is not None:
        if (not retained_execution or len(rewinds)!=1 or len(recovery_rewind) not in (2,3)
                or recovery_rewind[1]!=rewinds[0][0] or recovery_rewind[0]<=recovery_rewind[1]):
            raise RuntimeError("invalid expected B recovery traversal plan")
        completed,original=recovery_rewind[:2]
        requested=recovery_rewind[2] if len(recovery_rewind)==3 else completed
        recovery_marker=f"historical cancellation recovered run_id={run_id} point=after tick={original}"
        recovery=[i for i,line in enumerate(raw_text.splitlines()) if recovery_marker in line]
        owner_marker=(f"seek ownership state=Recovered B_tick={original} target={requested} current_tick={original} "
                      f"completed_tail_tick={completed} B_retained=false commit_decided=false "
                      f"executed_ticks={completed-rewinds[0][1]} ")
        owners=[i for i,line in enumerate(raw_text.splitlines()) if owner_marker in line]
        if len(recovery)!=1 or len(owners)!=1 or not commits[0][0]<owners[0]<recovery[0]:
            raise RuntimeError("B recovery rewind lacks completed ownership and observer witnesses")
        rewinds.append(tuple(recovery_rewind[:2]))
        commits.append((recovery[0],(run_id,str(completed),str(original))))
    if execution_retry:
        if recovery_rewind is not None or not retained_execution or rewinds!=[(210,205),(210,170)]:
            raise RuntimeError("invalid expected execution fallback traversal plan")
        receipt=execution_fallback_receipt(raw_text,run_id)
        recovery=[i for i,line in enumerate(raw_text.splitlines()) if receipt["recovery_marker"] in line]
        if len(recovery)!=1 or not commits[0][0]<recovery[0]<commits[1][0]:
            raise RuntimeError("execution fallback recovery is outside its two publications")
        if any(recovery[0]<position<commits[1][0] for position,_ in positioned_rows):
            rewinds.insert(1,(receipt["completed_tick"],210))
            commits.insert(1,(recovery[0],(run_id,str(receipt["completed_tick"]),"210")))
        else:
            # B was installed and independently observed, but not executed.
            # The next callee record directly follows discarded C with A170.
            rewinds[1]=(receipt["completed_tick"],170)
    previous_position = -1
    for ordinal, (position, row) in enumerate(positioned_rows, 1):
        if not row.get("frame", "").isdigit():
            raise RuntimeError("native boundary frame is missing or invalid")
        rewind = int(row["frame"]) < previous_frame
        permitted_rewind = (rewind_count < len(rewinds)
                            and previous_frame == rewinds[rewind_count][0]
                            and int(row["frame"]) in (rewinds[rewind_count][1], rewinds[rewind_count][1] + 1)
                            and previous_position < commits[rewind_count][0] < position)
        if (row.get("ordinal") != str(ordinal) or row.get("phase") not in phases
                or row.get("sample_version") not in ("2", "3", "4", "5")
                or (row.get("sample_version") in ("3", "4", "5") and not {"mt_cursor", "mt_hash"} <= row.keys()) or not required <= row.keys()
                or (row.get("sample_version") in ("4", "5") and not re.fullmatch(r"[0-9a-f]{8}", row.get("crt_state", "")))
                or (row.get("sample_version") == "5" and not valid_ground_lifecycle(row))
                or (rewind and not permitted_rewind)):
            raise RuntimeError("native boundary sequence is missing or invalid")
        rewind_count += rewind
        previous_frame = int(row["frame"])
        previous_position = position
    if rewind_count != len(rewinds):
        raise RuntimeError("historical capture must contain every planned verified rewind")
    return rows


def indexed_seek_boundary_streams(text: str, identity: str, *, recovery: str = ""):
    """One explicit full-index -> A170 -> requested target/suffix, including all tails."""
    if "indexed sequence case run_id=" in text:
        if recovery:raise RuntimeError("indexed sequence cancellation protocol is not implemented")
        from .indexed_sequence import boundary_streams
        return boundary_streams(text,identity)

    supported=retained_source_range(text,identity)
    begin=re.findall(r"indexed seek begin run_id=(\S+) origin=(\d+) target=(\d+) "+("supported_index" if supported else "full_index")+r"=true host_checkpoint=true callbacks=(\d+) interval=(\d+)",text)
    closure=re.findall(r"index closure run_id=(\S+) replay_tick=(\d+) native_tick=(\d+) callbacks_retained=true",text)
    supported=retained_source_range(text,identity)
    complete=re.findall(r"index "+("supported complete" if supported else "complete")+r" run_id=(\S+) entries=(\d+) ",text)
    retained=re.findall(r"index retained range run_id=(\S+) source_tick=(\d+) last_tick=(\d+) tail_ticks=(\d+) all_native_ticks_indexed=true",text)
    host_retained=re.findall(r"index retained coverage source_tick=(\d+) last_tick=(\d+) tail_ticks=(\d+) entries=(\d+) all_native_ticks_indexed=true",text)
    retained_protocol="index retained range" in text or "index retained coverage" in text
    if retained_protocol and (len(retained)!=1 or len(host_retained)!=1 or len(closure)!=1 or len(complete)!=1
            or retained[0][0]!=identity or retained[0][1:3]!=closure[0][1:3]
            or retained[0][1:]!=host_retained[0][:3] or complete[0][1]!=host_retained[0][3]
            or int(retained[0][3])!=int(retained[0][2])-int(retained[0][1])):
        raise RuntimeError("indexed seek retained range witnesses are missing or inconsistent")
    if (len(begin)!=1 or len(closure)!=1 or len(complete)!=1
            or any(row[0]!=identity for row in (begin[0],closure[0],complete[0]))
            or int(complete[0][1])!=int(closure[0][2 if retained_protocol or supported else 1])+1
            or begin[0][1]!=closure[0][2]
            or not 170<int(closure[0][1])<=int(begin[0][1])<=int(closure[0][1])+300):
        raise RuntimeError("indexed seek lacks its full native completion and bounded B origin")
    if supported and (int(closure[0][1])!=supported["source_tick"] or int(closure[0][2])!=supported["last_tick"]):
        raise RuntimeError("indexed seek disagrees with its retained supported range")
    origin=int(begin[0][1]);target=int(begin[0][2]);split=int(begin[0][3])
    selected=re.findall(r"indexed checkpoint selected run_id=(\S+) tick=(\d+) extra=(\d+) automatic=true",text)
    checkpoint=170
    if selected:
        if len(selected)!=1 or selected[0][0]!=identity:raise RuntimeError("indexed checkpoint selection is ambiguous")
        checkpoint=int(selected[0][1]);extra=int(selected[0][2])
        inventory=[int(tick) for tick in re.findall(r"index checkpoint owned tick=(\d+) retained=\d+ owned_bytes=",text)]
        managed=("later-middle index checkpoint selected " in text
            or len(re.findall(r"next-round index checkpoint selected ",text))>=2)
        if managed and (not 3<=len(inventory)<=16 or inventory!=sorted(set(inventory)) or inventory[0]!=170):
            raise RuntimeError("managed checkpoint inventory is incomplete")
        if (not 170<=checkpoint<=target or (managed and (checkpoint not in inventory or extra not in (0,inventory[-1])))
                or (not managed and (checkpoint not in (170,extra) or (extra and not 170<extra<target)))):
            raise RuntimeError("indexed checkpoint selection is outside the retained plan")
        if f"index checkpoint owned tick={checkpoint} " not in text:raise RuntimeError("indexed selection lacks retained owner")
    if recovery:
        proof=validate_indexed_recovery(text,identity,recovery,origin,checkpoint,target)
        rewinds=[(origin,checkpoint)] if recovery in ("drained","interior") else []
        rows=validate_boundary_capture(text,identity,historical_restore=bool(rewinds),expected_rewinds=rewinds,retained_execution=True)
        if not 0<split<=len(rows) or int(rows[split-1]["frame"])!=origin:
            raise RuntimeError("indexed recovery lacks original callback boundary")
        recovered_position=text.index(f"indexed recovery recovered run_id={identity} ")
        end=len(re.findall(r"\[ReplayQualification\] boundary ordinal=",text[:recovered_position]))
        discarded=rows[split:end]
        if recovery=="published":
            if discarded or proof["executed_ticks"] or proof["executed_intervals"]:
                raise RuntimeError("publication cancellation unexpectedly executed native work")
        elif (not discarded or int(discarded[0]["frame"]) not in (checkpoint,checkpoint+1)
                or int(discarded[-1]["frame"])!=target+(recovery=="interior") or discarded[-1]["phase"]!="actor_tail"
                or proof["executed_ticks"]!=target+(recovery=="interior")-checkpoint
                or proof["executed_intervals"]!=sum(r["phase"]=="actor_tail" for r in discarded)):
            raise RuntimeError("drained recovery work disagrees with native callbacks")
        if rows[end:]:
            raise RuntimeError("indexed recovery has unplanned post-B traversals")
        return rows,rows[:split],discarded,{"origin":origin,"interval":int(begin[0][4]),"checkpoint":checkpoint,
            "target":target,"discarded_end_tick":target+(recovery=="interior"),"last_tick":origin,"continuation_ticks":0,"prefix_callbacks":split,"recovery":proof}
    rows=validate_boundary_capture(text,identity,historical_restore=True,expected_rewinds=[(origin,checkpoint)],retained_execution=True)
    if not 0<split<len(rows) or int(rows[split-1]["frame"])!=origin or int(rows[split]["frame"]) not in (checkpoint,checkpoint+1):
        raise RuntimeError("indexed seek boundary split does not bracket the actual rewind")
    completions=re.findall(r"indexed seek continuation run_id=(\S+) first=(\d+) last=(\d+) ticks=(\d+)",text)
    if len(completions)!=1:raise RuntimeError("indexed seek lacks its unique completed suffix")
    identity_c,first,last,count=completions[0];first,last,count=map(int,(first,last,count))
    if identity_c!=identity or first!=target+1 or last!=target+count or not 120<=count<=600 or not 170<=target<last<=int(closure[0][1]):
        raise RuntimeError("indexed seek continuation window is invalid")
    completion=f"indexed seek continuation run_id={identity} first={first} last={last} ticks={count}"
    # Native execution continues until observer detachment/cleanup. Preserve
    # those observations for total execution accounting, but the independently
    # compared suffix ends at the completed actor tail witnessed by this marker.
    before_completion=text[:text.index(completion)]
    completed_count=len(re.findall(r"\[ReplayQualification\] boundary ordinal=",before_completion))
    suffix=rows[split:completed_count]
    if (any(int(row["frame"])>origin for row in rows[:split])
            or not suffix or any(int(row["frame"])>last for row in suffix)):
        raise RuntimeError("indexed seek has unplanned native traversals")
    if int(suffix[-1]["frame"])!=last or suffix[-1]["phase"]!="actor_tail":
        raise RuntimeError("indexed seek lacks completed final application observations")
    return rows,rows[:split],suffix,{"origin":origin,"interval":int(begin[0][4]),"checkpoint":checkpoint,"target":target,"last_tick":last,"continuation_ticks":count,
        "prefix_callbacks":split,"post_completion_callbacks":len(rows)-completed_count}


def validate_indexed_recovery(text: str, identity: str, point: str, origin: int, checkpoint: int, target: int) -> dict:
    """Cancellation at an endpoint B: no authored continuation is available there."""
    if point not in ("published","drained","interior") or not checkpoint<target<origin:
        raise RuntimeError("invalid indexed recovery request")
    def unique(pattern):
        matches=list(re.finditer(pattern,text))
        if len(matches)!=1 or matches[0][1]!=identity:
            raise RuntimeError("indexed recovery lacks unique ordered evidence")
        return matches[0]
    armed=unique(r"indexed recovery armed run_id=(\S+) point=(\w+) B=(\d+) A=(\d+) target=(\d+) complete_B=true commit_decided=false")
    recovered=unique(r"indexed recovery recovered run_id=(\S+) B=(\d+) executed_ticks=(\d+) executed_intervals=(\d+) original_recovered=true commit_decided=false")
    held=unique(r"indexed recovery held run_id=(\S+) B=(\d+) elapsed_us=(\d+) frames=(\d+) callbacks_unchanged=true epoch_unchanged=true HUD_unchanged=true")
    released=unique(r"indexed recovery released run_id=(\S+) B=(\d+) release_completed=true authored_ticks_remaining=0 resume_requested=false cleanup=native_exit")
    complete=unique(r"indexed recovery complete run_id=(\S+) B=(\d+) native_tick=(\d+) callbacks=(\d+) HUD_unchanged=true authored_continuation_unavailable=true resume_requested=false cleanup=native_exit B_observed_before_native_exit=true")
    if (armed[2]!=point or tuple(map(int,armed.group(3,4,5)))!=(origin,checkpoint,target)
            or any(int(m[2])!=origin for m in (recovered,held,released,complete))
            or not armed.start()<recovered.start()<held.start()<released.start()<complete.start()
            or int(held[3])<500000 or int(held[4])<30 or int(complete[3])!=origin):
        raise RuntimeError("indexed recovery coordinates, hold or ordering disagree")
    exit_rows=list(re.finditer(r"native replay exit cleanup completed requested_tick=(\d+) recovered_tick=(\d+) index_empty=true host_detached=true native_stop_invocations=1",text))
    exit_complete=unique(r"native session exit completed run_id=(\S+) observed_prefix_tick=(\d+) elapsed_us=(\d+) native_terminate_observed=true scene=replay_list executor_disabled=true host_inactive=true")
    if (len(exit_rows)!=1 or tuple(map(int,exit_rows[0].group(1,2)))!=(origin,origin)
            or not released.start()<exit_rows[0].start()<exit_complete.start()<complete.start()
            or int(exit_complete[3])<=0):
        raise RuntimeError("indexed recovery lacks completed native exit from held B")
    drained=list(re.finditer(r"indexed recovery drained run_id=(\S+) C=(\d+) B=(\d+) participant=injected_render_drained complete_B=true commit_decided=false",text))
    if point=="drained":
        if (len(drained)!=1 or drained[0][1]!=identity or tuple(map(int,drained[0].group(2,3)))!=(target,origin)
                or not armed.start()<drained[0].start()<recovered.start()):
            raise RuntimeError("indexed recovery did not cancel at Render::Drained")
    elif drained:
        raise RuntimeError("publication cancellation reached render settlement")
    if point=="interior":
        paused=unique(r"completion repeat held run_id=(\S+) tick=(\d+) elapsed_us=(\d+) application_updates=(\d+) surface_frames=(\d+) unchanged=true pending_task=true B_retained=true callbacks_unchanged=true release_requires_resume=true")
        queued=unique(r"indexed recovery interior queued run_id=(\S+) C=(\d+) B=(\d+) latest_request=B pending_repeat=true explicit_resume=false")
        consumed=list(re.finditer(r"indexed seek already held tick=(\d+)",text))
        owners=list(re.finditer(r"seek ownership state=Recovered B_tick=(\d+) target=(\d+) current_tick=(\d+) completed_tail_tick=(\d+) B_retained=false commit_decided=false executed_ticks=(\d+) executed_intervals=(\d+)",text))
        if (int(paused[2])!=target or int(paused[3])<500000 or int(paused[4])<30 or int(paused[5])<20
                or tuple(map(int,queued.group(2,3)))!=(target,origin)
                or len(consumed)!=1 or int(consumed[0][1])!=origin or len(owners)!=1
                or tuple(map(int,owners[0].group(1,2,3,4)))!=(origin,target,origin,target+1)
                or tuple(map(int,owners[0].group(5,6)))!=tuple(map(int,recovered.group(3,4)))
                or not armed.start()<paused.start()<queued.start()<owners[0].start()<consumed[0].start()<recovered.start()
                or "seek ownership state=Committing " in text or "seek ownership state=CompletingResumedTails " in text):
            raise RuntimeError("interior replacement lacks exact paused admission, B recovery or queue consumption")
    samples=list(re.finditer(r"index_recovery_B_(before|after) ordinal=1 phase=engine_post (sample_version=[^\r\n]+)",text))
    hud=list(re.finditer(r"indexed recovery HUD run_id=(\S+) point=(\w+) actor=(\d+/\d+) widget=(\d+/\d+) requested=(\d+) visibility=(\d+) values=([\d,]+) read_only=true",text))
    if (len(samples)!=2 or [m[1] for m in samples]!=["before","after"] or samples[0][2]!=samples[1][2]
            or len(hud)!=3 or any(m[1]!=identity for m in hud) or [m[2] for m in hud]!=["B_before","A_published","B_after"]
            or hud[0].group(3,4,5,6,7)!=hud[2].group(3,4,5,6,7)
            or hud[0].group(3,4)!=hud[1].group(3,4) or hud[0].group(5,6)!=("0","1")
            or hud[1].group(5,6)!=("1","3")
            or not samples[0].start()<hud[0].start()<hud[1].start()<armed.start()<samples[1].start()<hud[2].start()<recovered.start()):
        raise RuntimeError("indexed recovery independent B/HUD observations disagree")
    announcements=list(re.finditer(r"indexed recovery announcement run_id=(\S+) point=(\w+) widget=(\d+/\d+) enabled=(\d+) visibility=(\d+) active=(\d+) clock=([\d,]+) players=([\d,]+) native_viewport=true read_only=true",text))
    if (len(announcements)!=3 or any(m[1]!=identity for m in announcements)
            or [m[2] for m in announcements]!=["B_before","A_published","B_after"]
            or announcements[0].group(3,4,5,6,7,8)!=announcements[2].group(3,4,5,6,7,8)
            or announcements[0].group(3,4,6,7,8)!=announcements[1].group(3,4,6,7,8)
            or announcements[0][5] not in ("0","3","4") or announcements[1][5]!="1"
            or announcements[0][4] not in ("0","1")
            or not hud[0].start()<announcements[0].start()<hud[1].start()<announcements[1].start()<armed.start()
                <hud[2].start()<announcements[2].start()<recovered.start()):
        raise RuntimeError("indexed recovery independent announcement B observations disagree")
    active=int(announcements[0][6])
    clock=[int(v) for v in announcements[0][7].split(',') if v]
    players=[int(v) for v in announcements[0][8].split(',') if v]
    if (not 1<=active<=4 or len(clock)!=4 or len(players)!=18*active
            or any(players[i*18+16] or players[i*18+17] for i in range(active))):
        raise RuntimeError("indexed recovery announcement player coverage is incomplete")
    return {"result":"pass","point":point,"B":origin,"executed_ticks":int(recovered[3]),
        "executed_intervals":int(recovered[4]),"hold_us":int(held[3]),"hold_updates":int(held[4]),
        "announcement_B":{"result":"pass","active_players":active,"native_viewport":True,"clocks_unchanged":True},
        "cleanup_route":"native replay exit after observed held-B recovery; no playback resume",
        "scope":"same-process B gameplay/HUD/announcement recovery and release; zero authored B continuation ticks remain",
        "pixel_equality_required":False,"rollback_performance_qualified":False}



def summarize_native_execution(rows: list[dict]) -> dict:
    """Derive interval/input coverage from native callee observations only."""
    observed_publications = any(row["phase"] == "input_cache_publication" for row in rows)
    counts = {"intervals": 0, "tick_callbacks": 0, "zero_tick_intervals": 0,
              "multi_tick_intervals": 0, "move_state_3_ticks": 0,
              "input_cache_publications": 0, "repeated_ticks_without_new_publication": 0}
    interval_ticks = 0
    have_publication = used_publication = False
    move_path_open = False
    for row in rows:
        phase = row["phase"]
        if phase == "callback_950":
            move_path_open = True
        elif phase == "callback_870":
            move_path_open = False
        elif phase == "input_cache_publication":
            if have_publication and not used_publication:
                raise RuntimeError("native input publication has no intervening simulation callback")
            counts["input_cache_publications"] += 1
            have_publication, used_publication = True, False
        elif phase == "callback_a30":
            interval_ticks += 1
            counts["tick_callbacks"] += 1
            if move_path_open and row["move_state"] == "3":
                counts["move_state_3_ticks"] += 1
            elif observed_publications:
                if not have_publication:
                    raise RuntimeError("native simulation callback lacks a consumed input publication")
                counts["repeated_ticks_without_new_publication"] += used_publication
                used_publication = True
        elif phase == "actor_tail":
            if move_path_open:
                raise RuntimeError("native interval ends inside move-state handling")
            if have_publication and not used_publication:
                raise RuntimeError("native interval ends with an unconsumed input publication")
            counts["intervals"] += 1
            counts["zero_tick_intervals"] += interval_ticks == 0
            counts["multi_tick_intervals"] += interval_ticks > 1
            interval_ticks = 0
            have_publication = used_publication = False
    if interval_ticks or have_publication or move_path_open:
        raise RuntimeError("native execution coverage ends inside an interval")
    counts["publication_observer_present"] = observed_publications
    if not observed_publications:
        counts["input_cache_publications"] = None
        counts["repeated_ticks_without_new_publication"] = None
    return counts


def validate_empty_interval(raw_text: str, run_id: str, requested: bool):
    starts = re.findall(r"empty interval started run_id=(\S+) native_frame=(\d+)", raw_text)
    ends = re.findall(r"empty interval completed run_id=(\S+) native_frame=(\d+) unchanged=true", raw_text)
    if not requested:
        if starts or ends:
            raise RuntimeError("uninterrupted capture contains an unrequested empty interval")
        return None
    if len(starts) != 1 or ends != starts or starts[0][0] != run_id:
        raise RuntimeError("empty interval lacks matching unchanged native clocks")
    span = raw_text.split("empty interval started ", 1)[1].split("empty interval completed ", 1)[0]
    rows = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in span.splitlines()
            if "[ReplayQualification] boundary ordinal=" in line]
    if len(rows) != 1 or rows[0].get("phase") != "actor_tail" or rows[0].get("frame") != starts[0][1]:
        raise RuntimeError("empty interval must execute exactly one native actor tail and no traversal")
    return {"native_frame": int(starts[0][1]), "native_actor_tails": 1}


def validate_move_state_interval(raw_text: str, run_id: str, requested: bool):
    starts = re.findall(r"move state interval started run_id=(\S+) native_frame=(\d+) requested_state=3", raw_text)
    ends = re.findall(r"move state interval completed run_id=(\S+) native_frame=(\d+) source_unchanged=true state=0", raw_text)
    if not requested:
        if starts or ends:
            raise RuntimeError("uninterrupted capture contains an unrequested move-state interval")
        return None
    if (len(starts) != 1 or len(ends) != 1 or starts[0][0] != run_id or ends[0][0] != run_id
            or int(ends[0][1]) != int(starts[0][1]) + 1
            or raw_text.index("move state interval started") >= raw_text.index("move state interval completed")):
        raise RuntimeError("move-state interval lacks one completed native traversal")
    span = raw_text.split("move state interval started ", 1)[1].split("move state interval completed ", 1)[0]
    rows = [dict(re.findall(r"(\w+)=([^\s]+)", line)) for line in span.splitlines()
            if "[ReplayQualification] boundary ordinal=" in line]
    coverage = summarize_native_execution(rows)
    if (coverage["intervals"] != 1 or coverage["tick_callbacks"] != 1
            or coverage["move_state_3_ticks"] != 1
            or any(row["phase"] in ("input_cache_publication", "round_sequence") for row in rows)):
        raise RuntimeError("move-state interval lacks its distinct pre-input callback path")
    return {"from_tick": int(starts[0][1]), "to_tick": int(ends[0][1]),
            "scope": "native setter intervention; not a naturally authored replay traversal"}


def validate_intro_skips(raw: str, run_id: str) -> dict:
    requests = list(re.finditer(r"intro skip request run_id=" + re.escape(run_id)
        + r" native_frame=(\d+) world_mode=([67]) source_active=false ready=true", raw))
    completed = list(re.finditer(r"setup completed run_id=" + re.escape(run_id)
        + r" observations=\d+ native_frame=(\d+)", raw))
    ticks = [int(row[1]) for row in requests]
    if (not requests or len(completed) != 1 or ticks != sorted(set(ticks))
            or requests[-1].end() >= completed[0].start()
            or ticks[-1] >= int(completed[0][1])):
        raise RuntimeError("intro skipping lacks ordered requests followed by source activation")
    for request in requests:
        boundary = re.search(rf"boundary ordinal=\d+ phase=round_sequence sample_version=[2345] frame={request[1]} "
            + rf"[^\r\n]*source_active=false [^\r\n]*world_mode={request[2]}(?:\s|$)", raw[:request.start()])
        if boundary is None:
            raise RuntimeError("intro skip lacks an independent inactive-source intro boundary")
    return {"request_ticks": ticks, "world_modes": [int(row[2]) for row in requests],
            "source_activation_tick": int(completed[0][1]),
            "scope": "native intro input events; labelled diagnostic, not uninterrupted replay equivalence"}


def completed_replay_boundary_prefix(rows, endpoint, *, retained_source_stop=False):
    """Cut at the completed native tail, before next-interval publication.

    Publication uses the preceding completed tick, so filtering by frame alone
    includes unfinished work at precisely the coordinate being compared.
    """
    mode = "5" if retained_source_stop else "10"
    terminal = [i for i, row in enumerate(rows)
                if row["phase"] == "actor_tail" and int(row["frame"]) == endpoint
                and row["world_mode"] == mode and row["round_state"] == mode
                and row["source_active"] == "false"]
    if len(terminal) != 1:
        raise RuntimeError("indexed control lacks a unique completed terminal interval")
    return rows[:terminal[0] + 1], len(rows) - terminal[0] - 1


def compare_passive_controls(reference_path: Path, candidate_path: Path, *, candidate_mode="runtime", index_checkpoint=False):
    """Compare identical observation phases from their first recorded boundary."""
    if candidate_mode not in ("stock", "runtime"):
        raise ValueError("candidate mode must be stock or runtime")
    if candidate_mode == "stock":
        reference_report = json.loads(reference_path.read_text(encoding="utf-8"))
        candidate_report = json.loads(candidate_path.read_text(encoding="utf-8"))
        if (reference_path.resolve() == candidate_path.resolve()
                or reference_report.get("run_id") == candidate_report.get("run_id")):
            raise RuntimeError("stock repeatability requires two independent captures")
    protocols = [json.loads(path.read_text(encoding="utf-8")) for path in (reference_path, candidate_path)]
    index_cancel = index_checkpoint and all(report.get("index_cancel") for report in protocols)
    native_exit = index_checkpoint and all(report.get("native_session_exit") for report in protocols)
    bounded_index = index_cancel or native_exit
    if index_checkpoint and (candidate_mode!="runtime" or not protocols[1].get("index_checkpoint")
            or protocols[0].get("index_checkpoint")
            or any(not all(report.get(key) for key in ("record_index","skip_intros","native_fixed_seed","serial_particles")) for report in protocols)
            or any(bool(protocols[0].get(key))!=bool(protocols[1].get(key)) for key in ("index_cancel","native_session_exit","host_session"))
            or (index_cancel and native_exit)
            or (not bounded_index and any(not report.get("full_match") for report in protocols))
            or (bounded_index and any(report.get("full_match") for report in protocols))):
        raise RuntimeError("index checkpoint comparison requires matching explicit native intervention policies")
    if not index_checkpoint and any(report.get("skip_intros") for report in protocols):
        raise RuntimeError("intro-skipping diagnostics cannot qualify uninterrupted native equivalence")
    def load(path, mode):
        report = json.loads(path.read_text(encoding="utf-8"))
        proof = report.get("loaded_runtime", {})
        verification = ("owned_process_runtime_absent" if mode == "stock"
                        else "owned_process_mapped_file_and_sha256")
        if (report.get("result") != "captured" or report.get("mode") != mode
                or proof.get("verification") != verification
                or report.get("cleanup") != {"complete": True, "games_remaining": 0}):
            raise RuntimeError("passive control lacks execution or cleanup proof")
        identities = report["identities"]
        observer = report.get("loaded_observer", {})
        framework = report.get("loaded_framework", {})
        if "ucrt" in identities:
            crt = report.get("loaded_ucrt", {})
            if (crt.get("sha256") != identities["ucrt"] or crt.get("pid") != proof.get("pid")
                    or crt.get("process_created") != proof.get("process_created")
                    or crt.get("verification") != "owned_process_mapped_file_and_sha256"):
                raise RuntimeError("passive control has mixed native CRT identity")
        if (observer.get("sha256") != identities["observer"]
                or observer.get("pid") != proof.get("pid")
                or observer.get("process_created") != proof.get("process_created")
                or (mode == "runtime" and proof.get("sha256") != identities["runtime"])
                or not identities.get("framework") or framework.get("sha256") != identities["framework"]
                or framework.get("pid") != proof.get("pid")
                or framework.get("process_created") != proof.get("process_created")
                or framework.get("verification") != "owned_process_mapped_file_and_sha256"):
            raise RuntimeError("passive control has mixed loaded identities")
        raw = report["raw_log"]
        log = Path(raw["path"])
        if sha256_file(log) != raw["sha256"]:
            raise RuntimeError("passive control log changed")
        rows = []
        raw_text = log.read_text(encoding="utf-8", errors="replace")
        if not index_checkpoint and "intro skip request " in raw_text:
            raise RuntimeError("intro-skipping diagnostics cannot qualify uninterrupted native equivalence")
        accepted = re.findall(r"accepted passive trajectory run_id=(\S+) samples=(\d+)", raw_text)
        if accepted != [(report["run_id"], str(report["observations_requested"]))]:
            raise RuntimeError("passive control raw log belongs to another request")
        handoff = validate_payload_handoff(raw_text, report["run_id"])
        if "source_extents" in report:
            extents = validate_source_extents(raw_text, report["run_id"])
            if extents != report["source_extents"]:
                raise RuntimeError("source extent report differs from native evidence")
            handoff["source_extents"] = extents
        validate_initial_snapshot(raw_text, report["run_id"])
        timing = None
        if "timing" in report:
            timing = validate_trajectory_timing(raw_text, report["run_id"])
            if timing != report["timing"]:
                raise RuntimeError("trajectory timing report differs from native evidence")
        completion = report.get("completion")
        if completion is not None or report.get("full_match"):
            completion = validate_trajectory_completion(raw_text, report["run_id"],
                report["observations_requested"], bool(report.get("full_match", False)))
            if completion != report.get("completion"):
                raise RuntimeError("trajectory completion report differs from raw evidence")
        for line in raw_text.splitlines():
            if "[ReplayQualification] trajectory ordinal=" not in line:
                continue
            row = dict(re.findall(r"(\w+)=([^\s]+)", line))
            if int(row["ordinal"]) != len(rows) + 1 or row["phase"] != "engine_post":
                raise RuntimeError("passive control observation sequence is incomplete")
            if not rows and (row["round"] != "0" or row["cursor"] != "0"):
                raise RuntimeError("passive control missed the opening source boundary")
            if rows:
                previous = rows[-1]
                same_round = row["round"] == previous["round"]
                if (same_round and int(row["cursor"]) not in
                        (int(previous["cursor"]), int(previous["cursor"]) + 1)
                        or not same_round and (int(row["round"]) != int(previous["round"]) + 1
                                               or row["cursor"] != "0")):
                    raise RuntimeError("passive control skipped a source boundary")
            rows.append(row)
        observed_count = completion["observations"] if completion else report["observations_requested"]
        if len(rows) != observed_count or len(rows) < 120:
            raise RuntimeError("passive control has insufficient observations")
        if completion and completion.get("full_match"):
            endpoint = completion["native_endpoint"]
            if (any(int(rows[-1][key]) != value for key, value in endpoint.items())
                    or rows[-1].get("world_mode") != "10" or rows[-1].get("round_state") != "10"
                    or rows[-1].get("source_active") != "false"):
                raise RuntimeError("trajectory does not end at the verified native completion boundary")
        setup = validate_setup_capture(raw_text, report["run_id"]) if report.get("include_setup") else []
        boundaries = (indexed_seek_boundary_streams(raw_text,report["run_id"],recovery=report.get("index_recovery",""))[1] if report.get("index_seek")
                      else validate_boundary_capture(raw_text, report["run_id"]))
        empty = validate_empty_interval(raw_text, report["run_id"], bool(report.get("probe_empty_interval")))
        move = validate_move_state_interval(raw_text, report["run_id"], bool(report.get("probe_move_state")))
        return identities, rows, handoff, completion, setup, boundaries, empty, timing, move

    identities, reference, handoff, completion, setup, boundaries, empty, timing, move = load(reference_path, "stock")
    candidate_identities, candidate, candidate_handoff, candidate_completion, candidate_setup, candidate_boundaries, candidate_empty, candidate_timing, candidate_move = load(candidate_path, candidate_mode)
    closure_counts=None
    supported_completion=candidate_completion and candidate_completion.get("retained_range")
    if index_checkpoint:
        interventions=[validate_intro_skips(Path(r["raw_log"]["path"]).read_text(encoding="utf-8",errors="replace"),r["run_id"]) for r in protocols]
        if interventions[0]!=interventions[1]:
            raise RuntimeError("index checkpoint controls used different intro interventions")
        if not bounded_index and not supported_completion and (not completion or not candidate_completion or completion!=candidate_completion):
            raise RuntimeError("indexed controls disagree on native completion")
        endpoint=int(reference[-1]["frame"])
        if supported_completion:
            if (not protocols[1].get("host_session") or not protocols[1].get("index_seek") or not completion.get("full_match")
                or completion.get("round_winners")!=candidate_completion.get("round_winners")
                or not supported_completion["last_tick"]<endpoint):
                raise RuntimeError("retained prefix lacks independently completed native coverage")
            endpoint=supported_completion["last_tick"]
            reference=[row for row in reference if int(row["frame"])<=supported_completion["source_tick"]]
            if not candidate or int(candidate[-1]["frame"])!=supported_completion["source_tick"]:
                raise RuntimeError("retained trajectory omitted its source boundary")
        # The independently completed replay endpoint defines this comparison.
        # Retain post-replay counts separately; no full execution-gate claim.
        # The next interval publishes input at the preceding completed tick.
        # A numeric tick filter would include that publication without its tail.
        if not bounded_index:
            # Only the fully validated retained-range protocol above admits
            # mode5. The independent control still proves native completion.
            boundaries,native_closure=completed_replay_boundary_prefix(boundaries,endpoint,retained_source_stop=bool(supported_completion))
            candidate_boundaries,candidate_closure=completed_replay_boundary_prefix(candidate_boundaries,endpoint,retained_source_stop=bool(supported_completion))
            closure_counts={"native":native_closure,"candidate":candidate_closure}
    # load() proved HorseMod absent in the reference process. Its build-time
    # runtime hash is provenance, not an executed dependency. The candidate's
    # mapped runtime must still match its own report; all actual shared
    # dependencies remain exact. Never relabel the retained control's identity.
    if {k:v for k,v in identities.items() if k != "runtime"} != {k:v for k,v in candidate_identities.items() if k != "runtime"}:
        raise RuntimeError("passive controls use different dependencies")
    if bool(protocols[0].get("record_index")) != bool(protocols[1].get("record_index")):
        raise RuntimeError("passive controls use different index completion protocols")
    if handoff != candidate_handoff:
        raise RuntimeError("passive controls loaded different native payloads")
    if empty != candidate_empty:
        raise RuntimeError("controls have different empty-interval experiments")
    if move != candidate_move:
        raise RuntimeError("controls have different move-state interventions")
    compared = ("frame", "round", "cursor", "input_round", "input_time", "round_frame", "inputs",
                "p0_sim", "p0_step", "p0_render", "p1_sim", "p1_step", "p1_render")
    sample_versions = {row.get("sample_version", "1") for row in (*reference, *candidate)}
    if len(sample_versions) != 1 or not sample_versions <= {"1", "2", "3", "4", "5"}:
        raise RuntimeError("passive control sample versions differ or are unsupported")
    if sample_versions <= {"2", "3", "4", "5"}:
        compared += ("p0_vital", "p1_vital", "p0_moves", "p1_moves")
    if sample_versions <= {"3", "4", "5"}: compared += ("mt_cursor", "mt_hash")
    if sample_versions <= {"4", "5"}: compared += ("crt_state",)
    if sample_versions == {"5"}: compared += ("ground_roots", "ground_next_id", "ground_lifecycle")
    if sample_versions == {"5"} and any(not valid_ground_lifecycle(row) for row in (*reference,*candidate)):
        raise RuntimeError("passive control ground lifecycle is missing or invalid")
    if any("world_mode" in row for row in (*reference, *candidate)):
        compared += ("world_mode", "source_active", "manager_phase", "move_state", "round_state",
                     "published_count", "published_pairs")
    if any(any(field not in row for field in compared) for row in (*reference, *candidate)):
        raise RuntimeError("passive control sample is missing required fields")
    if sample_versions <= {"4", "5"} and any(not re.fullmatch(r"[0-9a-f]{8}", row["crt_state"]) for row in (*reference, *candidate)):
        raise RuntimeError("passive control sample has invalid native CRT state")
    result = {"result": "pass", "scope": ("stock_trajectory_repeatability" if candidate_mode == "stock"
                                             else "horsemod_loaded_trajectory_neutrality"),
              "identities": candidate_identities, "reference_identities": identities,
              "native_runtime_absence_verified": True, "observations_compared": len(reference),
              "payload_handoff": handoff,
              "sample_version": int(next(iter(sample_versions))),
              "completion": completion,
              "limitations": "Shared replay importer and other UE4SS mods remain in both paths; "
                             "this does not prove original recording fidelity, continuous capture health, "
                             "or seek correctness."}
    if timing is not None or candidate_timing is not None:
        result["pacing"] = {"reference": timing, "candidate": candidate_timing,
                            "measurement": "native ticks and viewport updates; displayed FPS is unmeasured"}
    if index_checkpoint:
        result["scope"]=("native_session_exit_observed_prefix" if native_exit else
                         "cancelled_index_native_continuation" if index_cancel else "indexed_native_execution_with_matching_intro_skip")
        result["post_replay_callbacks_observed_not_compared"]=closure_counts
        result["full_execution_gate_complete"]=False
        if supported_completion:
            result["completion"]=candidate_completion
            result["independent_native_completion"]=completion
            result["retained_range"]=supported_completion
            result["unsupported_native_tail"]={"first_tick":supported_completion["last_tick"]+1,
                "through_source_endpoint":completion["native_endpoint"]["frame"],"later_UI_tail":"unsupported"}
    if boundaries:
        result["native_boundaries_compared"] = len(boundaries)
        result["native_boundary_counts"] = {phase: sum(row["phase"] == phase for row in boundaries)
                                           for phase in sorted({row["phase"] for row in boundaries})}
        for ordinal, (expected, observed) in enumerate(zip(boundaries, candidate_boundaries), 1):
            if expected != observed:
                result.update(result="fail", first_native_boundary=ordinal,
                    different_fields=[key for key in expected.keys() | observed.keys()
                                      if expected.get(key) != observed.get(key)],
                    expected=expected, observed=observed)
                return result
    if len(boundaries) != len(candidate_boundaries):
        common = min(len(boundaries), len(candidate_boundaries))
        result.update(result="fail", failure="native boundary counts differ after an exact common prefix",
                      first_native_boundary=common + 1,
                      expected_boundary_count=len(boundaries), observed_boundary_count=len(candidate_boundaries),
                      expected=boundaries[common] if common < len(boundaries) else None,
                      observed=candidate_boundaries[common] if common < len(candidate_boundaries) else None)
        return result
    if empty:
        result["empty_interval_probe"] = empty
    if move:
        result["move_state_probe"] = move
    if len(setup) != len(candidate_setup):
        result.update(result="fail", failure="setup observation counts differ",
                      expected_setup_count=len(setup), observed_setup_count=len(candidate_setup))
        return result
    if setup:
        result["setup_observations_compared"] = len(setup)
        for ordinal, (expected, observed) in enumerate(zip(setup, candidate_setup), 1):
            if expected != observed:
                result.update(result="fail", first_setup_observation=ordinal,
                    different_fields=[key for key in expected.keys() | observed.keys()
                                      if expected.get(key) != observed.get(key)],
                    expected=expected, observed=observed)
                return result
    for ordinal, (expected, observed) in enumerate(zip(reference, candidate), 1):
        differences = [field for field in compared if expected[field] != observed[field]]
        if differences:
            result.update(result="fail", first_observation=ordinal,
                different_fields=differences, expected={key: expected[key] for key in compared},
                observed={key: observed[key] for key in compared})
            return result
    if len(reference) != len(candidate):
        result.update(result="fail", failure="observation counts differ after an exact common prefix",
                      first_observation=min(len(reference), len(candidate)) + 1,
                      expected_observations=len(reference), observed_observations=len(candidate))
    elif completion != candidate_completion and not supported_completion:
        result.update(result="fail", failure="completion bounds or outcomes differ",
                      expected_completion=completion, observed_completion=candidate_completion)
    if result["result"] == "pass" and boundaries:
        result["native_execution_coverage"] = summarize_native_execution(boundaries)
    return result


def load_trajectory(report_path: Path):
    report = json.loads(report_path.read_text(encoding="utf-8"))
    artifacts = report.get("artifacts", {})
    loaded = artifacts.get("loaded_horsemod", {})
    if (report.get("result") != "pass"
            or loaded.get("verification") != "owned_process_mapped_file_and_sha256"
            or loaded.get("sha256") != artifacts.get("horsemod_dll", {}).get("sha256")
            or report.get("cleanup", {}).get("deployment_restored") is not True
            or report.get("cleanup", {}).get("game_processes_remaining") != 0):
        raise RuntimeError("trajectory capture lacks loaded-runtime proof or complete deployment cleanup")
    logs = artifacts.get("raw_logs", {})
    if not logs:
        raise RuntimeError("trajectory capture lacks sealed raw logs")
    rows = {}
    frontier = None
    attempted = None
    restored = None
    completed = False
    fresh = set()
    for artifact in logs.values():
        path = Path(artifact["path"])
        if sha256_file(path) != artifact.get("sha256"):
            raise RuntimeError("trajectory raw log is stale")
        for line in path.read_text(encoding="utf-8", errors="strict").splitlines():
            if "[ReplayQualification] final canonical state " in line:
                break  # Exclude gameplay that continued during graceful exit.
            restore = re.search(r"owned replay seek restored target=(\d+) source_end=(\d+) resume_validation=true", line)
            if restore:
                if restored is not None or attempted is None:
                    raise RuntimeError("trajectory needs one source-bound executed seek")
                restored = (int(restore[1]), int(restore[2]))
                if attempted[0] != restored[1] or restored[0] >= restored[1]:
                    raise RuntimeError("seek restore does not match the observed source frontier")
                frontier = attempted[1]
                continue
            resume = re.search(r"owned replay seek resume verified target=(\d+) source_end=(\d+) verified_frames=(\d+) final=(\d+)", line)
            if resume:
                if (restored != (int(resume[1]), int(resume[2]))
                        or int(resume[3]) != int(resume[2]) - int(resume[1])
                        or int(resume[4]) != int(resume[2])):
                    raise RuntimeError("trajectory seek completion is inconsistent")
                completed = True
                continue
            if "[HorseMod] replay source boundary " not in line:
                continue
            fields = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
            if fields.get("forced") == "true":
                attempted = (int(fields["native_frame"]),
                    (int(fields["source_round"]), int(fields["source_cursor"])))
                continue
            if fields.get("historical") != "false":
                continue
            cursor = int(fields["source_cursor"])
            if cursor < 120 or cursor % 60:
                continue
            if fields.get("positions_valid") != "true":
                raise RuntimeError("trajectory sample lacks native positions")
            if not all(field in fields for field in TRAJECTORY_FIELDS):
                raise RuntimeError("trajectory sample is incomplete")
            key = (int(fields["source_round"]), cursor)
            sample = {field: fields[field] for field in TRAJECTORY_FIELDS}
            if key in rows and rows[key] != sample:
                raise RuntimeError(f"ambiguous source-frame trajectory at {key}")
            rows[key] = sample
            if completed and key[0] == frontier[0] and key[1] > frontier[1]:
                fresh.add(key)
    if attempted is not None and not completed:
        raise RuntimeError("trajectory capture has no completed seek")
    if not rows:
        raise RuntimeError("capture has no eligible trajectory measurements")
    identity = {name: artifacts.get(name, {}).get("sha256") for name in (
        "horsemod_dll", "replay_qualification_mod", "replay", "generated_schema", "game_executable")}
    if not all(identity.values()):
        raise RuntimeError("trajectory producer identity is incomplete")
    return identity, rows, frontier, fresh


def compare_trajectories(reference_path: Path, candidate_path: Path):
    reference_identity, reference, reference_frontier, _ = load_trajectory(reference_path)
    candidate_identity, candidate, frontier, future = load_trajectory(candidate_path)
    if reference_identity != candidate_identity:
        raise RuntimeError("trajectory captures have mixed producer identities")
    if reference_frontier is not None or frontier is None:
        raise RuntimeError("trajectory comparison requires an uninterrupted reference and a seek capture")
    missing = sorted(set(candidate) - set(reference))
    if missing:
        raise RuntimeError(f"reference lacks candidate source frames, first={missing[0]}")
    if len(future) < 6:
        raise RuntimeError("trajectory capture lacks six fresh samples beyond the original seek frontier")
    for key in sorted(candidate):
        changed = [field for field in TRAJECTORY_FIELDS if candidate[key][field] != reference[key][field]]
        if changed:
            return {"result": "fail", "first_source_boundary": list(key),
                    "different_fields": changed, "reference": reference[key], "observed": candidate[key]}
    return {"result": "pass", "scope": "sampled_seek_neutrality",
            "samples_compared": len(candidate), "fresh_samples": len(future),
            "source_frontier": list(frontier), "identities": candidate_identity,
            "limitations": "Does not establish full-frame or independent stock replay fidelity."}


def run_trajectory_comparison(args):
    reference = json.loads(args.reference.read_text(encoding="utf-8"))
    compare = (compare_passive_controls
        if reference.get("scope") == "passive_replay_trajectory" else compare_trajectories)
    result = compare(args.reference, args.candidate)
    result.update(reference_report=str(args.reference.resolve()),
                  candidate_report=str(args.candidate.resolve()))
    write_report(args.report, result)
    print(json.dumps(result, indent=2))
    return 0 if result["result"] == "pass" else 1
