# Ghidra quality audit - 2026-05-21

Target program: `SoulcaliburVI.exe`

Scope: human-touched HorseMod SC6 reverse-engineering work from investigation docs, Ghidra bookmarks, renamed functions, and related globals. This is not a full 141,229-function program audit.

Tooling note: `check_tools` reports `analyze_function_completeness`, `batch_set_comments`, and `get_plate_comment` as loaded/callable, but those dynamic tools are not exposed as direct client endpoints in this session. I used the exposed native MCP operations: decompile, function lookup, variable audit, rename, type setting, prototype setting, bookmarks, globals/data lookup, save. Score is therefore recorded as `tool-unavailable`.

## Summary counts

| Status | Count |
|---|---:|
| DONE | 10 |
| FIXED | 11 |
| NEEDS_STRUCT | 3 |
| REPORT_ONLY | 3 |
| SKIP | 0 |

## Ledger

| Status | Domain | Address | Current Name | Score | Issues Found | Fixes Applied | Residual Risk | Source |
|---|---|---:|---|---:|---|---|---|---|
| DONE | Replay/InputLog | 0x1403F2AB0 | `UpdateFrameInputLogCacheLocalMode` | tool-unavailable | None in first pass; only phantoms. | Added audit bookmark confirming clean prototype/variables and `+0x3A4` master-clock role. | Scorer endpoint unavailable. | `replay-timeline-scrub`, `rollback-netcode-methods` |
| REPORT_ONLY | Replay/InputLog | 0x1403FDF30 | `ProcessFrameInputLogCurrentInputRefresh` | tool-unavailable | Plate layout row still says `dwPlaybackCursor_at0x39C`; current struct/body uses `dwActiveSlotMask_at0x39C`. Duplicate PRE comments visible. | Added audit bookmark. | Needs `batch_set_comments`/comment endpoint to rewrite plate/PRE comments. | replay docs |
| FIXED | Replay/InputLog | 0x1403FDB80 | `ProcessFrameInputLogTickInputPipelineVTableC80` | tool-unavailable | Old name implied BattleManager ownership; body is `ALuxBattleFrameInputLog` vtable C80 pipeline. Plate still says `bDoubleTickGuard`. | Renamed from `LuxBattleManager_TickInputPipelineDispatcher_VTableC80`; added audit bookmark. | Type parser rejected reapplying `ALuxBattleFrameInputLog *` prototype despite existing signature; comment cleanup still needed. | rollback docs |
| DONE | Replay/InputLog | 0x1403FE8F0 | `ProcessFrameInputLogDrainCursor4410WhileReady` | tool-unavailable | None in first pass. | Added audit bookmark confirming loop-break freeze rationale remains accurate. | Scorer endpoint unavailable. | replay-freeze-drift |
| DONE | Replay/InputLog | 0x1403FEBA0 | `ProcessFrameInputLogDormantCursor4414WhileReady` | tool-unavailable | Unresolved `+0x43FC/+0x4414` fields remain intentionally unresolved. | Added audit bookmark documenting dormant-cursor status. | Struct expansion should wait for caller evidence. | replay docs |
| NEEDS_STRUCT | ReplayPlayer | 0x140435C20 | `ALuxBattleReplayPlayer_Tick_CopyRoundResetSnapshotAndSetMoveState4` | tool-unavailable | `0xC0` snapshot copy still decompiles as byte field moves; inline WORLDTICKGATE note appears stale against current typed body. | Added audit bookmark; no speculative struct fields applied. | Needs `FLuxBattleRoundStartData` field/layout pass and comment rewrite. | replay docs |
| FIXED | Rollback/Core Tick | 0x1402DBC60 | `LuxBattle_PerFrameTick` | tool-unavailable | `nLoopCounter` was `longlong` with `n` prefix. | Renamed to `llCharaSimSlotCountdown`; added audit bookmark. | Broader raw camera/frame globals remain struct work. | rollback docs |
| FIXED | Rollback/HgCpuDirect | 0x1403841E0 | `LuxBattle_HgCpuDirect_ExecMoveChangeAndPost` | tool-unavailable | Several `uint`/`ulonglong` locals had `u`, `l`, or pointer prefixes inconsistent with storage type. | Renamed byte-count/address locals to `qw*`; XMM0/fixup locals to `dw*`; added audit bookmark. | Vec16 descriptor still deserves a real struct, but current function is readable. | rollback docs, replay-freeze-drift |
| FIXED | Rollback/HgCpuDirect | 0x140384540 | `LuxBattle_HgCpuDirect_ExecFinalizeAndPost` | tool-unavailable | Same reader-side Hungarian mismatches as writer. | Renamed reader return/fixup/address-selector locals to `dw*`/`qw*`; added audit bookmark. | Vec16 descriptor struct not created. | replay-timeline-scrub |
| FIXED | Movement | 0x140306BB0 | `LuxBattleChara_IntegratePhysics_PerTick` | tool-unavailable | Matrix/quaternion scratch arrays were `undefined1[]`; pointer-like locals were integer typed; some `u/qw/ar` names mismatched types. | Typed `arRotationQuat`/`arRotMat3x3` as byte arrays, typed matrix/chara-slot locals, renamed scratch locals to `dw*`, `ll*`, `qw*`, `ab*`; added audit bookmark. | Large `FLuxBattleChara` field coverage still partial. | movement investigation |
| FIXED | Movement | 0x140344FC0 | `LuxMoveVM_ApplyMoveOffsetToChara` | tool-unavailable | `nOppRingByteOffset` was `longlong` with `n` prefix. | Renamed to `llOppRingByteOffset`; added audit bookmark. | Phantoms remain accepted. | movement investigation |
| DONE | Movement/Collision | 0x140316F80 | `LuxBattle_TickCharaCollisionPhysics` | tool-unavailable | Visible locals/prototype pass first check. | Added audit bookmark. | Frame-transform/terrain globals need separate struct pass. | movement investigation |
| DONE | Ranking | 0x1401363F0 | `InitializeGlobalRankIconStringTable` | tool-unavailable | None in first pass; `FStringFwd[38]` local is good. | Added audit bookmark confirming 38-entry table/count `0x26`. | No plate rewrite performed. | ranking bookmarks |
| DONE | Ranking | 0x14050AFC0 | `GetRankIconStringByRankId` | tool-unavailable | None in first pass. | Added audit bookmark confirming valid ids `0..37` and fallback. | No plate comment endpoint exposed. | ranking bookmarks |
| DONE | Ranking | 0x14047C620 | `MapRankIdToRankBand` | tool-unavailable | Many one-byte lazy-map scratch locals; mass renaming would add noise. | Added audit bookmark confirming default band `5` behavior. | Could benefit from a purpose-built rank-band map struct/table comment later. | ranking bookmarks |
| DONE | Ranking | 0x14044FB00 | `LuxMoveSlot_ComputeScaleFromRankDiff_WithLazyInit` | tool-unavailable | Queued/spot-checked by rank docs; no mutation in this pass. | Added audit bookmark preserving scale-index/default-band check. | Needs direct full function pass if rank work resumes. | ranking bookmarks |
| NEEDS_STRUCT | Profile | 0x140453020 | `LuxProfileBase_RebuildAllSlotHashMaps_FromWeakRefData` | tool-unavailable | 100+ `local_*` temporaries remain from repeated FString/TMap construction. | Added audit bookmark; no speculative local renames. | Needs helper temporary/FString/TMap modeling before cleanup. | profile bookmarks |
| NEEDS_STRUCT | Profile | 0x14044E420 | `LuxProfileBase_SyncSlotColorFlagsAndFloats_AllTypes` | tool-unavailable | Many `local_*` FString/TArray scratch blocks remain. | Added audit bookmark; no speculative local renames. | Needs profile helper scratch struct/type pass. | profile bookmarks |
| REPORT_ONLY | MoveVM Global | 0x144715400 | `g_LuxBattle_CCpuCommandArray` | tool-unavailable | Label root is still typed as `byte`; prior note says two `FLuxMoveSchedState` entries stride `0x60`. | Added audit bookmark. | Data-type application tools are not exposed; do not overlay speculatively. | MoveVM bookmarks |
| DONE | MoveVM Global | 0x144710060 | `g_LuxMoveSystem_DataTableA` | tool-unavailable | Older raw-global note is partly stale; current global is typed as `FLuxMoveSystemDataSlot[2]`. | Added audit bookmark noting improved current type and remaining field-validation work. | Field-level validation still pending. | MoveVM bookmarks |
| FIXED | StageBoundary | 0x1402D77C0 | `SetScbattleStageInfoBarrierGeometry` | tool-unavailable | Old name was lowercase namespace-style and plate claimed 24 entries, while body copies 12 entries / `0xC0` bytes. | Renamed from `scbattle_StageInfo_SetBarrierGeometry`; normalized prototype; added corrected StageBoundary/Audit bookmarks. | Plate comment rewrite still pending because comment endpoint unavailable. | stage boundary bookmarks |
| FIXED | StageBoundary | 0x1402D7730 | `GetScbattleStageInfoBarrierGeometry` | tool-unavailable | Lowercase namespace-style name/prototype params. | Renamed from `scbattle_StageInfo_GetBarrierGeometry`; normalized prototype; added corrected StageBoundary/Audit bookmarks. | Plate comment rewrite still pending. | stage boundary bookmarks |
| DONE | StageBoundary | 0x1402D7850 | `scbattle_StageInfo_SetBoundaryParams` | tool-unavailable | Not fully decompiled in this pass; existing bookmark semantics remain plausible. | Added audit bookmark for paired boundary batch. | Needs full pass if stage overlay work continues. | stage boundary bookmarks |
| REPORT_ONLY | StageBoundary | 0x140427490 | `LuxStage_RegisterBarrierActor_BattleEvent0x19` | tool-unavailable | Queued with boundary pair, not fully audited. | Added audit bookmark. | Needs focused pass with caller chain. | stage boundary bookmarks |
| FIXED | INFERRED/VFX | 0x1404834D0 | `INFERRED_LuxBattleActor_Tick_ProcessMoveFramesAndZeroAt3a8` | tool-unavailable | Prototype had untyped actor pointer; name remains intentionally provisional. | Set explicit `void *pUnknownTickActor` prototype; added audit bookmark. | Class still unknown; do not use for freeze patches without runtime proof. | replay-freeze-drift |
| FIXED | INFERRED/VFX | 0x140561E60 | `INFERRED_LuxBattleActor_Tick_InvokeEventDelegates` | tool-unavailable | `abFNameBuf` was scalar `uint`, not byte array; prototype had untyped actor pointer. | Renamed to `dwFNameBuf`; set `void *pVfxActor` prototype; added audit bookmark. | Name remains intentionally misleading/provisional until class is identified. | replay-freeze-drift |
| FIXED | INFERRED/VFX | 0x140562090 | `INFERRED_LuxBattleActor_Tick_InvokeSlotDecayCallbacks` | tool-unavailable | `abFNameBuf` was scalar `uint`; parameter was `undefined8 param_1`. | Renamed to `dwFNameBuf`; set `void *pVfxActor` prototype; added audit bookmark. | Name remains intentionally misleading/provisional until class is identified. | replay-freeze-drift |

## Verification performed

- Confirmed Ghidra auto-analysis idle before changes.
- Used `get_function_by_address` or decompile on representative functions in every priority domain.
- Ran `get_function_variables` before and after all local rename/type fix batches where exposed.
- Added `GhidraQualityAudit` bookmarks for each audited item so findings are discoverable in Ghidra.
- Avoided malware/IOC tools.
- Avoided direct Ghidra scripts and `.gpr` edits.
- Saved no speculative struct overlays.

## Tooling gaps

These plan items could not be completed exactly because the dynamic MCP tools are loaded in Ghidra but not exposed as callable client endpoints here:

- `analyze_function_completeness`
- `batch_set_comments`
- `get_plate_comment`
- `set_decompiler_comment`
- data-type creation/application tools needed for global overlays

Where that blocked a planned edit, I recorded the finding in a Ghidra bookmark and in the ledger instead of forcing an unsafe workaround.
