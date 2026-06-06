"""
Build a user-facing move graph from the per-slot TransitionEvents.

Pipeline:
  1. Run `stackvm_emulate.emulate` over every slot's bytecode to get a
     list of TransitionEvents.
  2. Build the directed graph of slot→slot edges (an edge per transition).
  3. Score slots as "stance roots" by their distinct user-input
     out-degree. The top scorers are the canonical neutral/standing /
     crouching / stance-entry roots.
  4. For each root, BFS the graph following user-input edges. Each path
     becomes a "user-facing move" with an input string and a chain of
     slot indices ending at a slot that has the attack cell of interest.

The output is independent of the chosen "canonical" neutral root — we
emit ALL slots' transitions, and let the UI choose which root to render.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from stackvm import walk_stackvm
from stackvm_emulate import emulate, decode_predicate, DecodedInput, Concrete, EffectEvent
from luxformats import KhdFile


# Input-kind classifier: which DecodedInput.kind values count as
# "this is something the player did", versus auto/state-driven.
USER_INPUT_KINDS = {"buttons", "direction", "command"}
AUTO_KINDS = {"auto", "frame", "stance", "from-move", "range"}


@dataclass
class SlotEdge:
    """One transition edge, expressed for graph consumers."""
    src_slot: int
    dst_slot: int                 # resolved linear slot index in KHD slot table
    dst_bank: int                 # FLuxMoveBank bucket index from packed move-id
    raw_move_id: int              # the full packed (bank<<12)|slot
    predicate_text: str
    predicate_kind: str           # "buttons" / "direction" / "command" / "auto" / "stance" / "frame" / "from-move" / "other" / "indirect" / "always"
    predicate_sub_op: Optional[int]  # the EvaluateIfOpcode sub-opcode kind
    predicate_args: list[Optional[int]]  # concrete values (None where indirect)
    is_indirect: bool             # True iff next_move_id is from LOAD_VAR
    source_pc: int
    callcond_idx: int             # 0x05..0x08 (TransitionAuthor variant)


@dataclass
class SlotGraph:
    """Holds the full slot transition graph for one character."""
    edges_by_src: dict[int, list[SlotEdge]] = field(default_factory=dict)
    edges_by_dst: dict[int, list[SlotEdge]] = field(default_factory=dict)
    # Per-slot incoming user-input count (a coarse "how often this slot is
    # entered by a user input" signal). Used to find non-root meaningful
    # nodes.
    user_in_count: dict[int, int] = field(default_factory=dict)
    user_out_count: dict[int, int] = field(default_factory=dict)
    distinct_user_inputs_out: dict[int, set[str]] = field(default_factory=dict)
    effects_by_src: dict[int, list[EffectEvent]] = field(default_factory=dict)


def build_slot_graph(bank: KhdFile, file_bytes: bytes) -> SlotGraph:
    """Walk every slot, emulate, build the directed graph."""
    g = SlotGraph()
    for slot in bank.slots:
        if slot.dwBytecodeOffset_38 == 0:
            continue
        try:
            script = walk_stackvm(file_bytes, slot.dwBytecodeOffset_38,
                                  max_bytes=0x10000)
        except Exception:
            continue
        result = emulate(script, slot.slot_index)
        g.effects_by_src[slot.slot_index] = result.effects
        edges: list[SlotEdge] = []
        for t in result.transitions:
            if t.next_move_slot is None:
                continue
            # Packed move ids are resolved through LuxMoveVM_ResolveBankSlot:
            # bits 15..12 select one of four buckets inside the same
            # FLuxMoveBank, and bits 10..0 select the slot within that
            # bucket. The graph stores the resolved linear slot index.
            resolved_slot = bank.resolve_packed_slot(t.next_move_id_raw)
            if resolved_slot is None:
                continue
            pred = decode_predicate(t.predicate)
            sub = t.predicate.sub_opcode if t.predicate else None
            arg_concretes: list[Optional[int]] = []
            if t.predicate:
                for a in t.predicate.args:
                    if isinstance(a, Concrete):
                        arg_concretes.append(a.value)
                    else:
                        arg_concretes.append(None)
            edge = SlotEdge(
                src_slot=slot.slot_index,
                dst_slot=resolved_slot,
                dst_bank=t.next_move_bank or 0,
                raw_move_id=t.next_move_id_raw or 0,
                predicate_text=pred.text,
                predicate_kind=pred.kind,
                predicate_sub_op=sub,
                predicate_args=arg_concretes,
                is_indirect=t.is_indirect,
                source_pc=t.source_pc,
                callcond_idx=t.callcond_idx,
            )
            edges.append(edge)
            g.edges_by_dst.setdefault(resolved_slot, []).append(edge)
            if pred.kind in USER_INPUT_KINDS:
                g.user_in_count[resolved_slot] = g.user_in_count.get(resolved_slot, 0) + 1
            if pred.kind in USER_INPUT_KINDS:
                g.user_out_count[slot.slot_index] = g.user_out_count.get(slot.slot_index, 0) + 1
                g.distinct_user_inputs_out.setdefault(slot.slot_index, set()).add(pred.text)
        g.edges_by_src[slot.slot_index] = edges
    return g


@dataclass
class StanceRoot:
    """A slot identified as a probable stance root."""
    slot_idx: int
    anim_index: int
    distinct_user_inputs: int
    total_user_outgoing: int
    incoming: int
    label: str = ""               # Auto-assigned (e.g., "stance#0 (anim 186)")


def identify_stance_roots(
    bank: KhdFile,
    g: SlotGraph,
    min_distinct_inputs: int = 2,
    min_total_outgoing: int = 4,
    max_roots: int = 24,
) -> list[StanceRoot]:
    """Identify the top stance-root candidates.

    A stance root is a slot from which many transitions originate — the
    player's neutral standing, crouching, stance variants, etc. We use
    TWO inclusion criteria (OR'd):

      1. distinct user-input out-edges >= min_distinct_inputs
         (catches "button-rich" neutrals like Mitsurugi's slot 406)

      2. total outgoing transitions >= min_total_outgoing
         (catches "dispatch hub" slots whose bytecode uses state-driven
         or frame-window gates instead of direct input checks — Astaroth
         and other slow grappler-style charas tend to look like this)

    Sentinel slots (anim == 0xFFFF) are excluded — they have no real
    animation and pollute the root list with junk.

    Dedup by animation index so duplicate-stance slots (one per facing
    side, etc.) collapse to a single entry. Tie-break by the full
    (distinct, total, incoming) tuple — keeps the most-meaningful rep.
    """
    candidates: list[StanceRoot] = []
    for slot in bank.slots:
        if slot.wAnimationIndex_00 == 0xFFFF:  # sentinel; skip
            continue
        distinct = len(g.distinct_user_inputs_out.get(slot.slot_index, set()))
        # total = ALL outgoing edges (user-input + state-gated + frame-gated).
        total = len(g.edges_by_src.get(slot.slot_index, []))
        if distinct < min_distinct_inputs and total < min_total_outgoing:
            continue
        user_total = g.user_out_count.get(slot.slot_index, 0)
        incoming = len(g.edges_by_dst.get(slot.slot_index, []))
        candidates.append(StanceRoot(
            slot_idx=slot.slot_index,
            anim_index=slot.wAnimationIndex_00,
            distinct_user_inputs=distinct,
            total_user_outgoing=total,    # field re-purposed: total transitions
            incoming=incoming,
        ))
    # Sort by (distinct, total, incoming) desc.
    candidates.sort(key=lambda r: (-r.distinct_user_inputs,
                                   -r.total_user_outgoing,
                                   -r.incoming))

    # Dedup by anim_index (keep the highest-scored representative per anim).
    seen_anims: dict[int, StanceRoot] = {}
    for r in candidates:
        existing = seen_anims.get(r.anim_index)
        if (existing is None
                or (r.distinct_user_inputs, r.total_user_outgoing, r.incoming)
                   > (existing.distinct_user_inputs,
                      existing.total_user_outgoing,
                      existing.incoming)):
            seen_anims[r.anim_index] = r
    deduped = sorted(seen_anims.values(),
                     key=lambda r: (-r.distinct_user_inputs,
                                    -r.total_user_outgoing,
                                    -r.incoming))
    out = deduped[:max_roots]
    for i, r in enumerate(out):
        r.label = f"root#{i} (slot {r.slot_idx}, anim {r.anim_index})"
    return out


def serialize_edge(e: SlotEdge) -> dict:
    """Plain-JSON-able dict for export_webui_data.py."""
    return {
        "src": e.src_slot,
        "dst": e.dst_slot,
        "bank": e.dst_bank,
        "rawId": e.raw_move_id,
        "input": e.predicate_text,
        "kind": e.predicate_kind,
        "subOp": e.predicate_sub_op,
        "args": e.predicate_args,
        "indirect": e.is_indirect,
        "callcond": e.callcond_idx,
        "pc": e.source_pc,
    }


def serialize_effect(e: EffectEvent) -> dict:
    """Plain-JSON-able effect event for export_webui_data.py."""
    return {
        "opcode": e.opcode,
        "opcodeHex": f"0x{e.opcode:04X}" if e.opcode is not None else None,
        "kind": e.kind,
        "args": e.concrete_args,
        "callcond": e.callcond_idx,
        "pc": e.source_pc,
        "isFacingRelated": e.is_facing_related,
        "targetWeight": e.target_weight,
        "rampSelector": e.ramp_selector,
    }


def serialize_root(r: StanceRoot) -> dict:
    return {
        "slot": r.slot_idx,
        "anim": r.anim_index,
        "label": r.label,
        "distinctInputs": r.distinct_user_inputs,
        "totalOutgoing": r.total_user_outgoing,
        "incoming": r.incoming,
    }


@dataclass
class FlatMove:
    """One attack-cell-bearing slot with the simplest input path to reach it."""
    slot_idx: int
    anim_index: int
    cell_idx: int                  # the slot's primary attack cell
    input_path: list[str]          # input strings from a root, oldest-first
    kind_path: list[str]
    slot_path: list[int]           # slots visited, root excluded
    root_slot: int                 # where the path started
    root_anim: int


def build_flat_moves(
    bank: KhdFile,
    g: SlotGraph,
    roots: list[StanceRoot],
    max_input_steps: int = 6,
    max_state_followup: int = 10,
) -> list[FlatMove]:
    """Enumerate EVERY attack-cell-bearing slot in the bank as a move.

    The user's expectation for SC6 characters is ~150-180 moves per
    fighter (matches: bank attack-cell count ≈ 250-350, deduped by
    anim/slot semantics). Our per-character command table (the .uasset
    side, at chara+0x971D8) isn't parsed yet, so we can't always know
    the canonical input notation. But we can ALWAYS enumerate the
    attack-bearing slots — that's the brute-force "every move" list.

    For each such slot we attach the best-effort input we can derive:
      1. Shortest user-input path from any stance root (BFS, two-phase
         with user-input + state closure).
      2. Falls back to cell.inputCond (button class A/B/K/G) when no
         path is found — denoted by a leading "?" to indicate uncertain
         direction component.

    Result: every attack-bearing slot becomes a row in the moves table.
    Coverage = 100% of attack-bearing slots. Input quality varies.
    """
    # Number of locally-defined cells (cellVariants values >= this are
    # cross-bank references to shared/common cells — we can't dereference
    # them without loading the shared cell file, so we skip them for now).
    local_cell_count = len(bank.sections[0].entries) if bank.sections else 0
    cell_slots: set[int] = set()
    for slot in bank.slots:
        if slot.wAnimationIndex_00 == 0xFFFF:
            continue
        if any(0 <= c < local_cell_count for c in slot.nCellBoneIndexPerVariant):
            cell_slots.add(slot.slot_index)

    # Edge kinds that count as "state-driven internal transitions" we'll
    # follow during phase 2 (without consuming an input step). Everything
    # except user-input and indirect.
    INTERNAL_KINDS = {"frame", "auto", "stance", "from-move", "always", "range", "other"}

    # Phase 1: BFS over user-input edges.
    # State: (slot, input_depth, [inputs], [kinds], [slots], root, root_anim)
    moves: dict[int, FlatMove] = {}
    from collections import deque

    def record_cell_slot(cell_slot: int,
                        inputs: list, kinds: list, path: list,
                        root_slot: int, root_anim: int) -> None:
        """Record a cell-bearing slot as a move iff we haven't seen it
        with a shorter input-path before AND it has at least one user
        input. A move requires the player to do something; cell-bearing
        slots that happen to also be stance roots aren't "moves" — they
        only become moves once you press something to reach them via
        the rest of the graph."""
        if len(inputs) == 0:
            return
        existing = moves.get(cell_slot)
        if existing is not None and len(existing.input_path) <= len(inputs):
            return
        slot = bank.slots[cell_slot]
        cell_idx = next((c for c in slot.nCellBoneIndexPerVariant
                         if 0 <= c < local_cell_count), -1)
        moves[cell_slot] = FlatMove(
            slot_idx=cell_slot,
            anim_index=slot.wAnimationIndex_00,
            cell_idx=cell_idx,
            input_path=list(inputs),
            kind_path=list(kinds),
            slot_path=list(path),
            root_slot=root_slot,
            root_anim=root_anim,
        )

    def state_closure(entry_slot: int,
                      inputs: list, kinds: list, path: list,
                      root_slot: int, root_anim: int) -> None:
        """Phase 2: from `entry_slot`, follow non-user-input edges up to
        `max_state_followup` hops and record every cell-bearing slot we
        reach. Includes `entry_slot` itself if it has a cell."""
        if entry_slot in cell_slots:
            record_cell_slot(entry_slot, inputs, kinds, path, root_slot, root_anim)
        visited: set[int] = {entry_slot}
        queue: deque = deque()
        queue.append((entry_slot, 0, list(path)))
        while queue:
            slot_idx, depth, slots_so_far = queue.popleft()
            if depth >= max_state_followup:
                continue
            for edge in g.edges_by_src.get(slot_idx, []):
                if edge.dst_bank != 0:
                    continue
                if edge.predicate_kind not in INTERNAL_KINDS:
                    continue
                if edge.dst_slot in visited:
                    continue
                visited.add(edge.dst_slot)
                new_path = slots_so_far + [edge.dst_slot]
                if edge.dst_slot in cell_slots:
                    record_cell_slot(edge.dst_slot, inputs, kinds, new_path,
                                     root_slot, root_anim)
                queue.append((edge.dst_slot, depth + 1, new_path))

    # Multi-source BFS, phase 1.
    # Seed from BOTH the labelled stance roots AND every "orphan" slot
    # — a slot with at least one outgoing user-input edge but ZERO
    # incoming user-input edges. Orphans are by definition input-
    # dispatch entry points (the engine reached them without consuming
    # a user input, e.g. through anim-id triggering or sentinel
    # trampoline slots with anim=0xFFFF). Seeding them lets BFS find
    # paths to slots downstream of these entry points.
    incoming_user_input: dict[int, int] = {}
    for slot_idx, outgoing in g.edges_by_src.items():
        for e in outgoing:
            if e.predicate_kind in USER_INPUT_KINDS and e.dst_bank == 0:
                incoming_user_input[e.dst_slot] = incoming_user_input.get(e.dst_slot, 0) + 1
    best_input_depth: dict[int, int] = {}
    p1: deque = deque()
    seeded: set[int] = set()
    for r in roots:
        p1.append((r.slot_idx, 0, [], [], [], r.slot_idx, r.anim_index))
        best_input_depth[r.slot_idx] = 0
        seeded.add(r.slot_idx)
    for slot_idx, outgoing in g.edges_by_src.items():
        if slot_idx in seeded:
            continue
        # Skip slots that are reachable via user inputs from somewhere else
        # — they're not entry points, they're intermediates and BFS will
        # discover them naturally.
        if incoming_user_input.get(slot_idx, 0) > 0:
            continue
        if any(e.predicate_kind in USER_INPUT_KINDS for e in outgoing):
            slot = bank.slots[slot_idx]
            p1.append((slot_idx, 0, [], [], [], slot_idx, slot.wAnimationIndex_00))
            best_input_depth[slot_idx] = 0
            seeded.add(slot_idx)

    while p1:
        slot_idx, depth, inputs, kinds, slots, root_slot, root_anim = p1.popleft()
        # At every BFS node, run state-closure to harvest cell slots.
        state_closure(slot_idx, inputs, kinds, slots, root_slot, root_anim)
        if depth >= max_input_steps:
            continue
        for edge in g.edges_by_src.get(slot_idx, []):
            if edge.predicate_kind not in USER_INPUT_KINDS:
                continue
            if edge.dst_bank != 0:
                continue
            new_depth = depth + 1
            existing = best_input_depth.get(edge.dst_slot)
            if existing is not None and existing <= new_depth:
                continue
            best_input_depth[edge.dst_slot] = new_depth
            p1.append((
                edge.dst_slot, new_depth,
                inputs + [edge.predicate_text],
                kinds + [edge.predicate_kind],
                slots + [edge.dst_slot],
                root_slot, root_anim,
            ))

    # Step 3 — Enumerate EVERY ATTACK-ROLE cell-bearing slot. Any that
    # wasn't already filled by the BFS gets a synthetic "?"+button-class
    # input from its cell's wU16InputCond field. Skip:
    #   - Slots already recorded by BFS
    #   - Slots that ARE themselves stance roots (their "input" is "from
    #     neutral", not the per-cell button class)
    #   - Slots whose primary cell role isn't "Attack" (Header /
    #     NonDamaging / Sentinel — these aren't user-facing moves)
    root_slot_set = {r.slot_idx for r in roots}
    for slot_idx in cell_slots:
        if slot_idx in moves or slot_idx in root_slot_set:
            continue
        slot = bank.slots[slot_idx]
        cell_idx = next((c for c in slot.nCellBoneIndexPerVariant
                         if 0 <= c < local_cell_count), -1)
        if cell_idx < 0:
            continue
        cell = bank.sections[0].entries[cell_idx]
        if cell.cell_role != "Attack":
            continue
        # We DO record the move so the UI can list every attack-cell in
        # the bank, but we don't fabricate a button-class label like "?A".
        # The UI shows "unknown" for kind_path == ["unknown"]. The actual
        # canonical input requires the per-chara command-input table at
        # chara+0x971D8 (in .uasset, not parsed offline yet).
        moves[slot_idx] = FlatMove(
            slot_idx=slot_idx,
            anim_index=slot.wAnimationIndex_00,
            cell_idx=cell_idx,
            input_path=[],
            kind_path=["unknown"],
            slot_path=[slot_idx],
            root_slot=-1,
            root_anim=-1,
        )

    return sorted(moves.values(), key=lambda m: (
        # Rank: known-input first (kind != "unknown"), then by path length,
        # then by slot index for stability.
        0 if m.kind_path and m.kind_path[0] != "unknown" else 1,
        len(m.input_path),
        m.slot_idx,
    ))


def serialize_flat_move(m: FlatMove) -> dict:
    return {
        "slot": m.slot_idx,
        "anim": m.anim_index,
        "cell": m.cell_idx,
        "inputs": m.input_path,
        "kinds": m.kind_path,
        "slots": m.slot_path,
        "rootSlot": m.root_slot,
        "rootAnim": m.root_anim,
    }
