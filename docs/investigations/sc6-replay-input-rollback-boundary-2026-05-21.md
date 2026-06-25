# SC6 Replay/Input Bridge And Rollback Boundary

Static Ghidra pass focused on replay freeze, timeline scrub, and rollback determinism.

## Replay/input bridge

- `ALuxBattleFrameInputSync+0x43FC` is `UserPlayerFlags`, proven by `ALuxBattleFrameInputSync_RegisterProperties` and initialized by `InitializeLuxBattleFrameInputSyncPlayerFlagsFromOnlineSession`.
- `+0x43FC` is distinct from `ALuxBattleFrameInputLog+0x39C`. `+0x39C` remains context-dependent in replay/cache paths and must not be globally renamed to player flags.
- `+0x4410/+0x4414` remain replay cursor-like in cache/drain code, even though the reflected sync class registers them as `SendTime` and `SyncTime`. Treat this as context/derived-class overlap until more evidence resolves it.

## Live input state

- `LuxBattle_PerFrameTick` writes `g_LuxBattle_LatestEngineInput_PerPlayer[0..1]` from `FLuxBattlePerFrameTickArgs`.
- `LuxBattle_TickCharaInput` consumes latest-engine-input, `g_LuxBattle_PerPlayerInputRing`, `g_LuxBattle_PerPlayerInputRingCursor`, and `g_LuxBattle_InputRingBaseOffset_PerPlayer`, then writes `FLuxBattleChara` input fields.
- `LuxBattle_RoundResultCinematic_StateMachineTick` resets latest-engine-input and clears the input rings. It is a reset path, not the primary live writer.

## MoveSystem scratch

- `g_LuxBattle_CCpuCommandArray @ 0x144715400` is the `pPad1DF8To3038+0x570` scheduler region inside `g_LuxMoveSystem_DataTableA[1]`, two `FLuxMoveSchedState` entries, stride `0x60`.
- `0x1447155C0` is the training dummy input record/playback cluster, per-player stride `0x98`.
- `0x144715650` is the training mode field: `0` off, `1` recording, `2` playback.
- Reset-only clusters `pPad1DF8To3038+0x10C`, `+0x174`, and `+0x54C` currently only clear in `LuxMoveVM_TickCharaCommandScheduler` special move-id `2` path. No struct split was applied.

## One-frame order

1. `LuxBattle_PerFrameTick` mirrors per-player input args into `g_LuxBattle_LatestEngineInput_PerPlayer`.
2. World mode/pause state advances and sets `g_LuxBattle_BattleAdvanceFlag`.
3. MoveVM pump state may run.
4. Per alive chara: `LuxMoveVM_TickCharaCommandScheduler`, clear prior input fields, then `LuxBattle_TickCharaInput`.
5. Animation/audio pre-sim updates.
6. `LuxBattle_PreTickStateSnapshotAndRoundDecision`.
7. `LuxBattle_TickCharaMainSimulation` for both charas.
8. Hit resolution: `LuxBattle_SyncLauncherHitToOpponentState`, `LuxBattle_TickHitResolutionAndBodyCollision`.
9. Secondary/decorator, hitstop/input mirror, VFX, camera, stage wind, auto-advance.
10. `g_LuxBattle_FrameCounter++`.

Best rollback timing: restore a pre-frame HgCpuDirect snapshot, restore HorseMod extras immediately after `ExecFinalizeAndPost`, inject frame inputs through normal args/global path, then execute `LuxBattle_PerFrameTick` once.

Related runtime notes: `luxbattle-runtime-map-2026-06-25.md` tracks the
provider/action-slot, damage-factor, training-setup, and battle-camera systems
that are adjacent to this frame boundary and useful when replay seek resumes
with inert characters or camera/hash drift.

## Rollback state matrix

Covered by HgCpuDirect:
- Two chara regions and their movement, hit, reaction, timers, active state, and most chara-local input fields.
- Fixed global battle ranges, stage stature, camera, timer, motion, physics, terrain flags, VFX, and pointer fixups.

Must be captured by HorseMod:
- `g_LuxBattle_LfsrState @ 0x14485EB30`.
- `g_dwLuxBattleLfsrIndex @ 0x14485EB94`.
- `g_LuxBattle_LatestEngineInput_PerPlayer @ 0x144855700`.
- `g_LuxBattle_PerPlayerInputRing @ 0x14485E750`.
- `g_LuxBattle_PerPlayerInputRingCursor @ 0x14485EB20`.
- `g_LuxBattle_InputRingBaseOffset_PerPlayer @ 0x14470DED0` if mode/setup code mutates it.
- `g_LuxBattle_CCpuCommandArray @ 0x144715400` unless proven included by a future HgCpuDirect global range audit.

Replay-scrub only:
- `ALuxBattleFrameInputLog` cache/cursors, including master clock and drain/dormant cursor state, when scrub uses the normal replay cache path.
- MoveSystem training dummy record/playback cluster if timeline tooling uses training playback semantics.

Not needed for live rollback:
- `KHit_TestPointVsHitbox` spring-chain collision state, unless visual skeleton/spring determinism is part of the hash target.
- Profile/customization state for this pass.

## KHit and reaction path

- `KHit_TestPointVsHitbox` is not the main battle hit classifier. Static callers show only `KHit_SolveSpringChainWithCollision`, reached from `LuxSkeleton_UpdateBoneTransforms`.
- Main battle geometry/classification path is:
  `LuxBattle_TickHitResolutionAndBodyCollision`
  -> `LuxBattleChara_UpdateAllKHitWorldCenters`
  -> `LuxBattle_ResolveAttackVsHurtboxMask22`
  -> `LuxBattleChara_ProcessHit`
  -> `LuxBattle_ComputeHitReactionParams`
  -> `LuxBattleChara_ApplyHitReactionMove`
  -> `LuxBattleChara_ApplyKnockbackForce` / hit-state and residual movement paths.
