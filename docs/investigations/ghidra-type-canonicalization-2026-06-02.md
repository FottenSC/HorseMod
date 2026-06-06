# Ghidra Type Canonicalization - 2026-06-02

Target program: `SoulcaliburVI.exe`

## Policy

- Root `/` is canonical for shared SC6 replay/gameplay structs.
- `/HorseMod/ReplaySeek` is reserved for ReplaySeek-specific types only.
- Do not create a same-name type under `/HorseMod/ReplaySeek` when a root type already exists.
- If a duplicate is intentional, use an explicit distinct name or set `allow_duplicate_type_name` in the autolabel plan and document why.

## Cleanup Performed

Removed duplicate `/HorseMod/ReplaySeek` definitions for:

- `ALuxBattleFrameInput_Partial`
- `ALuxBattleFrameInputLog`
- `ALuxBattleManager_Partial`
- `FLuxBattle_WorldModePump`
- `FLuxBattleRoundStartData`
- `FLuxHgCpuBuffer`
- `FLuxInteractiveReplayState`
- `FLuxReplayInputCacheEntry`

Removed scratch type:

- `TestStructX`

Preserved real ReplaySeek-specific type:

- `ALuxBattleReplayPlayer`
- `ALuxBattleReplayPlayer *`

## Validation Evidence

- `/HorseMod/ReplaySeek` now contains only `ALuxBattleReplayPlayer` and `ALuxBattleReplayPlayer *`.
- Canonical root structs still resolve and have expected sizes:
  - `ALuxBattleFrameInput_Partial`: `1280`
  - `ALuxBattleFrameInputLog`: `17617`
  - `ALuxBattleManager_Partial`: `2076358`
  - `FLuxBattle_WorldModePump`: `64`
  - `FLuxBattleRoundStartData`: `192`
  - `FLuxHgCpuBuffer`: `163864`
  - `FLuxInteractiveReplayState`: `984504`
  - `FLuxReplayInputCacheEntry`: `16`
- Representative root layouts were checked through MCP `get_struct_layout`:
  - `ALuxBattleFrameInput_Partial+0x3E0`: `ALuxBattleFrameInputLogSlotRecord[2] pSlotRecords`
  - `ALuxBattleFrameInputLog+0x3A4`: `int nMasterClock_at0x3A4`
  - `ALuxBattleFrameInputLog+0x3C0`: `FLuxReplayInputCacheEntry[1024] pReplayInputCache`
  - `ALuxBattleManager_Partial+0x450`: `ALuxBattleFrameInput_Partial * pBattleFrameInput_at0x450`
  - `ALuxBattleManager_Partial+0x478`: `ALuxBattleFrameInputLog * pBattleFrameInputLog_at0x478`
  - `ALuxBattleManager_Partial+0x1360`: `FLuxBattleRoundStartData RoundStartSnapshot_at0x1360`
  - `FLuxBattle_WorldModePump+0x30`: `ALuxBattleManager_Partial * pBattleManager`
  - `FLuxReplayInputCacheEntry`: `nFrameID`, `dwFrameIndex`, `dwInputValue`, `bFilled`

## Prevention

- `tools/ghidra_autolabel/ghidra_autolabel.py` now rejects namespaced struct creation when a canonical root type with the same base name already exists, unless the plan explicitly sets `allow_duplicate_type_name`.
- Run the read-only duplicate audit before and after type import/struct creation sessions:

```powershell
python tools/ghidra_audit_duplicate_types.py --focus-path /HorseMod/ReplaySeek --fail-on-any-duplicate
```

The audit exits non-zero if any same-name duplicate involving `/HorseMod/ReplaySeek` exists.

## Known Tooling Noise

Ghidra inline script execution still prints unrelated compile errors from old files in `C:\Users\prest\ghidra_scripts`. Those errors did not block the cleanup, but they should be fixed or moved out of the Ghidra script path to reduce future validation noise.
