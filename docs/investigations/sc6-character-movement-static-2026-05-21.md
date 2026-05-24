# SC6 character movement static investigation - 2026-05-21

Latest analyzer run: 2026-05-23.

## Current Result

The static analyzer now decodes authored root-motion curves from `.mot` clips and emits player-useful movement measurements for selected routes.

Current generated result:

- 29 character KHD banks analyzed.
- 2,692 direction transitions decoded.
- 384 nonzero-bucket direction transitions audited and resolved as internal move-bank buckets.
- 2,953 movement candidates emitted.
- 29 selected backstep rows emitted.
- 10 trusted basic backsteps ranked.
- 203 selected movement-quality rows emitted across backstep, sidestep, forward, and diagonal movement categories.
- 97 selected movement routes are trusted by the route model.
- 98 selected movement routes remain unresolved.
- 0 selected movement routes have confirmed static recovery/return timing.
- 97 selected movement routes received a numeric score.

The output is still static-only. Distances are decoded authored root motion, not runtime position after wall, ring edge, terrain, pushbox, hurtbox, or opponent interaction.

## Outputs

Static analyzer:

- `tools/moveset_parser/analyze_movement_system_static.py`

Motion decoder:

- `tools/moveset_parser/hgmotion_reference.py`
- `tools/moveset_parser/motion_decode.py`

Generated evidence:

- `docs/investigations/generated/movement_slot_candidates.json`
- `docs/investigations/generated/movement_slot_candidates.csv`
- `docs/investigations/generated/movement_transition_edges.csv`
- `docs/investigations/generated/movement_static_summary.json`
- `docs/investigations/generated/movement_unknowns.json`
- `docs/investigations/generated/backstep_quality_static.json`
- `docs/investigations/generated/backstep_quality_static.csv`
- `docs/investigations/generated/backstep_motion_curves.csv`
- `docs/investigations/generated/movement_quality_static.json`
- `docs/investigations/generated/movement_quality_static.csv`
- `docs/investigations/generated/movement_motion_curves.csv`
- `docs/investigations/generated/movement_decode_failures.json`
- `docs/investigations/generated/movement_route_audit.csv`
- `docs/investigations/generated/cross_bank_direction_edges.csv`
- `docs/investigations/generated/cross_bank_route_resolution.csv`
- `docs/investigations/generated/basic_movement_routes.json`
- `docs/investigations/generated/basic_movement_routes.csv`
- `docs/investigations/generated/route_trust_evidence.json`
- `docs/investigations/generated/route_trust_evidence.csv`
- `docs/investigations/generated/cell_semantics_audit.csv`
- `docs/investigations/generated/unresolved_basic_routes.csv`
- `docs/investigations/generated/recovery_trust_audit.csv`
- `docs/investigations/generated/report_value_audit.csv`

Readable report:

- `docs/investigations/sc6-character-movement-player-readable-2026-05-21.md`

## Confirmed Motion Bank Layout

Ghidra shows `LuxMoveVM_InitMotionPlayback @ 0x140300400` indexes the motion bank as:

```c
clipOffset = motionBank[animIndex + 2];
clip = motionBank + clipOffset;
```

So the `.mot` file layout is:

| Offset | Field |
|---:|---|
| `+0x00` | `uint count` |
| `+0x04` | `uint reserved_04`, observed zero |
| `+0x08` | `uint offsets[count]` |

The old parser treated `+0x04` as the first offset. All generated movement evidence has been regenerated with the corrected `+0x08 + animIndex * 4` layout.

## Ported Motion Decode Path

The Python reference port follows these Ghidra functions:

- `LuxMoveVM_InitMotionPlayback @ 0x140300400`
- `LuxMotion_SampleKeyframeTransforms @ 0x1402E7780`
- `LuxMotion_DecodeHuffmanKeyframeData @ 0x1402E71E0`
- `LuxMotion_BitStreamReadBits @ 0x1402E6F90`
- `LuxMotion_BuildHuffmanTable @ 0x1402E7050`
- `LuxMotion_BlendKeyframeTransforms @ 0x1402E79C0`

Confirmed clip details:

- `+0x00 ushort frame_count`
- `+0x02 ushort decoded_word_count_x2`
- `+0x04 uint motion_flags`
- `+0x08 ulonglong channel_presence_mask`
- `+0x1C short[] frame_group_size_table`
- frame groups contain 8 frames each
- the base frame words are followed by a Huffman delta stream

The channel-type stream is not stored in the motion clip. Ghidra shows callers pass one of two global streams:

- `DAT_143e83c00` for the normal stream
- `DAT_143e83da0` when clip flag `0x8000` selects the alternate stream

The decoder currently extracts root-motion channel `0x14`, with `0x17` and `0x18` retained as known root-related channels for later validation. Precision and alternate scale bits come from clip flags at `+0x04`; the channel-presence mask comes from `+0x08`.

## Route Trust Model

The analyzer now uses one shared selector for the player-facing route, route audit, and backstep quality outputs. This fixes the previous split where one output could pick a route while another output picked a different route for the same character.

The current trust statuses are:

| Status | Meaning |
|---|---|
| `trusted_basic` | Direct movement input, same character bank, neutral-like source, decoded root motion, and no active offensive cell evidence on the destination route. |
| `trusted_basic_with_late_followup` | Same as trusted basic, but offensive cell evidence appears only after a static return/recovery point. |
| `trusted_stance_basic` | Basic movement inside a proven stance or mode, not universal neutral movement. |
| `measured_but_not_basic` | Root motion decodes, but the source state is not proven to be neutral or stance-root basic movement. |
| `attack_or_special` | Static cell timing proves active offensive behavior during the movement window. |
| `unresolved` | Static data is insufficient to prove whether the route is basic movement. |

The important correction is that `has_attack_cell` no longer automatically means attack-linked. The analyzer now inspects each referenced cell and classifies it before deciding whether the movement route is blocked from ranking. Candidate scoring also no longer gives a raw bonus or penalty for merely having a cell reference.

## Cell Semantics

`tools/moveset_parser/route_trust.py` classifies slot cell references into static roles using the parsed `FLuxBattleAttackCell` fields:

- `offensive_attack`: damage-bearing cell with a valid active window.
- `offensive_cell_window_unknown`: damage-bearing cell with an unresolved or invalid active window.
- `header`, `nondamaging`, `sentinel`: non-damage cells, treated as body/collision/metadata until a stronger role is proven.
- `cross_bank_or_unknown`: referenced cell is outside the local KHD bank and must not be trusted yet.

The current trust model blocks ranking when offensive cells exist and no static return/recovery frame is known. That is why Hilde now reports as unresolved with a concrete reason instead of the vague previous "needs route proof" label.

The previous "cross-bank" label was wrong. Ghidra's `LuxMoveVM_ResolveBankSlot @ 0x1402FC400` proves that packed move ids are resolved inside the current `FLuxMoveBank`:

```text
bank = (packedMoveId >> 12) & 0xF
slot = packedMoveId & 0x7FF
linearSlot = bucket[bank].start + slot
```

So bank ids `1`, `2`, and `3` are not external KHD/MOT files. They are internal buckets in the same character move bank. The parser, graph builder, route resolver, and recovery model now use that rule.

## Cross-Bank Route Resolution

The resolver now handles these cases:

- `resolved_local`: bank `0`, proven to target the current character KHD slot table.
- `resolved_move_bucket`: bank `1..3`, proven to target another bucket inside the current character `FLuxMoveBank`.
- explicit unresolved statuses: unknown bank id, missing target slot, missing motion bank, or indirect bank context.

Current counts:

| Field | Count |
|---|---:|
| `cross_bank_direction_edge_count` | 384 |
| `cross_bank_resolved_count` | 384 |
| `cross_bank_unresolved_count` | 0 |

No nonzero-bucket edge is silently dropped. These edges can now participate in route selection and recovery audits as same-character routes.

## Recovery Trust Model

`tools/moveset_parser/recovery_trust.py` replaces the old coarse slot/cell recovery estimate.

For every selected route, the analyzer now inspects frame-gated outgoing edges from the movement destination and classifies them as:

- return to neutral/control,
- stance return,
- movement loop,
- attack follow-up,
- unresolved cross-bank recovery,
- unresolved destination/cell semantics,
- unknown frame edge.

The current model is intentionally conservative. It does not treat a non-offensive frame target as recovery unless the target is a proven neutral-like source or stance root. Current counts:

| Field | Count |
|---|---:|
| `recovery_confirmed_count` | 0 |
| `recovery_unknown_count` | 203 |
| `late_followup_unranked_count` | 0 |

That means `trusted_basic_with_late_followup` is not emitted in this run. Routes that look like movement but contain offensive timing remain `unresolved` until recovery semantics are proven.

Ghidra support for this model comes primarily from:

- `LuxMoveVM_EvaluateAttackRange @ 0x14035F670`, which reads active attack-cell range and reach fields.
- `LuxBattle_CheckYarareGate_StepRange @ 0x140360650`, which gates step-range hit interaction against the opponent active cell.
- `LuxBattle_CheckYarareGate_BackStepRange @ 0x1403607F0`, which gates backstep interaction using close and medium backstep range buckets.
- `LuxBattleChara_LoadMovesetEntries_AndBoneData @ 0x140312040`, which ties KHD move slots, motion entries, and runtime move tables together.

## Backstep Quality Result

Clean canonical backsteps ranked in the latest run:

| Character | Grade | Frame 4 | Frame 8 | Frame 16 | Total |
|---|---:|---:|---:|---:|---:|
| Xianghua | S | 0.306 | 0.562 | 0.705 | 0.826 |
| Sophitia | A | 0.006 | 0.502 | 1.185 | 1.526 |
| Siegfried | A | 0.026 | 0.104 | 0.399 | 0.819 |
| Nightmare | B | 0.026 | 0.104 | 0.399 | 0.819 |
| Hwang | B | 0.027 | 0.090 | 0.267 | 0.297 |
| Mitsurugi | B | 0.027 | 0.048 | 0.167 | 0.180 |
| Maxi | B | 0.048 | 0.066 | 0.011 | 0.069 |
| Cervantes | C | 0.018 | 0.040 | 0.042 | 0.089 |
| Amy | C | 0.000 | 0.000 | 0.000 | 0.000 |
| Haohmaru | D | 0.000 | 0.000 | 0.000 | 0.000 |

Unranked characters can still have decoded distance values. They are not ranked when the selected route is attack-linked, stance-adjacent, special-movement-adjacent, or otherwise not clean enough to call basic backstep.

## Movement Quality Outputs

`movement_quality_static.csv` extends the same model to:

- backstep,
- sidestep up,
- sidestep down,
- forward step,
- back diagonal,
- forward diagonal,
- ambiguous 8-way movement.

Current ranked counts:

| Movement type | Ranked rows |
|---|---:|
| backstep | 10 |
| sidestep up | 8 |
| sidestep down | 26 |
| forward step | 11 |
| back diagonal | 4 |
| forward diagonal | 10 |
| ambiguous 8-way movement | 28 |

The broad movement file is useful for investigation, but the player-facing report emphasizes the cleanest backstep, sidestep, and forward-step rows.

Current selected-route trust counts:

| Trust status | Rows |
|---|---:|
| `trusted_basic` | 97 |
| `measured_but_not_basic` | 8 |
| `unresolved` | 98 |

Late-followup routes are no longer promoted by the current recovery pass. The analyzer prefers an unresolved neutral/stance route over unrelated measured movement, but it does not rank that route until recovery is proven.

## Hit And Hurtbox Relevance

Backstep quality is not only distance.

`LuxBattle_CheckYarareGate_BackStepRange @ 0x1403607F0` uses special range buckets:

- close landmark: `16.0`
- backstep medium landmark: `30.0`

Those values remain useful hit-rule landmarks. None of the decoded authored root curves cross those values directly, so the report treats them as engine context rather than as a distance grade.

Hit and hurtbox interaction still depends on:

- active attack reach,
- defender hurtbox pose,
- movement state gates,
- pushbox/body collision,
- wall and ring-edge clipping,
- terrain,
- recovery after the motion route.

## Ghidra Work Completed

Updated or confirmed annotations were applied around:

- `LuxMoveVM_InitMotionPlayback @ 0x140300400`
- `LuxMotion_SampleKeyframeTransforms @ 0x1402E7780`
- `LuxMotion_DecodeHuffmanKeyframeData @ 0x1402E71E0`
- `LuxMotion_BlendKeyframeTransforms @ 0x1402E79C0`
- `LuxMotion_BitStreamReadBits @ 0x1402E6F90`
- `LuxMotion_BuildHuffmanTable @ 0x1402E7050`
- `LuxBattleChara_UpdateVelocityFromBoneMotion @ 0x1403043D0`
- `LuxMoveVM_UpdateMoveDataTable @ 0x14038F7D0`
- `LuxMoveVM_InitStaticMoveDataTable @ 0x14038F6F0`
- `LuxMoveVM_InitCharaFromMoveTable @ 0x140309B20`
- `LuxBattleChara_LoadMovesetEntries_AndBoneData @ 0x140312040`
- `LuxMoveVM_EvaluateAttackRange @ 0x14035F670`

The key correction from Ghidra is that root channel stream selection comes from the animation evaluation caller, not from scanning arbitrary motion data.

The latest bank/recovery pass added Ghidra bookmarks under `SC6MovementStatic` and improved local typing in `LuxMoveVM_EvaluateAttackRange`: `pAttackerChara` is typed as `FLuxBattleChara *`, `pCell` is typed as `FLuxBattleAttackCell *`, and the projected reach/slot locals have clearer names. Ghidra was saved after these annotations.

## Remaining Unknowns

The remaining static gaps are:

- clean route selection for every character,
- full left/right sidestep symmetry analysis,
- guard/block/punish recovery for every movement route,
- hurtbox pose over time,
- opponent collision, wall, terrain, and ring-edge adjustment,
- matchup-specific whiff outcomes.

## Validation From Latest Run

Commands run after updating the analyzer:

```powershell
python -m pytest -q tools\moveset_parser\tests
python tools\moveset_parser\analyze_movement_system_static.py --dump-battle E:\myMods\dump\Battle --full-dump-battle "C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump\Battle" --out docs\investigations\generated
python tools\moveset_parser\render_movement_player_report.py --generated docs\investigations\generated --out docs\investigations\sc6-character-movement-player-readable-2026-05-21.md
python tools\moveset_parser\validate_movement_outputs.py --generated docs\investigations\generated
```

Results:

- Full parser test suite passed: 153 tests.
- Analyzer completed successfully.
- Generated movement curves contain high-confidence rows from decoded root-motion channel `0x14`.
- Numeric scores are only emitted for high-confidence decoded routes.
- Cross-bank route resolution covers all 384 audited cross-bank direction links.
- Report value audit returned zero mismatches.
- Player-readable report safety scan returned no parser/raw-route terms.
- Runtime sampler string scan returned no matches.
