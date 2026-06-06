# SC6 Move Facing / Tracking - 2026-05-28

## Short Answer

A move faces the opponent through two separate mechanisms:

- **One-shot facing commit**: move-script effect opcode `0x1A` calls `LuxMoveVM_OnMoveStart_SnapPositionAndFacing_LockRetrack @ 0x1402FF3E0`. This snaps/commits the character's facing from the cached opponent look-at data and then sets `FLuxBattleChara+0x16E6 = 1`, which locks normal per-frame retracking for the move.
- **Per-frame retracking ramp**: move-script effect opcode `0x3C` writes the ramp block at `FLuxBattleChara+0x971A8..0x971B8` through `LuxMoveVM_SetFacingRetrackRamp @ 0x1403693D0`. `LuxBattleChara_RetrackFacingTowardOpponent @ 0x140369450` consumes that ramp every tick and applies the allowed yaw delta through `LuxBattleChara_ApplyFacingRotationDelta @ 0x140311350`.

So normal move facing is not just a static KHD slot flag. It is authored in MoveVM bytecode effect calls.

## Runtime Path

Per frame, `LuxBattle_TickCharaMainSimulation @ 0x14034DA70` calls `LuxBattleChara_UpdateOpponentRelativeAngles_PerTick @ 0x140305E50`, which computes the current angle from the character to its opponent at `chara+0x15A4` and calls `LuxBattleChara_RetrackFacingTowardOpponent`.

`LuxBattleChara_RetrackFacingTowardOpponent` has the important gate:

```text
if (chara+0x16E6 != 0 && chara+0x16E1 == 0) return;
```

Practical meaning:

- `0x16E6 == 0`: idle/walk/neutral facing can naturally keep turning toward the opponent.
- `0x16E6 == 1 && 0x16E1 == 0`: normal in-move animation owns facing; per-frame opponent retracking is blocked.
- `0x16E6 == 1 && 0x16E1 == 1`: fall/hit-reaction realignment path can still run.

After the gate, the function applies a clamped/weighted yaw delta. The controlling block is:

```text
chara+0x971A8  mode
chara+0x971AC  ramp frames remaining
chara+0x971B0  current weight
chara+0x971B4  target/preset weight
chara+0x971B8  per-frame increment
```

When `0x971AC > 0`, the current weight increments by `0x971B8` and the countdown decrements. When the countdown is zero, `0x971B0` is loaded from `0x971B4`. Mode `0` scales the target angle before clamping; mode `1` scales the per-frame turn cap.

## MoveVM Opcodes

`LuxMoveVM_DispatchEffectOp @ 0x140376B20` is the opcode dispatcher.

Relevant effect opcodes:

| Opcode | Native effect | Notes |
|---:|---|---|
| `0x1A` | call `LuxMoveVM_OnMoveStart_SnapPositionAndFacing_LockRetrack` | one-shot facing commit, then sets `chara+0x16E6 = 1` |
| `0x3B` | call `LuxMoveVM_SetFacingRetrackRamp(..., mode=0)` | supported by engine; not present in the local shipped KHD scan |
| `0x3C` | call `LuxMoveVM_SetFacingRetrackRamp(..., mode=1)` | authored per-frame retrack ramp/cap control |

For `0x3C`, argument 1 is decoded by `LuxMoveVM_DecodeLiteralArg @ 0x1402FC560` and then divided by 60 before storage. Argument 2 is only a nonzero ramp-vs-snap selector; if nonzero, the ramp duration is resolved from the current lane timing with `LuxMoveVM_MapBankSlotTimingIndex @ 0x1403002B0`.

## Ghidra Cleanup Notes

Created `FLuxMoveRetrackRamp` and applied it to `FLuxBattleChara` at the native offsets consumed by `LuxBattleChara_RetrackFacingTowardOpponent`:

| Offset | Type | Field |
|---:|---|---|
| `0x971A8` | `FLuxMoveRetrackRamp` | `facingRetrackRamp` |
| `0x971BC` | `FLuxMoveRetrackRamp` | `boneRetrackRamp` |
| `0x971D0` | `int` | `nBoneRetrackSelector` |

Also renamed the gate bytes used by retracking:

| Offset | Field |
|---:|---|
| `0x16E1` | `bMotionFlag11FallReaction` |
| `0x16E6` | `bMotionFlag16FacingLock` |

Important correction: an earlier struct edit accidentally placed these ramp fields at decimal `619432` (`0x973A8`). The correct decimal offset for `0x971A8` is `618920`; Ghidra now renders nested `facingRetrackRamp` and `boneRetrackRamp` accesses in the retrack consumer.

## Parser-Side Scan

I scanned `dump/Battle/hdr/*.khd` using the current stack-VM walker and a small concrete stack pass over `CALLCOND 0x02/0x03` effect dispatches.

Results:

| Effect opcode | Count |
|---:|---:|
| `0x1A` | 693 |
| `0x3C` | 763 |
| `0x3B` | 0 |

`0x3C` target weights observed after FP16 decode and `/60`:

| Raw arg | Weight | Count |
|---:|---:|---:|
| `0` | `0.000000` | 106 |
| `16896` | `0.050000` | 145 |
| `17664` | `0.083333` | 63 |
| `18688` | `0.166667` | 29 |
| `19712` | `0.333333` | 43 |
| `20352` | `0.500000` | 29 |
| `20736` | `0.666667` | 29 |
| `21056` | `0.833333` | 29 |
| `21376` | `1.000000` | 29 |
| `21600` | `1.166667` | 29 |
| `21760` | `1.333333` | 29 |
| `21920` | `1.500000` | 29 |
| `22080` | `1.666667` | 174 |

Only 28 of the 763 local `0x3C` occurrences are on slots with a non-`-1` first cell variant; most are sentinel or helper slots. That suggests the parser should model these as script events on slot bytecode, not as attack-cell fields.

## Parser Implication

The next useful parser feature is an `EffectEvent` extraction beside `TransitionEvent`:

```text
slot -> bytecode pc -> CALLCOND 0x02/0x03 -> effect opcode + concrete args
```

For tracking, the first implementation only needs to surface `0x1A`, `0x3B`, and `0x3C`, with decoded FP16 values for `0x3B/0x3C`. Then the web/export layer can label moves as:

- `facing_commit`: has effect opcode `0x1A`.
- `retrack_ramp_mode0`: has effect opcode `0x3B`.
- `retrack_ramp_mode1`: has effect opcode `0x3C`, including target weight and whether the ramp selector arg is present.
