# Rollback Beta Remaining Native Boundaries — Static RE Handoff

Date: 2026-08-05  
Binary: `SoulcaliburVI.exe`, image base `0x140000000`  
Method: Ghidra MCP, disassembly/decompilation, and static HorseMod source cross-check  
Runtime validation: the original static pass performed none; the 2026-08-06
follow-ups include strict normal-render replay qualification and read-only live
memory inspection, as called out below

## Executive handoff

This expanded pass found four confirmed rollback correctness defects and one unresolved support-scope hole that should block an unrestricted beta candidate:

1. Horse treats character `+0x324` as a temporary input-source mode and forces it to `1` across the entire native `LuxBattle_PerFrameTick`. Native code instead uses this field as the current MoveVM move ID. The override changes scheduler branches and can overwrite a legitimate native move transition when Horse restores the entry value after the tick.
2. Horse's native input-callback snapshot ends at object `+0x4A7`. A callback registered by the same MoveDispatch BeginPlay path writes a separate two-qword event-mask array described by the TArray header at `+0x4A8/+0x4B0/+0x4B4`. Twenty verified native predicates read those masks, including two predicates that deliberately read the other player slot. The constructor zeroes the allocation once and the destructor frees it; no per-frame or per-move clear exists in the complete direct-reference surface. The masks are object-lifetime gameplay/event state and are currently absent from rollback save, hash, restore, and verification.
3. Horse snapshots the complete `FLuxMoveSchedState[2]` byte range at `0x144715400`, including each record's owning `pSubVM` at `+0x50`, and the generic restore writes those pointer bytes back. Native command commit/factory paths replace that heap object and scalar-delete the previous generation. Restoring a snapshot across a replacement can therefore publish a freed pointer; even within one generation, the pointed-to mutable command object is not captured.
4. The 0x88-byte `g_abLuxMoveSystemVMPumpState` at `0x144100C70` is deterministic round-lifetime control state, but Horse does not snapshot it. Its state 3 counter controls the 60/120-coordinate cleanup threshold, and state 4 rebuilds both normal SubVMs, publishes meter/effect work, and may end the pump lifetime.
5. The adjacent 0x6070-byte `g_abLuxMoveCommandPlayers` arena at `0x14470F390` is also absent from rollback. It contains two 0x3038-byte MoveVM command-player records with parser, opcode, reaction, hit, timer, control, ring-out, and RNG-derived state. Whether ordinary human-versus-human ActiveBattle can prove this arena dormant for every admitted rollback frame is not statically closed. Until it is, unrestricted mode must either cover it semantically or fail closed around its active routes.

The pre-callback input-pair injection itself is ordered correctly. Horse should continue writing the desired `FLuxBattleInputPair` before the stock `BattleManager +0x1210` callback collection. That collection is gameplay authority and must be allowed to deterministically clear or replace the pair before `LuxBattle_PerFrameTick` consumes it.

The Steam transport has two lower-severity ownership problems: it accepts `SteamNetworking004` even though this SC6 binary requires `SteamNetworking005`, and it unconditionally enables a relay policy that is interface-wide rather than Horse-channel-local. Match teardown, VMPump Begin/End, and SubVM replacement are hard identity-generation boundaries. Physical controller routing, two-PC P2P behavior, and a 3,600-second soak cannot be certified statically.

## Scope

The pass targeted native systems surrounding the remaining beta gates:

- authoritative input production, filtering, MoveVM scheduling, and consumption;
- callback state that can influence input and MoveVM predicates;
- online-session role to Lux input-slot mapping;
- Steam interface acquisition, recovery, session policy, and channel ownership;
- match/round teardown and pointer lifetime;
- scheduler/thread stall and unload ownership visible from static code.

It did not launch the game, run replays, attach a debugger, inspect live memory, or perform network/controller tests.

## 1. Authoritative native input chain

### Verified order

The complete stock order is:

1. `LuxBattleChara_UpdatePlayerInputData_FromRoundCache` (`0x1403FCD10`) reads one replay-cache word, advances the per-slot previous-input word, and produces `{current, rising}` in the `FLuxBattleInputPair` array.
2. `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState` (`0x1403FE520`) runs `BattleManager +0x1210` callbacks on that array.
3. `FilterALuxBattleMoveDispatchInputPairByFrameSlot` (`0x140427940`) may clear both words, publish a saved action-window word, and advance pending-window state.
4. The simulation loop places the post-filter pair pointers in `FLuxBattlePerFrameTickArgs` and calls world-mode vtable `+0x68`, reaching `LuxBattle_PerFrameTick`.
5. `LuxBattle_PerFrameTick` runs `LuxMoveVM_TickCharaCommandScheduler` for P1 and P2 before `LuxBattle_TickCharaInput` consumes the selected source.

Horse's `input_pair_callbacks_hook` follows the correct ordering: `PublishRollbackNativeInputPairs` writes the desired pair and previous-input array, then calls the original collection. The original callback must remain after injection.

### Why full equality is not a valid post-filter invariant

The desired Gekko pair is a pre-filter input. The stock callback can legitimately:

- clear current and rising input during an active attack;
- clear input for pending-window counts 1 and 2;
- replace both words from saved action-window state at count 3;
- mutate the pending-window state while doing so.

Therefore, `postFilterPair == desiredPair` is only valid on frames where the stock filter leaves input unchanged. The robust invariant is:

- exact desired pair was published before the callback;
- the callback ran once at the expected collection/header boundary;
- `LuxBattle_PerFrameTick` consumed the callback's post-filter result;
- the filter's own mutable state was restored before each resimulated frame.

## 2. Confirmed defect: `+0x324` is the current MoveVM move ID

### Native proof

Three independent consumers establish the field:

- `LuxBattle_TickCharaInput` (`0x140312510`) switches on character `+0x324`. Move IDs 0 and `0x2B` clear input; 1 and `0x6A..0x6C` select the latest-engine publication; 3 selects the mirror route; remaining IDs use scheduler/CPU state.
- `LuxMoveVM_TickCharaCommandScheduler` (`0x1402E52D0`) compares `+0x324` with the prior per-player move-ID latch, clears a transition counter when it changes, and chooses reaction, CPU-direct, zero-input, or continuation behavior from the move ID.
- `LuxMoveVM_SelectMoveCommandContinuation` (`0x1402E5470`) tests MoveVM move-ID ranges and can write `0x2B` back to character `+0x324` during the tick.

`ALuxBattleChara_VerifiedPartial +0x324` is now typed and named `uint dwCurrentMoveId` in Ghidra. The earlier separate “input source mode” interpretation is disproved.

### Horse mismatch

[`RollbackNativeInputOverride.hpp`](../../HorseMod/horselib/RollbackNativeInputOverride.hpp) reads both fighters at `+0x324`, writes both fields to `1`, and restores the saved values after the outer tick. [`RollbackProductionRuntime.hpp`](../../HorseMod/horselib/RollbackProductionRuntime.hpp) keeps that override armed across the complete native PerFrameTick and repeats it around stock inter-round ticks.

Consequences:

- the scheduler observes move ID 1 instead of the real current move;
- transition detection and the previous-move latch are changed;
- reaction and CPU-direct branches for other move IDs are suppressed;
- `LuxBattle_TickCharaInput` is forced onto the move-1 live-input route;
- a native in-tick write such as the continuation's `0x2B` transition can be erased by restoring the entry value afterward.

The engine-input qword writes in the same helper are redundant during owned simulation: PerFrameTick republishes the callback's pair from its arguments. During stock inter-round passage, forcing move ID 1 is also not stock behavior.

### Minimal correction

1. Remove all `+0x324` writes and restoration from owned PerFrameTick.
2. Call `original(&owned)` with the post-filter pair supplied by the enclosing simulation loop.
3. Remove the entire neutral override from `run_stock_inter_round_tick`; call the stock trampoline with its stock arguments.
4. If diagnostics are desired, read `dwCurrentMoveId` before and after the call, but never restore it.
5. Replace `source_override_restored` as an ownership requirement. It proves recovery from an invalid mutation, not valid input ownership.

Do not move desired-pair injection after the stock callback. That would bypass deterministic action-window filtering.

## 3. Confirmed defect: frame-action event masks are outside the snapshot

### Registration and writer

`InitializeLuxBattleCharaFrameActionAndRegisterCallbacks` (`0x1404157D0`) registers two weak-object callbacks:

- `FilterALuxBattleMoveDispatchInputPairByFrameSlot` in `BattleManager +0x1210`;
- `SetLuxBattleCharaFrameActionEventBit` (`0x1404274E0`) in event-hub callback collection index `0x16`.

The event request is the recovered 8-byte `FLuxFrameActionEventBitRequest_Partial`:

| Offset | Type | Meaning |
| ---: | --- | --- |
| `+0x00` | `byte` | event bit index |
| `+0x01` | `byte[3]` | unknown/padding |
| `+0x04` | `int` | slot index |

The writer admits bit indices 0 through 37 and performs:

```text
pMoveDispatch->pEventBitMasks[slot] |= 1ULL << bitIndex
```

### Ownership and construction

`ALuxBattleMoveDispatch_Constructor` (`0x1404049E0`) initializes a native `TArray<ulonglong>` at:

| Offset | Type | Meaning |
| ---: | --- | --- |
| `+0x4A8` | `ulonglong *` | event-mask data owner |
| `+0x4B0` | `int` | event-mask count |
| `+0x4B4` | `int` | event-mask capacity |

The constructor reserves two entries and appends two zero qwords, one per player slot.

### Verified readers

The following native leaf predicates read this array using the request slot index:

| Address | Final Ghidra name | Tested bits |
| --- | --- | --- |
| `0x140415EF0` | `TestLuxBattleCharaFrameActionEventBits11To13` | 11, 12, 13 |
| `0x140415F10` | `TestLuxBattleCharaFrameActionEventBit16` | 16 |
| `0x140415F30` | `TestLuxBattleCharaFrameActionEventBit10` | 10 |
| `0x140415F50` | `TestLuxBattleCharaFrameActionEventBit9` | 9 |
| `0x140415F70` | `TestLuxBattleCharaFrameActionEventBit27` | 27 |
| `0x140415F90` | `TestLuxBattleCharaFrameActionEventBit26` | 26 |
| `0x140415FB0` | `TestLuxBattleCharaFrameActionEventBits26Or27` | 26 or 27 |
| `0x140416590` | `TestOtherLuxBattleCharaFrameActionSlotEventBit24` | bit 24 in slot `1 - request.slot` |
| `0x1404167D0` | `TestLuxBattleCharaFrameActionEventBit1` | 1 |
| `0x1404167F0` | `TestLuxBattleCharaFrameActionEventBits1Or2` | 1 or 2 |
| `0x140416A00` | `TestLuxBattleCharaFrameActionEventBit3` | 3 |
| `0x140416B60` | `TestLuxBattleCharaFrameActionEventBit2` | 2 |
| `0x140416BD0` | `TestLuxBattleCharaFrameActionEventBit15` | 15 |
| `0x140416BF0` | `TestLuxBattleCharaFrameActionEventBit4` | 4 |
| `0x140416C10` | `TestLuxBattleCharaFrameActionEventBit20` | 20 |
| `0x140417830` | `TestLuxBattleCharaFrameActionEventBit33` | 33 |
| `0x140417850` | `TestLuxBattleCharaFrameActionEventBits17Or33` | 17 or 33 |
| `0x140417920` | `TestOtherLuxBattleCharaFrameActionSlotEventBit23` | bit 23 in slot `1 - request.slot` |
| `0x140417940` | `TestLuxBattleCharaFrameActionEventBit25` | 25 |
| `0x140417960` | `TestLuxBattleCharaFrameActionEventBit22` | 22 |

The constructor installs these functions in the MoveDispatch trigger/predicate map. The masks therefore affect native gameplay/event conditions; they are not diagnostic or presentation-only state. The two other-slot predicates also prove that the pair is coupled state: rollback cannot restore only the locally owned slot.

### Lifetime closure

The complete direct `ALuxBattleMoveDispatch +0x4A8` reference surface in the native MoveDispatch subsystem is now classified:

- `ALuxBattleMoveDispatch_Constructor` allocates two qwords and initializes both to zero;
- `SetLuxBattleCharaFrameActionEventBit` is the sole verified writer and only ORs bits into an existing qword;
- the 20 predicates above read the masks;
- `DestroyALuxBattleMoveDispatchSubobjects` (`0x14040A850`) frees `pEventBitMasks` during object destruction.

No direct reference clears the masks per frame or per move, resizes the array after construction, or replaces its owner. The conservative static conclusion is therefore object-lifetime persistence. This does not change the minimal restore implementation—the live owner/count/capacity still must be checked and only the two qword values restored—but it rules out relying on a stock per-frame reset to heal speculative bits.

### Horse omission

[`RollbackNativeInputCallbackSnapshot.hpp`](../../HorseMod/horselib/RollbackNativeInputCallbackSnapshot.hpp) explicitly models mutable state only through `+0x4A7`. It captures:

- slot/frame indices and action-window state through `+0x494`;
- a separate `0x20`-stride subelement TArray at `+0x498/+0x4A0/+0x4A4`.

It never reads, hashes, restores, or verifies the event-mask TArray beginning at `+0x4A8`.

This is a direct save/restore hole: an event bit set on a speculative timeline can survive rollback, and a bit that should exist on the restored timeline can be absent.

### Minimal correction

Extend `RollbackNativeInputCallbackSnapshot` with:

- `uintptr_t event_mask_owner`;
- `int32_t event_mask_count` and `event_mask_capacity` for integrity;
- `std::array<uint64_t, 2> event_masks`.

Capture policy:

1. Read `+0x4A8/+0x4B0/+0x4B4`.
2. Require the verified SC6 layout: non-null owner, count exactly 2, capacity at least 2 and within a small sane bound.
3. Read exactly two qwords.
4. Include both qwords in the semantic hash and owner/count/capacity in local integrity.

Restore policy:

1. Require the same live object and event-mask owner.
2. Require live count 2 and capacity at least 2.
3. Restore only the two qwords, not the TArray pointer/count/capacity.
4. Include the masks in the existing recapture verification.

This is intentionally a two-qword fix. A generic unbounded container serializer is unnecessary for the verified binary.

## 3A. Confirmed defect: scheduler snapshot restores an owning SubVM pointer

### Native ownership proof

`g_abLuxBattleCpuCommandStatePerPlayer` (`0x144715400`) is an exact `FLuxMoveSchedState[2]`: two 0x60-byte records, total 0xC0. Each record contains:

| Offset | Classification | Meaning |
| ---: | --- | --- |
| `+0x10` | identity | current fighter pointer |
| `+0x50` | owning generation pointer | current `CCpuDirectCommand`/derived SubVM |
| remaining verified fields | deterministic scalars | selected slots, move IDs, prior IDs, transition counters, and parameters |

`LuxMoveVM_SchedState_CommitCommandSlot` (`0x1402E5660`) can commit a selected move ID to character `+0x324` and enter a SubVM factory. `LuxMoveVM_CreateCpuDirectState` (`0x1402E26A0`), `LuxMoveVM_CreateHgCpuDirectMoveSubVM` (`0x1402E5220`), `LuxMoveVM_InitSubVMForNormalMove` (`0x1402E5710`), and the default reset path at `0x1402E25A0` publish newly allocated command objects and delete the previous object. Verified allocation extents are class-dependent: 0x68, 0x70, 0x78, or 0x80 bytes.

The recovered 0x78-byte common AllGuard layout and 0x80-byte derived count layout also prove that these objects contain mutable frame counters, transitions, reaction timers/latches, scheduler back-pointers, and derived thresholds. They are not immutable strategy descriptors.

### Horse mismatch

[`RollbackSnapshot.hpp`](../../HorseMod/horselib/RollbackSnapshot.hpp) registers `g_LuxBattle_CCpuCommandArray` as one raw 0xC0 explicit range. Its canonical policy excludes pointer offsets from the peer hash, but `WriteRollbackSnapshotFrameUnchecked` still writes every byte of every range with `SafeWriteBytes`. Canonical exclusion therefore does not protect local restore ownership.

Two failure modes follow:

- if the live scheduler replaced its SubVM after capture, restore can write the freed old `pSubVM` back into the live scheduler;
- if the pointer generation is unchanged, restore still omits the mutable bytes inside the pointed-to SubVM, producing a mixed-time scheduler/object pair.

VMPump state 4 can trigger the same replacement before the ordinary per-character scheduler runs, so this is not made safe by the current whole-tick `+0x324=1` override.

### Minimal correction

1. Split each scheduler record so raw snapshot ranges exclude `pChara` and `pSubVM`; do not restore either pointer through the generic byte writer.
2. Capture the live fighter pointer, SubVM pointer, vtable RVA, and allocation-class/extent as local integrity metadata.
3. Before any rollback mutation, require the same fighter and SubVM generation. Abort/fail closed if either differs.
4. For a same-generation beta path, add a typed semantic SubVM snapshot for only the verified allocation classes and fields. Exclude vtable, owner identities, and unknown/uninitialized tail bytes from the peer hash.
5. If reconstruction across generations is required, call the verified native factory first, verify the new class/extent, then restore semantic bytes. Do not attempt to resurrect an old heap address.

This is deliberately not a generic heap serializer. The native factory set and four proven object extents provide a small allowlist.

## 3B. Confirmed defect: VMPump state is omitted; command-player arena support is unclosed

### The 0x88-byte pump transaction

`g_abLuxMoveSystemVMPumpState` (`0x144100C70`) is now typed as `FLuxMoveSystemVMPumpState_Partial`, exact size 0x88. `LuxBattle_PerFrameTick` tests its enabled field and dispatches outer states 0 through 4 before either character scheduler:

| State | Verified effect |
| ---: | --- |
| 0 | initialize/reset the round pump stage |
| 1 | increment the deterministic round-intro coordinate |
| 2 | advance lane A then lane B MoveVM state and command-player data |
| 3 | advance `dwStateFrame` by 0, 1, or 2 and transition only after strict 60/120-coordinate gates |
| 4 | set both fighters to move `0x2B`, rebuild normal SubVMs, publish guarded meter changes, finalize lane hit/effect state, then optionally call EndVMPump |

`LoadLuxMoveSystemPumpStateFromRoundInitSnapshot` (`0x1402DBB20`) is direct native serialization evidence. It copies fighter identities and semantic lane/control fields from a round-initialization snapshot while deliberately preserving live lane dispatch identities at `+0x10` and `+0x40`. `LuxMoveSystem_BeginVMPump` (`0x14031C950`) ends an existing transaction before rebinding fighters and lanes. `LuxMoveSystem_EndVMPump` restores saved fighter/timer fields and disables the transaction. Begin, state 4, and End are therefore generation/lifetime boundaries, not ordinary scalar ticks.

The interface surface is also closed. Four adjacent slots at `0x1432690B0..0x1432690C8` expose dual-lane Start, single-lane Start, End, and IsEnabled through exact `this + 8` adjustment thunks. The surrounding wider interface table also contains ordinary battle/stage operations such as round start, camera selection, and stage queries. Pump control is therefore part of the general battle adapter surface, not an isolated Horse diagnostic or a statically identifiable replay-only helper. Static virtual dispatch still does not prove that normal human-versus-human play invokes Start on every match, so mode-specific dormancy remains an admission proof rather than an assumption.

Horse has the pump RVA only in `NativeReplayTraceHook`; it is diagnostic and is not part of rollback save/hash/restore. The omission can make terminal cleanup occur early or late after rollback and can replay or skip SubVM replacement, meter notification, and lane-effect finalization.

Minimal coverage should use a dedicated typed snapshot:

1. Record fighter and lane dispatch pointers as local identity metadata, never peer-canonical bytes.
2. Capture/hash the verified lane scalars and top-level controls, including `nState`, intro/state counters, enabled flag, mode flags, and saved timer cursor.
3. Preflight the current pump generation and all identities before mutation.
4. Restore semantic fields only. Reject restore across Begin/End, fighter rebinding, or state-4 SubVM replacement unless the corresponding SubVM reconstruction transaction is implemented.

### The adjacent 0x6070-byte MoveCommand arena

`g_abLuxMoveCommandPlayers` (`0x14470F390`) is exact `FLuxMoveCommandPlayerSlot[2]`, size 0x6070. Each slot has a 0x3038 stride and ends exactly where the scheduler array begins. Native MoveVM and pump functions mutate the embedded parser/opcode/reaction/hit/timer/control/ring-out/RNG-derived state; `+0x08/+0x10` and other recovered pointers are current-round identities.

The native HgCpu global serializer at `0x14031DC00` does not cover this arena. Horse references its RVA in trace diagnostics and reads one interior field for a fixture, but no rollback snapshot owns the array.

`IsLuxMoveSystemVMPumpEnabled` (`0x1402D7500`) is an exact bool query over `dwPumpEnabled`, and the same general interface exposes it immediately after End. The non-overengineered beta choices are:

- prove a narrowly supported rollback mode never admits frames while VMPump or another arena-mutating scheduler route is active, and fail closed when that proof is not true; or
- finish a typed semantic snapshot of the two fixed-size slots, validating/rebinding identity pointers and excluding unknown/tail bytes from the canonical hash.

A raw 0x6070-byte copy is not acceptable because it would restore identity pointers and potentially allocator residue. Removing the incorrect `+0x324` override broadens the scheduler routes that can reach this arena, so the support gate or semantic coverage must ship in the same correction batch.

## 4. Controller and Lux input-slot boundary

### What native code proves

`Steam_InitAllInterfaces` (`0x1404C3DD0`) acquires `SteamController005` at `SteamContext +0x68`, but the only `SteamController005` string reference is interface bootstrap.

`InitializeLuxBattleFrameInputSyncPlayerFlagsFromOnlineSession` (`0x1403FA330`) derives Lux input-slot masks from the local online-session role:

| Role | active mask | remote mask | interpretation |
| ---: | ---: | ---: | --- |
| 0 | 1 | 2 | local side A |
| 1 | 2 | 1 | local side B |
| 2 | 3 | 0 | spectator-style two-slot observation |
| 3 | 0 | 3 | hidden/lobby-style role |

The full controller-to-frame-input chain is now recovered:

1. `FLuxInputRouter_Partial` owns two `TArray<int32>` headers: default owner assignment at `+0x00` and active owner assignment at `+0x10`. `ProcessLuxInputKey` writes `pOwnerBySlot[nLogicalSlotIndex] = nNewOwnerId`, with `-2` and `-3` as special/rejected sentinels. `ResolveLuxInputOwnerLogicalSlot` scans that table and excludes the slot currently being rebound. Its formerly unknown signed field at `+0x24` is a slot-zero fallback owner filter: `-1` permits any otherwise-unmapped owner to resolve to an unassigned slot 0, while any other value permits only the matching owner ID.
2. `DispatchAndCompactInputOwnerCallbacks` broadcasts four ABI values: registered UObject context, logical slot index, new owner id, and accepted flag. Its first payload was previously mislabeled as a previous owner; disassembly and the consumer prove it is the logical slot.
3. `HandleLuxInputOwnerChanged` receives the full ABI but intentionally uses only `nLogicalSlotIndex`: 0 resets the left processor cache and 1 resets the right cache. It ignores the physical owner id and accepted flag.
4. Binding-table changes use a separate no-payload weak callback wrapper. `HandleLuxInputBindingsChanged` receives only the registered processor context and resets both side caches; the router payload preserved by the generic dispatcher is not forwarded by this specialization.
5. `InitializeULuxBattleInputProcessor` constructs two independent 0xC0-byte sides. Side 0 is bound to the CDO's `+0x28` left binding array and stored at `+0x48/+0x50`; side 1 uses `+0x38` and `+0x58/+0x60`.
6. `UpdateLuxBattleFrameInputSlotStateFromProcessor` uses the two selector integers at `ALuxBattleFrameInput +0x500` to select the left or right processor bitfield. It then computes current, pressed, released, repeated, and 32 hold timers in exact 0x90-byte slot records at `+0x3E0`.
7. `GetCurrentInputForFrameInputLogSlot` (`0x1403F0680`) consumes the already-routed BattleManager slot record. No physical device handle, Steam handle, or UE LocalPlayer identity enters `ALuxBattleFrameInput` or the rollback callback pair.

The connection lifecycle is also closed. `InitializeLuxInputRouterSubsystem` constructs exactly two default `-1` owner entries, copies them into the active table, and registers controller and secondary-input connection callbacks. `HandleLuxControllerConnectionChanged` and `ProcessLuxSecondaryInputOwnerDisconnect` resolve a disconnected physical owner to a logical slot, then forward only that slot through `DispatchIndexedCallbackEntriesAndCompact`; the owner ID and the controller callback's unused 64-bit payload stop at the handler. Shutdown restores defaults, invalidates dependent caches, unregisters both connection callbacks, and leaves the two arrays for the guarded process-exit destructor.

### Static conclusion

Native code does maintain a logical-slot-to-physical-owner routing table, including a signed slot-zero fallback policy, but that identity remains upstream. Owner changes and disconnections are reduced to a logical slot before callbacks invalidate derived side caches; later sampling rebuilds a logical left/right bitfield, and only that downstream bitfield crosses into `ALuxBattleFrameInput`. Horse injects at the downstream logical pair boundary, so it does not need to snapshot or emulate the physical owner table for deterministic resimulation.

Lux local/remote authority remains session-role-based after UE/engine input routing. Static code proves which Lux slot is local or remote, but it cannot certify which Windows/Steam/UE device supplies a live local bitfield. Controller hot-plug, two local devices, Steam Input remapping, and focus changes remain physical qualification cases rather than rollback-state blockers.

Do not use successful `SteamController005` acquisition as evidence that controller-to-player routing is correct.

## 5. Steam interface, recovery, and ownership

### Exact stock interface contract

`Steam_InitAllInterfaces` fills a 0xA8 `SteamContext` with 21 pointers. Relevant offsets are:

- `+0x40`: exact `SteamNetworking005`;
- `+0x68`: exact `SteamController005`.

The function returns false on the first null acquisition and leaves any previously acquired prefix populated. It does not roll the context back.

`ResetSteamContextAndInitializeIfPipeAvailable` (`0x1404CF9A0`) explicitly clears `+0x00..+0x98`, does not clear `pSteamVideo` at `+0xA0`, and tail-calls the full acquisition only when `HSteamPipe` is nonzero. The wrapper discards the acquisition result. This is stock recovery behavior, not a readiness API.

### Horse compatibility

Horse correctly reuses the process's existing `steam_api64.dll`, HSteamUser, and HSteamPipe and does not initialize Steam or pump callbacks itself. It closes only channel `0x484F`, not the shared peer session. Those choices match the native ownership boundary.

### Issues

1. Horse tries `SteamNetworking005` and then `SteamNetworking004`. The version-locked SC6 binary requires 005. Falling back to 004 can make Horse appear available while the stock interface contract is not satisfied. Require 005 and fail closed.
2. `AllowP2PPacketRelay(true)` accepts only the interface and a boolean. It is not parameterized by peer or channel, so it changes shared networking policy and is never restored. Either remove the call and inherit stock policy, or explicitly declare one-way session-policy ownership. There is no prior-value getter in this API.
3. `AcceptP2PSessionWithUser(remote)` is peer-session-wide, not channel-local. It is likely idempotent, but the code comment should acknowledge the shared mutation. `CloseP2PChannelWithUser(remote, 0x484F)` remains the correct shutdown scope.

## 6. Match/round lifetime

`ScbattleWorldMode_FinalizeBattleThunk` (`0x1403BB2B0`) reaches `LuxBattle_TeardownMatchState_OnEnd` (`0x1402DC380`). Teardown:

- releases both frame-transform shared resources through `LuxBattle_ReleaseFrameTransformSharedResources` (`0x140314820`);
- resets MoveVM, fighters, effect/camera, time-scale, freeze state, and a shared match resource through `LuxBattle_ResetMatchRuntimeStateAndReleaseSharedResource` (`0x1402FB2C0`);
- returns the world-mode pump to idle and clears terminal/time-dilation gates.

This is a hard match epoch. No snapshot containing character, MoveDispatch, frame-transform, effect, animation, or callback-object identities may cross it. A rematch or new lobby must discover fresh identities and capture a fresh baseline.

Horse's round coordinator already compares match identity at major boundaries. As defense in depth, the owned outer simulation scope can cheaply compare the current live BattleManager/fighter token against the latched epoch once per owned iteration. Per-field live checks are unnecessary.

## 7. Stalls, worker ownership, and unload

Static inspection found no network wait in the synchronous native simulation call. Steam packet waits occur on the worker thread. The transport stop sequence sets the stop flag, joins the bootstrap thread, stops the worker, then closes the Horse channel, preventing bootstrap from starting a worker after shutdown.

The physical build intentionally rejects hot unload after native hooks are installed. This is a safe product restriction, not a defect.

Static code cannot certify:

- queue/backpressure behavior during a long OS or Steam stall;
- peer liveness across real suspend/resume or route migration;
- two-PC callback/session coexistence;
- long-session memory growth or rare generation rollover.

## Prioritized findings

| Priority | Finding | Confidence | Required action |
| --- | --- | --- | --- |
| P0 | Whole-tick `+0x324` override mutates current MoveVM move ID and can erase in-tick transitions | confirmed by three native consumers and Horse writes | remove the override before beta |
| P0 | MoveDispatch event-mask TArray `+0x4A8` is object-lifetime gameplay state but omitted from rollback snapshot | confirmed constructor, OR-only writer, 20 readers, destructor-only free, and Horse snapshot bounds | capture/hash/restore both qwords |
| P0 | Raw scheduler restore writes owning `pSubVM` pointers without capturing the pointed-to mutable object | confirmed generic write path plus native replacement/delete factories | split pointer fields from raw ranges; generation-preflight and typed SubVM restore |
| P0 | 0x88-byte VMPump state/counters are absent and state 4 is a SubVM/effect lifetime boundary | confirmed native round-init loader and states 0..4 | add typed semantic snapshot and reject cross-generation restore |
| P0 for unrestricted support / P1 for an explicit narrow gate | 0x6070-byte MoveCommand arena is mutable and omitted | confirmed native consumers and absence from Horse snapshot | semantically cover fixed slots or fail closed whenever an arena-mutating route is active |
| P1 | Steam relay enable is shared interface policy, despite channel-only ownership claim | confirmed API signature and open/close code | remove or explicitly own/document policy |
| P1 | `SteamNetworking004` fallback broadens beyond SC6's exact 005 contract | confirmed native version literal | require 005 |
| P2 | Input ownership telemetry assumes pre-filter source equality on the proving frame | confirmed native filter can legitimately mutate pair | report pre- and post-filter evidence separately |
| P2 | Fighter identity is latched and not re-read in every owned per-frame helper | static defense-in-depth gap | validate one live epoch token at outer iteration boundary |

## Main-agent implementation order

1. Delete the current-move-ID override path and update ownership evidence/status names so no field is described as an input source mode.
2. In the same correction batch, split `FLuxMoveSchedState` identity pointers from generic raw restore and add SubVM generation preflight. Do not allow the corrected scheduler routes to run under rollback until same-generation SubVM semantic coverage or an explicit fail-closed gate exists.
3. Add the dedicated 0x88-byte VMPump semantic snapshot, treating Begin/End/state 4 as generation boundaries.
4. Either complete typed MoveCommand arena coverage or formally narrow supported rollback admission and fail closed while an arena-mutating route is active.
5. Extend `RollbackNativeInputCallbackSnapshot` with the verified two event-mask qwords and add restrained capture/restore/hash tests.
6. Keep pre-filter pair injection and the original `+0x1210` callback order unchanged.
7. Tighten Steam to exact `SteamNetworking005`; decide relay-policy ownership explicitly.
8. Add one outer-iteration live epoch/token comparison if it is not already guaranteed by the enclosing simulation scope.
9. Re-run final qualification only after these static defects and support gates are resolved. This report itself performed no runtime validation.

## Ghidra change ledger

| Address | Final name/type work | Result / remaining uncertainty |
| --- | --- | --- |
| `0x140312510` | `LuxBattle_TickCharaInput`; `dwCurrentMoveId` conclusion and plate corrected | effective completeness 84%; remaining score deductions are historical globals/register projections |
| `0x1402E52D0` | `LuxMoveVM_TickCharaCommandScheduler`; move-ID ownership and in-tick `0x2B` consequence documented | effective 82.1%; remaining globals/one conservative raw field |
| `0x1402E5470` | `LuxMoveVM_SelectMoveCommandContinuation` cross-check | effective 100%; one structural register projection |
| `0x1402E5660` | `LuxMoveVM_SchedState_CommitCommandSlot`; scheduler scalar commit and SubVM replacement boundary | confirms character `+0x324` write and owning `+0x50 pSubVM` transition |
| `0x1402E25A0/0x1402E26A0/0x1402E5220/0x1402E5710` | default reset, CPU-direct factories, and normal-move SubVM initialization | verified replacement/delete ownership and allocation allowlist 0x68/0x70/0x78/0x80 |
| `0x14031C950/0x14031CAC0` | `LuxMoveSystem_BeginVMPump` / `LuxMoveSystem_EndVMPump` | pump identity generation and saved fighter/timer transaction documented; End effective 89%, 11 mechanical alias points |
| `0x14031D460` | `LuxMoveSystem_PumpVMSlots` | exact lane-A then lane-B state 1/2/3 dispatch; effective 92%, fixable 8 from interior-global aliases |
| `0x14031D530` | `LuxMoveSystem_VMPumpState3_RoundEndWait` | exact +0/+1/+2 counter progression and strict 60/120 gates; effective 92%, fixable 8 from typed-parent interior aliases |
| `0x14031D5B0` | `LuxMoveSystem_VMPumpState4_RoundCleanup` | SubVM rebuild, meter publication, lane-effect finalization, and optional End boundary documented; scorer retains 11 mechanical points from interior aliases |
| `0x1402DBB20` | `LoadLuxMoveSystemPumpStateFromRoundInitSnapshot` | native selective-restore evidence; preserves lane dispatch identities at `+0x10/+0x40` |
| `0x1402D7460/0x1402D74A0` | `StartSingleLaneLuxMoveVMPump` / `StartDualLaneLuxMoveVMPump`; exact authored pair and player-index ABI | one- and two-lane entry boundaries fully typed and documented |
| `0x1403CFEB0/0x1403CFE90` | `HandleSingleLanePumpSecondaryInterfaceThunk` / `HandleDualLanePumpSecondaryInterfaceThunk` | exact `this + 8` multiple-inheritance forwarding boundary documented |
| `0x1402D74F0/0x1402D7500` | `EndLuxMoveSystemVMPumpFromInterface` / `IsLuxMoveSystemVMPumpEnabled` | exact interface End and bool-enabled query; supplies a fail-closed pump gate |
| `0x1403CFFB0/0x1403C3B40` | `HandleEndLuxMoveVMPumpSecondaryInterfaceThunk` / `HandleIsLuxMoveVMPumpEnabledSecondaryInterfaceThunk` | completes the adjacent four-slot Start/Start/End/IsEnabled interface slice |
| `0x14036B1B0/0x14036B260/0x14036B2A0/0x14036B310/0x14036B3B0` | common AllGuard counter/tick family typed to `CCpuDirectAllGuardCounters_TypedPartial` | all completeness fixable deductions at or below 9.1 |
| `0x14036B450/0x14036B4A0/0x14036B4D0/0x14036B570/0x14036B5C0` | stand/sit derived init, scalar delete, and ticks typed to the 0x80 derived overlay | destructor effective 100%; remaining functions at or below 5 fixable points |
| `0x1403FCD10` | `LuxBattleChara_UpdatePlayerInputData_FromRoundCache` cross-check | effective 96.1% |
| `0x1403FE520` | simulation-loop ordering cross-check | effective 96.9% |
| `0x140427940` | input-filter ordering comment and BeginPlay name update | effective 96.9%; exact authored enum names at sub-frame bytes remain unknown |
| `0x1404157D0` | renamed `InitializeLuxBattleCharaFrameActionAndRegisterCallbacks`; exact prototype, locals, labels, plate/PRE/EOL | effective 92.8%, fixable deductions 7.2 |
| `0x1404274E0` | created `SetLuxBattleCharaFrameActionEventBit`; exact prototype and comments | effective 100% |
| `0x140415EF0..0x140415FB0` | created and typed seven event-mask predicate leaves | all effective 100% |
| `0x140416590..0x140417960` | created, named, prototyped, and documented 13 additional event-mask predicates | all effective 100%; two explicitly select the other player slot |
| `0x1404049E0` | constructor comments identify the `TArray<ulonglong>` at `+0x4A8` and fixed two-slot initialization | constructor is very large; only the verified event-mask subrange was changed |
| `0x14040A850` | corrected to `DestroyALuxBattleMoveDispatchSubobjects`; typed MoveDispatch parameter and documented final free of `+0x4A8` | effective 89%; 11 fixable scorer points remain solely because the exact vtable base is intentionally not coerced to a generic pointer array |
| `0x1403DC330` | `InitializeALuxBattleFrameInputRuntimeStorage`; exact slot/delegate/selector initialization | effective 92%, fixable 8 |
| `0x1403F5CD0` | `UpdateALuxBattleFrameInputPlayerSlotStates`; exact two-slot virtual update path | effective 98.1%, fixable 0 |
| `0x1403FC640` | `UpdateLuxBattleFrameInputSlotStateFromProcessor`; selector, delegate, edges, repeat, and hold timers typed | effective 100% after structural-only register projection |
| `0x1404BE600/0x1404BE650` | left/right processor bitfield getters | effective 92.1%, fixable 7.9 each |
| `0x140949C30` | `GetLuxBattleInputProcessorStaticClass`; exact module/package/null metadata helpers and split wide class-name storage | effective 96.1%, fixable 3.9 |
| `0x140947630/0x1404A3F10` | construction wrapper and `InitializeULuxBattleInputProcessor` | exact 0x68 CDO, left/right binding arrays, side objects, and shared controllers recovered; constructor fixable 8 |
| `0x14049EF40/0x1404B81A0` | side construction and cache reset | exact 0xC0 side layout; constructor fixable 8, reset 100% |
| `0x1404C2FC0` | `ProcessLuxInputKey` rechecked against recovered router layout | active owner table and `-2/-3` sentinels verified; fixable 8, remaining locals are register projections |
| `0x1404C18A0` | `ResolveLuxInputOwnerLogicalSlot`; exact signed owner-id and logical-slot ABI | resolves exact active mappings, excludes captured slot, and applies the slot-zero fallback filter; effective 95.7%, fixable 4.3 |
| `0x1404C4100/0x1404BBAD0` | `InitializeLuxInputRouterSubsystem` / `ShutdownLuxInputRouterSubsystem` | exact two-slot default construction, callback registration/removal, cache invalidation, and process-lifetime array ownership; shutdown effective 97.1%, initialization's remaining 19.1 scorer points are one false PARAM reference from literal `CL=1` normalized to `DAT_14406ED00/+1` |
| `0x1404B67D0/0x1404CF6B0` | `CancelLuxInputOwnerCaptureForLogicalSlot` / `SetLuxInputOwnerCaptureForLogicalSlot` | capture lifecycle and `ownerBySlot[slot] = -1` transition verified; fixable 9.1 each or less |
| `0x1404C6B90` | `GetLuxInputKeyboardLayoutPolicyResult`; exact USER32 import signature added | one-check cache and native `0x040C` false case verified; effective 100%, structural register projection only |
| `0x1404C71B0/0x1404C7BD0` | created `ProcessLuxSecondaryInputOwnerDisconnect` / `HandleLuxControllerConnectionChanged` | both reduce physical owner loss to a logical slot; effective 98.6% and 100% after structural projections |
| `0x140415940` | corrected historical name to `DispatchIndexedCallbackEntriesAndCompact(FCallbackEntryCollection_Partial *, int)` | assembly proves EDX is preserved and forwarded to callback virtual slot `+0x68`; fixable 7.2, remaining locals are register projections |
| `0x1404B65E0` | corrected first callback payload name to `nLogicalSlotIndex` | full slot/new-owner/accepted dispatch ABI verified; fixable 3.9 |
| `0x1404BB5E0` | `InvokeWeakLuxInputOwnerCallback` | full four-argument weak callback wrapper recovered; fixable 7.3 |
| `0x1404C7140` | `HandleLuxInputOwnerChanged` | full four-argument ABI; only logical slot selects the cache; 100% |
| `0x141E4CAE0` | `InvokeWeakUObjectNoPayloadCallback` | proves binding-change specialization discards generic router payload; fixable 7.9 |
| `0x1404C74C0` | corrected `HandleLuxInputBindingsChanged(ULuxBattleInputProcessor_Partial *)` | resets both caches; fixable 7.9 |
| `0x141DDFCD0` | `CopyInt32Array` | proves router reset copies default owner assignments into active assignments |
| `0x1404CA430` | `ResetLuxInputRouterOwnerAssignmentsAndNotifyBindingsChanged` | exact default-to-active copy and binding-change broadcast; effective 100% |
| `0x1431EF860` | created `DestroyLuxInputRouterOwnerArraysAtExit` | frees only the active and default TArray allocations after the guarded process lifetime; effective 100% |
| `0x14225CCD0/0x140067310/0x140B73AB0` | `GetEngineModuleName`, `GetNullPointerStub`, `GetLuxorGameScriptPackageName` | generated registration metadata fully identified; each fixable 8 or less |
| `0x1409A9480` | renamed `GetLuxTutorialDataAssetStaticClass`; exact return/prototype; class global typed | effective 79%; generated UE registration helpers remain conservative |
| `0x1409A5E90` | created `ReleaseLuxTutorialDataAssetRegistrationObject`; prototype/comments | effective 90%; callee's older name remains imperfect |
| `0x1404C3DD0` | `SteamContext` completed to 21 pointers; `pSteamFriends` corrected; plate corrected | exact interface sequence verified; generated call-site constants remain scorer deductions |
| `0x1404CF9A0` | renamed `ResetSteamContextAndInitializeIfPipeAvailable`; exact void prototype and comments | clear range and discarded result verified |
| `0x140314820` | named/prototyped frame-transform shared-resource release helper | effective 84%; remaining global documentation deductions |
| `0x1402DC380` | match teardown epoch documented | effective 84%; remaining global documentation deductions |
| `0x1403BB2B0` | finalization thunk documented | 100% |

### Structs and global data changed

- `ALuxBattleChara_VerifiedPartial +0x324`: `uint dwCurrentMoveId`.
- `SteamContext`: 0xA8, 21 interface pointers; `+0x40 pSteamNetworking`, `+0x68 pSteamController`, `+0xA0 pSteamVideo`.
- `LuxBattleCharaFrameActionRuntimeV2_Partial`: extended through `+0x4B4` with `pEventBitMasks`, `nEventBitMaskCount`, and `nEventBitMaskCapacity`; `+0x388` corrected to the embedded owner-resolver vtable; final verified overlay size 0x4B8.
- `FLuxFrameActionEventBitRequest_Partial`: new conservative 8-byte request overlay.
- `FLuxMoveSchedState[2]`: exact two-record 0xC0 scheduler array; `+0x10 pChara` is fighter identity and `+0x50 pSubVM` is an owning replaceable heap generation.
- `FLuxMoveSystemVMPumpState_Partial`: exact 0x88 pump transaction with two 0x30-byte lanes and top-level state/counter/enable/mode/timer controls.
- `FLuxMovePumpMoveLevelPair_Partial`: exact four-byte `{ushort wMoveId; ushort wLevelId;}` authored entry argument.
- `FLuxMoveCommandPlayerSlot[2]`: exact two-slot 0x6070 MoveCommand arena, 0x3038 stride per player.
- `CCpuDirectAllGuardCounters_TypedPartial`: exact 0x78 common mutable AllGuard SubVM layout.
- `CCpuDirectAllGuardCount_TypedPartial`: exact 0x80 derived layout with guard threshold at `+0x78` and unsupported/uninitialized tail at `+0x7C`.
- `CCpuDirectAllGuardCountVtable_TypedPartial` and `CCpuDirectCommandVtable_Partial`: exact eight-slot 0x40 vtable types with preserved callback signatures.
- `g_abLuxBattleCpuCommandStatePerPlayer` (`0x144715400`), `g_abLuxMoveSystemVMPumpState` (`0x144100C70`), and `g_abLuxMoveCommandPlayers` (`0x14470F390`) were fully typed and documented with rollback ownership classifications.
- `g_abCCpuDirectStandAllGuardCountVtable`, `g_abCCpuDirectSitAllGuardCountVtable`, and `g_abCCpuDirectCommandVtable` were typed with their exact eight-slot vtable structures. Superseded malformed interim AllGuard types/signatures were deleted after a successful reference-safe dry run.
- `ALuxBattleFrameInput_Partial`: exact enabled flag, repeat delays, read delegate, two 0x90-byte slot records, and two selectors through final size 0x508.
- `FLuxBattleFrameInputSlotRecord`: exact 0x90 current/pressed/released/repeated plus 32 hold timers.
- `ULuxBattleInputProcessor_Partial`: exact 0x68 CDO with left/right 0x10 binding arrays and side/control smart-pointer pairs.
- `FLuxBattleInputProcessorSide_Partial`: exact 0xC0 logical-side object with side index `+0x60`, current bitfield `+0x64`, cached owner/state `+0x68`, and sparse/hash cache tail.
- `FLuxBattleInputProcessorOwnerCapture_Partial`: exact 0x10 vtable/owner capture object.
- `FLuxBattleInputProcessorOwnerCaptureStorage_Partial`: 0x48 movable callback capture storage.
- `FLuxBattleInputProcessorSharedControl_Partial`: exact 0x18 strong/weak controller.
- `FLuxInputBindingArray_Partial`: exact 0x10 pointer/count/capacity header.
- `FInlineSharedRefSparseArray_Partial`: exact 0x38 sparse shared-reference cache.
- `FLuxInputRouter_Partial`: corrected to 0x2B; `+0x00` is an `FTArrayInt32_Partial` default-owner table, `+0x10` is the active owner table, `+0x20` is the captured logical slot, `+0x24` is the signed slot-zero fallback owner ID, and `+0x28..+0x2A` are capture/layout flags.
- `FTArrayInt32_Partial`: exact 0x10 int32 TArray header.
- `FLuxWeakInputOwnerCallback_Partial` and `FWeakUObjectNoPayloadCallback_Partial`: separate exact 0x18 weak-UObject callback-prefix layouts for the two proven ABIs.
- `ULuxTutorialDataAssetFrameSlotTable_Partial`: new conservative 0x3C UObject overlay with class metadata and authored frame-slot fields. It replaces the conflicting older overlay only at the independently verified MoveDispatch field.
- `g_pLuxTutorialDataAssetClass`: typed `UClass_Partial *` and documented.
- Input globals/vtables were named and documented, including the processor class/CDO, processor/side/capture/control vtables, owner/binding/disconnection callback collections, decomposed router storage, `g_pGEngine`, the exact `GetKeyboardLayout` import signature, and exact `L"Engine"`, `L"/Script/LuxorGame"`, and `L"LuxBattleInputProcessor"` registration storage. By-value callback collections use an `g_ab...` prefix to denote inline storage while retaining their exact `FCallbackEntryCollection_Partial` type. Vtable bases whose full entry extent was not revalidated remain intentionally untyped rather than being flattened into generic pointer arrays.

Ghidra was re-decompiled after the structural changes and saved after the final cross-system audit.

## Remaining static uncertainty

- The physical owner-ID namespace is intentionally not guessed. Static code proves equality, sentinel, fallback, and logical-slot behavior, but not whether every producer uses a Windows, Steam, UE platform-user, or translated Lux identifier.
- The controller connection callback's third register argument is ABI-real and ignored. It remains a conservative opaque 64-bit value because no consumer in this specialization establishes a stronger type.
- The keyboard-layout helper's exact authored product-policy purpose remains unknown. Its cache behavior and the `0x040C` false case are exact.
- The authored names of frame-action event bits 0..37 remain unknown. Their storage, writer range, individual tested positions, cross-slot reads, and object-lifetime persistence are established independently of those names.
- Exact vtable entry extents not needed for these boundaries remain conservative. They were not flattened to generic arrays merely to improve decompiler output.
- Static code does not yet prove that the 0x6070 MoveCommand arena is dormant throughout every rollback-admitted frame in the intended human-versus-human beta mode. Its mutation surface is proven; the exact product-mode admission relationship remains open.
- The exact safe reconstruction policy for every derived `CCpuDirectCommand` class is not fully closed. Four allocation extents and the AllGuard family are proven, but unknown classes must remain fail-closed rather than using a generic byte copy.
- VMPump fighter/dispatch pointers are typed as current-round identities. The native round-init loader proves which dispatch identities it preserves, but Horse still needs an explicit generation token tying pump, fighters, schedulers, and SubVMs together before any restore mutation.
- The four pump interface slots are exact, but the full surrounding general battle/stage interface-table extent was not revalidated. It remains intentionally unflattened and untyped rather than replacing a much larger callback table with a guessed generic pointer array.
- `LuxMoveSystem_VMPumpState4_RoundCleanup`, `LuxMoveSystem_EndVMPump`, and the round-init pump loader retain 11-point mechanical completeness deductions because Ghidra's scorer treats named interior aliases of the fully typed parent global as independent untyped globals. `IsLuxMoveSystemVMPumpEnabled` has the same artifact at 8 points. Applying overlapping data types at those aliases would contradict the verified 0x88 parent layout, so no speculative type was forced.
- `InitializeLuxInputRouterSubsystem` has a 19.1-point mechanical completeness deduction caused solely by a spurious Ghidra PARAM reference from immediate `CL=1` at `0x1404C41E8`, normalized by different audit views to `DAT_14406ED00` or `DAT_14406ED00+1`. The instruction is documented as a literal boolean, and no data type or symbol was fabricated at the false target.
- No runtime, replay, debugger, emulator, network, or live-controller validation was performed in this pass.

## Runtime-only beta qualification left to the main agent

After the static blockers and explicit support gates above are resolved, the remaining beta decision still requires evidence this pass cannot produce:

- all 14 replays on one immutable final artifact;
- controller attach/isolation and focus/hot-plug cases;
- recovery and induced-stall cases;
- physical two-PC Steam P2P qualification;
- the physical 3,600-second / 216,000-frame soak;
- an immutable candidate manifest tying binary, config, corpus, and reports together.

Static reverse engineering should not be used to waive those gates.

## 2026-08-06 remediation and beta-readiness decision

### Decision

Rollback is **not ready for an unrestricted beta**. Four confirmed defects from
this report are fixed, the Steam interface contract is narrowed, and the final
artifact passes the available static/unit and replay checks. The production
manifest now intentionally fails closed because the mutable 0x6070-byte
`g_abLuxMoveCommandPlayers` arena still lacks typed semantic capture and restore.
Removing that gate would conceal a known correctness hole.

### Executed plan

| Work item | Result |
| --- | --- |
| Stop treating `ALuxBattleChara +0x324` as an input selector | Complete. Whole-tick and inter-round writes/restores were removed. Native `dwCurrentMoveId` now remains owned by the MoveVM scheduler. Trace terminology was corrected from `input_source_mode` to `current_move_id`. |
| Cover the MoveDispatch event masks | Complete. Both qwords are captured, hashed, restored, and verified while the live TArray owner/header is preserved. Count/capacity/owner validation fails closed. |
| Remove raw scheduler pointer restoration | Complete. `FLuxMoveSchedState[2]` restore now copies only verified scalar ranges and validates fighter/SubVM identities before any mutation. |
| Cover SubVM state safely | Complete for the statically verified factory allowlist. Concrete vtable RVA and class extent select semantic ranges; scheduler, object, vtable, fighter, opponent, and owner-scheduler identities are integrity-only. Unknown classes and generation changes fail closed. The known 0x80-byte AllGuard uninitialized tail remains excluded. |
| Cover the 0x88 VMPump | Complete. Lane state/counters and top-level control fields are semantic; fighter/dispatcher bindings are integrity identities. State and enable values are validated before capture/restore. |
| Fix Steam ownership assumptions | Complete. Horse now requires exact `SteamNetworking005`; the unsupported `SteamNetworking004` fallback and interface-wide relay-policy mutation were removed. |
| Close the MoveCommand arena | Not complete. Native evidence proves ordinary scheduler and command-VM paths mutate it, but the two 0x3038-byte records still contain unresolved pointer-rich parser/opcode/reaction/hit/control regions. Raw copying would restore identities, vtables, uninitialized storage, and potentially stale owners. |
| Enforce the unresolved boundary | Complete. Snapshot ABI is 44, canonical schema is 31, pump and SubVM capabilities are declared tested, and the command arena is a `PendingEvidence` manifest entry. Production activation reports `pending-gameplay-coverage` and remains unavailable. |

### Implementation locations

- `HorseMod/horselib/RollbackLuxMoveSystemSnapshot.hpp`: typed VMPump capture,
  identity validation, semantic hashing, and semantic-only restore.
- `HorseMod/horselib/RollbackLuxSubVmSnapshot.hpp`: concrete SubVM allowlist,
  extent checks, identity preflight, and semantic-only restore.
- `HorseMod/horselib/RollbackSnapshot.hpp` and
  `HorseMod/horselib/RollbackStepHarness.hpp`: scheduler identity-preserving
  restore, generation preflight, capability/manifest closure, and integration.
- `HorseMod/horselib/RollbackNativeInputCallbackSnapshot.hpp`: exact two-qword
  MoveDispatch event-mask coverage without replacing its TArray owner.
- `HorseMod/horselib/RollbackNativeInputOverride.hpp` and
  `HorseMod/horselib/RollbackProductionRuntime.hpp`: removal of the invalid
  `dwCurrentMoveId` override.
- `HorseMod/horselib/RollbackSteamP2PTransport.hpp`: exact interface acquisition
  and removal of the global relay-policy side effect.

### Additional Ghidra closure

`LuxBattle_InitCommandPlayerState` at `0x1402DED20` was renamed, re-prototyped
as `void __fastcall LuxBattle_InitCommandPlayerState(FLuxMoveCommandPlayer
*pCommandPlayer)`, and retyped. Its reaction-row loop initializes 30 rows of
default 85.0 values inside the command player. The loop variables were typed and
renamed, the generation/identity boundary was documented with plate/PRE/EOL
comments, direct command-player offsets were annotated, and completeness is
100%. Ghidra was saved after verification.

Cross-checking `LuxMoveVM_TickCharaCommandScheduler` (`0x1402E52D0`),
`LuxMoveVM_SelectMoveCommandContinuation` (`0x1402E5470`),
`LuxMoveSystem_SeedVMSlotForMove` (`0x140367E40`),
`LuxMoveVM_TickDriver` (`0x1403656B0`), and
`LuxBattle_ResetRoundCountersAndCommandSlots` (`0x140302930`) establishes that
the command arena is normal gameplay state: move reactions read/write it, the VM
mutates parser/opcode/control/published-input state, an opcode may consume CRT
`rand`, and round reset rebinds its two slots. The arena cannot be admitted by a
"pump disabled" predicate and cannot safely be copied as an opaque 0x6070-byte
blob.

### Verification on the final artifact

Final DLL:
`E:\myMods\build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll`

- Build timestamp: `2026-08-06T03:14:45.7832979+02:00`
- SHA-256: `283D794D6AA097DE1FF4E6521954090C6A5A2F8B93B5BE69BB26E063739DD65F`
- CTest: 66/66 passed, including `RollbackLuxMoveStateSelfTest`, scheduler
  identity-preserving restore, event-mask restore, exact Steam interface policy,
  and the production fail-closed manifest assertion.
- Strict normal-render replay run: `20260806-032536-seek`.
- Trace:
  `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260806_032539_pid43672.jsonl`
- Generation: 11,006 frames, five rounds, integrity and oracle checks true.
- Seek cases: 4/4 passed at 10%, 25%, 50%, and 75%; 2,400/2,400 resumed frames
  compared with zero state mismatches; measured tick rate 59.9-60.4 Hz; every
  seek validation completed within the 0.5-second threshold.

This replay run certifies that the remediation did not regress the prescribed
seek/restore path. It does not exercise the deliberately disabled production
rollback path and therefore does not close the MoveCommand arena defect.

### Remaining beta plan

1. Recover typed semantic regions for `FLuxMoveCommandPlayer`, starting from the
   parser/opcode state, command definition and reaction arrays, published input,
   hit/timer/control state, ring-out state, and RNG-derived fields. Classify every
   pointer, owner, vtable, and padding range as identity, reconstructible, or
   unsupported.
2. Add capture/hash/restore only for independently verified mutable semantic
   fields. Preserve live identities and fail closed on allocation, class,
   fighter/opponent binding, or generation changes. Do not raw-copy the arena.
3. Remove the `PendingEvidence` manifest entry only after the arena capability
   has targeted restore/hash tests and direct-caller re-decompilation shows no
   uncovered mutable region in admitted gameplay routes.
4. Build one new immutable candidate and run the complete 14-replay corpus,
   physical controller attach/focus/hot-plug/isolation cases, induced-stall and
   recovery cases, physical two-PC Steam P2P qualification, and the 3,600-second
   / 216,000-frame soak.
5. Publish a candidate manifest tying DLL hash, schema/ABI, configuration,
   replay corpus, controller/network reports, and soak report. Beta approval is
   valid only if production `live_ready` is true and every physical gate passes.

## 2026-08-06 MoveCommand ownership-closure follow-up

### Result

The beta decision remains **no**. The remaining production gate is not a stale
manifest entry: the 0x6070-byte MoveCommand arena combines mutable command-VM
state with live process identities. This pass deliberately did not replace that
gate with an opaque arena copy.

The executed closure plan was:

1. Re-read the current beta gate and recover the two-slot arena layout from its
   initializers, ordinary scheduler/VM callers, reset paths, and CPU-personality
   producers.
2. Classify pointer-bearing fields independently of adjacent mutable state and
   apply a conservative exact-size overlay in Ghidra.
3. Check whether the prescribed replay route exercises the arena and perform a
   read-only pointer census; do not write game memory.
4. Re-decompile the changed functions and their direct callers, save Ghidra,
   and retain the production fail-closed gate unless semantic closure is proven.

### Corrected layout and ownership

`FLuxMoveCommandPlayerRollbackOverlay_Partial` is an exact 0x3038-byte overlay.
The following qword offsets are now classified as live identities in each slot:

`+0x08`, `+0x10`, `+0x28`, `+0x30`, `+0x340`, `+0xBA8`, `+0xBB0`,
`+0xBB8`, `+0xBC0`, `+0xBC8`, `+0xBD0`, `+0xBD8`, `+0xBE0`, `+0xCC8`,
`+0xCD8`, `+0xCE0`, and `+0x1998`.

They include the self/opponent characters, two interior aliases, DTP tables,
a callback, the active weight bank, an embedded vtable, three
personality-object-owned pointers, the selected personality object, reaction
and move-definition tables, and the cell array. The final four bytes at
`+0x3034` remain unsupported/uninitialized tail storage.

The personality producer also corrected an older interpretation. The mutable
range at `+0x988..+0xBA7` is a contiguous 0x220-byte derived-pick workspace; it
is not merely eight 0x18-byte records. The exact nested
`FLuxCpuPersonalityState348ToBA8_Partial` (0x860 bytes) now exposes:

- `+0x948..+0x967`: four-qword base-weight bank;
- `+0x968..+0x987`: four-qword live-weight bank;
- `+0x988..+0xBA7`: 0x220-byte derived-pick workspace.

`LuxBattle_InitCpuPersonalityData` (`0x140364950`),
`LuxBattle_RefreshCpuPersonalityData` (`0x140364BC0`), and
`LuxBattle_SetCpuPersonalityAlternateData` (`0x140364C90`) now use the exact
rollback overlay in their prototypes. Their variables, control-flow label,
plate/PRE/EOL comments, and direct callers were re-decompiled. The fallback
weight globals are typed and named `g_qwDefaultCpuPersonalityWeights[4]` and
`g_qwEmptyCpuPersonalityWeights[4]`. Final mechanical completeness is 80.93%,
87.07%, and 96.13%, respectively. The remaining deductions in the first two
are the deliberately conservative `+0x10/+0x18` personality-vtable calls; no
speculative full vtable ABI was invented. Ghidra was saved after verification.

### Read-only runtime evidence

A 30-second sample of both 0x3038-byte slots during the prescribed replay route
observed no changed bytes. That result is useful only to show that replay
playback can leave this native command route dormant; it cannot qualify live
rollback, because production human input reaches the ordinary MoveCommand
scheduler/VM path that the replay route can bypass.

The live pointer census agreed with the static overlay and found the same
pointer pattern in both slots. It also confirmed that raw copying would restore
module pointers, heap/DTP owners, interior pointers, and a vtable alongside
gameplay state. No game-memory write, debugger mutation, or rollback admission
change was performed.

### Why implementation stops at the gate

Capturing every byte except the 17 currently known pointer qwords would still
be a raw arena copy. The remaining large parser/reaction/control spans have not
had every pointer, owner, padding byte, and reconstruction rule closed across
all admitted native routes. A partial semantic snapshot would also be
incorrect because known mutable consumers extend into those unresolved spans.

Therefore the existing `PendingEvidence` capability for
`g_abLuxMoveCommandPlayers` remains the correct implementation. Production
activation continues to return `pending-gameplay-coverage`; snapshot ABI 44,
canonical schema 31, and the previously qualified DLL remain unchanged by this
Ghidra/documentation-only follow-up.

### Work still required for beta

1. Recover the remaining parser/opcode/reaction/hit/timer/control/ring-out
   subobjects from all ordinary scheduler consumers, not just their initializer.
2. Define typed semantic capture or deterministic reconstruction for every
   mutable field, with identity/generation preflight before any restore write.
3. Add targeted save/hash/restore tests and remove `PendingEvidence` only when
   direct-caller coverage finds no mutable gap in an admitted route.
4. Build one immutable candidate, then complete the 14-replay corpus,
   controller/focus/hot-plug/isolation cases, stall/recovery cases, physical
   two-PC Steam P2P qualification, and the 3,600-second soak.

## 2026-08-06 MoveCommand semantic closure and beta-gate execution

### Outcome

The native correctness blocker is now implemented and the production coverage
manifest reports `live_ready=true` with zero pending gameplay entries. Rollback
is **not yet release-qualified for beta distribution**, because the repository's
authoritative beta gate additionally requires immutable local qualification,
manual two-controller isolation evidence, and a physical two-PC Steam P2P
qualification manifest for this exact artifact. Those external evidence files
do not exist for the new ABI/schema and cannot be manufactured by static or
single-machine testing.

### Final Ghidra ownership partition

Re-decompilation of the ordinary human MoveCommand scheduler, opcode executor,
predicate refresh, personality producer, and reaction consumers closed the two
0x3038-byte slots without using a raw arena copy. Each slot is partitioned
exactly as follows:

| Ownership | Offset/size |
|---|---:|
| Semantic header state | `+0x0000 / 0x0008` |
| Semantic header scalars | `+0x0018 / 0x0010` |
| Primary parser/control state | `+0x0038 / 0x0308` |
| CPU-personality state | `+0x0348 / 0x0860` |
| Personality runtime state | `+0x0BE8 / 0x00E0` |
| Predicate control | `+0x0CD0 / 0x0008` |
| Parser/reaction state | `+0x0CE8 / 0x0CB0` |
| Predicate/VM state | `+0x19A0 / 0x1088` |
| Reaction state | `+0x2AA8 / 0x058C` |
| Seventeen identity qwords | 136 bytes at the previously listed offsets |
| Diagnostic text | `+0x2A28 / 0x0080` |
| Unsupported/uninitialized tail | `+0x3034 / 0x0004` |

The nine semantic banks total 12,076 bytes. Together with 136 identity bytes,
128 diagnostic bytes, and the four-byte tail, they account for all 12,344
bytes in one slot with no overlap or gap.

Ghidra now also contains the exact 0x78-byte
`FLuxMoveVmPredicateFlagRing_Partial` and the conservative minimum-size
`FLuxMoveCommandPlayerPredicateOverlay_Partial`. The overlay was applied to
`LuxMoveVM_RefreshConditionFlagRing` (`0x140364D10`); its self/opponent locals,
predicate fields, plate/PRE/EOL comments, and direct caller were rechecked. Its
fixable completeness deduction is 9.80 points, below the ten-point threshold.

The following one-hop command-dispatch helpers were named, typed, and
documented with `FLuxCommandDispatchContext_Partial *` parameters:

- `0x1402D4AD0 GetLuxCommandPlayerReactionSelectorClamped0To12`
- `0x1402D4B40 GetLuxCommandPlayerField2C0CAfterCpuRefresh`
- `0x1402D4BA0 SetLuxCommandPlayerField2CA0`
- `0x1402D4BC0 GetLuxCommandPlayerField2CA0`

Major reaction consumers now use `FLuxMoveCommandPlayer *` rather than generic
pointers, including `LuxMoveVM_TickWithNotifTokenSwitch` and the standard
launch, back-breaker, reaction-0x40, and reaction-0x3F handlers. Ghidra was
saved after the batch.

The indexed field anchors at `0x144711F90`, `0x144711F9C`, and `0x144712030`
are now typed, named, and plate-commented as
`g_nLuxMoveCommandPlayer0ReactionSelector2C00`,
`g_dwLuxMoveCommandPlayer0Field2C0C`, and
`g_dwLuxMoveCommandPlayer0Field2CA0`. This closes the remaining global audit
deductions: all four dispatch helpers now score 100% completeness. Predicate
refresh remains at 9.80 fixable points because two SSA/register projections
and conservative constants are not forced into speculative types.

### Implemented snapshot contract

`HorseMod/horselib/RollbackLuxMoveCommandSnapshot.hpp` implements fixed-size,
allocation-free capture/hash/restore for those nine banks. It:

- captures all 17 qwords only as local generation identities;
- requires active self/opponent identities;
- preflights every identity in both slots before the first write;
- excludes every identity, diagnostic byte, and uninitialized byte from the
  peer-canonical hash and restore image;
- restores only the nine verified semantic banks;
- recaptures and verifies the semantic hash after restore.

`RollbackStepState` now captures, integrity-checks, peer-hashes, restores, and
post-verifies this component. The production manifest entry is a tested
`DynamicSnapshot` capability rather than `PendingEvidence`. The incompatible
wire/state contract was bumped from runtime ABI 44 / canonical schema 31 to
ABI 45 / schema 32.

### Verification on the new artifact

Artifact:
`E:\myMods\build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll`

- SHA-256: `534DAC013161D0A84B0B39B0089BB490FDEB59275A56AC56F0AC0142ECBB7357`
- Built and deployed byte-identically to the game.
- `RollbackLuxMoveStateSelfTest` passes semantic rewind, identity exclusion,
  diagnostic/tail preservation, generation rejection, and recapture checks.
- All 66 rollback CTest cases pass.
- The stock-online acceptance runner self-test and static policy lint pass.
- Strict normal-render replay run `20260806-042205-seek` passes 4/4 cases.
  It generated 11,006 frames across five rounds, compared 2,400/2,400 resumed
  frames with zero mismatches, sustained 59.9-60.4 ticks/s, and completed every
  seek within the 0.5-second limit.
- Report:
  `E:\myMods\reports\replay_tests\replay_seek_e2e_20260806-042205-seek.json`.
- Ghidra was re-decompiled against the direct callers and saved after the final
  global/completeness audit.

### Remaining beta qualification—not an implementation defect

The old static correctness defects are closed. The remaining blockers are
release evidence gates for this exact ABI-45/schema-32 artifact:

1. regenerate an immutable trusted normal-replay golden/candidate bundle for
   the new DLL (the checked-in golden manifest names an older DLL);
2. complete the 14-replay two-client corpus gate;
3. capture the interactive physical-controller neutral/host-only/Sandboxie-only
   isolation report;
4. complete the full local Steam/Sandboxie network, recovery, both-role stall,
   match-lifecycle, and one-hour soak qualification;
5. complete the physical two-PC, distinct-consumer-network Steam P2P matrix and
   produce its signed/immutable qualification manifest;
6. feed the local and physical manifests to `--beta-release-gate` and require
   an exact artifact, runner, profile, ABI, and schema match.

Until these evidence gates pass, the correct answer to "ready for beta" is no,
despite the production snapshot manifest itself now being live-ready.

Attempting to freeze `candidate-abi45-schema32.json` correctly failed before
writing a candidate: the checked-in trusted golden manifest is schema 2, while
the current golden validator requires schema 4/oracle schema 12 and complete
clean-provenance fields. Re-labeling or mechanically upgrading that old file
would fabricate evidence; the normal-render golden corpus must be regenerated
on the exact new DLL from a clean source identity.

The authoritative corpus inventory was still generated at
`reports/rollback_beta/replay-corpus-inventory-abi45-schema32.json`: it finds
all 14 unique replays, with `ready=0`, `needs_oracle=14`, and ten cases requiring
the existing test-only selection-availability path. Consequently, repeatedly
running normal seek on those files would not satisfy the missing two-client
corpus gate; each needs a new exact-artifact oracle and corrected two-client
case under the immutable candidate contract.
