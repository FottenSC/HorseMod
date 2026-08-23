# Deterministic simulation contract (2026-08-23)

## Status and admission rule

This document is the evidence ledger for the replacement deterministic engine. It is not a list of byte ranges copied from the retired rollback implementation. A region enters `Schema::production_regions` only after its owner, complete writer set, lifetime, validation, capture phase, restore order, repair work, and verification are all proven.

The production region manifest is currently empty. Consequently no native SC6 adapter, replay seek, or online rollback path can activate. This is intentional fail-closed behavior while the remaining subsystems are audited.

The sole supported binary is the open Ghidra program `SoulcaliburVI.exe`. Addresses below are image virtual addresses for that binary. The Ghidra program was saved after the input/simulation-boundary audit.

## Coordinate and simulation boundary

One deterministic coordinate is one completed call to `LuxBattle_PerFrameTick` (`0x1402DBC60`), ending with the increment of `g_dwLuxBattleFrameCounter` at `0x1402DC34C`. It is not one Unreal tick.

`LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState` (`0x1403FE520`) can invoke the per-frame traversal more than once during one outer manager tick. Native move-state 3 performs an early traversal with zero/default arguments, and manager state at `+0x1462` can repeat the inner traversal and post-frame phase without rebuilding the player input pairs. An adapter placed around the outer UE tick would therefore assign inputs and snapshots to the wrong native iteration.

The admitted hook point must bracket each `LuxBattle_PerFrameTick` traversal and use the native frame/round identity described below. Hook installation is not authorized until all state regions reached by the traversal are admitted.

## Audited region: frame input log and produced input pairs

| Property | Evidence-backed contract |
|---|---|
| Native owner | `ALuxBattleFrameInputLog_Runtime`, reached through the battle manager. |
| Layout | InputDelay `+0x390`; LocalInputMask `+0x394`; PlayerNum `+0x398`; LocalPlayerFlags `+0x39C`; GameRound `+0x3A0`; GameTime/PauseTime/UpdateTime `+0x3A4/+0x3A8/+0x3AC`; RecorderTime/RecorderStopTime `+0x3B0/+0x3B4`; CurrentInputBySlot `[2]` at `+0x3B8`; two 512-entry cache slabs at `+0x3C0`, 16 bytes per row. |
| Producers/writers | `InitializeFrameInputLogCacheForRound` (`0x1403F7C60`) initializes the complete round/cache region. `ProcessFrameInputLogCurrentInputRefresh` (`0x1403FDF30`) clears and refreshes both current-input words. The FrameInputSync send/receive paths remain to be fully enumerated before this region can be admitted. |
| Readers | `GetCurrentInputForFrameInputLogSlot` (`0x1403F0680`) samples a routed logical slot, synthesizes PRA `0x4000`, and removes LocalInputMask bits. `GetCachedInputForFrameInputLogSlot` (`0x1403F0720`) rebases by InputDelay and requires exact round and full-frame tags. `LuxBattleChara_UpdatePlayerInputData_FromRoundCache` (`0x1403FCD10`) produces held/rising pairs and applies manager/gameplay suppression masks. |
| Downstream filter | `FilterALuxBattleMoveDispatchInputPairByFrameSlot` (`0x140427940`) is a gameplay-authoritative callback after pair production and before `LuxBattle_PerFrameTick`; its action-window state must be captured or deterministically rebuilt. |
| Lifetime boundary | `InitializeFrameInputLogCacheForRound` and `ResetFrameInputSyncRoundState` (`0x1403F7D20`) establish a new GameRound epoch. Cache rows are valid only when both their round and full frame identity match. |
| Classification | Clocks, cache identities/values, current-input mirrors, previous raw words, suppression/action-window state, and the resulting input pairs are canonical gameplay state. Transport bookkeeping that does not influence pair production may later be classified as client-local only after its writers are enumerated. |
| Capture phase | Immediately before the admitted per-frame traversal, after native pair production and the registered MoveDispatch filters have run. The authoritative external timeline stores the exact resulting pair for that native coordinate. |
| Restore order | Validate round/generation and object identity; restore clocks/cache and previous-word/action-window state; restore the exact produced pair; rebuild no presentation; verify tags, clocks, masks, prior words, and pair values before advancing. |
| Admission status | **Blocked.** The complete FrameInputSync packet/cache writer surface and manager-owned previous-word/action-window extents are not yet enumerated. |

Important behavior: `GetCachedInputForFrameInputLogSlot` selects `frame & 0x1FF`, but still validates the full frame and round stored in the row. Treating the cache as a bare 512-entry ring would accept stale input from another frame or round.

## Audited region: round-state sequencing

`LuxBattleManager_Tick_ProcessRoundStateSequence` (`0x1403FCE80`) owns the byte sequence at manager `+0x1470`, count/capacity at `+0x1478/+0x147C`, and current state at `+0x1480`. It commits a new state byte before executing its entry branch. State ID 1 synchronously broadcasts the callback collection at `+0xB80`; registered consumers reset trace/hit dispatch and activate linked actors. Other branches mutate pause, stage, world-mode, score, track, and game-flow state and can disconnect the online session.

Queue index/state, the manager frame-advance counter, pause grace state, track selection, and gameplay callback mutations are canonical. Downstream audio/UI/cinematic terminals are presentation and must eventually be journaled at their irreversible boundary, not by skipping the state transition. The producer of the separate callback collection at `+0x8E0` is unresolved, so the round subsystem is not admitted.

## Audited region: MoveVM pump generation boundary

| Property | Evidence-backed contract |
|---|---|
| Native owner | `g_abLuxMoveSystemVMPumpState` at `0x144100C70`, typed 0x88-byte partial. |
| Begin writer | `LuxMoveSystem_BeginVMPump` (`0x14031C950`). It ends an overlapping pump, binds the primary fighter and current-round opponent, saves fighter `+0x324/+0x34BC/+0x1E50`, clears selected command-player/training heads, publishes authored IDs/mode, enables the pump, and saves HgCpu `+0x13C`. |
| Active consumers | Pump state handlers and `LuxMoveVM_TickCharaCommandScheduler` (`0x1402E52D0`). The scheduler uses fighter `+0x324` as gameplay state, a transition latch, and an input-policy selector before character input consumption. |
| End writer | `LuxMoveSystem_EndVMPump` (`0x14031CAC0`). It finalizes both lane hit-effect transactions, restores saved fighter fields, copies `+0x43E0C` to the vital candidate, reinitializes each normal SubVM, restores HgCpu `+0x13C`, clears enabled, and may reset interactive-replay round-result state. |
| Related arenas | `g_abLuxMoveCommandPlayers` at `0x14470F390`, two records with 0x3038 stride; `g_abLuxMoveVMSlotParamArray` at `0x14470E0C`, two records with 0x2C stride and a verified 0x28 semantic prefix; per-player CPU command state at `0x144715400`. |
| Lifetime boundary | Begin through End is one indivisible deterministic transaction. Fighter, opponent, timer, scheduler, command-player, and SubVM identities are current-round identities. |
| Pointer rule | Bound fighter/opponent/SubVM/timer pointers are validation identities only. Snapshotting their numeric addresses as restorable bytes is forbidden. A mismatch invalidates the whole generation. |
| Restore order | Validate all current-round identities; restore value fields and owned arena contents; restore HgCpu state; rebuild normal/derived SubVM state through verified native routines; compare canonical fields. Never resurrect a pump after a round/session reconstruction. |
| Classification | Saved move/variant/mode values, pump state, command-player gameplay fields, scheduler latches, and HgCpu cursor are canonical. Rebuilt SubVM pointers/caches are derived. Hit/VFX/audio terminals require separate presentation classification. |
| Admission status | **Blocked.** Complete writers for the command-player arena, HgCpu graph, MoveVM/SubVM arena, and hit/effect lists are not yet closed. |

The previous whole-tick move-ID override was invalid: forcing fighter `+0x324` to mode 1 suppresses reaction and CPU-direct scheduling, corrupts the prior-move latch, and can erase a native move transition written within the traversal.

## Remaining audit gates

The following gates must be completed and saved in Ghidra before the first non-empty production schema:

1. FrameInputSync packet/cache writers and all input filtering/consumption state.
2. RNG streams and floating-point control/status environment.
3. Fighter, HgCpu, MoveVM, command-player, VMPump, hit/reaction, animation, root motion, and allocation generations.
4. Camera, stage/barriers, wind, destructibles, and dynamic allocation owners.
5. Audio, VFX, listener callbacks, UI/cinematic events, and other irreversible presentation terminals.
6. Match entry, round exit/re-entry, scene change, disconnect, module/process cleanup, and hook teardown.

Until these close, the engine can be tested only against fake adapters. This prevents tests from turning an unverified byte-copy experiment into an apparently supported runtime feature.
