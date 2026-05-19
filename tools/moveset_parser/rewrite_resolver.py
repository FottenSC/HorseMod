from pathlib import Path
path = Path(r"E:/myMods/tools/moveset_parser/export_webui_data.py")
text = path.read_text(encoding='utf-8')
start = text.index("def _resolve_main_index(")
end = text.index("\n\n\n_THROW_INPUT_RE", start)
new_block = '''def _resolve_main_index(
    main_index: int,
    khd: KhdFile | None,
) -> dict[str, int]:
    """Resolve a MovePlayData `MainIndex` value to the cell-and-slot the
    UI should join against. Empirically this field is hybrid:

      * For ~70% of moves: a DIRECT index into the Attack-cell table
        (i.e. it IS the cell number).
      * For the remaining ~30%: an index into the slot table; the slot's
        first valid cell variant is the move's hit data.

    Both interpretations are valid and there's no flag distinguishing
    them at this layer. Heuristic: prefer the cell interpretation if
    `cells[mainIndex]` is an Attack-role cell; otherwise fall back to
    the slot. This gets 84%+ of Mitsurugi's moves correctly mapped.
    """
    out = {
        "cellIdx": -1,
        "slotIdx": -1,
        "resolution": "none",
        "candidateCount": 0,
        "candidateBestRank": 0,
        "candidateScore": 0,
    }
    if khd is None or main_index <= 0:
        return out

    cells = khd.sections[0].entries if khd.sections else []
    slots = khd.slots

    def _score_cell(cell: LuxBattleAttackCell, variant_index: int = 0) -> int:
        """Heuristic score for selecting the best candidate attack cell."""
        if cell.cell_role != "Attack" or cell.wI16BaseDamage <= 0:
            return -1_000_000
        score = 50
        score += min(cell.wI16BaseDamage, 80)
        active_len = cell.wI16MasterWindowEnd - cell.wI16MasterWindowStart
        if active_len > 0:
            score += min(active_len, 30)
        if cell.wI16MasterWindowStart >= 0:
            score += 2
        # Prefers earlier slot variants when otherwise tied.
        score += max(0, 3 - variant_index)
        return score

    def _best_slot_attack_candidate(slot_idx: int) -> tuple[int, int, int] | None:
        if slot_idx < 0 or slot_idx >= len(slots):
            return None
        slot = slots[slot_idx]
        best: tuple[int, int, int] | None = None
        for variant_idx, c in enumerate(slot.nCellBoneIndexPerVariant):
            if not (0 <= c < len(cells)):
                continue
            cell = cells[c]
            score = _score_cell(cell, variant_idx)
            if score < 0:
                continue
            cand = (score, c, variant_idx)
            if best is None or cand > best:
                best = cand
        return best

    # Try as cell index first.
    if 0 <= main_index < len(cells):
        cell = cells[main_index]
        if cell.cell_role == "Attack":
            direct_score = _score_cell(cell, 0)
            # Reverse-resolve to a slot that USES this cell (for navigation).
            matching_slots = []
            for s_idx, s in enumerate(slots):
                if main_index in s.nCellBoneIndexPerVariant:
                    matching_slots.append(s_idx)
                    break
            out.update(
                {
                    "cellIdx": main_index,
                    "slotIdx": matching_slots[0] if matching_slots else -1,
                    "resolution": "cell-direct",
                    "candidateCount": 1,
                    "candidateBestRank": 1,
                    "candidateScore": direct_score,
                }
            )

            # If mainIndex also points to a slot, compare slot-local
            # candidates and switch only when the slot candidate is
            # materially stronger.
            if matching_slots:
                replacement = None
                replacement_slot = -1
                for slot_idx in matching_slots:
                    slot_best = _best_slot_attack_candidate(slot_idx)
                    if slot_best is None:
                        continue
                    slot_score, slot_cell_idx, _slot_variant = slot_best
                    if replacement is None or slot_score > replacement[0]:
                        replacement = (slot_score, slot_cell_idx)
                        replacement_slot = slot_idx
                if replacement is not None:
                    slot_score, slot_cell_idx = replacement
                    out["candidateCount"] = 2
                    out["candidateScore"] = direct_score
                    if slot_score - direct_score >= 16:
                        out.update(
                            {
                                "cellIdx": slot_cell_idx,
                                "slotIdx": replacement_slot,
                                "resolution": "slot-overrides-direct",
                                "candidateBestRank": 2,
                                "candidateScore": slot_score,
                            }
                        )
                            return out
            return out

        # main_index is non-attack as cell; try the slot interpretation
        # and return the best attack variant available.
        if 0 <= main_index < len(slots):
            slot_best = _best_slot_attack_candidate(main_index)
            if slot_best is not None:
                slot_score, slot_cell_idx, _slot_variant = slot_best
                out.update(
                    {
                        "cellIdx": slot_cell_idx,
                        "slotIdx": main_index,
                        "resolution": "slot",
                        "candidateCount": 1,
                        "candidateBestRank": 1,
                        "candidateScore": slot_score,
                    }
                )
                return out

    # Fall back to slot.
    if 0 <= main_index < len(slots):
        slot_best = _best_slot_attack_candidate(main_index)
        if slot_best is not None:
            slot_score, cell_idx, _slot_variant = slot_best
            out.update(
                {
                    "cellIdx": cell_idx,
                    "slotIdx": main_index,
                    "resolution": "slot",
                    "candidateCount": 1,
                    "candidateBestRank": 1,
                    "candidateScore": slot_score,
                }
            )
            return out
        # Slot exists but has no attack cell — still a valid navigation target.
        out["slotIdx"] = main_index
        out["resolution"] = "slot-no-cell"

    return out
'''
new_text = text[:start] + new_block + text[end:]
path.write_text(new_text, encoding='utf-8')
