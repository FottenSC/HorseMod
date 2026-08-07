"""Build the immutable evidence/coverage manifest for the SC6 static model.

The command is intentionally conservative: scanning authored bytecode proves
reachability, not implementation.  A reachable CALLCOND is a blocker until it
is present in ``IMPLEMENTED_CALLCOND_HANDLERS`` and its native semantics have
an executable model.  The output therefore cannot accidentally label the
current formula-based page ``static-complete``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Sequence

from locomotion_movement import CHARACTERS
from lux_input_transform_vtables import (
    discover_input_transform_vtables,
    discover_provider_constructions,
)
from lux_imported_math import WindowsUcrtMath
from luxformats import parse_khd
from stackvm import StackVMInstruction


MANIFEST_SCHEMA = "sc6-static-evidence-v1"
MODEL_SCHEMA = "sc6-lux-reference-v1"

# This registry means "native-equivalent executable implementation", not just
# a name or a Ghidra plate comment.  Handlers move here only with focused unit
# tests for their full reachable authored argument/state domain.
IMPLEMENTED_CALLCOND_HANDLERS: frozenset[int] = frozenset(
    {0x02, 0x06, 0x07, 0x09, 0x0A, 0x0C, 0x0D, 0x10, 0x14, 0x15, 0x19, 0x1A, 0x23, 0x25}
)

# Argument counts are part of each reviewed native contract. A changed asset
# corpus cannot silently reach an implemented handler with a new call shape.
IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS: dict[int, frozenset[int]] = {
    0x02: frozenset({1, 2, 3}),
    0x06: frozenset({1, 2, 3}),
    0x07: frozenset({1, 2, 3, 4, 5, 6}),
    0x09: frozenset({1}),
    0x0A: frozenset({1}),
    0x0C: frozenset({2, 3}),
    0x0D: frozenset({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}),
    0x10: frozenset({0}),
    0x14: frozenset({2}),
    0x15: frozenset({1}),
    0x19: frozenset({2, 3, 4, 5, 11, 15}),
    0x1A: frozenset({0}),
    0x23: frozenset({1}),
    0x25: frozenset({1, 2}),
}

IMPLEMENTED_CALLCOND_EFFECT_OPCODES: dict[int, frozenset[int]] = {
    0x02: frozenset({0x0004, 0x0006, 0x000E}),
}

# CALLCOND 0x16 is not implemented yet, but its complete authored operand
# domain is independently bounded.  Keeping this separate from the implemented
# registry prevents corpus drift while TransitionToMove remains unresolved.
REVIEWED_CALLCOND_16_ARGUMENTS: frozenset[tuple[int, ...]] = frozenset(
    {(), (0x0002,)}
)

# CALLCOND 0x26's word is intentionally dynamic: authored scripts commonly
# push a local/global value selected by multi-hit progression.  Static evidence
# can prove the one-word shape without pretending the runtime value is literal.
REVIEWED_CALLCOND_26_ARGUMENT_COUNTS: frozenset[int] = frozenset({1})

# Completion is wider than bytecode reachability.  Keeping the subsystem gate
# in the same manifest prevents a future "all CALLCONDs are named" milestone
# from accidentally certifying missing pose, collision, or input production.
REQUIRED_SUBSYSTEMS: frozenset[str] = frozenset({
    "movevm_opcode_core",
    "all_reachable_callconds",
    "raw_input_to_current_snapshot",
    "raw_input_source_selection_and_delay_ring",
    "raw_and_encoded_transform_transaction",
    "all_concrete_input_transform_providers",
    "per_player_training_input_record_and_playback",
    "shared_dummy_dual_training_input_record_and_playback",
    "training_input_stop_event_dispatch",
    "camera_relative_input_side_source",
    "current_input_snapshot_to_history_commit",
    "input_history_condition_scanner",
    "move_scheduler_and_lane_lifecycle",
    "motion_root_and_velocity_integration",
    "pose_skeleton_and_blending",
    "body_weapon_and_attack_volumes",
    "khit_intersection",
    "all_authored_state_and_stat_profiles",
    "exact_context_query_engine",
    "context_explorer_ui",
    "required_asset_parsers_and_hashes",
    "independent_lifted_ir_agreement",
})
IMPLEMENTED_SUBSYSTEMS: frozenset[str] = frozenset({
    "movevm_opcode_core",
    "raw_input_to_current_snapshot",
    "raw_input_source_selection_and_delay_ring",
    "raw_and_encoded_transform_transaction",
    "all_concrete_input_transform_providers",
    "camera_relative_input_side_source",
    "per_player_training_input_record_and_playback",
    "shared_dummy_dual_training_input_record_and_playback",
    "training_input_stop_event_dispatch",
    "current_input_snapshot_to_history_commit",
    "input_history_condition_scanner",
})


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True)
class FileEvidence:
    path: str
    size: int
    sha256: str


def file_evidence(path: Path, *, relative_to: Path | None = None) -> FileEvidence:
    resolved = path.resolve(strict=True)
    label = resolved.relative_to(relative_to.resolve()).as_posix() if relative_to else str(resolved)
    return FileEvidence(label, resolved.stat().st_size, sha256_file(resolved))


def require_single_root(paths: Iterable[Path], root: Path) -> tuple[Path, ...]:
    resolved_root = root.resolve(strict=True)
    output = []
    for path in paths:
        resolved = path.resolve(strict=True)
        try:
            resolved.relative_to(resolved_root)
        except ValueError as error:
            raise ValueError(
                f"mixed extraction roots refused: {resolved} is outside {resolved_root}"
            ) from error
        output.append(resolved)
    return tuple(output)


def git_identity(repo_root: Path) -> dict[str, str | bool]:
    def run(*args: str) -> bytes:
        return subprocess.check_output(
            ["git", *args], cwd=repo_root, stderr=subprocess.DEVNULL
        )

    commit = run("rev-parse", "HEAD").decode("ascii").strip()
    diff = run("diff", "--binary", "HEAD")
    untracked = run("ls-files", "--others", "--exclude-standard")
    identity_material = diff + b"\0UNTRACKED\0" + untracked
    return {
        "commit": commit,
        "dirty": bool(diff or untracked.strip()),
        "working_tree_identity_sha256": hashlib.sha256(identity_material).hexdigest(),
    }


def extract_contiguous_literal_index(
    instructions: Sequence[StackVMInstruction],
    call_index: int,
    argument_count: int,
    bank_count: int,
) -> int:
    """Prove one indexed CALLCOND's authored producer window."""

    if argument_count < 1 or call_index < argument_count:
        raise ValueError("lacks the complete preceding argument producer window")
    producers = instructions[call_index - argument_count : call_index]
    if any(not producer.push_flag for producer in producers):
        raise ValueError("argument producers do not all push")
    if any(
        left.pc + left.length != right.pc
        for left, right in zip(producers, producers[1:])
    ) or producers[-1].pc + producers[-1].length != instructions[call_index].pc:
        raise ValueError("argument producers are not contiguous")
    index_producer = producers[0]
    if index_producer.opcode not in (0x09, 0x0B) or index_producer.imm_u16 is None:
        raise ValueError("index producer is not a literal")
    index = index_producer.imm_u16
    if not 0 <= index < bank_count:
        raise ValueError(f"literal index {index} is outside 0..{bank_count - 1}")
    return index


def build_manifest(
    *,
    repo_root: Path,
    dump_root: Path,
    executable: Path,
    implemented_callconds: frozenset[int] = IMPLEMENTED_CALLCOND_HANDLERS,
) -> dict:
    header_root = dump_root / "Battle" / "hdr"
    khd_paths = [header_root / f"hdr{cid}.khd" for cid in CHARACTERS]
    resolved_assets = require_single_root(khd_paths, dump_root)
    transform_vtables = discover_input_transform_vtables(executable)
    provider_constructions = discover_provider_constructions(executable)

    opcodes: Counter[int] = Counter()
    callconds: Counter[int] = Counter()
    callcond_argument_counts: Counter[tuple[int, int]] = Counter()
    indexed_float_param_indices: Counter[int] = Counter()
    indexed_float_param_contract_violations: list[str] = []
    chara_state_short_indices: Counter[int] = Counter()
    chara_state_short_contract_violations: list[str] = []
    effect_opcodes_by_callcond: dict[int, Counter[int]] = {0x02: Counter()}
    effect_opcode_contract_violations: list[str] = []
    callcond_16_arguments: Counter[tuple[int, ...]] = Counter()
    callcond_16_contract_violations: list[str] = []
    unresolved_first_word_values: dict[int, Counter[int]] = {
        0x01: Counter(),
        0x03: Counter(),
    }
    unresolved_first_word_provenance: dict[int, Counter[str]] = {
        0x01: Counter(),
        0x03: Counter(),
    }
    truncated: list[str] = []
    script_count = 0
    for cid, path in zip(CHARACTERS, resolved_assets):
        bank = parse_khd(path.read_bytes())
        for slot in bank.slots:
            script = slot.bytecode
            if script is None:
                continue
            script_count += 1
            if script.truncated:
                truncated.append(f"{cid}:slot=0x{slot.slot_index:X}")
            opcodes.update(instruction.opcode for instruction in script.instructions)
            callconds.update(
                instruction.imm_b0
                for instruction in script.instructions
                if instruction.opcode == 0x25 and instruction.imm_b0 is not None
            )
            callcond_argument_counts.update(
                (instruction.imm_b0, instruction.imm_b1)
                for instruction in script.instructions
                if instruction.opcode == 0x25
                and instruction.imm_b0 is not None
                and instruction.imm_b1 is not None
            )
            instructions = script.instructions
            for instruction_index, instruction in enumerate(instructions):
                if instruction.opcode == 0x25 and instruction.imm_b0 in (0x01, 0x03):
                    argument_count = instruction.imm_b1 or 0
                    try:
                        first_word = extract_contiguous_literal_index(
                            instructions,
                            instruction_index,
                            argument_count,
                            0x10000,
                        )
                    except ValueError as error:
                        unresolved_first_word_provenance[instruction.imm_b0][
                            str(error)
                        ] += 1
                    else:
                        unresolved_first_word_values[instruction.imm_b0][first_word] += 1
                if instruction.opcode == 0x25 and instruction.imm_b0 == 0x16:
                    location = (
                        f"{cid}:slot=0x{slot.slot_index:X}:pc=0x{instruction.pc:X}"
                    )
                    argument_count = instruction.imm_b1 or 0
                    if argument_count == 0:
                        callcond_16_arguments[()] += 1
                    elif argument_count == 1:
                        try:
                            value = extract_contiguous_literal_index(
                                instructions,
                                instruction_index,
                                argument_count,
                                0x10000,
                            )
                        except ValueError as error:
                            callcond_16_contract_violations.append(
                                f"{location} {error}"
                            )
                        else:
                            callcond_16_arguments[(value,)] += 1
                    else:
                        callcond_16_contract_violations.append(
                            f"{location} unreviewed argument count {argument_count}"
                        )
                if instruction.opcode == 0x25 and instruction.imm_b0 == 0x02:
                    location = (
                        f"{cid}:slot=0x{slot.slot_index:X}:pc=0x{instruction.pc:X}"
                    )
                    try:
                        effect_opcode = extract_contiguous_literal_index(
                            instructions,
                            instruction_index,
                            instruction.imm_b1 or 0,
                            0x10000,
                        )
                    except ValueError as error:
                        effect_opcode_contract_violations.append(
                            f"{location} {error}"
                        )
                    else:
                        effect_opcodes_by_callcond[0x02][effect_opcode] += 1
                    continue
                if instruction.opcode != 0x25 or instruction.imm_b0 not in (0x0C, 0x14):
                    continue
                location = (
                    f"{cid}:slot=0x{slot.slot_index:X}:pc=0x{instruction.pc:X}"
                )
                argument_count = instruction.imm_b1 or 0
                bank_count = 14 if instruction.imm_b0 == 0x0C else 74
                try:
                    index = extract_contiguous_literal_index(
                        instructions,
                        instruction_index,
                        argument_count,
                        bank_count,
                    )
                except ValueError as error:
                    violations = (
                        indexed_float_param_contract_violations
                        if instruction.imm_b0 == 0x0C
                        else chara_state_short_contract_violations
                    )
                    violations.append(f"{location} {error}")
                    continue
                if instruction.imm_b0 == 0x0C:
                    indexed_float_param_indices[index] += 1
                else:
                    chara_state_short_indices[index] += 1

    reachable_callconds = frozenset(callconds)
    unresolved = [
        f"reachable CALLCOND 0x{index:02X} lacks native-equivalent model"
        for index in sorted(reachable_callconds - implemented_callconds)
    ]
    for function_index in sorted(implemented_callconds):
        observed_counts = frozenset(
            argc
            for (index, argc), count in callcond_argument_counts.items()
            if index == function_index and count
        )
        approved_counts = IMPLEMENTED_CALLCOND_ARGUMENT_COUNTS.get(
            function_index, frozenset()
        )
        if not observed_counts:
            unresolved.append(
                f"implemented CALLCOND 0x{function_index:02X} has no authored reachability"
            )
        unexpected = observed_counts - approved_counts
        if unexpected:
            rendered = ", ".join(str(value) for value in sorted(unexpected))
            unresolved.append(
                f"implemented CALLCOND 0x{function_index:02X} has unreviewed argument counts: {rendered}"
            )
        approved_effect_opcodes = IMPLEMENTED_CALLCOND_EFFECT_OPCODES.get(function_index)
        if approved_effect_opcodes is not None:
            observed_effect_opcodes = frozenset(effect_opcodes_by_callcond[function_index])
            unexpected_effect_opcodes = observed_effect_opcodes - approved_effect_opcodes
            missing_effect_opcodes = approved_effect_opcodes - observed_effect_opcodes
            if unexpected_effect_opcodes:
                rendered = ", ".join(
                    f"0x{opcode:04X}" for opcode in sorted(unexpected_effect_opcodes)
                )
                unresolved.append(
                    f"implemented CALLCOND 0x{function_index:02X} has unreviewed effect opcodes: {rendered}"
                )
            if missing_effect_opcodes:
                rendered = ", ".join(
                    f"0x{opcode:04X}" for opcode in sorted(missing_effect_opcodes)
                )
                unresolved.append(
                    f"implemented CALLCOND 0x{function_index:02X} approved effect opcodes are no longer reachable: {rendered}"
                )
    unresolved.extend(f"truncated bytecode {item}" for item in truncated)
    unresolved.extend(
        f"CALLCOND 0x0C authored contract violation: {item}"
        for item in indexed_float_param_contract_violations
    )
    unresolved.extend(
        f"CALLCOND 0x14 authored contract violation: {item}"
        for item in chara_state_short_contract_violations
    )
    unresolved.extend(
        f"CALLCOND 0x02 effect-opcode contract violation: {item}"
        for item in effect_opcode_contract_violations
    )
    unexpected_callcond_16_arguments = (
        frozenset(callcond_16_arguments) - REVIEWED_CALLCOND_16_ARGUMENTS
    )
    unresolved.extend(
        "CALLCOND 0x16 authored argument contract changed: "
        + ", ".join(f"0x{value:04X}" for value in arguments)
        for arguments in sorted(unexpected_callcond_16_arguments)
    )
    unresolved.extend(
        f"CALLCOND 0x16 authored contract violation: {item}"
        for item in callcond_16_contract_violations
    )
    observed_callcond_26_argument_counts = frozenset(
        argc
        for (index, argc), count in callcond_argument_counts.items()
        if index == 0x26 and count
    )
    unexpected_callcond_26_counts = (
        observed_callcond_26_argument_counts
        - REVIEWED_CALLCOND_26_ARGUMENT_COUNTS
    )
    if unexpected_callcond_26_counts:
        unresolved.append(
            "CALLCOND 0x26 authored argument contract changed: "
            + ", ".join(str(value) for value in sorted(unexpected_callcond_26_counts))
        )
    unresolved.extend(
        f"required subsystem {name} is not static-complete"
        for name in sorted(REQUIRED_SUBSYSTEMS - IMPLEMENTED_SUBSYSTEMS)
    )

    source_paths = [
        repo_root / "tools" / "moveset_parser" / name
        for name in (
            "stackvm.py",
            "lux_reference_engine.py",
            "lux_numeric.py",
            "lux_gameplay_rng.py",
            "lux_attack_cell_variant.py",
            "lux_chara_state_shorts.py",
            "lux_indexed_float_params.py",
            "lux_lane_lifecycle.py",
            "lux_motion_input_flags.py",
            "lux_scheduled_effects.py",
            "lux_transition_author.py",
            "lux_movement_vm.py",
            "lux_input_history.py",
            "lux_input_codec.py",
            "lux_camera_input_side.py",
            "lux_input_pipeline.py",
            "lux_callcond_handlers.py",
            "lux_input_transform_vtables.py",
            "lux_imported_math.py",
            "lux_effect_dispatch_subset.py",
            "pe_static_image.py",
            "locomotion_movement.py",
            "static_model_coverage.py",
            "luxformats.py",
        )
    ]
    manifest = {
        "manifest_schema": MANIFEST_SCHEMA,
        "model_schema": MODEL_SCHEMA,
        "qualification": "static-complete" if not unresolved else "static-incomplete",
        "claim_boundary": {
            "runtime_validated": False,
            "open_plane_only": True,
            "inferno": False,
            "creations": False,
            "stage_geometry": False,
        },
        "executable": asdict(file_evidence(executable)),
        "imported_code": [WindowsUcrtMath.load_verified().evidence()],
        "asset_root": str(dump_root.resolve(strict=True)),
        "assets": [
            asdict(file_evidence(path, relative_to=dump_root))
            for path in resolved_assets
        ],
        "sources": [
            asdict(file_evidence(path, relative_to=repo_root)) for path in source_paths
        ],
        "source_identity": git_identity(repo_root),
        "coverage": {
            "roster_count": len(CHARACTERS),
            "script_count": script_count,
            "opcode_counts": {f"0x{key:02X}": opcodes[key] for key in sorted(opcodes)},
            "callcond_counts": {
                f"0x{key:02X}": callconds[key] for key in sorted(callconds)
            },
            "callcond_argument_counts": {
                f"0x{index:02X}/argc={argc}": callcond_argument_counts[index, argc]
                for index, argc in sorted(callcond_argument_counts)
            },
            "callcond_0x02_effect_opcode_domain": {
                "observed_call_count": sum(effect_opcodes_by_callcond[0x02].values()),
                "observed_opcodes": {
                    f"0x{opcode:04X}": effect_opcodes_by_callcond[0x02][opcode]
                    for opcode in sorted(effect_opcodes_by_callcond[0x02])
                },
                "contract_violations": effect_opcode_contract_violations,
            },
            "callcond_0x16_argument_domain": {
                "observed_call_count": sum(callcond_16_arguments.values()),
                "observed_arguments": {
                    (
                        "[]"
                        if not arguments
                        else "[" + ",".join(
                            f"0x{value:04X}" for value in arguments
                        ) + "]"
                    ): callcond_16_arguments[arguments]
                    for arguments in sorted(callcond_16_arguments)
                },
                "approved_arguments": [
                    (
                        []
                        if not arguments
                        else [f"0x{value:04X}" for value in arguments]
                    )
                    for arguments in sorted(REVIEWED_CALLCOND_16_ARGUMENTS)
                ],
                "contract_violations": callcond_16_contract_violations,
            },
            "callcond_0x26_argument_domain": {
                "observed_call_count": callconds[0x26],
                "observed_argument_counts": {
                    str(argc): callcond_argument_counts[0x26, argc]
                    for argc in sorted(observed_callcond_26_argument_counts)
                },
                "approved_argument_counts": sorted(
                    REVIEWED_CALLCOND_26_ARGUMENT_COUNTS
                ),
                "contract_violations": [
                    f"unreviewed argument count {argc}"
                    for argc in sorted(unexpected_callcond_26_counts)
                ],
                "argument_value_provenance": "dynamic MoveVM stack value",
            },
            "unresolved_callcond_first_word_domains": {
                f"0x{function_index:02X}": {
                    "observed_call_count": callconds[function_index],
                    "literal_first_word_count": sum(
                        unresolved_first_word_values[function_index].values()
                    ),
                    "literal_first_words": {
                        f"0x{value:04X}": unresolved_first_word_values[
                            function_index
                        ][value]
                        for value in sorted(
                            unresolved_first_word_values[function_index]
                        )
                    },
                    "nonliteral_or_noncontiguous": dict(
                        sorted(
                            unresolved_first_word_provenance[
                                function_index
                            ].items()
                        )
                    ),
                }
                for function_index in (0x01, 0x03)
            },
            "callcond_0x14_state_short_index_domain": {
                "bank_count": 74,
                "observed_call_count": sum(chara_state_short_indices.values()),
                "observed_min": min(chara_state_short_indices, default=None),
                "observed_max": max(chara_state_short_indices, default=None),
                "observed_indices": {
                    str(index): chara_state_short_indices[index]
                    for index in sorted(chara_state_short_indices)
                },
                "contract_violations": chara_state_short_contract_violations,
            },
            "callcond_0x0c_indexed_float_param_domain": {
                "bank_count": 14,
                "observed_call_count": sum(indexed_float_param_indices.values()),
                "observed_min": min(indexed_float_param_indices, default=None),
                "observed_max": max(indexed_float_param_indices, default=None),
                "observed_indices": {
                    str(index): indexed_float_param_indices[index]
                    for index in sorted(indexed_float_param_indices)
                },
                "contract_violations": indexed_float_param_contract_violations,
            },
            "implemented_callconds": [
                f"0x{key:02X}" for key in sorted(implemented_callconds)
            ],
            "implemented_subsystems": sorted(IMPLEMENTED_SUBSYSTEMS),
            "required_subsystems": sorted(REQUIRED_SUBSYSTEMS),
            "input_transform_vtables": [asdict(item) for item in transform_vtables],
            "input_transform_provider_constructions": [
                asdict(item) for item in provider_constructions
            ],
            "unresolved": unresolved,
        },
    }
    return manifest


def canonical_json(manifest: dict) -> str:
    return json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    repo_default = Path(__file__).resolve().parents[2]
    parser.add_argument("--repo-root", type=Path, default=repo_default)
    parser.add_argument("--dump-root", type=Path, default=repo_default / "dump")
    parser.add_argument(
        "--executable",
        type=Path,
        default=Path(
            r"E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe"
        ),
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()

    manifest = build_manifest(
        repo_root=args.repo_root,
        dump_root=args.dump_root,
        executable=args.executable,
    )
    rendered = canonical_json(manifest)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        print(rendered, end="")
    if args.require_complete and manifest["qualification"] != "static-complete":
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
