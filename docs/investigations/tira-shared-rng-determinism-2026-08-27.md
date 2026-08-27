# Tira shared-RNG determinism contract — 2026-08-27

## Question

Tira has authored moves whose outcome can switch her between distinct behavior
and move sets. A rollback implementation is invalid if it restores only the
global random state while omitting the selected character state, or if
resimulation skips a non-gameplay consumer of the same random stream and shifts
the later gameplay draw sequence.

## Native RNG contract

Ghidra program `/SoulcaliburVI.exe` proves that
`LuxMoveVM_GetRandXorshift96Gameplay @ 0x14034F1F0` advances the single
three-word state at `g_stLuxBattleXorshift96State @ 0x14470E2C8`.
Its 26 direct callers include gameplay hit/block/damage and MoveVM probability
routes, plus camera, effect-variant, intro, round-pose, and CPU routes. The
stream is therefore shared. Camera and effect calls cannot be removed from
resimulation merely because their immediate output is presentation: omitting a
draw changes later gameplay decisions.

The two relevant authored MoveVM probability contracts are independently
verified:

- `LuxMoveVM_GetRandWeightedIndex @ 0x1402E58B0` implements CALLCOND `0x23`,
  consumes exactly one xorshift96 draw, and maps it through authored weights.
- `LuxMoveVM_EvaluateIfOpcode @ 0x1403732F0`, IF `0x007F` at
  `0x140374451`, calls `LuxMoveVM_GetRandFloat01` before reading/comparing the
  authored percentage threshold. It consumes exactly one draw even when the
  threshold is outside the ordinary 0..100 range.

`LuxMoveVM_GetRandU32 @ 0x14034F130` is not the same generator. It owns a
25-word LFSR plus cursor. The deterministic candidate already captures and
restores all 25 words and the between-draw cursor, in addition to the LCG,
xorshift96, and wind state.

## Tira authored-data evidence

`dump/Battle/hdr/hdr023.khd` is the Tira movement program. A complete decoded
instruction scan found:

- 104 CALLCOND `0x23` instances across 46 move slots;
- 16 IF `0x007F` instances across slots 338, 356, 357, 358, 359, 2005, 2427,
  2683, 2946, and 2947;
- 92 direct reads of MoveVM state-short index 25 across 72 slots;
- 182 direct writes of state-short index 25 across 68 slots.

Slots 338 and 359 write an authored value to state-short index 25 immediately
before IF `0x007F` consumes it as the percentage threshold. Slots 356 through
358 use the same probability opcode with threshold 5. This establishes that
Tira actively couples character-specific MoveVM state and the shared gameplay
xorshift stream. It does not, by itself, prove that index 25 is the sole native
field named “mood.”

UE reflection metadata gives two additional, distinct inputs:

- `FLuxBattlePlayerParam.TiraSide` is an `int` at `+0x4C` in setup state.
- `FLuxBattlePlayerResetParam.TiraMode` is an `int` at `+0x14` in the 0x38-byte
  round/reset input.

The symbol `TriggerChangeMood` at `0x140928510` was a misleading anonymous
function during this audit. Ghidra now names it
`GetLuxBattleDramaticVoiceTriggerChangeMoodUFunction`; decompilation proves it
only constructs UE reflection metadata for a dramatic-voice event and consumes
no battle RNG. It is not the gameplay writer.

## Implementation consequence

The native HgCpuDirect fighter archive remains the sole writer during restore,
but opaque local archive bytes are deliberately excluded from peer canonical
hashes. That previously left the complete MoveVM state-short bank without an
independent typed peer-canonical projection.

HorseMod now captures all 240 `ushort` state shorts for both fighter roots from
`ALuxBattleChara+0x197C`, appends them to canonical checkpoint bytes, and gives
them dedicated native fingerprint slot 30. Restore does not perform a second
write; the post-HgCpu typed comparison verifies that the authoritative archive
restored the exact bank before simulation resumes. Snapshot schema 45 and
checkpoint format 18 prevent older images from being accepted.

The native self-test mutates fighter 0 state-short index 25 and requires both
the aggregate canonical hash and native fingerprint slot 30 to change. This
closes the prior silent-state hole, but runtime qualification still must use a
genuine Tira workload that observes a real probability-driven transition and
must retain exact xorshift state plus exact canonical fighter state after every
correction.

## Remaining runtime gate

A Tira workload is certifying only when it records an actual authored
probability route/mood transition, performs real corrections across that route,
and reports:

- exact xorshift96 landing state;
- exact MoveVM state-short bank and canonical hash;
- no omitted camera/effect shared-stream draws;
- exact subsequent move availability/selection;
- exact peer-confirmed hashes in the paired Steam run.

Base-game Tira recordings retained under
`ReplayExample/baseGameTiraReplays` are useful input workloads, but older Horse
documentation explicitly rejects them as state oracles for its then-modified
RNG contract. Fresh current-build evidence is required.
