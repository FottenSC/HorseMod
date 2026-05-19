# Next Area: ReplayPlayer to FrameInputLog Scrub/Resume Pipeline

Chosen after the first continuous-labeling scaffolding pass and a sub-agent
review.

## Why This Area

This path is directly tied to replay timeline scrub/resume behavior:

- seek-to-round and resume playback
- stale replay input cache state
- match replay versus training replay separation
- ownership boundaries between `ALuxBattleReplayPlayer`,
  `ALuxBattleFrameInputLog`, `BM+0x450`, and chara replay-state fields

It is higher value than broad `FUN_` cleanup because each label reduces mod
risk around replay control and freeze/step behavior.

## Snapshot Set

Evidence snapshots are already captured under this directory:

| Address | Function | Effective Score | Fixable |
|---|---|---:|---:|
| `0x14097BEB0` | `ALuxBattleReplayPlayer_RegisterProperties` | 30.8 | 69.2 |
| `0x14097A470` | `ALuxBattleReplayPlayer_RegisterNativeFunctions` | 45.0 | 55.0 |
| `0x1409A5490` | `CheckReplayPlayerHasNextRoundExec` | 76.1 | 23.9 |
| `0x140435C20` | `LuxReplayChara_Tick_CopyNextFrameToManager_SetMoveState4` | 76.8 | 23.2 |
| `0x1403FDF30` | `ProcessFrameInputLogCurrentInputRefresh` | 77.7 | 22.3 |
| `0x1403F0680` | `GetCurrentInputForFrameInputLogSlot` | 72.6 | 27.4 |
| `0x1403F2AB0` | `UpdateFrameInputLogCacheLocalMode` | 75.8 | 24.2 |
| `0x1403F2B60` | `UpdateFrameInputLogCacheOnlineOrLocal` | 59.3 | 40.7 |

## First Struct Targets

- `ALuxBattleReplayPlayer_Partial`
  - `+0x39C` `nCurrentRound`
  - `+0x3A0` `flCurrentTime`
  - `+0x3A8` `pStateResetData`
  - `+0x3B8` `pRecordingData`
  - `+0x3D0` `fIsPlayingBack`

- `ALuxBattleFrameInputLog_Partial`
  - Keep the existing known cursor/cache fields, but do not over-claim
    `+0x39C` as a universal playback cursor without checking the current
    function context.

## Anti-Goals

- Do not mix in `ALuxBattleTrainingReplayPlayer`, `ALuxBattleKeyRecorder`, or
  `GetTrainingReplayPlayer` unless doing an explicit training-mode audit.
- Do not add HorseMod patch sites from this labeling pass.
- Do not rely on generated SDK headers when Ghidra property registration gives
  a more precise runtime offset or type.
- Prefer partial structs and EOL offset comments over broad whole-object
  layouts when offsets are not verified.

## Suggested Continuous Pass

1. Start with `ALuxBattleReplayPlayer_RegisterProperties`.
2. Use the existing `ALuxBattleReplayPlayer` struct rather than creating a
   duplicate partial type.
3. Apply that type to `CheckReplayPlayerHasNextRoundExec` and replay-player
   accessors.
4. Move into `FrameInputLog` cache functions and type only the fields each
   body proves.
5. Save after each function that reaches an effective score above 80.

## Progress 2026-05-18

- `ALuxBattleReplayPlayer_RegisterProperties @ 0x14097BEB0`
  - Prototype set to `void * ALuxBattleReplayPlayer_RegisterProperties(void)`.
  - Plate/PRE/EOL comments refreshed around property registration.
  - Global metadata table labels added:
    `g_pALuxBattleReplayPlayer_MetadataTable` and
    `g_pALuxBattleReplayPlayer_MetadataTableInit`.
  - Effective score improved from 30.8 to 64.8. Remaining deductions are mostly
    large-function magic constants and stack scratch-buffer typing.

- `CheckReplayPlayerHasNextRoundExec @ 0x1409A5490`
  - Created `FFrame_Partial` with `pCode` at `+0x20`.
  - Prototype set to
    `void CheckReplayPlayerHasNextRoundExec(ALuxBattleReplayPlayer * pReplayPlayer, FFrame_Partial * pStack, bool * pbRetVal)`.
  - Plate/PRE/EOL comments refreshed.
  - Effective score improved from 76.1 to 92.0.

- `ALuxBattleReplayPlayer_RegisterNativeFunctions @ 0x14097A470`
  - Prototype set to `void ALuxBattleReplayPlayer_RegisterNativeFunctions(void)`.
  - Plate/PRE/EOL comments added to tie the native registration table at
    `0x14339A7E8` to `CheckReplayPlayerHasNextRoundExec @ 0x1409A5490`.
  - Effective score improved from 45.0 to 92.0.

- `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30`
  - Extended `ALuxBattleFrameInputLog`:
    `+0x398` is now `int nActiveSlotCount`;
    `+0x3B8` is now `uint[2] pCurrentInputBySlot`.
  - Plate/PRE/EOL comments refreshed to document the BM+0x450 current-input
    mirror into `pCurrentInputBySlot`.
  - Effective score improved from 77.7 to 89.7.
  - Important note preserved: `+0x39C` is used as an active-slot mask in this
    function; do not globally assume the field means only playback cursor.

- `GetCurrentInputForFrameInputLogSlot @ 0x1403F0680`
  - Created `FLuxFrameInputSlotRecord_Partial` with
    `dwCurrentInput` at `+0x00` and stride `0x90`.
  - Created `ALuxBattleFrameInput_Partial` with slot records at `+0x3E0`.
  - Extended `ALuxBattleManager_Partial` with
    `ALuxBattleFrameInput_Partial * pBattleFrameInput` at `BM+0x450`.
  - Plate/PRE/EOL comments refreshed around the `BM+0x450` input source, PRA
    replay UI bits, and final `InputLog+0x394` mask-out.
  - Effective score improved from 72.6 to 84.9.

- Tooling fix:
  - `ghidra_autolabel.py snapshot` now calls `/get_function_xrefs` with the
    correct `name` query parameter.
