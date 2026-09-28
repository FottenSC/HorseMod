"""Correction-local observations. Requested settings and lifetime totals are not proof."""
from __future__ import annotations

import re
from typing import Any

COMBAT_PHASE = re.compile(
    r"\[HorseMod\] online qualification run_id=(?P<run>\S+) combat_phase "
    r"generation=(?P<generation>\d+) frame=(?P<frame>\d+) world_mode=(?P<mode>-?\d+) "
    r"native_round=(?P<native_round>\d+) result=(?P<result>\d+) combat_round=(?P<round>\d+) "
    r"completed_round=(?P<completed>\d+) active=(?P<active>[01]) valid=(?P<valid>[01])")


def combat_history(text: str, run_id: str) -> list[dict[str, int]]:
    """Reconstruct played rounds from native mode/result observations."""
    rows = [{key: int(value) for key, value in match.groupdict().items() if key != "run"}
            for match in COMBAT_PHASE.finditer(text) if match.group("run") == run_id]
    played = completed = 0
    for row in rows:
        if row["valid"] != 1:
            raise RuntimeError("native combat tracker invalidated")
        if played and row["native_round"] == played + 1 and row["result"] in (1, 2, 3, 4, 8, 9, 10):
            completed = played
        if row["active"]:
            native_round = row["native_round"]
            if row["mode"] != 2 or row["result"] or native_round != row["round"]:
                raise RuntimeError("active combat contradicts native mode/result")
            if native_round == played + 1 and completed == played:
                played = native_round
            if native_round != played or row["completed"] != completed:
                raise RuntimeError("combat round lacks its preceding played result")
    return rows


def require_combat_boundary(text: str, run_id: str, round_number: int) -> dict[str, int]:
    """Inspect the last completed forward observation before stimulus arming."""
    rows = combat_history(text, run_id)
    if not rows:
        raise RuntimeError("correction lacks a native combat-phase observation")
    row = rows[-1]
    if (row["valid"] != 1 or row["active"] != 1 or row["mode"] != 2 or row["result"] != 0
            or row["round"] != round_number or row["native_round"] != round_number
            or row["completed"] != round_number - 1):
        raise RuntimeError("intro or unproven round replacement cannot establish combat coverage")
    return row

CORRECTION = re.compile(
    r"\[HorseMod\] online correction converged run_id=(?P<run>\S+) "
    r"confirmed=(?P<generation>\d+):(?P<first>\d+) "
    r"final=(?P<final_generation>\d+):(?P<last>\d+) "
    r"base=(?P<base_generation>\d+):(?P<base>\d+) depth=(?P<depth>\d+) "
    r"batches=(?P<batches>\d+) coordinates=(?P<coordinates>\d+) "
    r"total_us=(?P<total_us>\d+) plan_stage=(?P<plan_stage>\d+)"
)
IDENTITY = re.compile(
    r"\[HorseMod\] online correction identity run_id=(?P<run>\S+) correction=(?P<correction>\d+) "
    r"generation=(?P<generation>\d+) frame=(?P<frame>\d+) input_player=(?P<player>[01]) "
    r"held=(?P<held>\d+) rising=(?P<rising>\d+) presentation_id=(?P<presentation_id>\d+) "
    r"final_sha256=(?P<sha256>[0-9a-f]{64})"
    r"(?: combat_round=(?P<combat_round>\d+) combat_active=(?P<combat_active>[01]) "
    r"combat_begin=(?P<combat_generation>\d+):(?P<combat_frame>\d+))?")
PRESENTATION = re.compile(
    r"\[HorseMod\] online correction presentation run_id=(?P<run>\S+) id=(?P<id>\d+) "
    r"generation=(?P<generation>\d+) frame=(?P<frame>\d+) "
    r"replacement_events=(?P<replacement_events>\d+) reused_events=(?P<reused_events>\d+) "
    r"published_events=(?P<published_events>\d+) changed_published_events=(?P<changed_published_events>\d+) "
    r"payload_identity=(?P<payload_identity>[0-9a-f]{16}) final_drain=(?P<final_drain>[01]) "
    r"discarded_events=(?P<discarded_events>\d+) observation_losses=(?P<observation_losses>\d+)")


def correction_local_detail(text: str, run_id: str,
                            observed: dict[str, int]) -> dict[str, Any] | None:
    identities = [match for match in IDENTITY.finditer(text)
        if match.group("run") == run_id
        and int(match.group("generation")) == observed["generation"]
        and int(match.group("frame")) == observed["first"]]
    if not identities:
        return None
    if len(identities) != 1:
        raise RuntimeError("ambiguous correction input identity")
    identity = identities[0].groupdict()
    drains = [match for match in PRESENTATION.finditer(text)
        if match.group("run") == run_id and match.group("id") == identity["presentation_id"]
        and match.group("generation") == identity["generation"]]
    if len(drains) != 1:
        return None
    presentation = {key: (value if key == "payload_identity" else int(value))
        for key, value in drains[0].groupdict().items() if key not in ("run", "id")}
    presentation["final_drain"] = bool(presentation["final_drain"])
    return {"input": {key: int(identity[key]) for key in ("player", "held", "rising")},
            "final_sha256": identity["sha256"], "presentation": presentation,
            "combat_interval": ({key: int(identity[key]) for key in
                ("combat_round", "combat_active", "combat_generation", "combat_frame")}
                if identity["combat_round"] is not None else None)}


def observed_correction(text: str, run_id: str, expected_depth: int,
                        generation: int) -> dict[str, int] | None:
    matches = [match for match in CORRECTION.finditer(text)
               if match.group("run") == run_id]
    if not matches:
        return None
    # Select the first response to this stimulus. Searching for any convenient
    # later depth would let a subsequent correction mask the missing boundary.
    row = {key: int(value) for key, value in matches[0].groupdict().items() if key != "run"}
    if row["depth"] != expected_depth:
        raise RuntimeError(f"requested depth {expected_depth}, observed depth {row['depth']}")
    if (row["generation"] != generation or row["final_generation"] != generation
            or row["base_generation"] != generation or row["base"] >= row["first"]
            or row["depth"] != row["last"] - row["first"] + 1 or row["batches"] <= 0
            or row["coordinates"] < row["last"] - row["base"]):
        raise RuntimeError("correction has an invalid generation/interval/native-work identity")
    row["requested_depth"] = expected_depth
    return row


def presentation_outcome(row: dict[str, Any]) -> str | None:
    """Only explicit local transaction observations establish changed/empty."""
    peers = row.get("presentation", {})
    if set(peers) != {"host", "sandbox"}:
        return None
    left, right = peers["host"], peers["sandbox"]
    keys = ("replacement_events", "reused_events", "published_events",
            "changed_published_events", "payload_identity", "final_drain",
            "discarded_events", "observation_losses")
    if any(key not in left or key not in right for key in keys):
        return None
    if any(left[key] != right[key] for key in keys) or left["final_drain"] is not True:
        raise RuntimeError("correction-local presentation did not reconcile")
    if left["discarded_events"] or left["observation_losses"]:
        raise RuntimeError("correction-local presentation was discarded or evidence was lost")
    if left["changed_published_events"] > 0 and left["payload_identity"]:
        return "changed"
    if left["replacement_events"] == 0 and left["published_events"] == 0:
        return "empty"
    if (left["replacement_events"] == left["reused_events"] and left["published_events"] == 0):
        return "reused"
    return "unchanged-publication"
