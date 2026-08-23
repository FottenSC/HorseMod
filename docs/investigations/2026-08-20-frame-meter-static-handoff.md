# SC6 frame meter static handoff (v1)

Status: **static-only; runtime-unvalidated**. No game, replay, deployment,
UE4SS trace, or live-memory inspection was used for this handoff.

## Deliverables

- `tools/moveset_parser/frame_meter_static.py` builds the native-key ledger,
  validates every record, writes deterministic JSON, and generates the compact
  C++ lookup table. It reuses `native_frame_analysis.py`; it is not a parallel
  timing engine.
- `tools/moveset_parser/schemas/frame-meter-static-v1.schema.json` fixes the
  interchange schema.
- `artifacts/frame-meter-static-v1/frame-meter-static-v1.json` is the frozen
  corpus result, including executable, KHD, and `yarare.dat` hashes.
- `HorseMod/generated/FrameMeterStaticData.generated.hpp` is the compact table.
- `HorseMod/horselib/FrameMeterLiveReaderContract.hpp` is the typed live-reader
  contract. Its `kRuntimeValidated` constant is intentionally false.

Regenerate from `E:\myMods`:

```powershell
python tools/moveset_parser/frame_meter_static.py `
  --battle-root dump/Battle `
  --json artifacts/frame-meter-static-v1/frame-meter-static-v1.json `
  --cpp HorseMod/generated/FrameMeterStaticData.generated.hpp
```

Run the focused acceptance checks:

```powershell
python -m pytest tools/moveset_parser/tests/test_frame_meter_static.py -q
```

Final static acceptance on 2026-08-20: the complete moveset-parser suite
passed (`681 passed in 1453.54s`), the generated JSON passed both
`validate_dataset()` after a write/load round trip and Draft 2020-12 schema
validation, and Hermes reported no remaining static correctness blocker.

## Native identity

The UI must build this exact key:

`style, bank kind, packed move, attack-cell index, contact mode, reaction context, reaction row, contact coordinate`.

Packed move bits 15..12 select the `FLuxMoveBank` bucket, bits 10..0 select
the slot, and bit 11 is ignored by native slot resolution. The export clears
bit 11 so aliases have one canonical key; the live reader must apply the same
mask before lookup. Reaction context records the grounded/air, promoted,
guard-condition, classifier, or throw route and is not merely physical posture.
Cell identity is
pointer arithmetic against the selected bank's Section-A base with an exact
0x70-byte stride. Any null pointer, out-of-range pointer, non-integral stride,
hash mismatch, missing posture, or missing reaction route suppresses the
number. Official movelist names are not part of lookup identity.

## Clock and endpoint boundary

`LuxMoveVM_ClassifyHitboxFrameState @ 0x140300620` truncates the lane's float
animation coordinate. It labels start inclusive as Active and labels phase 3
as PostActive. Phase 3 is not actionability evidence.

The common audited 0x3020 recovery route queues its transition at the authored
lane end minus its recovery lead. `LuxMoveVM_ExecuteOpStream @ 0x1402FDEA0`
drains the transition before `LuxMoveVM_AdvanceLaneFrame`. Consequently the
exported `lastLockedAttackerCoordinate` is `totalFrames - recoveryLead - 1`.
That is an animation coordinate, not an elapsed-tick proof. Lane advancement
also uses authored playback speed and per-character time dilation, so
`lastLocked-c+1` must not be presented as recovery ticks without symbolic
coordinate-to-step execution. The exact same-tick decrement and command-
acceptance ordering is also still open. Version 1 therefore exports no numeric
advantage and leaves both `*FirstActionableTick` fields null.

The ledger emits one record per **authored candidate coordinate**. It does not
call every integer in the master window reachable: playback jumps, subwindows,
alternate lanes, hit-once/re-hit rules, and classifier gates remain separate
proof obligations.

`LuxBattleChara_ApplyHitReactionMove @ 0x1403448A0` publishes finite defender
counter state at +0x1364/+0x136C/+0x1374. The +0x1370 field is the delta
initialized to zero and later subtracted from +0x1364, not another counter.
`LuxBattle_TickCharaMainSimulation @ 0x14034DA70` subtracts +0x1370 from
+0x1364 once per logical MoveVM step and clamps it to zero. This proves a
counter clock, not the defender's first command-acceptance tick.

Primary decompile re-check corrected an earlier parser interpretation:
`chara+0x132C >= 2` contributes to the air/cinematic predicate; it is not a
saved Counter-Hit selector. On the standard reaction path KHD +0x46/+0x48 are
grounded versus air/cinematic counters. On the promoted path +0x4C/+0x4E are
the equivalent pair. +0x50/+0x52 select grounded versus air/cinematic reaction
rows. Counter Hit does not directly select +0x48, so the v1 Counter-Hit route
is explicitly unresolved.

## Hitstop

Shared full freeze advances neither endpoint, but asymmetric/per-character
dilation can change coordinate-to-tick mapping. The dataset exports this as a
separate context-dependent classification and does not subtract any hitstop
duration from a fabricated advantage.

## Fail-closed taxonomy

Every record is one of `Numeric`, `CategoricalKnockdown`,
`CategoricalCinematic`, `CategoricalThrowSuccess`, `ContextDependent`, or
`UnsupportedStatic`. A nonnumeric record must also carry a concrete
`reasonCode`: `route-ambiguity`, `transition-predicate`, `attacker-endpoint`,
`reaction-endpoint`, `clock-alignment`, `posture-column`, or
`dynamic-context`. The generator has no generic `unknown` status and the
validator rejects nonnumeric records without a reason.

Coverage reconciles every candidate slot/cell reference against either an
emitted ledger entry or a concrete exclusion reason. Master-window coordinates
are candidates, not asserted reachability. Current v1 has no numeric entries:
the remaining `clock-alignment`, reaction-exit, and command-release gaps are
real certification blockers rather than UI implementation tasks.

Frozen corpus coverage:

- 8,833 candidate native slot/cell references;
- 8,258 emitted and 575 explicitly excluded (490 unsupported cell kinds,
  85 invalid authored windows);
- 156,504 per-coordinate/context JSON records;
- 69 categorical throw-success records and 156,435 `UnsupportedStatic`;
- 7,212 attacker-endpoint, 24,928 clock-alignment, and 124,364
  reaction-endpoint classifications;
- zero silent fallbacks, generic unknowns, or numeric entries.

Repeated generation was byte-identical. JSON SHA-256 is
`CF67BB6E281060539FB908158023D05DA62AD70B47B7A062DD2185C1BF59EE0F`;
generated C++ SHA-256 is
`7FA76A80A52FA3980547ABA20364EAABA7E8900A2FD86A516F38BE7E0E1A8F33`.

## UI implementation boundary

The future ImGui agent may implement drawing and bounded history storage, but
must not derive actionable ticks by subtracting animation coordinates. Use the
compact table for native identity and authored rectangle bounds only. Render
startup, active, PostActive/recovery, shared freeze,
blockstun, and reaction/fall as distinct rectangle kinds.

Do not label +0x16DB as hitstun. It is the broader reaction/fall aggregate;
+0x16DC is the blockstun byte. Sample accepted contact explicitly and preserve
its native key in history. A source-hash mismatch disables numeric lookup.

Runtime validation remains a separate follow-up and is required before any
claim that pointer lifetime, sampling phase, or on-screen values are correct.

## Ghidra audit note

The requested structures already existed and were retained rather than
duplicated: `FLuxMoveLane` (0x468), `FLuxBattleAttackCell` (0x70),
`FLuxHitReactionParams_Partial` (0x40), `FLuxMoveCommandPlayer` (0x302C),
`FLuxBattleVMFreezeRecord` (0x40), and
`ALuxBattleChara_VerifiedPartial`. The named attacker, defender, scheduler,
and freeze functions were plate-audited and completeness-scored through MCP.

The audit typed and named the 59.94-Hz freeze-output divisor as
`g_flLuxNominalTicksPerSecond`, typed the per-player time-dilation globals,
and added frame-meter comments to `LuxBattle_ComputeFreezeBlendOutput` and
`LuxMoveVM_GetTimeDilationScalar`. The Ghidra program was saved through MCP.
The permitted `refresh_ghidra_calibur.ps1` exporter was attempted afterward,
but refused this bridge because its schema exposes `run_ghidra_script` rather
than the required `/run_script` endpoint. Therefore the Ghidra database is the
authority for those latest annotations; no claim is made that the checked-in
GhidraCalibur generation contains them.
