"""Freeze the authored CALLCOND 0x01 domain for an admitted KHD pair.

The ordinary coverage manifest deliberately scans the full extracted roster.
This module provides the narrower qualification view required by the
standalone simulator: every authored EvaluateIfOpcode site in the two admitted
banks, including first operands supplied through a concrete CALLCOND 0x0D
local frame.  An unresolved local, indirect packed target, or transition into
a parameterised helper is reported as a blocker; it is never guessed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path

from luxformats import KhdFile, parse_khd
from stackvm_emulate import emulate
from trace_movevm_calltree import Value, _nested_local_frame, trace_slot


SCHEMA = "sc6-callcond-01-authored-domain-v1"


@dataclass(frozen=True)
class DynamicSite:
    slot: int
    pc: int
    argument_count: int
    source: str
    concrete_first_words: tuple[int, ...]
    callers: tuple[tuple[int, int, int, tuple[int, ...]], ...]


@dataclass(frozen=True)
class BankDomain:
    path: str
    sha256: str
    site_count: int
    argument_counts: dict[str, int]
    concrete_first_words: tuple[int, ...]
    authored_operand_patterns: tuple[dict[str, object], ...]
    dynamic_sites: tuple[DynamicSite, ...]
    blockers: tuple[str, ...]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _transition_targets(bank: KhdFile) -> set[int]:
    targets: set[int] = set()
    for slot_index, slot in enumerate(bank.slots):
        if slot.bytecode is None:
            continue
        try:
            transitions = emulate(slot.bytecode, slot_index).transitions
        except Exception as error:
            raise RuntimeError(
                f"slot {slot_index} transition scan failed closed: {error}"
            ) from error
        for transition in transitions:
            if transition.next_move_id_raw is None:
                continue
            target = bank.resolve_packed_slot(transition.next_move_id_raw)
            if target is None:
                raise RuntimeError(
                    f"slot {slot_index} has unresolved transition target "
                    f"0x{transition.next_move_id_raw:04X}"
                )
            targets.add(target)
    return targets


def scan_bank(path: Path) -> BankDomain:
    raw = path.read_bytes()
    bank = parse_khd(raw)
    sites: dict[tuple[int, int], list[object]] = defaultdict(list)
    nested_events: list[tuple[int, object]] = []
    blockers: list[str] = []

    for slot_index, slot in enumerate(bank.slots):
        if slot.bytecode is None:
            continue
        for call in trace_slot(slot.bytecode, slot_index):
            if call.function_index == 0x01:
                if call not in sites[(slot_index, call.pc)]:
                    sites[(slot_index, call.pc)].append(call)
            if call.function_index != 0x0D or not call.args:
                continue
            nested_events.append((slot_index, call))

    dynamic_operand_slots = {
        slot_index
        for (slot_index, _pc), calls in sites.items()
        if any(any(value.value is None for value in call.args) for call in calls)
    }
    nested_callers: dict[int, list[tuple[int, int, int, tuple[int, ...]]]] = (
        defaultdict(list)
    )
    for slot_index, call in nested_events:
        packed = call.args[0].value
        if packed is None:
            blockers.append(
                f"slot {slot_index} pc 0x{call.pc:X}: indirect CALLCOND 0x0D target"
            )
            continue
        target = bank.resolve_packed_slot(packed)
        if target is None:
            blockers.append(
                f"slot {slot_index} pc 0x{call.pc:X}: invalid packed target "
                f"0x{packed:04X}"
            )
            continue
        if target not in dynamic_operand_slots:
            continue
        supplied = tuple(value.value for value in call.args[1:])
        if any(value is None for value in supplied):
            blockers.append(
                f"slot {slot_index} pc 0x{call.pc:X}: target 0x{packed:04X} "
                "has non-concrete local arguments"
            )
            continue
        nested_callers[target].append(
            (slot_index, call.pc, packed, tuple(int(value) for value in supplied))
        )

    transition_targets = _transition_targets(bank)
    first_words: set[int] = set()
    dynamic_sites: list[DynamicSite] = []
    argument_counts: Counter[int] = Counter()
    operand_pattern_sites: dict[
        tuple[tuple[int | None, ...], tuple[str, ...]], set[tuple[int, int]]
    ] = defaultdict(set)
    for (slot_index, pc), calls in sorted(sites.items()):
        call = calls[0]
        if any(len(event.args) != len(call.args) for event in calls):
            blockers.append(
                f"slot {slot_index} pc 0x{pc:X}: path-dependent argument count"
            )
        argument_counts[len(call.args)] += 1
        effective_calls = calls
        callers = tuple(sorted(set(nested_callers.get(slot_index, ()))))
        if (
            any(any(value.value is None for value in event.args) for event in calls)
            and callers
            and slot_index not in transition_targets
        ):
            contextual_calls: list[object] = []
            for _caller_slot, _caller_pc, _packed, supplied in callers:
                traced = trace_slot(
                    bank.slots[slot_index].bytecode,
                    slot_index,
                    _nested_local_frame(
                        (Value(0, "packed-id-placeholder"),)
                        + tuple(Value(value, "caller-local") for value in supplied)
                    ),
                )
                contextual_calls.extend(event for event in traced if event.pc == pc)
            if contextual_calls and all(
                all(value.value is not None for value in event.args)
                for event in contextual_calls
            ):
                effective_calls = list(dict.fromkeys(contextual_calls))

        for event in effective_calls:
            operand_pattern_sites[
                (
                    tuple(value.value for value in event.args),
                    tuple(
                        "" if value.value is not None else value.source
                        for value in event.args
                    ),
                )
            ].add((slot_index, pc))
            for operand_index, value in enumerate(event.args[1:], 1):
                if value.value is None:
                    blockers.append(
                        f"slot {slot_index} pc 0x{pc:X}: operand {operand_index} "
                        f"requires unresolved authored/runtime source {value.source}"
                    )
        if not call.args:
            blockers.append(f"slot {slot_index} pc 0x{pc:X}: empty predicate stream")
            continue
        concrete_site_first_words = {
            event.args[0].value & 0xFFFF
            for event in effective_calls
            if event.args and event.args[0].value is not None
        }
        first_words.update(concrete_site_first_words)
        unknown_first = next(
            (
                event.args[0]
                for event in calls
                if event.args[0].value is None
            ),
            None,
        )
        if unknown_first is None:
            continue
        first = unknown_first

        if slot_index in transition_targets:
            blockers.append(
                f"slot {slot_index} pc 0x{pc:X}: dynamic predicate is also an "
                "authored transition target"
            )
        if not callers:
            blockers.append(
                f"slot {slot_index} pc 0x{pc:X}: {first.source} has no concrete callers"
            )
            continue

        resolved: set[int] = set()
        for caller_slot, caller_pc, packed, supplied in callers:
            del caller_slot, caller_pc, packed
            context_calls = trace_slot(
                bank.slots[slot_index].bytecode,
                slot_index,
                _nested_local_frame(
                    (Value(0, "packed-id-placeholder"),)
                    + tuple(Value(value, "caller-local") for value in supplied)
                ),
            )
            matches = [event for event in context_calls if event.pc == pc]
            if len(matches) != 1 or not matches[0].args or matches[0].args[0].value is None:
                blockers.append(
                    f"slot {slot_index} pc 0x{pc:X}: caller context did not "
                    "resolve the first predicate word"
                )
                continue
            resolved.add(matches[0].args[0].value & 0xFFFF)
        first_words.update(resolved)
        dynamic_sites.append(
            DynamicSite(
                slot=slot_index,
                pc=pc,
                argument_count=len(call.args),
                source=first.source,
                concrete_first_words=tuple(sorted(resolved)),
                callers=callers,
            )
        )

    return BankDomain(
        path=str(path.resolve()),
        sha256=_sha256(raw),
        site_count=len(sites),
        argument_counts={str(key): value for key, value in sorted(argument_counts.items())},
        concrete_first_words=tuple(sorted(first_words)),
        authored_operand_patterns=tuple(
            {
                "words": words,
                "sources": sources,
                "site_count": count,
            }
            for (words, sources), sites_for_pattern in sorted(
                operand_pattern_sites.items(),
                key=lambda item: (
                    tuple(-1 if value is None else value for value in item[0][0]),
                    item[0][1],
                ),
            )
            for count in (len(sites_for_pattern),)
        ),
        dynamic_sites=tuple(dynamic_sites),
        blockers=tuple(sorted(set(blockers))),
    )


def build_pair_domain(paths: tuple[Path, Path]) -> dict[str, object]:
    banks = tuple(scan_bank(path) for path in paths)
    first_word_blockers = sorted(
        {
            blocker
            for bank in banks
            for blocker in bank.blockers
            if (
                "indirect CALLCOND 0x0D target" in blocker
                or "invalid packed target" in blocker
                or "dynamic predicate is also" in blocker
                or "has no concrete callers" in blocker
                or "resolve the first predicate word" in blocker
            )
        }
    )
    return {
        "schema": SCHEMA,
        "qualification": (
            "authored-domain-complete"
            if not any(bank.blockers for bank in banks)
            else "static-incomplete"
        ),
        "first_word_qualification": (
            "authored-domain-complete"
            if not first_word_blockers
            else "static-incomplete"
        ),
        "banks": [asdict(bank) for bank in banks],
        "pair": {
            "site_count": sum(bank.site_count for bank in banks),
            "argument_counts": dict(
                sorted(
                    sum(
                        (Counter({int(k): v for k, v in bank.argument_counts.items()}) for bank in banks),
                        Counter(),
                    ).items()
                )
            ),
            "concrete_first_words": sorted(
                set().union(*(bank.concrete_first_words for bank in banks))
            ),
            "blockers": sorted(set().union(*(bank.blockers for bank in banks))),
            "first_word_blockers": first_word_blockers,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("banks", nargs=2, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = build_pair_domain(tuple(args.banks))
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0 if result["qualification"] == "authored-domain-complete" else 2


if __name__ == "__main__":
    raise SystemExit(main())
