# Deterministic simulation contract (2026-08-23)

## Status and admission rule

This document is the evidence ledger for the replacement deterministic engine. It is not a list of byte ranges copied from the retired rollback implementation. A region enters `Schema::production_regions` only after its owner, complete writer set, lifetime, validation, capture phase, restore order, repair work, and verification are all proven.

The production region manifest is currently empty. Consequently no native SC6 adapter, replay seek, or online rollback path can activate. This is intentional fail-closed behavior while the remaining subsystems are audited.

The sole supported binary is the open Ghidra program `SoulcaliburVI.exe`. Addresses below are image virtual addresses for that binary. The Ghidra program was saved after the input/simulation-boundary audit.

## Coordinate and simulation boundary

One deterministic coordinate is one completed call to `LuxBattle_PerFrameTick` (`0x1402DBC60`), ending with the increment of `g_dwLuxBattleFrameCounter` at `0x1402DC34C`. It is not one Unreal tick.

`LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState` (`0x1403FE520`) can invoke the per-frame traversal more than once during one outer manager tick. Native move-state 3 performs an early traversal with zero/default arguments, and manager state at `+0x1462` can repeat the inner traversal and post-frame phase without rebuilding the player input pairs. An adapter placed around the outer UE tick would therefore assign inputs and snapshots to the wrong native iteration.

The complete native batch envelope is `LuxBattleManager_Tick_MainStateMachine_At1461` (`0x1403FBF30`). In active main state 2 it calls the whole simulation-loop worker at `0x1403FE520` once, then performs round-over evaluation, online synchronization bookkeeping, and the parent `AActor_TickActor` lifecycle. Calling either `LuxBattle_PerFrameTick` or this outer tick once per external coordinate is therefore forbidden: the inner call omits the batch lifecycle, while repeated outer calls would incorrectly repeat that lifecycle.

The replacement uses two complementary boundaries. The landing fencepost observes every completed deterministic coordinate and records its exact produced input pair. A signature-gated, read-only detour around `0x1403FBF30` observes the enclosing native batch before and after the original call, including `DeltaSeconds`, main/round state, input-log generation and time, manager cursors, and the global frame counter. The original outer function is still called exactly once by its native caller. This instrumentation may measure zero-, single-, multi-coordinate, and repeat-containing batches, but does not yet authorize synthetic advancement.

The eventual adapter must replay recorded native batches through the complete outer lifecycle while reproducing the exact per-coordinate input sequence inside each batch. Exact batch records, including `DeltaSeconds`, must be generation-scoped. Hook installation for observation is fail-closed on the verified 16-byte function signature; restore or synthetic advancement remains unauthorized until all state regions reached by the traversal are admitted.

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

Queue index/state, the manager frame-advance counter, pause grace state, track selection, and gameplay callback mutations are canonical. Downstream audio/UI/cinematic terminals are presentation and must eventually be journaled at their irreversible boundary, not by skipping the state transition. Static registration/removal is now closed for the later callback collections at `+0x8E0`, simulation `+0xA30`, round `+0xB80`, and unpause `+0xF70`; their weak-owner storage is generation identity and must be runtime-signed rather than serialized. The round subsystem remains blocked by the state graphs those callbacks mutate, not by unknown collection membership.

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

## Audited region: RNG families

Native initialization is one coupled transaction in `LuxBattle_InitRngAndHashPrimes` (`0x14034F610`). Initial match setup and `LuxBattle_NewRound_OnEnter` call it. It seeds the current thread's hidden UCRT state with `dwSeed >> 4`, seeds the Lux MT helper with the full seed, warms both by `dwSeed & 0xFFF` draws, derives the explicit LCG/LFSR states, seeds gameplay xorshift96, then consumes gameplay xorshift96 to seed the distinct wind RNG.

| Family | Owner and extent | Complete direct mutators/consumers established so far | Classification and restore rule |
|---|---|---|---|
| Park-Miller LCG | `g_dwLuxBattleLcgRngState` `0x14485EB28`, `uint` | `LuxBattle_InitRngAndHashPrimes` writes it. `LuxMoveVM_GetRandLCG` (`0x14034F550`) is the state-advancing helper and has sole direct caller `LuxMoveVM_ApplyAIPaletteMode`; several wind constructors contain inlined recurrence forms and xref the same word. | Canonical whenever AI palette or those constructors are reachable. Restore/hash independently. The helper is now fully documented in Ghidra with no fixable completeness deductions. |
| Battle LFSR | `g_adLuxBattleLfsrState` `0x14485EB30`, `uint[25]` (0x64 bytes), plus `g_dwLuxBattleLfsrIndex` `0x14485EB94`, `uint` | Initialization and `LuxMoveVM_GetRandU32` (`0x14034F130`) are the complete direct writers. Its caller inventory includes reaction/hit gameplay, CPU-direct paths, camera shake, and stage-wind constructors/updates. | Canonical shared-order state. Presentation-looking wind/camera consumers cannot simply be skipped if that changes draw order; suppress only their irreversible terminal output. |
| Gameplay xorshift96 | `g_stLuxBattleXorshift96State` `0x14470E2C8`, `FLuxBattleXorshift96State`, 12 bytes | Initialization and `LuxMoveVM_GetRandXorshift96Gameplay` (`0x14034F1F0`) advance it. Native `LuxBattle_SaveRoundRestoreSnapshot`/`LuxBattle_RestoreRoundSnapshot` and two HgCpu direct snapshot executors also access it. Callers include hit/damage/block logic, MoveVM probability/transition paths, round poses, camera actions, and effect variants. | Canonical shared-order state. Native round snapshots covering it do not cover the separate LFSR. |
| Wind combined RNG | `g_stLuxBattleWindCombinedRngState` `0x14470E2B0`, `FLuxBattleWindCombinedRngState`, 24 bytes | Initialization and `IwWind_GetRandCombinedRngU32` (`0x14034F3C0`). Direct consumers are the verified wind object constructors and oscillator updates. | Canonical because updated wind state feeds later traversal outputs and initialization order. Restore with the validated wind graph, never as a substitute for graph identity/lifetime validation. |
| Lux MT helper | `g_stLuxBattleMtState` `0x144100EA0`, `FLuxBattleMtState`, 5000 bytes | `LuxBattle_MT_Seed` initializes it; `LuxBattle_MT_GenerateNext`/refresh advance it through `LuxBattle_RandIntInRange`. Verified consumers are attention-camera initialization, replay-mode entry, and RNG initialization. | The state is deterministic but its visible consumers are currently presentation/replay setup. It remains outside production until lifecycle reachability proves whether an owned traversal can consume it. |
| UCRT `rand` | Current-thread UCRT internal state; no Lux global address | Seeded and warmed inside `LuxBattle_InitRngAndHashPrimes`. The warm-up call is at `0x14034F652`; MoveVM opcode and presentation caller inventories still need final closure. | **Blocked.** Hidden thread-local state cannot be admitted as a raw native region. The replacement needs a verified scoped broker or a proven native get/set contract, and must restore the caller's thread state after each owned iteration. |

The floating-point contract remains open. MXCSR and x87 control/status must be captured at the exact admitted traversal entry, normalized only to a versioned policy proven against the native caller, and restored to the caller on every exit including failure. No process-wide floating-point mutation is permitted.

## Remaining audit gates

The following gates must be completed and saved in Ghidra before the first non-empty production schema:

1. FrameInputSync packet/cache writers and all input filtering/consumption state.
2. RNG streams and floating-point control/status environment.
3. Fighter, HgCpu, MoveVM, command-player, VMPump, hit/reaction, animation, root motion, and allocation generations.
4. Camera, stage/barriers, wind, destructibles, and dynamic allocation owners.
5. Audio, VFX, listener callbacks, UI/cinematic events, and other irreversible presentation terminals.
6. Match entry, round exit/re-entry, scene change, disconnect, module/process cleanup, and hook teardown.

Until these close, the engine can be tested only against fake adapters. This prevents tests from turning an unverified byte-copy experiment into an apparently supported runtime feature.
