# Rollback Effects and Lux Battle-Event Hub Investigation

**Status:** Active static and runtime-backed investigation  
**Started:** 2026-08-04  
**Last updated:** 2026-08-07 - complete listener registration inventory and typed phase-latch owners  
**Scope:** HorseMod production rollback, side-effect journaling, audio, VFX, effect notifications, Lux-to-Unreal fighter synchronization, animation/montage state, and adjacent presentation state  
**Primary evidence:** Soulcalibur VI v2.31 decompilation and disassembly through Ghidra MCP; current HorseMod source  
**Runtime validation:** Normal-renderer strict replay seek and two-client rollback replay qualification are included from the remediation pass onward; no-render runs remain non-certifying

> **2026-08-06 beta-readiness update:** The latest remediation removes the
> invalid `ALuxBattleChara +0x324` whole-tick override, covers the MoveDispatch
> masks, VMPump, verified SubVM classes, and now the two-slot MoveCommand arena.
> The final ownership audit partitions each 0x3038-byte slot into 12,076
> semantic bytes, 17 identity qwords, 128 diagnostic bytes, and a four-byte
> uninitialized tail. Horse preflights every identity, restores and peer-hashes
> only semantic banks, and leaves the other bytes untouched. ABI 45/schema 32
> reports zero pending gameplay entries. The current immutable candidate is
> `E4CEC84B31F08F167C42C57CA461172E12511F83FB25ABE193B872257139470C`;
> it passes 66/66 CTest and both host/sandbox 250 ms worker-stall cases. A
> Replay 102 normal-render versus local two-peer run has now been localized
> across 1,780 pair-confirmed frames: all gameplay constituents and both peers
> agree under the explicit `1e-5` cross-mode float tolerance, while exact peer
> canonical hashes remain mandatory. Distribution is still not beta qualified
> until the exact artifact completes the replacement 14-replay corpus,
> controller, recovery/soak, and physical two-PC release gates.
>
> **MoveCommand ownership follow-up:** A subsequent Ghidra pass recovered 17
> live pointer-bearing offsets per 0x3038-byte MoveCommand slot and corrected
> `+0x988..+0xBA7` to a contiguous 0x220-byte derived-pick workspace. Read-only
> replay sampling left the arena unchanged, which demonstrates only that replay
> playback can bypass the live command route. It did not justify an
> arena-dormant predicate. A subsequent complete ownership partition replaced
> the former `PendingEvidence` gate with typed semantic coverage; identities,
> diagnostics, and uninitialized storage are never peer-hashed or restored.

## Executive conclusion

The current source now inventories all 41 listener collections before
activation and rejects the seven collections with no native registration
producer unless they are empty. This closes callback-graph discovery as an
admission mechanism, but it does not by itself make presentation complete.
The inventory binds the exact lifecycle objects that own round actor `+0x489`
and `+0x494` and phase-active actor `+0x4B8`; those reversible source-frame
latches still need typed capture/restore. Collections 25-29 and 35 remain
persistent character presentation routes requiring reconstruction rather than
terminal journaling.

HorseMod currently intercepts the wrong abstraction boundary for rollback effects.

The global at `SoulcaliburVI.exe!0x14470D188` is not a terminal VFX renderer. It points to an embedded battle-event dispatcher interface owned by `ULuxBattleEventListenerHub`. Its virtual methods convert native requests into callback packets, synchronously invoke registered subscribers, and compact callback collections. Those subscribers include stateful VFX, sound, animation, provider, and subsystem handlers.

Consequently, suppressing all dispatcher calls during speculative simulation does more than suppress particles or audio. It suppresses semantic handler execution, handler-owned state changes, provider calls, Blueprint publication, and callback-list maintenance. Replaying the original dispatcher call after confirmation then evaluates some events against later/current gameplay and UObject state rather than the state that generated the event.

The event hub also owns a live weak-callback graph. Each broadcast walks the current collection in reverse registration order, resolves each weak UObject target, and compacts dead/disabled entries after recursion unwinds. HorseMod neither snapshots this graph nor records which callbacks actually observed an event. Confirmation therefore re-enters a potentially different callback graph, not merely the same subscribers with a delayed packet.

The current 38-slot blanket hook should not be treated as a valid presentation boundary. The long-term correction is to classify every route and subscriber, preserve rollback-relevant semantic work, and journal only proven terminal irreversible emissions.

A second major boundary error exists in fighter presentation reconciliation. HorseMod reproduces the stock virtual transform-builder and `AActor::SetActorTransform` calls, but the virtual does not convert native `PLAYER +0x94/+0xA0` fields. It obtains the current `ScbattleWorldMode` facade, creates/selects a player-info handler, reads primary-bank matrix 0 through native `PLAYER +0x35A0`, converts that battle-pose matrix to Unreal render space, and decomposes it into `FTransform`. The separately read native scalar fields are diagnostic-only. The narrow root-publication ABI is correct and consumes a matrix bank already covered by `RollbackHgCpuSnapshot`.

The completed `ALuxBattleChara_TickActor` ownership audit corrects an earlier conclusion in this report. Actor `+0x53C` is a UE-tick Soul Charge fade-in counter, actor `+0x537` is a cached presentation selector, and actor `+0x4C0` is a 0x50-byte TSet-like provider container—not a gameplay move table. The Soul Charge refresh is overwhelmingly presentation work, with one native presentation sidecar that applies or clears four named secondary-sway bone records. The full tick is still unsafe as a confirmed-frame reconciliation primitive because it executes arbitrary queued asset/lifecycle delegates and then enters a base tick that can dispatch `ReceiveTick` through `ProcessEvent`. The production weakness is therefore incomplete confirmed presentation reconciliation and unowned lifecycle callbacks, not omitted deterministic Soul Charge gameplay.

The matrix-bank timing audit finds the production frame boundary sound: the native finalizer rotates both three-slot banks first, solves the new primary pose into the selected current slot, and completes secondary skeleton work before HorseMod captures the rollback step. Production restore invokes the native HgCpu reader before restoring HorseMod's controller state and all three captured slots. Runtime replay evidence invalidated the former 97-matrix admission policy: Replay 100 advertises 231 and 379 matrices, although the physical primary slots contain 768 matrices each. HorseMod now captures the complete `0xC000` bytes of every primary physical slot and transports the enlarged authority image with a 64-bit chunk receipt mask. The non-production history helper was also corrected to reconstruct a post-step historical frame from `current_slot`, not the already-previous `provider_slot`.

The animation audit finds three separate ownership domains crossing the Lux/Unreal boundary. The main body and weapon use a custom AnimGraph node that samples restored Lux matrix banks; appendix actors instead derive a UE skeletal-mesh clock from the current Lux header frame and synchronously publish threshold events into an appendix AnimBlueprint; UE montages live in an independent `UAnimInstance` pointer graph. `RollbackCharaAnimationState` covers native Lux clip/scheduler state despite its broad name, but it covers neither the appendix clock/event flags nor montage instances. Production confirmation only republishes the root actor transform, so it cannot reconcile these two UE-owned animation histories.

## Investigation rules

- All game-behavior claims in this document must be supported by decompiled code, disassembly, xrefs, recovered types, or current HorseMod source.
- In-game descriptions are not evidence.
- Ghidra changes use native MCP tools only.
- Relevant functions, parameters, locals, globals, and structures are renamed or typed as they are verified.
- Runtime/game/replay evidence is permitted when its exact artifact and report
  are recorded; static behavior claims remain grounded in Ghidra and source.
- Hypotheses and unverified risks are labeled explicitly.

## Current HorseMod design

### Side-effect policy

`HorseMod/horselib/RollbackSideEffectLedger.hpp` states that deterministic native schedulers keep running while external audio and VFX are represented by idempotent events and committed after confirmation.

The production runtime currently journals:

- audio wrapper calls;
- all 38 two-argument event-dispatcher slots from vtable `+0x08` through `+0x130`;
- camera vibration output;
- selected confirmed transitions and reconciliation work.

The dispatcher installation is in `RollbackProductionRuntime.hpp::install_vfx_dispatcher_hook`. It patches slots in the shared vtable and excludes only `+0x138` and `+0x140` because their ABIs and query semantics differ.

During an owned simulation tick, `vfx_dispatch_slot`:

1. validates the current global dispatcher and patched slot;
2. suppresses the native call;
3. reads a route-specific request size;
4. normalizes selected value ranges into `RollbackProductionVfxInvocation`;
5. queues the invocation in the confirmed-frame side-effect ledger.

At confirmation, `commit_side_effect` re-resolves the current dispatcher and calls the retained original virtual method with the normalized request.

### Immediate contradiction

`RollbackSnapshot.hpp` still describes a capability covering only eight verified VFX slots:

- `+0x08`
- `+0x28`
- `+0x78`
- `+0xB8`
- `+0xC0`
- `+0xC8`
- `+0x100`
- `+0x128`

It also says other state/query methods are outside the capability. The current implementation patches 38 slots. The safety manifest therefore no longer describes the implemented system.

### Weak-area priority from the static audit

| Priority | Weak area | Static basis | Consequence |
|---|---|---|---|
| P0 | Event-hub broadcasts are treated as terminal VFX | All 38 two-argument hub slots enter `vfx_dispatch_slot`; Ghidra shows synchronous stateful VFX, sound, color-fade, provider, animation, and Blueprint subscribers | Speculative simulation skips semantic work and confirmation replays it against later state |
| P0 | Confirmation uses the current weak-listener graph | `ProcessAndCompactCallbackEntries` walks callbacks in reverse order, weak-target failure requests compaction, and registration/compaction mutate the 0x70-byte collection outside recursion; the journal stores only route/request bytes | A confirmed packet can reach a different subscriber set/order and stale weak entries are cleaned at the wrong frame |
| P0 | Audio hooks are above cue resolution and voice allocation | The two hooked RVAs build collection-6 requests; collection 4 independently reaches current-state audio routing and `LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr` | Cue/provider selection, active-voice IDs, tracking sets, and Blueprint timing move to confirmation |
| P0 | Event payloads are assumed to be complete semantic records | Collections 0, 1, 2, 3, 4, 11, 25, and 31 all read handler/manager/asset/component state not present in the stored request | Identical queued bytes can produce a different result when committed later |
| P0 | One route has a false native ABI | Slot `+0x08` captures 0x50 bytes from an exact 0x44-byte source request | Guarded 12-byte over-read and a false size embedded in the journal contract |
| P0 | Fighter reconciliation is root-transform-only | `reconcile_confirmed_presentation` calls only actor vtable `+0x6A0` and `SetActorTransform`; stock `ALuxBattleChara_TickActor` continues with hair, appendix/weapon, material, mesh, visibility, move-state visibility, and base-actor work | A successful confirmation publish can leave dependent Unreal presentation state stale or temporally inconsistent |
| P0 | `ALuxBattleChara_TickActor` mixes presentation with lifecycle/arbitrary-code work | The chara-specific Soul Charge lane is presentation, but the tick executes a queued float delegate and then a base tick that can `ProcessEvent` `ReceiveTick` and service UE tick dependencies | Calling the full tick at confirmation can duplicate asset setup, Blueprint/native callback work, and tick scheduling; suppressing it wholesale leaves dependent presentation stale |
| P0 | Appendix animation clocks and threshold flags are not rollback-owned | Stock TickActor compares current Lux header frame with `ALuxCharaAppxActor::LastHeaderFrame`, writes the difference to `USkeletalMeshComponent::GlobalAnimRateScale`, and mutates `AnimationParams[].IsUsed` around synchronous Blueprint events | After correction, the next ordinary UE tick can catch up by multiple frames, freeze on a rewind, burst threshold events, or re-fire already presented events |
| P1 | Stateful presentation managers are outside the rollback snapshot | Recovered VFX slot tables, color-fade queues/lanes, and sound-handler voice-tracking lanes are persistent and tick/callback consumed; `RollbackSnapshot.hpp` contains no corresponding state | Rollback/resimulation and later confirmed commits observe histories that were neither rewound nor advanced at stock time |
| P1 | UE montage state is outside rollback and confirmed reconciliation | Active montage pointers/count/capacity live at `UAnimInstance +0x80`, a montage lookup map begins at `+0x90`, root-motion ownership is at `+0x338`, and each 0x190-byte montage instance carries position and marker caches | Montage position, section/marker state, notifies, delegates, and root-motion ownership follow unreconciled UE tick history rather than corrected Lux history |
| P1 | Presentation actor identity validation is too weak | Binding accepts the first two BattleManager `+0x390` actors after checking only player indices `+0x3A0` and root components `+0x168` | A lifecycle/class mismatch can reach a signature-compatible but semantically different virtual `+0x6A0` target |
| Closed | Primary matrix rollback assumed an unvalidated 97-matrix content ceiling | Ghidra proves each physical slot holds 768 matrices; Replay 100 advertises 231 and 379 matrices, so the old admission rule rejected stock content | HorseMod now snapshots all 768 matrices (`0xC000` bytes) in all three slots and the authority protocol carries the complete image |
| Closed | The rollback step harness reconstructed matrix history from the wrong slot role | Rotation proves provider/previous already represents frame `t-age-1` | The helper now uses `current_slot` for each post-step historical frame |
| P1 | Coverage is inferred from a central dispatcher that is not actually central to all effects | The manifest already excludes direct stage particles; native Blueprint/provider/component paths require separate inventories | Effects can bypass suppression, or semantic work can be over-suppressed while terminal work elsewhere remains unclassified |
| P1 | Shared-vtable patching is validated against one current object | The active class vtable is patched globally while the hook requires dispatcher identity to equal the current global pointer | A second live hub using the same vtable would fail closed during lifecycle overlap |
| P2 | Actor world-mode override lifetime is not explicitly validated | The default BattleManager world-mode facade is BeginPlay-owned and the player handler reads snapshotted native matrix-bank state, but actor `+0x558/+0x560` can override the facade | A stale or lifecycle-changed actor override could select a different interface despite otherwise correct matrix restoration |
| P2 | Parallel AnimGraph sampling ownership is unproven | The custom SCBattle node reads native Lux matrix providers during AnimGraph evaluation, but static evidence has not proved that `ULuxCharaAnimInstance` disables parallel update/evaluation | A worker could sample matrix banks while owned resimulation restores or advances them; this remains a hypothesis, not a confirmed defect |
| P2 | `presentation_exactly_once` is an accounting label | It checks masks and counter inequalities; commit callbacks return `void` and the ledger counts return from the callback | It does not prove that stock-equivalent output occurred once |

This ordering is about architectural risk, not implementation convenience. The first correction target should be the interception boundary; expanding snapshots or packet schemas around the current blanket hub hook would preserve the central timing error.

## Lux-to-Unreal fighter synchronization

### Recovered stock route

The fighter root-transform route is:

```text
ALuxBattleChara_TickActor                           0x1403D0590
  -> actor vtable +0x6A0
     BuildCharaActorWorldTransformFromBattleWorldMode 0x1403C0200
       -> actor vtable +0x690
          GetBattleWorldModeSharedPtrForCharaActor    0x1403BE670
            -> actor override +0x558/+0x560, or
            -> BattleManager +0x1450/+0x1458
       -> validate ScbattleWorldMode facade
       -> player slots 0/1: world-mode vtable +0xE0
          LuxBattle_CreatePlayerInfoHandler           0x1403C28F0
       -> BuildUnrealWorldTransformFromPlayerInfoHandler 0x1403A3840
          -> wrapper vtable +0x10
             CopyPlayerBoneMatricesFromWrapper       0x1403C1540
             -> CopyPlayerBoneMatricesFromPrimaryBank 0x1402D2E70
                -> g_pLuxBattleCharaP1[player]
                -> matrix count at PLAYER +0x42550
                -> primary CMatrixBank at PLAYER +0x35A0
                -> copy matrix index 0 (FMatrix64)
          -> ConvertBattlePoseMatrixToUERenderMatrix  0x1404555A0
          -> ConvertFMatrixToTransform                 0x1403CCFD0
       -> identity FTransform on missing/invalid world mode
  -> AActor::SetActorTransform                        0x141C2A1D0
  -> hair, appendix/weapon, material, mesh/visibility,
     Soul Charge, move-state visibility, and base-actor tick work
```

The non-battle actor branch uses world-mode slot `+0xF8` (`LuxBattle_CreateDemoHumanInfoHandler` at `0x1403C07C0`) and its alternate transform builder at `0x1403A3750`. Fighter slots 0/1 use the player-info path above.

This root-transform path does not call `ConvertLuxVectorToUnrealCentimeters` at `0x140455910`. That converter is verified for value adapters such as VFX/event positions and implements `UE(X,Y,Z) = Lux(X,Z,Y) * 100`. Fighter sync instead reads native primary-bank matrix 0 and calls `ConvertBattlePoseMatrixToUERenderMatrix`, then decomposes that UE render matrix into `FTransform`. Treating vtable `+0x6A0` as a direct scalar-position converter was therefore incorrect.

`RollbackHgCpuSnapshot` now captures and restores the 0x38-byte control at native `PLAYER +0x35A0` and the complete 0xC000-byte contents of all three primary physical slots. Matrix 0 and every wider skeleton consumer inside the recovered 768-matrix allocation therefore consume restored rollback state. Provided the accepted native PLAYER pointers, actor identity, and default world-mode lifetime remain valid, the transform builder consumes restored rollback state. The root publication call is not inherently wrong; its remaining weakness is omission of the rest of the actor sync pipeline.

### What HorseMod currently does

`NativeBinding::publishCharaPresentationTransform` reproduces the stock narrow call sequence accurately:

1. read the actor vtable and root component;
2. call virtual `+0x6A0` with an exact 0x30-byte `FTransform` output;
3. pass the returned transform to `AActor::SetActorTransform`;
4. use no sweep, no hit-result output, and no teleport.

`RollbackProductionRuntime::reconcile_confirmed_presentation` separately reads three floats from native simulation `PLAYER +0xA0` and facing from `PLAYER +0x94`, then calls the narrow actor publication helper. The four native values are copied into diagnostics. They are not compared to the matrix-bank-derived actor transform or supplied to the builder.

The success predicate checks only that the virtual and setter were called, the setter returned true, no structured exception occurred, and the returned transform contained finite floats. `presentation_transforms_finite` and `reconciliation_ok` therefore prove call/API success, not Lux/Unreal pose agreement.

`HorseMod/horselib/ActorTickGate.hpp` also has a Site 22 bare-`RET` gate for the entire `ALuxBattleChara_TickActor`. That gate shares the `WorldTickGate` freeze/replay/slow-motion admission policy; it is not the production rollback confirmed-reconciliation path. Suppressing the whole tick is appropriate while intentionally freezing visual time because it stops AnimInstance refresh, root/hair/appendix publication, Soul Charge fade/material work, pending asset callbacks, and base `ReceiveTick` from consuming wall frames. Its comments currently describe Soul Charge gauge work as generic “chara state,” which overstates deterministic ownership. The same whole-function gate must not be repurposed as the production confirmation solution: production needs selective publication, not wholesale replay of or permanent suppression of the callback/base-tick lanes.

### Static correctness assessment

| Concern | Assessment | Evidence |
|---|---|---|
| Virtual slot and function ABI | Correct for the current SC6 binary | ALuxBattleChara vtable root `0x143268078`, slot `+0x6A0` at `0x143268718`, target `0x1403C0200`, exact `FTransform48 *` return/output contract |
| `SetActorTransform` ABI and arguments | Correct | Matches the stock call at `0x1403D0823`: no sweep, null hit result, teleport type 0 |
| Source semantics assigned to virtual `+0x6A0` | Incorrect in HorseMod names/comments | The target selects a player-info handler, reads native primary-bank matrix 0, converts the battle pose matrix to UE render space, and never reads native `PLAYER +0x94/+0xA0` |
| Root transform publication | Implemented, but narrow | Only the root actor transform is published |
| Complete Lux-to-Unreal actor synchronization | Not implemented | Stock work after the setter is omitted from confirmed reconciliation |
| Semantic validation of native versus Unreal pose | Not implemented | Native position/facing and actor transform are stored side by side without a comparison or verified common-space conversion |
| Root-transform frame data rollback ownership | Implemented for the complete recovered bank | `RollbackHgCpuSnapshot` captures/restores primary CMatrixBank control plus all 0xC000 bytes of all three slots at native `PLAYER +0x35A0`; player-info wrapper virtual `+0x10` reads matrix 0 and wider consumers remain inside the 768-matrix extent |
| Actor identity safety | Incomplete | Binding checks player index and root-component presence, but not class identity or the expected vtable target |
| Mixed actor-tick admission | Unsafe/unfinished | The tick is primarily presentation, but it also drains arbitrary asset/lifecycle delegates and enters base `ReceiveTick`/UE tick scheduling; production confirmation publishes only the root transform |

### Why the full stock tick is not a safe confirmation fix

Calling `ALuxBattleChara_TickActor` after confirmed correction would restore more presentation work, but the full entry also executes work whose replay/idempotence contract is broader than presentation publication:

- actor `+0x548` is a `TArray` of 0x50-byte float delegates, not a montage queue;
- the first delegate is invoked with `flDeltaSeconds` and removed only when its boolean result is true;
- recovered producers enqueue asynchronous weapon/asset setup callbacks that configure trace, weapon meshes, materials, and optional external callback code;
- `ALuxCharaActor_TickActor` can find and `ProcessEvent` `ReceiveTick`, dispatch/schedule tick tasks, walk tick dependencies, and enter world/lifecycle handling;
- actor `+0x53C/+0x537/+0x540` drives Soul Charge fade/material/provider presentation, not proven gameplay state;
- the Soul Charge refresh reaches native PLAYER only to apply/clear four secondary-sway presentation records.

The durable design therefore needs a split below this mixed entry: reconcile independently proven presentation consumers, while separately admitting or deduplicating the queued delegate and base-tick/`ReceiveTick` lanes. The reason not to call the full tick is duplicate arbitrary/lifecycle execution—not a deterministic move-table transition.

### Type correction made during this pass

The previous Ghidra prototype used `ALuxBattleChara_Partial *` for both native simulation and Unreal actor code. That structure is a large native simulation overlay and caused actor `+0x98` to decompile as a native pose float. A new exact-through-`+0x567` `ALuxBattleCharaSyncActor_Partial` overlay now separates the Unreal actor fields used by this route:

- `+0x00`: vtable;
- `+0x98`: `ALuxBattleManager_Partial *`;
- `+0x168`: `USceneComponent_Partial *` root component;
- `+0x3A0`: player index;
- `+0x530/+0x531`: animation refresh flags;
- `+0x537/+0x53C/+0x540`: Soul Charge presentation selector/fade-in counter/floor;
- `+0x558/+0x560`: world-mode override and shared-reference guard.

The recovered transform builder and world-mode getter now use this actor-specific type. This is important because native `PLAYER +0x94/+0xA0` and Unreal actor `+0x98/+0x168/+0x3A0` belong to different objects despite overlapping numeric offsets.

### `ALuxBattleChara_TickActor` ownership audit

The stock tick at `0x1403D0590` is a 941-instruction, 126-basic-block dispatcher. Its ownership is not uniform:

| Lane | Evidence | Ownership | Rollback implication |
|---|---|---|---|
| Pending callback at actor `+0x548` | `ExecuteDelegateWithFloatParam` (`0x1403B1750`) receives the first 0x50-byte entry plus `flDeltaSeconds`; callback result in `AL` controls removal | Asset/lifecycle setup plus arbitrary delegate code | Do not replay via full TickActor without a callback identity/deduplication contract |
| AnimInstance binding refresh | actor `+0x530/+0x531` call `RefreshScbattleAnimNodeBindings` for character/weapon meshes | Presentation binding | Candidate selective reconciliation lane; only run on proven dirty transitions |
| Root transform | actor virtual `+0x6A0` reads PLAYER `+0x35A0`, followed by `SetActorTransform` at `0x1403D0823` | Confirmed presentation publication from restored native state | HorseMod implements this lane narrowly and correctly at the ABI level |
| Hair and appendix actors | Maegami state machine plus per-appendix bone transform and actor publication | Presentation | Missing from current confirmed reconciliation |
| PlayerInfoHandler reads | wrapper slots `+0x60/+0x68/+0x78/+0x88/+0x90/+0xA0` read gauge, lane, byte, and word state from native PLAYER | Native authoritative reads | Inputs must come from the confirmed/restored native state; no duplicate native mutation here |
| Soul Charge actor state | `+0x53C` fade-in ticks, `+0x537` cached presentation selector, `+0x540` presentation floor | UE presentation cache | Do not add these fields to deterministic rollback state as gameplay |
| Soul Charge provider/entity rebuild | `RefreshBattleCharaSoulChargePresentationState` (`0x1403C90B0`) rebuilds `+0x470` visibility records and `+0x4C0` `FLuxSoulChargeProviderSet_Partial`, then updates meshes/materials/hair/trace | Presentation | Candidate selective confirmed-state rebuild, subject to lifecycle validity |
| Native sway sidecar | PlayerInfoHandler slot `+0x268` -> `ApplySwayBonePresentationModeViaPlayerInfoWrapper` -> `ApplyOrClearBattleCharaSwayBoneParameters` | Native secondary-bone presentation | Not serialized by HgCpuDirect and outside HorseMod's supplemental skeleton block; reconstruct deliberately |
| Visibility | `ALuxBattleChara_SyncMoveStateVisibility` updates character, weapon, attached components, appendix actors, and trace activity | Presentation | Missing from root-only reconciliation |
| Base actor tick | `ALuxCharaActor_TickActor` can dispatch `ReceiveTick` through `ProcessEvent`, schedule tick tasks, walk dependencies, and call world/lifecycle handling | Lifecycle/arbitrary Blueprint/native code | Explicitly exclude from a presentation-only reconciler until every concrete receiver is classified |

The recovered callback producer `InitializeBattleCharaProviderAndQueueSetupCallbacks` (`0x1403CF790`) enqueues three ordered setup entries: subsystem readiness, weapon setup, then weapon equip/completion. `SetupBattleCharaWeaponCallbackThunk` (`0x1403B5940`) reaches `SetupChara006WeaponAndInnerChestMaterial` (`0x1403A34F0`) for trace-manager, weapon, and material setup. `InvokeBattleCharaWeaponEquipCallbackThunk` (`0x1403B5E40`) reaches `ALuxBattleChara_WeaponEquipCallback` (`0x1403A36C0`) for equip virtual `+0x6A8`, an optional external callback, Soul Charge presentation refresh, and setup-dirty state. These concrete producers support a lifecycle/presentation classification, but the optional external delegate preserves the arbitrary-code hazard.

#### Demo-human completion-path correction

The caller and reflected-registration audit retracts the former `LuxMoveVM_Tick_ProcessInputAndDispatch` interpretation at `0x1404483F0`. The function is `ProcessDemoHumanCreationCompletion`, and its sole executable caller is the five-byte `InvokeDemoHumanCompletionDelegateThunk` at `0x140450470`. Static data also references the callback, but no actor tick chain calls it.

`SpawnOrReuseDemoHumanForPlayerSetup` (`0x140455D00`) is the native body of the reflected `CreateDemoHuman` method. `GetLuxDemoHumanManagerClassMetadata` (`0x140A21D60`) registers the exact reflected class name `LuxDemoHumanManager` with size `0x530`. The creation body:

1. spawns or reuses an `ALuxBattleChara`;
2. creates a world-mode-sized zeroed buffer with shared ownership;
3. allocates the manager-scoped player index and publishes the actor mapping;
4. captures `{ weak manager, player index, ULuxBattlePlayerSetup* }` in an exact 0x20-byte delegate target;
5. queues the three provider/setup callbacks; and
6. enters `ProcessDemoHumanCreationCompletion` only after that ordered chain completes.

The completion worker resolves the manager weak pointer at execution time, consumes the current guarded world-mode reference, builds setup response parameters, initializes the player slot through world-mode virtual `+0xB0`, and creates the demo-human info handler through virtual `+0xF8`. On success it emits actor event `0x1276`, processes the manager completion-task pool, looks up `player index -> FLuxFName tag` through the manager's UE-style sparse-set/hash map at `+0x460`, and dispatches/prunes the live setup-listener array at `+0x390`. On failure it destroys/unregisters the player-index lifecycle state.

The adjacent reflected family is now type-closed as `CreateDemoHumanFromProfile` (`0x140456050`), `CreateDemoHumanFromProfileTagged` (`0x140456080`), and `CreateDemoHumanTagged` (`0x1404560C0`). Tagged creation first removes any prior owner for the same eight-byte FName, then publishes exact reciprocal maps: `tag -> player index` at manager `+0x410` and `player index -> tag` at `+0x460`. `RemoveExistingDemoHumanForTag` (`0x140456390`) destroys the resolved player and erases the reciprocal map before the tag map. `DestroyDemoHumanForPlayerIndex` (`0x140456290`) crosses world-mode slot `+0xB8`, actor destruction, the manager's player/actor map, and its completion-task pool.

`InitializeDemoHumanActorAfterSetupReady` (`0x140455B90`) was also mislabeled as a generic move-provider launch gate. It is called from `ALuxDemoHumanActor_TickActor`, waits for setup loaders, reuses the existing derived actor in the shared creation body, latches derived-actor `+0x615`, and establishes reciprocal actor/manager ownership. Both independent class-registration paths prove that `ALuxDemoHumanActor` is exactly `0x620` bytes, extending eight bytes beyond the `0x618` base presentation overlay.

The corrected derived tick at `0x1404865B0` performs all of its demo-human work before tail-calling `ALuxBattleChara_TickActor`: readiness/manager registration, three-bit presentation-mode and Soul Charge publication, dirty track-vector/scalar publication, authored playback-state normalization, frame publication, target-mode reconciliation, two weak-target virtual-`+0xE0` event lanes, and visibility synchronization. The exact seven-entry switch table covers authored state codes `0x1C..0x22`; the exact class tail caches target mode at actor `+0x61A`. This is another tick-time lifecycle/animation producer above the base-character boundary.

This is therefore a deferred actor/setup lifecycle transaction crossing current Lux world-mode state and current Unreal object topology. It is neither a per-frame MoveVM input dispatcher nor a terminal effect/VFX callback. Replaying it from rollback would re-resolve weak objects, maps, listeners, shared ownership, and world-mode slots at replay time.

#### Player-info scratch storage correction

The stack pair previously labeled as a weapon AnimInstance is heterogeneous shared-pointer scratch storage. Actor vtable `+0x698` first returns `ScbattleWorldMode`; world-mode virtual `+0xE0` then overwrites the same pair with `ScbattlePlayerInfoHandlerWrapper`. Consequently, the later virtual calls are player-info operations, not weapon-animation provider calls. The concrete wrapper vtable at `0x143268728` resolves the observed slots as follows:

- `+0x60`: current native Soul Charge time gauge (`PLAYER +0x1B4C`);
- `+0x68`: Soul Charge gauge limit, default 600;
- `+0x78`: copy native move-lane state;
- `+0x88`: read native state byte;
- `+0x90`: read native state word at `+0x197C`;
- `+0xA0`: read the native special-state word;
- `+0x268`: apply/clear player secondary-sway presentation parameters.

This correction removes the former basis for treating the actor counter/selector as an independent gameplay lane.

#### Native sway sidecar and snapshot coverage

`RefreshBattleCharaSoulChargePresentationState` is not literally UE-only. For player slots it calls wrapper `+0x268` with the provider-selected active flag. The concrete route is:

```text
RefreshBattleCharaSoulChargePresentationState       0x1403C90B0
  -> PlayerInfoHandler wrapper +0x268               0x1403CD570
     ApplySwayBonePresentationModeViaPlayerInfoWrapper
       -> ApplyPlayerIndexedSwayBonePresentationMode 0x1402D4D00
          -> g_pLuxBattleCharaP1[nPlayerIndex]
          -> ApplyOrClearBattleCharaSwayBoneParameters 0x1403095B0
```

The terminal function walks 0x70-byte native descriptors between ushort indices at PLAYER `+0x40440/+0x40442`, matches four `OPPAI_*` swing names, copies or clears quantized values in descriptor fields rooted at `+0x2B550`, sets the dirty word at `+0x2B4BC`, and resynchronizes the secondary-sway solver rooted at `+0x29120/+0x2B520`. This is native presentation state, not MoveVM/hit/attack/RNG state.

Coverage is nevertheless incomplete:

- the native HgCpuDirect fighter writer serializes `+0x2B3E0` for 0x50 bytes and then jumps to `+0x43D80`; it does not serialize `+0x2B4BC`, the source records at `+0x2B4C4`, or the descriptors at `+0x2B550`;
- HorseMod's supplemental skeleton snapshot copies `PLAYER +0x29120` for 0x22C0 bytes, ending at `+0x2B3DF`;
- therefore the sidecar is neither in the native snapshot stream nor the supplemental skeleton history.

This is a presentation-reconstruction gap, not evidence that the entire range should be folded into deterministic state. A future selective Soul Charge reconciler can reapply/clear the four records from confirmed provider state, while leaving the arbitrary callback/base-tick lanes untouched.

#### Subsequent-round move-entry and sway-source producer closure

The subsequent-round producer at `0x1402DA710` is now type-closed as `LuxBattle_ReinitCharaSlotForMove_SubsequentRound`. Its direct table wrapper at `0x1403CFAC0` forwards an exact `FLuxBattleCharaInitMoveEntry_SubsequentRound_Partial *`; the wrapper does not transform or reinterpret the entry. The entry is exactly `0x158` bytes at this boundary.

The disassembly corrects the prior load-packet size. Six consecutive 16-byte moves populate stack packet `+0x20..+0x7F`, so `FLuxMoveDataLoadPacket_Partial` is exactly `0x80`, not `0x78`. Its first `0x20` bytes hold the NMD pointer/count/replace policy and Japanese VTB pointer. The remaining exact `0x60` bytes are `FLuxMoveDataLoadPayload_Partial`, copied from init-entry `+0x48..+0xA7`. A separate conservative init-entry overlay was used so the pre-existing shared type was not destructively rewritten.

The two records at init-entry `+0xAC` and `+0xC8` are inline `FLuxSwayBoneParameterSource_Partial` values, not pointers and not hit-scan ranges. The initializer copies them verbatim into native PLAYER `+0x2B4C4` and `+0x2B4E0` before AI/MoveVM setup. This is the load-time producer for the two native secondary-sway source slots later consumed by `ApplyOrClearBattleCharaSwayBoneParameters`.

The same function also proves the subsequent-round ordering boundary:

1. reset the VFX-anchor/hitbox block and republish character/move-table identity;
2. map the move-table index through the exact 41-row internal-character table and publish the scaled authored duration;
3. load the `0x80` moveset/bone/VTB packet;
4. publish both inline sway-source records;
5. initialize AI and MoveVM state;
6. fan the `+0x20` motion/hit-offset bank into its runtime consumers and mirror the `+0x28` move bank into the event-tree owner;
7. deserialize body, hurtbox, and attack KHit streams into three independently recovered output lanes;
8. load three VTB event stacks and bind three LPD motion banks.

`ALuxBattleChara_VerifiedPartial+0x4446C` is now an exact `0x68` `FLuxKHitListOutputControlBlock_Partial`. The deserializer ABI independently proves list head, maximum authored slot, scratch-byte count, scratch-tail pointer, and node count for each body/attack/hurtbox lane. Unsupported padding remains explicitly unknown. The body lane uses one of two exact `0x4000` scratch regions selected by player index.

This producer is not a terminal presentation callback. It reconstructs native character content, MoveVM/event-tree inputs, collision lists, animation-event files, motion banks, and the native sway sidecar in a specific order. A rollback path must restore or reproduce those products at the correct native load boundary; replaying only the later UE Soul Charge presentation request cannot reconstruct the authored sway sources or the other move-entry-owned state.

### Matrix-bank producer timing and ownership

The native player owns two independent three-slot matrix banks:

| Bank | Native offset | Matrices per physical slot | Bytes per physical slot | Horse capture / native HgCpu prefix |
|---|---:|---:|---:|---:|
| Primary bone pose | `PLAYER +0x35A0` | `0x300` (768) | `0xC000` | Horse: `0xC000` complete; native HgCpu: `0x1840` (97 matrices) |
| Secondary/small bone pose | `PLAYER +0x27760` | `0x20` (32) | `0x800` | `0x800` (complete slot) |

`LuxBattle_InitBoneTransformBuffers` (`0x14030B7D0`) and `LuxBattle_InitSmallBoneMatrixBuffers` (`0x14030B6E0`) allocate and identity-initialize all three physical slots. The recovered `CMatrixBankHeader_Partial` is exactly 0x38 bytes: vtable, three slot pointers, active-slot index/reserved word, current pointer, and previous/provider pointer.

Both bank vtables use `AdvanceCMatrixBankRingBuffer` (`0x14030B630`) at slot `+0x40`. Rotation computes `(active + 2) % 3`, giving physical order `0 -> 2 -> 1 -> 0`; assigns old current to previous/provider; and assigns the selected physical slot to current. Rotation copies, clears, and initializes no matrix bytes. The selected destination consequently retains its older contents until the producer overwrites them.

The production coordinate is explicit in `LuxBattleChara_FinalizeTickPoseAndState` (`0x140305B50`):

```text
rotate primary bank                             0x140305B82
rotate secondary bank                           0x140305B8F
tick MoveVM playback lanes / apply root motion
publish pose-anchor inputs
remember previous primary bone 0 at +0x96F60
obtain new primary current slot
LuxBattleChara_SolveBonePose into current slot
increment pose-finalize counter
return through the remaining owned simulation iteration
secondary/decorator skeleton consumers read current slots
HorseMod CaptureRollbackStepState
```

`LuxBattleChara_PerTickAdvanceAll` and `LuxBattle_TickCharaSecondaryAndDecorators` do not perform another bank rotation. HorseMod's production `run_owned_native_simulation_iteration` returns only after this finalization/consumer sequence, and `CaptureRollbackStepState` follows that return. Production capture is therefore post-producer for the logical frame, not one frame early. `CaptureRollbackHgCpuSnapshot` also takes its emergency all-slot prefix copy before the observably mutating native HgCpu writer and restores that emergency state afterward. During restore, the native HgCpu reader writes the current-slot prefixes first; HorseMod then restores the 0x38-byte controller and every captured slot prefix. That ordering is consistent with native ownership.

### Authored primary matrix count and complete-bank correction

`LoadBattleCharaMovesetEntriesBonesAndJapaneseVtb` (`0x140312040`) is the only recovered writer of `PLAYER +0x42550`, now typed as `dwPrimaryMatrixCount`. It resets the count during content load, scans 0x70-byte authored bone records, derives `matrixIndex = (signed short)wSlotIndex + 2`, and stores `max(currentCount, matrixIndex + 1)`. Equivalently, the final count is the maximum authored signed slot index plus three. No native clamp to 97 was found.

The count is load-time/round-lifetime content state rather than per-frame simulation state, so rewriting it during rollback is not indicated. Native HgCpu serializes only 0x1840 bytes of the primary current slot and 0x800 bytes of the secondary current slot. HorseMod deliberately goes wider: it copies the complete 0xC000-byte primary allocation for all three physical slots. General matrix-copy interfaces can therefore consume any advertised index within the recovered 768-matrix allocation after rollback.

Runtime evidence disproved the proposed 97-matrix stock-content convention: Replay 100 produced counts of 231 for Siegfried and 379 for Tira. The former `1..97` admission rule was therefore a HorseMod defect, not a safe stock invariant. Complete physical-slot capture is the durable correction; counts above 768 remain invalid for the recovered allocation.

### Non-production harness history reconstruction defect

`RollbackRestoreMotionBankHistoryFromTimeline` reconstructs the two non-current physical slots after the step harness finishes a sequence. Timeline entries are captured after each native `step`, so entry `t-age` records that frame's `current_slot` as frame `t-age` and its `provider_slot` as frame `t-age-1`.

For final current slot `s` at frame `t`, the helper correctly targets physical slot `(s + age) % 3`. It incorrectly sources bytes from `timeline[t-age].provider_slot`. For `age == 1`, that writes frame `t-2` into the slot intended to contain frame `t-1`; for `age == 2`, it writes frame `t-3` into the slot intended to contain frame `t-2`. The source role should be the historical entry's `current_slot` if the helper is to recreate native post-frame history.

This helper is referenced by `RollbackStepHarness.hpp`, not by the production restore in `RollbackProductionRuntime`. It can nevertheless contaminate the harness's baseline, predicted, and corrected end-state captures, so passing harness comparisons are weaker evidence than their names suggest. No implementation was changed during this static investigation.

### Animation and montage state across the UE boundary

The phrase "animation state" currently hides three mechanisms with different owners and rollback requirements:

| Lane | Stock owner and transport | Current HorseMod coverage |
|---|---|---|
| Main character and weapon pose | `ULuxCharaAnimInstance` custom `FLuxCharaAnimNode_SCBattle` samples native Lux matrix providers | Native matrix prefixes are restored; node binding metadata exists in UE and is not a clock |
| Appendix animation | `ALuxCharaAppxActor` converts Lux header-frame deltas into `USkeletalMeshComponent::GlobalAnimRateScale` and emits threshold changes to its AnimBlueprint | Not captured or selectively reconciled |
| UE montage playback | `UAnimInstance` owns active `FAnimMontageInstance` objects, lookup state, marker caches, delegates/notifies, and root-motion ownership | Not captured or selectively reconciled |

#### HorseMod's animation snapshot is native Lux state

`RollbackCharaAnimationState.hpp` captures the native character's auxiliary clip player at `+0x95ED0`, clip runtime at `+0x2B270`, pose-event cue owner at `+0x95720`, and its heap-backed Enshutsu scheduler/trigger graph. Its fail-closed pointer-identity checks are appropriate for that native graph. The type name is nevertheless broader than its coverage: the snapshot contains no `USkeletalMeshComponent`, `UAnimInstance`, `FAnimMontageInstance`, AnimInstance proxy, montage map, montage position, marker, notify, delegate, or appendix actor field.

`ReplayAnimationPresentation.hpp` does not close this production gap. It is a replay-specific actor/material/override snapshot containing the actor output at `+0x390`, dynamic material state at `+0x908`, animation overrides beginning at `+0x95C`, and one-shot refresh flags at actor `+0x530/+0x531` and demo actor `+0x616`. It does not capture appendix actors, skeletal-mesh clocks, or montage instances, and production rollback does not use it as a general animation snapshot.

#### Main fighter and weapon: matrix-sampling AnimGraph bridge

`RefreshScbattleAnimNodeBindings` (`0x140480C50`) refreshes every custom SCBattle node exposed by the AnimInstance proxy and the embedded node at `ULuxCharaAnimInstance +0x378`. The recovered exact layouts are:

```text
FLuxCharaAnimNode_SCBattle_Partial, 0x68 bytes
  +0x30 int       PlayerNo
  +0x34 byte      MatrixType
  +0x38 int *     BoneMap
  +0x40/+0x44     BoneMap count/capacity
  +0x48/+0x4C     cached front-hair bone indices
  +0x50/+0x58     shared ScbattleWorldMode provider pair
  +0x60           ALuxBattleChara *

ULuxCharaAnimInstance_Partial, 0x4A0 bytes
  +0x20           owning USkeletalMeshComponent *
  +0x350          FAnimInstanceProxy *
  +0x378          embedded SCBattle node
  +0x480          battle actor
  +0x488/+0x490   shared ScbattleWorldMode provider pair
```

`BindScbattleAnimNodeToPlayerAndSkeleton` (`0x14047FBF0`) selects the player or alternate skeleton interface, translates Lux bone names to UE reference-skeleton indices, and caches the two front-hair indices. The refresh copies provider ownership, binds player/matrix type and bone translation, and assigns the battle actor. It does not advance or restore a UE animation clock, montage, notify, or section. Main-body pose continuity therefore depends primarily on restored native matrix-bank content; treating this binding refresh as an animation-state restore would be incorrect.

#### Appendix actors: a separate UE clock and event bridge

The recovered reflected class size of `ALuxCharaAppxActor` is exactly `0x408`. Its animation-relevant tail is:

```text
+0x388  USkeletalMeshComponent *AppxMeshComponent
+0x390  int TargetBoneID
+0x394  int TargetModeID
+0x398  FRotator TargetRotation
+0x3A4  int TargetModeValue
+0x3A8  float LastHeaderFrame
+0x3AC  int TargetHeaderPhase
+0x3B0  TMap<int, FAppxMeshAnimationParam> AnimationParams
```

For each appendix actor, `ALuxBattleChara_TickActor` obtains the current Lux header/move-lane frame, computes `currentLuxFrame - LastHeaderFrame`, updates `LastHeaderFrame`, and writes the delta to `AppxMeshComponent +0xB60`. Reflection and `TickSkeletalMeshAnimation` (`0x141DAEFF0`) prove that field is `USkeletalMeshComponent::GlobalAnimRateScale`: the engine passes `DeltaSeconds * GlobalAnimRateScale` to main, linked, and post-process AnimInstances. A Lux delta of zero freezes the appendix graph, one advances normally, and a value greater than one advances it by a catch-up-sized UE delta.

A negative delta, or the relevant mode-change corridor, is handled as a rewind signal. Stock code forces the animation-rate delta to zero and re-arms authored threshold records; it does not rewind the current UE animation or montage position. The exact threshold record is 0x0C bytes and contains a frame at `+0x00`, five one-byte parameters at `+0x04..+0x08`, and `bool IsUsed` at `+0x09`. Crossing an unused threshold sets `IsUsed` and synchronously invokes `ULuxCharaAppxAnimInstance::OnChangeAnimationState` through `ProcessEvent`. Rewind re-arms applicable `IsUsed` flags. `OnTargetModeValueChange` is a distinct actor event.

The appendix AnimInstance reflects a `SetAnimationPosition(float)` Blueprint event, but the recovered native TickActor corridor does not call it. Static evidence therefore proves a rate/delta bridge and threshold-event bridge, not a native absolute-position reconciliation path.

#### Montage state is independently UE-owned

`PlayAnimInstanceMontage` (`0x141CA1B00`) validates the montage/skeleton, handles conflicts, allocates an exact 0x190-byte `FAnimMontageInstance`, initializes its position at `+0x100`, resets marker caches at `+0x98/+0xA0` to `-2`, appends the pointer to the `UAnimInstance +0x80` active-instance array, inserts it into the lookup storage beginning at `+0x90`, updates root-motion ownership at `+0x338`, and broadcasts montage start.

`SetAnimInstanceMontagePosition` (`0x141CA2210`) either visits all active instances or resolves one montage through the lookup map, writes the new position at instance `+0x100`, and invalidates both marker caches. This establishes that montage time is not derived automatically from the native Lux clip/scheduler snapshot. Direct native callers are normal engine/proxy/dynamic-montage paths; no direct call was found from `ALuxBattleChara_TickActor` or `RefreshScbattleAnimNodeBindings`. That absence does not rule out appendix AnimBlueprint montage calls, because Blueprint reaches these functions through reflected exec/`ProcessEvent` paths.

#### Static rollback assessment

Production owned resimulation advances/restores native Lux state without advancing the ordinary UE actor/AnimInstance path. After correction, the next stock appendix tick compares a corrected Lux frame with stale `LastHeaderFrame`. It can therefore produce a multi-frame catch-up delta or a negative rewind, re-arm threshold flags, and emit Blueprint events against the current UObject state. Any already-active montage remains at the position produced by its previous UE tick history. `RollbackProductionRuntime::reconcile_confirmed_presentation` only republishes the root transform, so none of these fields is repaired at confirmation.

The durable design requires an explicit policy for each UE-owned lane: either capture and restore sufficient scalar/graph state, or deterministically reset/recreate it from confirmed Lux state without replaying arbitrary Blueprint/lifecycle work. Calling full `ALuxBattleChara_TickActor` is not a safe substitute because that function also runs queued delegates and a base tick capable of arbitrary `ReceiveTick` dispatch.

One concurrency question remains open. The custom node's matrix sampling occurs during AnimGraph evaluation, which Unreal can execute in parallel, but the current static evidence does not prove whether this specific AnimInstance opts out. Until that is recovered, concurrent worker sampling during native matrix restore/advance is a P2 hypothesis rather than a confirmed race.

## Corrected object model

### Global dispatcher

Ghidra global:

```text
Address: 0x14470D188
Old name: g_pLuxVfxDispatcher
Corrected name: g_pLuxBattleEventDispatcher
Type: FLuxVfxDispatcher_Partial *
Static xrefs: 168
```

The xrefs span MoveVM effect dispatch, hit/contact processing, audio cue publication, animation notifications, training, replay/new-round logic, handler registration, and collision bookkeeping.

### Live listener hub

`ULuxBattleEventListenerHub_Constructor` at `0x1409128E0` establishes:

- listener-hub object size: approximately `0x12A0`;
- embedded dispatcher subobject: hub `+0x28`;
- active dispatcher vtable: `0x14337C4F8`;
- callback collection array: 41 entries;
- callback collection stride: `0x70` bytes;
- first collection relative to dispatcher: `+0x08`.

For broadcast slots `+0x08..+0x130`, the corresponding collection index is:

```text
collection_index = (vtable_offset - 0x08) / 0x08
collection_address_relative_to_dispatcher = 0x08 + collection_index * 0x70
```

The static fallback vtable at `0x143E853C8` binds most broadcast routes to `HandleLuxNoOpVirtual` at `0x1402D2BC0`. The fallback being a no-op does not make the live listener-hub implementation presentation-only.

### Adapter behavior

Every active-hub route from `+0x08` through `+0x130` has a compatible two-argument void ABI. The adapters generally:

1. read a native value request;
2. convert or reorder it into a callback packet;
3. select one `FCallbackEntryCollection_Partial`;
4. call `ProcessAndCompactCallbackEntries` synchronously.

Slots `+0x138` and `+0x140` are different provider/query operations and are correctly excluded by HorseMod.

### Callback-collection transaction semantics

`ProcessAndCompactCallbackEntries` at `0x141D38300` proves that a hub virtual is a transaction over the current listener graph:

1. increment collection recursion depth at `+0x64`;
2. iterate `nCount - 1` down to zero, so subscribers run in reverse registration order;
3. select the inline or heap-backed 0x40-byte callback entry;
4. invoke callback-object vtable slot `+0x68` with the adapter's converted packet;
5. mark the collection dirty when an entry is empty/disabled or the callback returns false;
6. decrement recursion depth and immediately compact dirty entries only after the outer broadcast has unwound.

The exact `FCallbackEntryCollection_Partial` is 0x70 bytes. Verified fields are inline entry storage at `+0x00`, heap entries at `+0x40`, count/capacity at `+0x50/+0x54`, compaction threshold at `+0x60`, and recursion depth at `+0x64`. `ProcessWeakCallbackEntryCompaction` at `0x140399DF0` refuses mutation during recursion, applies the threshold policy used by registration, removes compactable callback objects, resets the threshold to at least two or twice the live count, and may shrink external storage.

Two helpers previously mislabeled in Ghidra as asynchronous task submission are synchronous weak-listener registration:

- `AddWeakUObjectEventCallback` at `0x1403A3930` builds a 0x30-byte weak callback object and appends it as one 0x40-byte collection entry.
- `AddWeakUObjectCallbackWithBoundByte` at `0x1403A3B70` does the same while binding a byte that becomes the callback's third argument. The bound value is semantic input, not a task flag.

Their final callback vtables resolve the `FWeakObjectPtr_Partial` target before calling `callback(target, event)` or `callback(target, event, boundByte)`. A dead target returns false to the broadcaster, which requests collection compaction. Registration and broadcast therefore change collection topology as well as delivering an event.

This invalidates any model in which confirmation merely invokes the same target later. HorseMod records a route id and normalized request bytes, then calls the original adapter through the current dispatcher at commit. It does not record callback identities, callback order, weak-target validity, bound registration arguments, or the compaction result from the source frame.

### Registration inventory result for unresolved routes

The complete direct-xref inventory for `GetLuxBattleEventListenerHubFromWorld` was scanned for collection 32 (`hub +0xE30`, dispatcher slot `+0x108`) and collection 36 (`hub +0xFF0`, dispatcher slot `+0x128`). No direct native registration for either collection appears in those callers. This narrows the uncertainty but does not prove the collections are always empty: an indirect hub pointer, another registration path, or non-native/reflected code could still populate them. `ULuxBattleEventListenerHub` exposes no reflected registration functions in the recovered class metadata.

## Verified subscriber registrations

The registrations below are from native `BeginPlay` implementations. They demonstrate that the dispatcher is a semantic event bus. This map is not yet exhaustive because other handler classes can register additional subscribers.

### `ALuxBattleVFxEventHandler_BeginPlay` — `0x1403B5530`

| Collection | Vtable slot | Registered callback | Observed role |
|---:|---:|---|---|
| 0 | `+0x08` | `LuxMove_AllocateMeshActorSlots_WithRemap` | Resolves the event against current move/provider state, builds authored descriptors, allocates persistent live slots, registers ownership, and publishes `ReceiveEnableVFx` |
| 1 | `+0x10` | `HandleDisableVfxEvent` | Resolves disable transitions, mutates both live VFX slot tables, and publishes `ReceiveDisableVFx` |
| 10 | `+0x58` | `LuxMove_SetSituationName_AndAllocMeshSlot` | Updates situation name and allocates a slot |
| 11 | `+0x60` | `HandleCollection11VfxStateClear` | Clears current VFX-handler state; the five-byte event value is ignored |
| 14 | `+0x78` | `LuxMove_AllocateMeshSlot_ForHipBone` | Allocates a bone-attached mesh slot |
| 15 | `+0x80` | `LuxMove_AllocateMeshSlots_WithSideTracksAndGrouping` | Allocates/group tracks by side |
| 16 | `+0x88` | `LuxMove_AllocateMeshSlots_WithSideTracksAndGrouping` | Allocates/group tracks by side |
| 17 | `+0x90` | `LuxMove_AllocateMeshSlots_FromMoveTypeMask` | Resolves move mask and allocates slots |
| 30 | `+0xF8` | `BattleMgr_SendCmd_Type5_ToSubsystem` | Sends a subsystem command |
| 34 | `+0x118` | `LuxMove_AllocateSingleMeshActorSlot` | Allocates one mesh actor slot |

The same handler also registers manager and auxiliary-provider delegates, refreshes provider/mesh caches, and clears lifetime-owned containers. Its callbacks are above terminal rendering and mutate handler-owned state.

### `ALuxBattleSoundEventHandler_BeginPlay` — `0x1403B4610`

| Collection | Vtable slot | Registered callback | Observed role |
|---:|---:|---|---|
| 4 | `+0x28` | `HandleContactSoundEventForBattleSound` | Resolves/starts contact sound and updates active-voice tracking |
| 5 | `+0x30` | `HandleStopSoundEventForBattleSound` | Resolves a tracked voice by contact key and queues StopSE |
| 6 | `+0x38` | `ALuxBattleSoundEventHandler_HandleCharaCueEvent` | Remaps and dispatches character cues |
| 7 | `+0x40` | `LuxMove_ResetTrackDataPair_ForPlayerSide` | Resets player-side track state |
| 10 | `+0x58` | `LuxMove_OnBattlePhaseChanged` | Processes phase change |
| 11 | `+0x60` | `LuxMove_InitMoveFlags_AndSetTimer` | Mutates flags and timer state |
| 12 | `+0x68` | `LuxMove_ClearFlag3E5_AndResetAnim` | Clears state and resets animation |
| 14 | `+0x78` | `LuxMove_SendAnimCmd_Type2_ByParams` | Sends animation command |
| 16 | `+0x88` | `LuxMove_UpdateTrajectory_ByPlayerIndex` | Updates trajectory state |
| 23 | `+0xC0` | `LuxMove_TriggerCleanup_IfTrackFlagSet` | Conditional cleanup |
| 24 | `+0xC8` | `LuxMove_DispatchCmdsFromByteArray_Type3` | Dispatches command array |
| 34 | `+0x118` | `LuxMove_SendSubsystemCmd_Type50` | Sends subsystem command |

This is direct evidence that several HorseMod “VFX” lanes contain sound-handler semantic state transitions.

### `InitializeLuxBattleColorFadeManagerBeginPlay` — `0x14044F2C0`

The class metadata fixes `ALuxBattleColorFadeManager` at 0x3B0 bytes over the 0x388-byte `ALuxActor` base. Its native BeginPlay registers the following listener-hub subscribers:

| Collection | Vtable slot | Registered callback | Observed role |
|---:|---:|---|---|
| 0 | `+0x08` | `ApplyEnableVfxToBattleColorFadeState` | Resolves authored settings, appends active lane state, and applies first-entry material parameters |
| 1 | `+0x10` | `ApplyDisableVfxToBattleColorFadeState` | Resets, removes, or transitions active color-fade state from the shared DisableVFx request |
| 2 | `+0x18` | `HandleBattleColorFadeQueueEvent` | Inserts and priority-sorts persistent 0x34-byte fade layers |
| 3 | `+0x20` | `ApplyBattleColorFadeLayerTimingUpdate` | Rewrites runtime timings in matching queued layers |
| 10 | `+0x58` | `HandleBattleColorFadePhaseResetEvent` | Resets all player fade state when the phase byte is 1 |
| 11 | `+0x60` | `HandleBattleColorFadeStateResetEvent` | Unconditionally resets all player fade state |

BeginPlay also resolves the current `ColorFadeSettingList`, initializes native per-player/per-lane state, and resolves the default `FadePatternTex`. Collections 2 and 3 are therefore inputs to a persistent, tick-consumed manager, while collections 10 and 11 are manager teardown/reset events.

## High-confidence defects and risks

### 1. Semantic event handling is delayed as if it were rendering

**Confidence:** Confirmed

`vfx_dispatch_slot` suppresses the entire hub broadcast. Registered subscriber callbacks therefore do not execute during the frame that generated the event.

Effects include:

- mesh/provider cache mutation;
- track and timer changes;
- animation and subsystem commands;
- Blueprint/provider calls;
- callback collection compaction;
- terminal VFX or audio emission.

The statement that deterministic native schedulers continue while only external presentation is deferred is not true for scheduler or handler work performed inside these subscribers.

### 2. Audio cue meaning is resolved using later state

**Confidence:** Confirmed

HorseMod hooks `LuxAudio_FireSoundCue_ViaVfxDispatcher` at `0x1403110B0` and `LuxAudio_FireSoundCueForSlotViaDispatcher` at `0x140311190`. It records only:

- wrapper variant;
- logical character slot;
- ABI context value;
- cue ID;
- cue sub-ID.

At confirmation it resolves the current fighter pointer and calls the original wrapper.

The downstream `ALuxBattleSoundEventHandler_HandleCharaCueEvent` at `0x1403C6B80` reads current battle manager, sound manager, move/player-side state, setup/style data, track tables, weak provider objects, and provider virtual behavior. It then remaps the cue, optionally invokes provider virtual `+0xC0`, starts native audio, and invokes a Blueprint event.

The confirmed call can therefore produce a different semantic result from the original frame-N call even when the stored four scalar arguments are identical.

### 3. `+0x08` request-size overread

**Confidence:** Confirmed

HorseMod declares the `+0x08` request size as `0x50`. Ghidra proves `FLuxSecondaryVFXDispatchRequest_Partial` is exactly `0x44` bytes and `ConvertSecondaryVfxRequestToCallbackPacket` consumes that typed request.

The hook reads 12 bytes beyond the verified request contract before normalization. Existing producers often use a larger zeroed stack buffer, but that allocation detail is not part of the adapter ABI.

### 4. Shared-vtable patch with single-object identity validation

**Confidence:** Confirmed mechanism; multiple-instance occurrence not yet proven

HorseMod patches the shared active class vtable rather than cloning a table for the current dispatcher object. Every object using that vtable will enter HorseMod’s hook. The hook then requires the caller object to equal the pointer currently stored in the global.

If two listener hubs sharing the vtable coexist during world/lifecycle transitions, a call from the non-global instance will fail closed. Static analysis has not yet proven whether that coexistence occurs.

### 5. Exactly-once status is accounting, not semantic validation

**Confidence:** Confirmed

`presentation_exactly_once` mainly verifies installed-slot masks and `committed <= queued` relationships. The ledger uses a `void` commit callback and increments committed counters immediately after returning from it.

It does not establish that:

- the target subscriber graph was the same graph that existed at event time;
- the callback used event-time gameplay state;
- the handler did not return early;
- an external effect was emitted;
- the effect was semantically equivalent to stock execution.

### 6. Presentation coverage remains incomplete

**Confidence:** Confirmed in manifest; full bypass inventory pending

The manifest acknowledges direct stage-break particles outside the dispatcher capability. Blueprint events, animation notifies, component spawns, provider calls, and specialty VFX managers must also be classified independently. A broad dispatcher hook is not proof of complete effect coverage.

### 7. Collection 25 is a persistent timed-state initializer

**Confidence:** Confirmed

The collection-25 subscriber is `InitializeLuxMoveEffectColorFadeAbsoluteParams` at `0x1403C51A0`. It copies a 0x40-byte callback packet into the character presentation object at `+0x988`, resets the fade cursor at `+0x958`, and publishes the initial material-slot state. `LuxMove_Tick_EffectColorFade` at `0x1403D2D10` consumes that stored block on later actor ticks.

Deferring slot `+0xD0` therefore changes when the fade schedule begins and which actor/material state it is applied to. The route is not a terminal draw call.

The native callback ABI spans 0x40 bytes. The adapter initializes bytes `+0x00..+0x3C`; bytes `+0x3D..+0x3F` are stack padding, but the subscriber performs a 16-byte load from `+0x30`. Those three bytes must not be peer-hashed or treated as semantic data.

### 8. Slot `+0x100` is live weapon-node visibility state

**Confidence:** Confirmed

Collection 31 is registered by `ALuxBattleChara_SetupPlayerChara` to `ALuxBattleChara_SubDlg_OnWeaponNodeAlphaChange` at `0x1403C4D50`. Its event is `{ playerIndex, weaponNodeIndex, alpha }`, not a generic color-variant payload.

The subscriber verifies the player, constructs `WeaponNodeFunc_<index>`, reads the current battle-manager gate and current weapon-node tables, calls `USceneComponent_SetVisibility`, and clears live lookup state when alpha is non-positive. Replaying only the three scalars at confirmation can target a later component graph and later manager state.

### 9. Collection 0 mixes native allocation, terminal activation, persistent bookkeeping, and Blueprint publication

**Confidence:** Confirmed

`LuxMove_AllocateMeshActorSlots_WithRemap` does much more than forward a secondary-VFX request. It resolves move IDs and transforms against current battle state, builds bone-track descriptors, selects mesh definitions, allocates presentation actors, registers situation/track state, appends slot IDs, and finally publishes the reflected `ReceiveEnableVFx` event.

The selected definition has two concrete branches:

- `AllocateParticleSystemMeshActorSlot` constructs/configures a particle-system component in the current `UWorld`, activates it, binds `ALuxVFxInstanceManager::OnParticleSystemFinished`, optionally attaches it to a current component, and appends a persistent 0xC0-byte slot record.
- `AllocateGroundDebrisMeshActorSlot` acquires a pooled ground-debris actor, initializes component-ring and transform state, activates it, binds `OnGroundDebrisDeactivated`, and appends a separate persistent 0xC0-byte slot record.

After this native work, `InvokeReceiveEnableVFxBlueprintEvent` copies the 0x4D-byte callback packet and dispatches `ReceiveEnableVFx` through `UObject::ProcessEvent` at vtable `+0x1F8`.

Collection 0 also has an independently stateful `ALuxBattleColorFadeManager` subscriber, now recovered as `ApplyEnableVfxToBattleColorFadeState`. The callback packet is the exact dumped 0x4D-byte `FLuxEnableVFxParam`: `{ ID, Group, BankType, Transform, FollowPlayerNo, FollowObject, five follow flags }`. The subscriber consumes only `ID`, `BankType`, and `FollowPlayerNo`; it resolves `ID` against the manager's current 0x50-stride `ColorFadeSettingList`, maps `BankType` to one of six 0x20-byte lanes, resolves a source-object token, and appends a 0x18-byte persistent active entry. `EFB_Stage` and invalid bank values have no color-fade lane.

When the selected lane was empty, the callback initializes its current index/value/rate/mode and calls `ApplyBattleColorFadeMaterialParamsToPlayerLane`. That path resolves the current player character, enumerates weak attached entities or current weapon meshes, derives material-index arrays, and reaches `ApplyBattleColorFadeParamsToMeshComponent`. The terminal helper writes and commits six dynamic-material parameters: `FadeType`, `FadeMidPoint`, `FadeMidValue`, `FadeOuterExp`, `FadePatternTex`, and `FadeTexParam`. If the lane was already active, the callback appends the entry and forces the current lane into fade-out transition mode, clamping the relevant transition rate to at least `0.2`.

This route proves that there is no single clean boundary at either the event-hub adapter or the mesh-slot resolver. Native semantic resolution, live allocation/pooling, persistent color-fade ordering, component selection, committed material mutation, terminal activation, lifecycle bookkeeping, and Blueprint-visible behavior are interleaved. Replaying only `FLuxEnableVFxParam` later can select different setting assets, lane contents, weak components, weapon state, material slots, and default textures.

### 10. Slot `+0x60` is a cross-system state reset, not a shaped VFX command

**Confidence:** Confirmed for three known native subscribers

HorseMod names this route `uint-byte-60`, which describes only its 5-byte packet. Ghidra shows that `HandlePresentationStateResetFromLuxEventHub` broadcasts collection 11 to at least three handlers, and none consumes those fields:

- `HandleCollection11VfxStateClear` overwrites the second argument with zero and tail-calls `LuxMove_ClearStateAndNotify`. That target resets two manager-side VFX facilities, frees owned provider-cache entries, clears a runtime table, and notifies the auxiliary VFX event actor.
- `HandleCollection11SoundStateClear` clears `ALuxBattleSoundEventHandler_Partial::bDeferredRankedSoundRequested` at `+0x3E5`, then stops/resets two CRI Atom manager subchannels through singleton state.
- `HandleBattleColorFadeStateResetEvent` ignores the packet and tail-calls `ResetBattleColorFadePlayerState` with `-1`. That path clears every manager-owned active lane and queued fade-layer array, restores native lane defaults, and reinitializes associated player slots.

Deferring slot `+0x60` delays teardown in VFX, audio, and battle-color-fade systems and applies it to later singleton/handler state. The stored `{ uint, byte }` is not what makes this route deterministic; the subscribers' current object graphs are the behavior.

### 11. Character-cue resolution and active-voice playback are separate boundaries

**Confidence:** Confirmed

`ALuxBattleSoundEventHandler_HandleCharaCueEvent` first resolves the event against current handler, player-setup, style, track-provider, and cue-metadata state. It may call the current provider virtual at `+0xC0`, then calls `LuxAudio_ResolveAndPlayCharaCue`, and finally publishes the original event to Blueprint regardless of whether native cue playback was available.

`LuxAudio_ResolveAndPlayCharaCue` is still semantic. It selects the current per-player shared audio object from a 0x10-byte manager array, resolves the current cue-family hash entry, combines the remapped table index and offset, applies authored manager setup, and only then calls the active-voice thunk.

The concrete native voice boundary is `LuxAudio_RegisterActiveVoiceInstance` at `0x14054F8B0`. It resolves the current CRI Atom cue entry, draws UCRT `rand()` until it has a non-sentinel id absent from the live active-voice map, inserts the voice record, marks the owner active, and publishes it through the current map.

HorseMod's CRT broker deliberately recognizes only the verified MoveVM and Rannyu return RVAs as gameplay callers. The audio voice-id draw therefore stays on the native presentation stream while gameplay draws use the rollback-owned stream. That isolation is directionally consistent with the recovered boundary, but it is an explicit departure from the stock binary's shared UCRT import and must remain part of the rollback contract.

### 12. Slot `+0x10` is a semantic DisableVFx transaction, not four opaque integers

**Confidence:** Confirmed

HorseMod identifies this route only as `FourUints10` / `four-uints-10`. The native request is the exact 0x10-byte `FLuxDisableVFxParam` value `{ ID, Group, keepFrame, FadeoutFrame }`, initialized by the game to `{-1, -1, 0, 0}` for reflected Blueprint use.

The VFX-handler collection-1 subscriber `HandleDisableVfxEvent` does not merely emit a visual. It obtains the current battle-manager-owned VFX subsystem, optionally resolves the request ID against current move state, expands the request through `LuxMove_BuildTransitionList`, and converts each 0x10-byte transition into a 0x18-byte disable filter. It applies every filter to both persistent 0xC0-stride slot tables before unconditionally publishing `ReceiveDisableVFx` through `UObject::ProcessEvent`.

The two tables have distinct non-immediate behavior. The primary table calls an actor virtual at `+0x238`; the secondary table mutates actor state at verified raw offsets including `+0x830`, `+0x878`, `+0x87C`, `+0x880`, `+0x938`, `+0x93C`, `+0xA04`, `+0xA0C`, and `+0xA10`. Immediate removal deactivates the actor through virtual `+0x360`, prunes weak slot callbacks, destroys owned record contents, compacts the table, and restarts the scan at the removed index.

The same collection has a second native subscriber, `ApplyDisableVfxToBattleColorFadeState`, owned by `ALuxBattleColorFadeManager`. An `ID` of `-1` resets the player state selected by one-based `Group`. For a specific ID, the callback resolves the current `ColorFadeSettingList`, scans the selected player's 0x20-byte lane states and their 0x18-byte active entries, immediately erases matching non-current entries, and marks a matching current entry for transition-mode-2 removal. Its removal rate comes from the current setting's authored fadeout frame count. This subscriber does not consume the event's `keepFrame` or `FadeoutFrame`; replaying the packet under different current setting/lane state can therefore produce a different result even when all four integers match.

Deferring the hub route therefore delays current-state ID resolution, transition expansion, actor disable/removal, weak-reference pruning, two VFX slot-table mutations, color-fade lane mutation or reset, and Blueprint publication. Replaying the same four integers later cannot preserve the original transaction's semantics.

### 13. `Transform18` and `FourUints20` are a paired persistent color-fade protocol

**Confidence:** Confirmed

HorseMod identifies slots `+0x18` and `+0x20` only by packet shape. Ghidra ties them to MoveVM effects `0x040B` and `0x040C` and to the native `ALuxBattleColorFadeManager`:

- effect `0x040B` builds a 0x24-byte request containing the selected player, priority, RGB values normalized from authored `/255` words, three timing-frame values, and a decoded float. The adapter inserts alpha `1.0` and broadcasts collection 2 as a 0x28-byte callback.
- `HandleBattleColorFadeQueueEvent` converts the three authored frame counts to seconds using the binary's exact `1/60` constant, grows the selected player's persistent queue, shifts all existing 0x34-byte records right, inserts the new layer at index zero, initializes its runtime state, and sorts the full queue by priority.
- effect `0x040C` sends `{ playerIndex, priority, timingFrames0, timingFrames1 }` through collection 3.
- `ApplyBattleColorFadeLayerTimingUpdate` treats priority `-1` as a wildcard, walks every matching persistent layer, writes two new runtime timings after converting them to seconds, and resets the layer runtime cursor.

No terminal component, draw, or particle operation occurs at either callback. Delaying `+0x18` changes insertion/order time; delaying `+0x20` changes when existing layers' runtime state is rewritten. Replaying either packet against a later queue is not equivalent to executing the paired protocol at the source frame.

### 14. Collection 0 capture uses the wrong native request size and reads past the request

**Confidence:** Confirmed

Ghidra fixes `DispatchSecondaryVfxRequestFromLuxEventHub`'s source request at exactly 0x44 bytes (`FLuxSecondaryVFXDispatchRequest_Partial`) and its converted callback value at exactly 0x4D bytes (`FLuxEnableVFxParam`). The native adapter's highest source read is the tail flag at `+0x40`; bytes `+0x41..+0x43` are source padding.

HorseMod instead declares slot `+0x08` as `RollbackVfxRoute::Hit` with `request_bytes = 0x50`. `RollbackProductionRuntime` passes that value directly to `SafeReadBytes`, so every intercepted collection-0 request attempts a guarded twelve-byte read beyond the exact 0x44-byte native object. The normalizer then copies only `{+0x00,0x0C}`, `{+0x10,0x0C}`, `{+0x20,0x0C}`, and `{+0x30,0x11}`, which stops at the real last field `+0x40`; the extra read contributes no semantic data.

The incorrect 0x50 value is also stored in `RollbackProductionVfxInvocation::request_bytes` and required by `RollbackVfxInvocationValid`, cementing a false ABI in the journal. Replaying the oversized zero-filled buffer happens to satisfy the adapter's verified reads, but that does not make the capture safe or the schema correct. This defect is static and does not require runtime validation to establish.

### 15. Slot `+0x28` is a current-state positional sound transaction, not positional VFX

**Confidence:** Confirmed

HorseMod calls slot `+0x28` `RollbackVfxRoute::Positional` and journals its exact 0x28-byte source request as a VFX event. The source size is correct, but the boundary and classification are not.

`HandleContactSoundRequestBroadcastFromLuxEventHub` at `0x140400880` converts `FLuxContactVfxDispatchRequest_Partial` into an exact 0x18-byte `FLuxContactVfxEvent_Partial`:

```text
+0x00 byte       dispatchClass
+0x04 uint       contactType
+0x08 FVector3f  converted UE-centimeter position
+0x14 byte       modeIndex
sizeof = 0x18
```

The adapter then synchronously publishes collection 4. `ALuxBattleSoundEventHandler_BeginPlay` registers `HandleContactSoundEventForBattleSound` at `0x1403C63C0` on that collection. The subscriber:

1. resolves the current battle manager and validates `contactType` against the current `voiceCueIds` table;
2. reads current player modes, current `StageMaterialSoundTable`, current action-type limits, current player setup, current style ID, and the current alternate-contact-sound flag;
3. selects/remaps the authored cue and calls `LuxBattleManager_DispatchBattleEventByClass`;
4. derives current-listener `Pan` and `Distance` parameters for positional events;
5. calls `LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr`, which allocates/registers a real active voice and returns its instance ID;
6. walks all seven persistent 0x50-byte active-voice tracking sets, removing IDs that are no longer active;
7. removes the old contact key and inserts the returned active-voice ID in the event-selected lane;
8. invokes the exact reflected Blueprint event `ReceivePlaySE` through `InvokeReceivePlaySEBlueprintEvent`.

The relevant handler state spans `ALuxBattleSoundEventHandler` fields `+0x388..+0x407`, including authored definition arrays, current player modes, the stage-material table, seven persistent tracking lanes, and remap limits. HorseMod's rollback snapshot does not capture this state. Its journal also carries only the source request; it does not capture the resolved cue/provider, listener-derived parameters, active-voice ID, pre-event tracking-set contents, or Blueprint target state.

Deferring `+0x28` therefore moves cue selection, terminal voice allocation, persistent tracking reconciliation, and `ReceivePlaySE` to confirmation. The later call can choose a different cue/provider and computes positional audio against a different listener and active-voice set. This proves that a correct request size is insufficient when the intercepted operation is a mixed semantic/persistent/terminal transaction.

### 16. Slot `+0x30` is the keyed stop half of the contact-sound protocol, not VFX

**Confidence:** Confirmed

HorseMod classifies slot `+0x30` as `RollbackVfxRoute::ModeAndUint` and journals its exact eight-byte source request as generic VFX. Ghidra proves the route is the stop half of the collection-4 sound transaction.

`HandleStopSoundRequestBroadcastFromLuxEventHub` at `0x140400920` converts `FLuxStopSoundDispatchRequest_Partial { uint dispatchClass; uint contactKey; }` into the exact eight-byte `FLuxStopSoundEvent_Partial { byte playerClass; padding[3]; uint contactKey; }` and publishes collection 5. `HandleStopSoundEventForBattleSound` at `0x1403C7720` then:

1. resolves the current battle manager;
2. selects one of the handler's current seven active-voice tracking sets using `playerClass`;
3. hashes `contactKey` and walks the current collision chain;
4. reads the matched element's current `activeVoiceId`;
5. calls `EnqueueStopVoiceByClassAndSharedPlayer`, which targets both the current class-specific audio player and the current shared player;
6. appends an exact 0x18-byte `FLuxAudioCommandRecord_Partial` under each owner's critical section with opcode `2`, the resolved voice ID, and immediate flag zero;
7. invokes the exact reflected Blueprint event `ReceiveStopSE` even if the tracking lookup did not find a native voice to stop.

The journaled packet carries neither the active-voice ID nor either target audio player. Those are resolved only when the subscriber runs. Deferral can therefore let a speculative sound continue past its intended stop, then stop a different replacement voice if the same key has been rebound, or issue no native stop after the tracking entry disappears. Blueprint `ReceiveStopSE` can still run in the miss case, so a single exactly-once counter cannot establish semantic equivalence.

Collections 4 and 5 must consequently be treated as one stateful protocol. Collection 4 allocates a runtime voice and writes `{contactKey -> activeVoiceId}` into persistent handler state; collection 5 consumes that later mapping to create terminal stop commands. Delaying both opaque packets without snapshotting or reproducing their intervening state does not preserve their pairing.

### 17. Primary matrix rollback coverage was a prefix, not a complete bank — resolved

**Confidence:** Confirmed defect and runtime-demonstrated stock-content violation; corrected

The primary bank initializer allocates 0x300 `FMatrix64` entries per physical slot, or 0xC000 bytes. Native HgCpu reads/writes only the current slot's first 0x1840 bytes, exactly 97 matrices. HorseMod formerly copied that prefix from all three slots. It now captures and restores each complete 0xC000-byte primary slot after the native reader.

The advertised count at `PLAYER +0x42550` is computed from authored 0x70-byte records as `max(signed slotIndex + 3)` and is not clamped to 97. Replay 100 measured stock counts of 231 and 379, proving the earlier admission limit invalid. Complete capture now covers every advertised matrix inside the recovered 768-entry allocation.

### 18. Step-harness matrix history was reconstructed one frame too old — resolved

**Confidence:** Confirmed static defect; corrected in the non-production harness

`RunRollbackStepSequence` captures each motion-bank timeline entry after the corresponding native step. `RollbackRestoreMotionBankHistoryFromTimeline` formerly sourced `timeline[t-age].provider_slot`; provider/previous at frame `f` contains frame `f-1`, so every reconstructed age was shifted back one additional frame. The helper now sources the historical `current_slot` for the intended frame age.

Production rollback does not call this timeline helper. The correction restores the diagnostic harness's intended post-frame physical-slot history.

### 19. Appendix animation clock and threshold-event state are outside rollback

**Confidence:** Confirmed static defect

Stock `ALuxBattleChara_TickActor` carries appendix animation time across the Lux/UE boundary through actor `LastHeaderFrame`, skeletal-mesh `GlobalAnimRateScale`, and per-threshold `IsUsed` flags. Those values determine how far the UE graph advances and whether synchronous `OnChangeAnimationState` Blueprint events fire. Neither `RollbackCharaAnimationState` nor production confirmed reconciliation captures or reconstructs them. After corrected native resimulation, stale UE state can create catch-up, freeze, burst, or duplicate-threshold behavior on the next ordinary tick.

### 20. UAnimInstance montage state is neither captured nor reconciled

**Confidence:** Confirmed ownership gap; concrete appendix montage usage still under investigation

The active montage array/map/root-motion pointer and 0x190-byte instance objects are UE-owned state with independent positions and marker caches. HorseMod's native Lux animation snapshot contains none of this graph, and root-only confirmed reconciliation never visits an AnimInstance. Static xrefs do not prove which appendix AnimBlueprints use montages, so the concrete content impact remains to be inventoried; the architectural coverage gap itself is proven.

### 21. Confirmation replays into a different event-hub graph and moves graph cleanup

**Confidence:** Confirmed mechanism and implementation mismatch

The installed hook suppresses `ProcessAndCompactCallbackEntries` completely during owned speculative frames. It therefore suppresses reverse-order subscriber execution, weak-target resolution, dead/disabled-entry detection, recursion accounting, and the resulting collection compaction. At confirmation, `commit_side_effect` sets `m_committing_effect` and calls the retained adapter trampoline with the current dispatcher; the adapter then walks the callback collection that exists at confirmation time.

The journal contains no source-frame subscriber identities or topology result. A listener added after the source frame can observe an old event, a listener destroyed after the source frame can disappear from it, registration order changes can reorder callbacks, and a dead entry that stock execution would have compacted remains live until confirmation or another broadcast. Even if every request ABI and payload field were corrected, the blanket adapter replay would still be temporally non-equivalent.

Collection 0 demonstrates why moving the hook down by only one call is insufficient. `LuxMove_AllocateMeshActorSlots_WithRemap` reads current move/provider state, applies a conditional id remap through `GetCurrentLuxVfxRemapMode`, builds 0x50-byte authored descriptors, computes a resolved 0x70-byte spawn request, allocates a persistent slot, registers situation/track ownership, appends the slot id to handler state, and finally publishes `ReceiveEnableVFx`. `BattleMgrSubsystem_LookupOrAllocateMeshActorSlot` is also not terminal: it normalizes the lookup key and selects between stateful particle-system and pooled-ground-debris allocation branches.

The particle branch constructs and activates a `UParticleSystemComponent`, binds completion, resolves attachment, and appends a 0xC0-byte live-slot record. The debris branch consumes the current UWorld/pool, initializes component-ring state, activates a pooled actor, binds deactivation, and appends the same class of persistent record. Terminal UE object work and later-event bookkeeping are interleaved.

The safe long-term boundary is therefore route-specific:

- execute source-frame semantic resolution where later events depend on its result;
- separate or shadow persistent presentation slot/lane state when physical UE objects must not be created speculatively;
- journal fully resolved terminal commands below current move/provider/asset lookup, not the original hub packet;
- publish irreversible component, audio, material, or Blueprint effects only under a route-specific confirmation contract;
- never attempt to snapshot or replay raw UObject pointers, weak-callback objects, delegate handles, or collection storage.

There is no evidence for one universal replacement hook below all 38 routes. Audio voice allocation, VFX slot allocation, color-fade lanes, visibility, animation commands, and reset protocols require separate ownership decisions.

## Recovered routes and types

### Slot `+0x10` — disable VFX request

```text
Adapter: 0x140400590
Name: BroadcastDisableVfxFromLuxEventHub
Request/callback: FLuxDisableVFxParam, 0x10 bytes
Collection: 1
Subscribers:
  HandleDisableVfxEvent, 0x1403C5250
  ApplyDisableVfxToBattleColorFadeState, 0x1404704F0
Reflected event: ReceiveDisableVFx
```

`FLuxDisableVFxParam` contains signed `ID`, `Group`, `keepFrame`, and `FadeoutFrame` fields. The subscriber can replace the ID with a current-state resolved move ID, expands zero or more transition entries, and derives filters whose negative integer fields act as wildcards. A transition mode of zero selects immediate removal; other modes perform table-specific soft-disable work.

The subsystem overlay recovered for this path has weak callback storage at `+0x388`, a primary record pointer/count at `+0x3E8/+0x3F0`, and a secondary record pointer/count at `+0x3F8/+0x400`. Both tables use an exact 0xC0-byte record stride. The concrete class of every stored actor remains intentionally unresolved because the secondary path's verified `+0xA04/+0xA0C/+0xA10` meanings conflict with the existing ground-debris overlay; no actor type was forced from shape alone.

The color-fade subscriber interprets only `ID` and `Group`. `ID == -1` resets player `Group - 1`. A specific ID is matched against active 0x18-byte fade entries in each 0x20-byte lane: non-current matches are compacted out immediately, while a current match is flagged pending removal and faded according to the currently resolved setting's authored fadeout duration. The callback intentionally ignores the packet's `keepFrame` and `FadeoutFrame`, so its result depends on manager state at dispatch time rather than on the packet alone.

### Slot `+0x18` — queue battle color-fade layer

```text
Producer: LuxMoveVM_DispatchEffectOp, effect 0x040B
Adapter: 0x1404007C0
Name: HandleColorFadeLayerBroadcastFromLuxEventHub
Request: FLuxMoveEffect040BColorFadeRequest, 0x24 bytes
Callback: FLuxBattleColorFadeQueueEvent, 0x28 bytes
Collection: 2
Subscriber: HandleBattleColorFadeQueueEvent, 0x140470750
```

The adapter synthesizes callback alpha `1.0`. The subscriber targets `ALuxBattleColorFadeManager_Partial::pPlayerStates` at `+0x390`, whose count is at `+0x398` and whose element stride is 0x28. Its queued-layer array lives at player-state `+0x10/+0x18/+0x1C`; each layer is 0x34 bytes and is sorted by the priority field at `+0x10`.

### Slot `+0x20` — update battle color-fade layer timings

```text
Producer: LuxMoveVM_DispatchEffectOp, effect 0x040C
Adapter: 0x140400840
Name: HandleColorFadeTimingUpdateBroadcastFromLuxEventHub
Request/callback: FLuxBattleColorFadeTimingUpdateEvent, 0x10 bytes
Collection: 3
Subscriber: ApplyBattleColorFadeLayerTimingUpdate, 0x140470470
```

The subscriber selects a player, applies a priority filter with `-1` as wildcard, converts both authored frame values using `g_flFramesToSeconds60Hz`, writes queued-layer runtime state at `+0x28/+0x2C`, and resets the cursor at `+0x30`.

### Slot `+0x08` — typed EnableVFx transaction

```text
Adapter: 0x140400540
Name: DispatchSecondaryVfxRequestFromLuxEventHub
Request: FLuxSecondaryVFXDispatchRequest_Partial, 0x44 bytes
Callback: FLuxEnableVFxParam, 0x4D bytes
Collection: 0
Subscribers:
  LuxMove_AllocateMeshActorSlots_WithRemap
  ApplyEnableVfxToBattleColorFadeState, 0x1404708A0
```

The adapter converts Lux coordinates/rotation/scale and synchronously broadcasts the exact UE `FLuxEnableVFxParam`. The VFX-handler subscriber allocates/remaps mesh actor slots; the color-fade-manager subscriber resolves the current setting asset and mutates persistent lane state.

The subscriber's lower path is now partially recovered:

```text
LuxMove_AllocateMeshActorSlots_WithRemap
  -> BattleMgrSubsystem_LookupOrAllocateMeshActorSlot
       -> AllocateParticleSystemMeshActorSlot
          or AllocateGroundDebrisMeshActorSlot
  -> RegisterSituationDescriptorListsForMeshSlot
  -> LuxMove_RegisterActorWithSubsystem_Bounded (conditional)
  -> InvokeReceiveEnableVFxBlueprintEvent
       -> UObject::ProcessEvent("ReceiveEnableVFx")
```

`BattleMgrSubsystem_LookupOrAllocateMeshActorSlot` consumes a complete 0xB0-byte request. The first 0x70 bytes are its value projection: the first 0x0C bytes form the definition lookup key; later fields carry the current world transform, attachment index, a lifecycle-local attachment component pointer at `+0x58`, an attachment `FName` at `+0x60`, and fallback-control flags. `LuxMove_ComputeEffectWorldTransform @ 0x1403B6F20` proves the split: the weapon branch stores `ALuxBattleChara_GetWeaponMesh` at `+0x58`, while the front-edge/table branches store the resolved socket name at `+0x60`. The pointer is identity-only and cannot enter a journal or peer hash; confirmation must resolve an equivalent component under the matching lifecycle epoch.

The remaining request bytes are not padding. `CopyResolvedMeshActorSpawnRequest @ 0x1403ADD60` clones a type-erased provider beginning at `+0x70`; its storage has inline bytes, an optional heap pointer at `+0x90`, and an element count at `+0xA0`. `ResolveMeshActorSpawnTransform @ 0x140896840` later invokes provider virtual `+0x38` to validate its weak source and `+0x60` to compute the current transform. The fixed selectors are now proven: `LuxMove_GetFrontEdgeSocketName @ 0x1403C0B20` maps authored selectors `0x8017..0x801F` to nine front-edge socket names, and `GetLuxSkeletonBoneNameByIndex @ 0x140464510` maps indices `0..96` to the supported build's fixed skeleton-name table. Horse can therefore journal those semantic selectors, never the process-local `FName` value.

There are two recovered provider kinds. `AssignBonePoseTransformProvider @ 0x1403A3DB0` retains a weak character, pose selector, bone index, and `ALuxBattleChara_GetPoseTransform_Decomposed @ 0x1403C4070`. `AssignChestMidpointTransformProvider @ 0x1419F6B20` retains a weak context and the thunk at `0x1403C4060`, whose target `LuxMove_ComputePlayersChestMidpointTransform @ 0x1403BF3E0` averages both players' live `MUNE1` transforms. Their UObject, vtable, callback, allocator, and sequence identities remain reconstruction-only lifecycle metadata.

The independent color-fade subscriber reaches a different concrete lower path:

```text
ApplyEnableVfxToBattleColorFadeState
  -> LookupBattleColorFadeSettingById
  -> append FLuxBattleColorFadeActiveEntry_Partial
  -> ApplyBattleColorFadeMaterialParamsToPlayerLane (first lane entry only)
       -> resolve attached-entity / weapon components and material indices
       -> ApplyBattleColorFadeParamsToMeshComponent
            -> set FadeType, FadeMidPoint, FadeMidValue, FadeOuterExp
            -> set FadePatternTex and FadeTexParam
            -> UMaterialInstance_CommitParamChange
```

Only `ID`, `BankType`, and `FollowPlayerNo` are consumed by the color-fade subscriber. Its output still depends on dispatch-time manager, asset, lane, character, weak-object, component, material-slot, and default-texture state, none of which is captured by the current route payload.

HorseMod currently labels this route `Hit` and sets `request_bytes` to 0x50. The exact native input is 0x44 bytes. Its configured value ranges stop at `+0x40` and therefore cover the adapter's real reads, but the preceding `SafeReadBytes` still reads twelve bytes beyond the request and the journal records the wrong size.

### Slot `+0x28` — positional contact sound transaction

```text
Adapter: 0x140400880
Name: HandleContactSoundRequestBroadcastFromLuxEventHub
Request: FLuxContactVfxDispatchRequest_Partial, 0x28 bytes
Callback: FLuxContactVfxEvent_Partial, 0x18 bytes
Collection: 4
Subscriber: HandleContactSoundEventForBattleSound, 0x1403C63C0
Terminal voice allocation: LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr, 0x14054F6D0
Reflected event: ReceivePlaySE
```

The source adapter's coordinate/mode conversion is value-only. The subscriber is not: it resolves current authored sound state, allocates an active voice, reconciles seven persistent tracking sets, and publishes Blueprint state. The callback value is therefore an input to a later stateful transaction, not a self-contained presentation command.

### Slot `+0x30` — keyed contact-sound stop transaction

```text
Adapter: 0x140400920
Name: HandleStopSoundRequestBroadcastFromLuxEventHub
Request: FLuxStopSoundDispatchRequest_Partial, 0x08 bytes
Callback: FLuxStopSoundEvent_Partial, 0x08 bytes
Collection: 5
Subscriber: HandleStopSoundEventForBattleSound, 0x1403C7720
Native router: EnqueueStopVoiceByClassAndSharedPlayer, 0x140525460
Terminal queue writer: EnqueueStopVoiceCommand, 0x140560D50
Reflected event: ReceiveStopSE
```

The subscriber consumes the persistent mapping produced by collection 4. The terminal native record is not present in the callback: it is synthesized later from the current tracking lane, current manager player table, and current shared player. A tracking miss suppresses only the native opcode-2 command; `ReceiveStopSE` still executes.

### Slot `+0x38` — character cue event

```text
Adapter: 0x140400B40
Name: ULuxBattleEventListenerHub_BroadcastCharaCueEvent
Request: FLuxBattleCharaCueRequest, 0x10 bytes
Callback: FLuxBattleCharaCueEvent, 0x10 bytes
Collection: 6
```

The registered sound handler performs semantic cue remapping and provider work before native audio and Blueprint publication.

### Slot `+0x60` — presentation-state reset

```text
Adapter: 0x1404009A0
Name: HandlePresentationStateResetFromLuxEventHub
Request/callback: FLuxCollection11ResetEvent, 0x05 bytes
Collection: 11
VFX subscriber: HandleCollection11VfxStateClear, 0x1403C7070
Sound subscriber: HandleCollection11SoundStateClear, 0x1403C7640
```

Both recovered subscribers ignore the packet and perform current-state teardown. This is one of the clearest demonstrations that the hub's route ABI cannot be used as the rollback journal's semantic contract.

### Slot `+0x108` — MoveVM scheduler commit notification

```text
Producer: 0x1402E5660
Producer name: LuxMoveVM_SchedState_CommitCommandSlot
Adapter: 0x1404002C0
Adapter name: BroadcastMoveCommandSlotCommitFromLuxEventHub
Request/callback: FLuxMoveCommandSlotCommitEvent, 0x0C bytes
Collection: 32
```

The producer commits deterministic scheduler/SubVM state before broadcasting the event. The adapter is a semantic command-transition notification, not a renderer.

### Slot `+0x128` — hit-category VFX event

```text
Adapter: 0x1403FFFF0
Name: BroadcastHitCategoryVfxFromLuxEventHub
Request: FLuxHitCategoryVfxRequest, 0x30 bytes
Callback: FLuxHitCategoryVfxCallback, 0x20 bytes
Collection: 36
```

The adapter converts the Lux hit position to Unreal centimeters and copies scale, preset ID, decoded VFX category, and the super-armor flag. Registered subscribers for collection 36 are not yet fully mapped.

### Slot `+0xD0` — MoveVM effect `0x2AFA`

```text
Producer: LuxMoveVM_DispatchEffectOp, opcode/effect 0x2AFA
Adapter: 0x1404003F0
Name: BroadcastMoveEffect2AFAFromLuxEventHub
Request: FLuxMoveEffect2AFARequest, 0x34 bytes
Callback ABI: FLuxMoveEffect2AFACallbackPacket, 0x40 bytes
Collection: 25
```

The adapter reorders three integer arguments and converts ten floats into three `FLuxVec4` values plus a flag. `InitializeLuxMoveEffectColorFadeAbsoluteParams` copies the packet into persistent actor state, resets its fade cursor, and starts material-slot publication. The last three ABI bytes are uninitialized native padding and are not semantic fields.

### Slot `+0x100` — weapon-node alpha/visibility

```text
Adapter: 0x140400240
Name: BroadcastWeaponNodeAlphaChangeFromLuxEventHub
Request/callback: FLuxWeaponNodeAlphaChangeEvent, 0x0C bytes
Collection: 31
Subscriber: ALuxBattleChara_SubDlg_OnWeaponNodeAlphaChange, 0x1403C4D50
```

The subscriber resolves the event against current battle-manager and actor-owned weapon-node state. Its terminal operation includes `USceneComponent_SetVisibility`, but it also mutates the actor's current lookup/state tables. The hub adapter is therefore above both semantic resolution and the concrete component operation.

## Ghidra improvements made during this investigation

- Renamed the misidentified `UE4_AsyncTask_SubmitWeakPtrTask` helpers to `AddWeakUObjectEventCallback` (`0x1403A3930`) and `AddWeakUObjectCallbackWithBoundByte` (`0x1403A3B70`), corrected their prototypes, callback function signatures, locals, and synchronous registration comments.
- Renamed `UE4_AsyncTaskPool_Compact` to `ProcessWeakCallbackEntryCompaction` at `0x140399DF0` and documented its recursion guard, threshold policy, removal scan, threshold reset, and storage-shrink behavior.
- Expanded `FCallbackEntryCollection_Partial` with verified `nCapacity`, `nCompactionThreshold`, and `nRecursionDepth` fields while preserving its exact 0x70-byte size.
- Created exact 0x30-byte `FWeakUObjectCallbackWithByte_Partial` and `FWeakUObjectEventCallback_Partial` structures plus `WeakUObjectCallbackWithByteFn` and `WeakUObjectCallbackFn` signature types.
- Named and typed the constructing/final 14-slot callback vtables at `0x14326BF78`, `0x14326BFE8`, `0x1432853C8`, and `0x143285438`; documented handle, destruction, weak-resolution, and invocation slots.
- Corrected `LuxMove_AllocateMeshActorSlots_WithRemap` to the `ALuxBattleVFxEventHandler_Partial`/`FLuxSecondaryVfxCallbackPacket_Partial` callback ABI and documented current-state resolution, descriptor construction, live-slot ownership, and Blueprint publication.
- Named the conditional remap globals `g_nConditionalVfxRemapSourceId` and `g_nConditionalVfxRemapTargetId`, and recovered `GetCurrentLuxVfxRemapMode` at `0x1403BFDF0`.
- Renamed `g_pLuxVfxDispatcher` to `g_pLuxBattleEventDispatcher` at `0x14470D188`.
- Corrected its type to `FLuxVfxDispatcher_Partial *` and replaced the misleading presentation-only plate comment.
- Corrected `ULuxBattleEventListenerHub_BroadcastCharaCueEvent` at `0x140400B40` and created `FLuxBattleCharaCueRequest`.
- Renamed and typed `BroadcastMoveEffect2AFAFromLuxEventHub` at `0x1404003F0`.
- Created `FLuxMoveEffect2AFARequest` and `FLuxMoveEffect2AFACallbackPacket`.
- Corrected the collection-25 callback ABI to 0x40 bytes and documented its three bytes of indeterminate stack padding.
- Recovered `InitializeLuxMoveEffectColorFadeAbsoluteParams` and its tick consumer `LuxMove_Tick_EffectColorFade`.
- Renamed and typed `BroadcastWeaponNodeAlphaChangeFromLuxEventHub` at `0x140400240`.
- Created `FLuxWeaponNodeAlphaChangeEvent` and `ALuxBattleCharaWeaponNodePresentation_Partial`.
- Typed and documented `ALuxBattleChara_SubDlg_OnWeaponNodeAlphaChange` at `0x1403C4D50`.
- Renamed the wide-string globals at `0x14325D5B8` and `0x14325B7C8` to `g_awWeaponNodeFuncPrefix` and `g_awEmptyWideString`.
- Created `FLuxResolvedMeshActorSpawnRequest_Partial` (0x70), `FLuxParticleMeshSpawnConfig_Partial` (0x48), and `FLuxGroundDebrisSpawnConfig_Partial` (0x78).
- Created `ALuxGroundDebrisActor_Partial` (0xA14) for the verified configured flag, presentation flags, component ring, auxiliary transform state, activation serial, and cached lifecycle state.
- Created `FLuxLiveMeshActorSlotRecord_Partial` (0xC0) for the persistent subsystem record returned by both concrete allocation branches.
- Renamed, typed, and documented `AllocateParticleSystemMeshActorSlot` at `0x1408A3C90` and `AllocateGroundDebrisMeshActorSlot` at `0x1408A3300`.
- Renamed and typed the fallback lifecycle thunk as `InvokeGroundDebrisDeactivatedVirtual` at `0x1408954B0`; its owner is the recovered `FLuxMeshSlotSubsystemView *`, not an opaque presentation actor.
- Defined and named the exact 51-wide-character callback literal at `0x14333FA10` as `g_awOnGroundDebrisDeactivatedQualifiedName`.
- Corrected `BattleMgrSubsystem_LookupOrAllocateMeshActorSlot` to accept the full resolved spawn request and documented its primary/fallback split.
- Renamed and typed `InvokeReceiveEnableVFxBlueprintEvent` at `0x1409A9920`, plus its packet initializer/copy helpers.
- Renamed the cached reflected-event name at `0x14414E3C0` to `g_qwReceiveEnableVFxFName` and documented its `ReceiveEnableVFx` meaning.
- Created `FLuxCollection11ResetEvent` and renamed/typed/documented dispatcher slot `+0x60` as `HandlePresentationStateResetFromLuxEventHub`.
- Created and documented the previously missing callback thunk `HandleCollection11VfxStateClear` at `0x1403C7070`.
- Corrected the collection-11 sound subscriber to `HandleCollection11SoundStateClear` with `ALuxBattleSoundEventHandler_Partial` and documented its CRI Atom reset behavior.
- Created `FLuxBattleSoundManager_Partial` (0x47C), `FLuxSharedAudioPlayerEntry_Partial` (0x10), `FSharedReferenceController_Partial` (0x10), `FLuxSoundCueFamilyEntry_Partial` (0x10), `FLuxSharedAudioPlayer_Partial`, and `FLuxActiveVoiceOwner_Partial` (now extended to 0xB0).
- Applied the recovered manager/player types to `LuxAudio_ResolveAndPlayCharaCue`, `LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr`, and `LuxAudio_RegisterActiveVoiceInstance`.
- Documented the semantic resolver, five-instruction shared-player thunk, cue-family lookup, shared-reference lifetime, CRI cue resolution, random unique-id loop, active flag, and live voice-map insertion.
- Created the exact `FLuxDisableVFxParam` (0x10) request type plus `FLuxVfxDisableTransitionEntry_Partial` (0x10), `FLuxVfxDisableFilterCommand_Partial` (0x18), `FLuxVfxSlotDisableSubsystem_Partial` (0x404), and `FLuxLiveVfxDisableSlotRecord_Partial` (0xC0).
- Renamed, typed, and documented `BroadcastDisableVfxFromLuxEventHub`, `HandleDisableVfxEvent`, `InvokeReceiveDisableVfxBlueprintEvent`, and `InitializeDisableVfxBlueprintParams`.
- Renamed, typed, and documented the primary, secondary, and combined persistent-slot filter functions, including removal/compaction loops and the distinct soft-disable paths.
- Renamed the cached reflected-event name at `0x14414E3B8` to `g_qwReceiveDisableVfxFName` and applied its scalar type.
- Created `FLuxMoveEffect040BColorFadeRequest` (0x24), `FLuxBattleColorFadeQueueEvent` (0x28), `FLuxBattleColorFadeTimingUpdateEvent` (0x10), `FLuxBattleColorFadeQueuedLayer_Partial` (0x34), `FLuxBattleColorFadePlayerState_Partial` (0x28), and the exact 0x3B0-byte `ALuxBattleColorFadeManager_Partial` overlay.
- Renamed, typed, and documented the collection-2 adapter/subscriber as `HandleColorFadeLayerBroadcastFromLuxEventHub` and `HandleBattleColorFadeQueueEvent`.
- Created, typed, and documented the previously unrecognized collection-3 callback function `ApplyBattleColorFadeLayerTimingUpdate`, plus its adapter `HandleColorFadeTimingUpdateBroadcastFromLuxEventHub`.
- Corrected the broad 1/60 constant name from `g_flIwWindOscillatorAxisScale` to `g_flFramesToSeconds60Hz` after verifying its bytes and frame-to-seconds use.
- Corrected and documented `InitializeLuxBattleColorFadeManagerBeginPlay`, its collection-10/11 reset thunks, and `ResetBattleColorFadePlayerState`; renamed the `FadePatternTex` global literal.
- Created `FLuxBattleColorFadeActiveEntry_Partial` (0x18) and `FLuxBattleColorFadeLaneState_Partial` (0x20), then propagated the lane pointer into `FLuxBattleColorFadePlayerState_Partial`.
- Renamed, typed, and documented the collection-1 color-fade subscriber as `ApplyDisableVfxToBattleColorFadeState`, including its reset sentinel, immediate compaction, current-entry transition, and intentionally ignored event timing fields.
- Created the exact dumped `ELuxEffectBankType` and `FLuxEnableVFxParam` (0x4D), replacing the generic collection-0 callback shape in the converter and subscriber.
- Created the exact 0x50-byte `FLuxBattleColorFadeSettingEntry_Partial`, 0x54-byte `FLuxBattleColorFadeMaterialParams_Partial`, `FLuxInt32Array_Partial`, `FLuxSparseAttachedEntityStorage_Partial`, and minimal verified `UMeshComponent_Partial` types.
- Renamed, typed, and documented `ApplyEnableVfxToBattleColorFadeState`, `LookupBattleColorFadeSettingById`, `ApplyBattleColorFadeMaterialParamsToPlayerLane`, and `ApplyBattleColorFadeParamsToMeshComponent` through the committed dynamic-material writes.
- Corrected `FTransform48` from three anonymous float arrays to typed `FVector4` rotation/translation/scale fields and renamed the broad 0.2 constant to `g_flOneFifth` after verifying its bytes and 103 cross-domain xrefs.
- Named and typed all eight fade-material parameter-name literals plus the four scalar components of the default `{1,1,0,0}` `FadeTexParam` vector.
- Renamed and typed `BroadcastHitCategoryVfxFromLuxEventHub` at `0x1403FFFF0`.
- Created `FLuxHitCategoryVfxRequest` and `FLuxHitCategoryVfxCallback`.
- Renamed and typed `BroadcastMoveCommandSlotCommitFromLuxEventHub` at `0x1404002C0`.
- Applied `FLuxMoveCommandSlotCommitEvent` to its callback value.
- Added rollback-relevant plate, PRE, and EOL comments to the recovered adapters.
- Created and propagated `FLuxContactVfxEvent_Partial` (0x18), `ALuxBattleManagerAudioState_Partial` (0x480), `ULuxBattleAudioWorldContext_Partial` (0x528), `ALuxSoundEventHandlerBase_Partial` (0x388), `ALuxBattleCharaSoundState_Partial` (0x538), and the typed active-voice tracking set/element/array layouts.
- Renamed, typed, and documented collection-4 adapter/subscriber `HandleContactSoundRequestBroadcastFromLuxEventHub` and `HandleContactSoundEventForBattleSound`, its current-player lookup `FindBattleCharaSoundStateByPlayerIndex`, callback initializer, and exact `ReceivePlaySE` Blueprint wrapper/name initializer.
- Corrected `ALuxBattleSoundEventHandler_Partial` with the verified battle/world context, `StageMaterialSoundTable`, and seven typed 0x50-byte active-voice tracking lanes.
- Retyped and documented `LuxBattleManager_DispatchBattleEventByClass` through listener-relative `Pan`/`Distance` writes and `LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr`; named the exact audio parameter literals and the `1/1800` pan-position scale.
- Created and propagated the exact `FLuxStopSoundDispatchRequest_Partial` and `FLuxStopSoundEvent_Partial` collection-5 types.
- Renamed, typed, and documented `HandleStopSoundRequestBroadcastFromLuxEventHub`, `HandleStopSoundEventForBattleSound`, `EnqueueStopVoiceByClassAndSharedPlayer`, `EnqueueStopVoiceCommand`, the callback initializer, and the exact `ReceiveStopSE` Blueprint wrapper/name initializer.
- Recovered `FLuxAudioCommandRecord_Partial` (0x18) and `FLuxAudioCommandQueue_Partial` (0x18), expanded `FLuxActiveVoiceOwner_Partial` through its queue and critical section, and applied the typed queue to the opcode-2 stop writer.
- Renamed vtable `+0x6A0` target `0x1403C0200` to `BuildCharaActorWorldTransformFromBattleWorldMode` and applied its exact two-argument `FTransform48 *` prototype.
- Created `ALuxBattleCharaSyncActor_Partial` (0x568) to separate the Unreal actor sync overlay from the huge native `ALuxBattleChara_Partial` simulation object.
- Created `ALuxBattleCharaSyncWorldTransformBuilderFn`, applied it to `g_pfnBuildCharaActorWorldTransform` at `0x143268718`, and documented the vtable slot's matrix-bank-derived semantics.
- Renamed/retyped `GetBattleWorldModeSharedPtrForCharaActor` at `0x1403BE670` to the actor-specific overlay and documented actor `+0x558/+0x560`, actor `+0x98`, and BattleManager `+0x1450/+0x1458` world-mode selection.
- Created `FScbattleWorldModeSharedPtr` and `ALuxBattleCharaWorldModeGetterFn`, typed/renamed the transform builder's two 0x10-byte world-mode shared-pointer locals, and documented world-mode slots `+0xE0/+0xF8`, info-handler transform resolution, identity fallback, and reference-count cleanup.
- Named and typed ALuxBattleChara vtable slots `+0x690/+0x698` as `g_pfnGetCharaWorldModeSlot690` and `g_pfnGetCharaWorldModeSlot698`.
- Cross-checked `InitializeLuxBattleManagerBeginPlayPipeline` at `0x1403E79C0`, proving that BattleManager `+0x1450/+0x1458` owns an 8-byte `ScbattleWorldMode_Partial` facade plus its shared-reference controller rather than a pose-bank object.
- Created `FScbattlePlayerInfoHandlerSharedPtr`, corrected `FLuxPlayerIndexCallbackContext_Partial` with its vtable, and renamed/typed/documented `BuildUnrealWorldTransformFromPlayerInfoHandler`, `CopyPlayerBoneMatricesFromWrapper`, and `CopyPlayerBoneMatricesFromPrimaryBank`.
- Proved the player-info route reads matrix count at native `PLAYER +0x42550`, obtains the current primary-bank buffer through `PLAYER +0x35A0`, copies exact 0x40-byte `FMatrix64` entries, converts the battle pose matrix to UE render space, and splits it into `FTransform48`.
- Re-audited and replaced all stale `ALuxBattleChara_TickActor` ownership comments. The new plate separates presentation, native reads, native sway-presentation writes, queued arbitrary callbacks, and base `ReceiveTick`/lifecycle work.
- Recovered the concrete player-info wrapper slots used by the tick and named the gauge, move-lane, state-byte/state-word, and special-state accessors plus their wrapper thunks.
- Renamed `ALuxBattleChara_RefreshSoulChargeState` to `RefreshBattleCharaSoulChargePresentationState` and corrected its prototype to the new `ALuxBattleChara_SoulChargePresentationOverlay`.
- Reclassified actor `+0x4C0` as `FLuxSoulChargeProviderSet_Partial` (0x50-byte TSet-like storage) and renamed its builder to `PopulateSoulChargeProviderSet`; the old move-table label was incorrect.
- Created and propagated `ALuxBattleCharaActorPresentation_Partial` with explicit animation flags, visibility flags, Soul Charge presentation fields, pending callback array, and world-mode override pair.
- Recovered the 0x50-byte pending delegate ABI, created `FLuxFloatDelegateEntry_Partial` plus `LuxFloatDelegateInvoke`, and renamed/typed `ExecuteDelegateWithFloatParam`; the second argument is `float` in `XMM1`, not an integer.
- Renamed and typed `RefreshScbattleAnimNodeBindings` and corrected the class constructor name to `Z_Construct_UClass_ULuxCharaAnimInstance`.
- Renamed/typed `DispatchActorReceiveTickEvent` and corrected `ALuxCharaActor_TickActor`'s float-delta prototype, documenting the arbitrary `ProcessEvent` boundary.
- Traced PlayerInfoHandler slot `+0x268` into native secondary-sway presentation, renamed/typed `ApplySwayBonePresentationModeViaPlayerInfoWrapper`, `ApplyPlayerIndexedSwayBonePresentationMode`, and `ApplyOrClearBattleCharaSwayBoneParameters`.
- Created `FLuxSwayBoneParameterSource_Partial` and minimum-extent `LuxNativePlayerSwayBoneOverlay_Partial`, and named the verified 127.0 quantization constant `g_flSignedByteQuantizationScale`.
- Cross-checked the sway writes against both the native HgCpuDirect writer/reader and HorseMod supplemental skeleton snapshot; neither covers the `+0x2B4BC/+0x2B550` sidecar.
- Corrected `CMatrixBankHeader_Partial` to its exact 0x38-byte pointer-width layout and documented the primary and secondary initializers' true 0xC000-byte and 0x800-byte physical slot extents.
- Renamed and documented `AdvanceCMatrixBankRingBuffer`; proved its `0 -> 2 -> 1 -> 0` rotation, previous/provider semantics, and absence of any matrix copy or clear.
- Corrected two secondary-bank getters previously mislabeled as hit/hurt-area accessors to `GetCMatrixBank32PreviousBoneMatrix` and `GetCMatrixBank32CurrentBoneMatrix`, with exact `FMatrix64 *` prototypes.
- Renamed, typed, and documented `GetPlayerPrimaryBoneMatrixCount` and `CopyAllPlayerPrimaryBoneMatrices`, including the distinction between the advertised count and the native HgCpu prefix.
- Recovered the sole matrix-count writer in `LoadBattleCharaMovesetEntriesBonesAndJapaneseVtb`, typed `PLAYER +0x42550` as `dwPrimaryMatrixCount` in both verified character overlays, and documented its unclamped authored-record maximum.
- Documented the bank rotations and solve ordering in `LuxBattleChara_FinalizeTickPoseAndState`, then cross-checked that HorseMod captures only after the owned native iteration completes.
- Added a plate comment to `g_abBoneSocketSoftRefDefault` and retained `g_FTransformIdentity48` as a typed 0x30-byte identity transform. The completeness audit still reports the identity transform's overlapping component symbols as separate untyped globals; changing those interior symbols would damage the parent `FTransform48` definition, so they were left intact as a scorer artifact.
- Recovered exact reflected extents for `FLuxCharaAnimNode_SCBattle_Partial` (0x68), `ULuxCharaAnimInstance_Partial` (0x4A0), `ALuxCharaAppxActor_Partial` (0x408), `ULuxCharaAppxAnimInstance_Partial` (0x378), and `USkeletalMeshComponent_AnimationPartial` (0xFF0), including `GlobalAnimRateScale` at component `+0xB60`.
- Created `FAppxMeshAnimationParam_Partial` (0x0C), `FAnimMontageInstance_Partial` (0x190), `UAnimInstance_MontagePartial` (0x378), `UAnimMontage_Partial`, montage-map storage/entry types, and the verified Lux AnimInstance-proxy/node-list overlays.
- Renamed, prototyped, typed, and documented `SetScbattleAnimNodeWorldModeProvider`, `BindScbattleAnimNodeToPlayerAndSkeleton`, `RefreshScbattleAnimNodeBindings`, `InvokeAppxAnimationStateChange`, `InvokeAppxTargetModeValueChange`, `PlayAnimInstanceMontage`, `SetAnimInstanceMontagePosition`, and `TickSkeletalMeshAnimation`.
- Named the appendix event `FName` globals, default montage-group name, Unreal GC/game-thread globals, imported `GetCurrentThreadId` pointer, and the two authored front-hair bone-name globals. Final completeness checks leave only low-value structural/decompiler deductions above the recovered semantic coverage.
- Saved `SoulcaliburVI.exe` after the edits.

## Correct aspects of the current implementation

- All active-hub slots `+0x08..+0x130` have compatible two-argument void ABIs.
- Excluding `+0x138` and `+0x140` is correct.
- The main tick, character advance, input, wind, camera vibration, and stage visibility hook addresses and calling conventions match the current Ghidra program.
- Normalizing only bytes actually consumed by native adapters is directionally correct once the intercepted function is a proven terminal or replay-safe boundary.
- Re-resolving transient UObject pointers rather than serializing them is necessary, although it does not solve event-time semantic context.
- Separating verified gameplay CRT callers from presentation callers matches the recovered need for RNG-domain isolation; the caller inventory must remain complete and version-specific.
- The narrow fighter root-transform helper uses the correct current-binary vtable slot, exact 0x30-byte `FTransform`, stock setter ordering, and stock no-sweep/no-teleport arguments.
- Matrix-bank capture is placed after native rotation and primary pose solve, and production restore reapplies the controller plus all three captured prefixes after the native HgCpu current-slot reader.

## Required architecture work

### Route and subscriber classification

For every collection, recover:

1. all native producers;
2. exact request layout and size;
3. adapter transformation;
4. every registered subscriber;
5. subscriber reads and writes;
6. RNG use;
7. provider/Blueprint calls;
8. terminal external emissions;
9. object and registration lifetime.

Each route should then be classified as one or more of:

- authoritative gameplay mutation;
- deterministic semantic scheduling;
- confirmed-only presentation state;
- terminal irreversible emission;
- query/provider operation;
- lifecycle/configuration work.

### Correct journaling boundary

The likely long-term model is:

- execute or reproduce rollback-relevant semantic work with frame-correct state;
- maintain confirmed-only presentation state in chronological order where appropriate;
- capture all event-time inputs required by confirmed-only resolvers;
- journal only proven terminal emissions exactly once;
- never snapshot UObject pointers, callback identities, provider caches, or delegate storage.

For audio, the current wrapper boundary is too high. Candidate lower boundaries include the resolved native cue/voice-start path, but provider virtuals and Blueprint events must be classified before selecting the final hook.

For VFX, the hub is too high. Candidate lower boundaries are the concrete mesh/component/particle spawn and activation functions reached after handler-owned semantic state has been updated.

### Fighter synchronization boundary

Do not fix fighter reconciliation by calling the full `ALuxBattleChara_TickActor` at confirmation. First recover and separate:

1. the primary CMatrixBank update that produces matrix 0 for the root actor-world transform;
2. the actor-owned Soul Charge fade/material/provider presentation lane and its native secondary-sway sidecar;
3. root component publication;
4. hair/weapon/skeleton attachment publication and main SCBattle-node binding;
5. appendix `LastHeaderFrame`, `GlobalAnimRateScale`, target-mode state, and threshold `IsUsed` ownership;
6. appendix AnimBlueprint animation-position/state-change publication;
7. active `UAnimInstance` montage instance/map/marker/root-motion ownership;
8. material and mesh/visibility publication;
9. pending asset/lifecycle callback admission and deduplication;
10. base `ALuxCharaActor_TickActor`/`ReceiveTick` behavior.

Items 2 through 8 are presentation consumers and can be candidates for selective confirmed-state reconstruction, but they do not share one safe replay primitive. Item 2 must include the native sway sidecar because neither HgCpuDirect nor the supplemental skeleton snapshot covers it. Items 5 through 7 require an explicit state policy: restore safe scalar/instance state, or reset/recreate it from confirmed Lux state while deduplicating Blueprint events, notifies, delegates, and root-motion ownership. Items 9 and 10 are not safe publication primitives because they can execute arbitrary callback/lifecycle code. Item 1's timing and width are now covered: rotation and pose production precede production capture, and all 768 matrices in each primary physical slot are restored. Actor override lifetime, AnimGraph worker-thread ownership, and concrete appendix montage usage remain open. Simply checking that the returned transform is finite is insufficient.

## Open investigation queue

- [x] Recover the directly registered native subscribers for collections 25 (`+0xD0`) and 31 (`+0x100`).
- [x] Recover collection 1 (`+0x10`) through both known native subscribers: transition expansion, both persistent VFX disable tables, record removal/compaction, `ReceiveDisableVFx` publication, and battle-color-fade lane reset/removal.
- [x] Recover collections 2/3 (`+0x18/+0x20`) as the paired battle-color-fade queue/timing protocol, including exact request types and persistent manager structures.
- [x] Scan the complete direct `GetLuxBattleEventListenerHubFromWorld` caller inventory for collection 32 (`+0x108`) and collection 36 (`+0x128`) registration; no direct native registration was found.
- [ ] Recover any indirect hub-pointer propagation or alternate registration path for collections 32/36; Blueprint/indirect registration remains possible despite the negative direct scan.
- [x] Identify and document `LAB_1403C7070`, now `HandleCollection11VfxStateClear`, registered by the VFX handler on collection 11.
- [ ] Trace every `ALuxBattleVFxEventHandler` callback to concrete terminal component/particle operations. Collection 0's VFX-handler branch is partially traced through particle-system and ground-debris allocation/activation plus `ReceiveEnableVFx`; its separate color-fade branch is traced through committed dynamic-material parameter writes.
- [ ] Build a per-route subscriber/topology inventory that records callback owner class, bound arguments, registration lifetime, reverse-order dependencies, semantic state writes, and first irreversible emission.
- [ ] Design a shadow presentation-slot protocol for collection 0 that preserves source-frame id/provider/descriptor resolution and later disable ownership without constructing rollback-doomed UE components.
- [ ] Identify route-specific terminal boundaries for audio voice allocation, particle/debris activation, material commits, visibility, Blueprint publication, and reset protocols; do not replace the blanket hub hook with another universal hook.
- [x] Trace `ALuxBattleSoundEventHandler_HandleCharaCueEvent` below semantic remapping to provider dispatch, semantic manager lookup, native active-voice insertion, and Blueprint publication boundaries.
- [ ] Determine whether every non-character sound route converges on `LuxAudio_RegisterActiveVoiceInstance` or uses additional CRI/player boundaries. Collection 4 (`+0x28`) is confirmed through `LuxBattleManager_DispatchBattleEventByClass` to active-voice allocation; collection 5 (`+0x30`) is confirmed as its keyed stop path through opcode-2 audio command queues. Remaining sound-handler routes are pending.
- [ ] Determine whether audio/VFX handler state is read back by gameplay code or remains presentation-owned.
- [ ] Inventory direct VFX/effect paths that bypass `g_pLuxBattleEventDispatcher`.
- [ ] Prove or reject the singleton assumption for the active listener hub across world and round transitions.
- [ ] Audit every current `RollbackVfxRouteDescriptor::request_bytes` against the exact native request type and highest adapter read. Slot `+0x08` is now proven wrong: configured 0x50 versus exact native 0x44.
- [ ] Replace shape-based route names such as `FourUints10` and `TransformD8` with verified semantic names.
- [ ] Reconcile the eight-slot manifest with the 38-slot implementation before any further feature work.
- [ ] Rename `presentation_exactly_once` or strengthen it so it cannot be mistaken for semantic validation.
- [x] Recover the fighter root-transform virtual and prove that it selects a player-info handler, reads native primary-bank matrix 0, converts it to UE render space, and does not directly convert native `PLAYER` position fields.
- [x] Separate the Ghidra Unreal actor sync overlay from the native simulation `ALuxBattleChara_Partial` type.
- [x] Recover the producer of BattleManager `+0x1450/+0x1458`: BeginPlay allocates a process-lifetime `ScbattleWorldMode` facade and shared-reference controller.
- [x] Recover the player-info handler returned by world-mode `+0xE0` through its matrix-read virtual and prove that native `PLAYER +0x35A0` primary-bank matrix 0 produces the actor world transform.
- [x] Cross-check and correct the recovered matrix dependency against rollback capture/restore: primary bank control and all 0xC000 bytes of all three physical slots are explicitly covered.
- [x] Prove the provenance/immutability of native matrix count `PLAYER +0x42550`: content-load code computes the unclamped maximum signed authored slot index plus three; it is round-lifetime state, so admission/baseline validation is preferable to per-frame serialization.
- [x] Prove primary/secondary matrix-bank producer timing: both rotate at the start of `LuxBattleChara_FinalizeTickPoseAndState`, the new primary current slot is solved before the owned iteration returns, and production step-state capture follows that return.
- [x] Cross-check native/Horse matrix extents: native HgCpu covers primary 97/current and secondary 32/current; Horse now covers complete primary 768/all-three and complete secondary 32/all-three physical slots.
- [x] Audit and correct timeline reconstruction in the rollback step harness: it now sources historical `current_slot`; production restore does not use this helper.
- [x] Recover the main fighter/weapon SCBattle AnimGraph binding and exact 0x68 custom-node/0x4A0 Lux AnimInstance layouts; prove it binds native matrix providers rather than restoring UE animation time.
- [x] Recover the appendix header-frame bridge through `LastHeaderFrame`, `GlobalAnimRateScale`, exact 0x0C threshold records, `IsUsed`, and synchronous `OnChangeAnimationState` dispatch.
- [x] Recover `UAnimInstance` montage ownership through its active-instance array, lookup map, root-motion pointer, 0x190-byte instances, position, and marker-cache resets.
- [ ] Inventory concrete appendix AnimBlueprint implementations of `OnChangeAnimationState` and `SetAnimationPosition` to determine which use montages, state machines, sequences, or custom absolute-position logic.
- [ ] Define production ownership for appendix clocks/threshold flags and montage instances, or prove a deterministic reset/recreation path that does not replay arbitrary Blueprint/lifecycle work.
- [ ] Prove whether `ULuxCharaAnimInstance` matrix sampling can execute in parallel with owned native resimulation; if so, gate or otherwise synchronize the native matrix-provider mutation window.
- [ ] Design selective confirmed animation reconciliation without entering full `ALuxBattleChara_TickActor`, queued delegates, or base `ReceiveTick`.
- [x] Replace the invalid `dwPrimaryMatrixCount <= 97` admission invariant with complete 768-matrix physical-slot capture; Replay 100 stock counts 231/379 validate why the wider capture is required.
- [ ] Recover all writers/lifetime transitions for the actor world-mode override at `+0x558/+0x560`.
- [x] Split `ALuxBattleChara_TickActor` into presentation, native-read, native sway-presentation, queued-callback, and base `ReceiveTick`/lifecycle corridors.
- [x] Determine ownership of Soul Charge actor `+0x537/+0x53C/+0x540`: presentation selector/fade counter/floor, not authoritative gameplay state.
- [x] Recover player-info wrapper `+0x268` and prove the Soul Charge refresh's native write is a four-record secondary-sway presentation update.
- [x] Cross-check native sway sidecar coverage: absent from HgCpuDirect and outside HorseMod's supplemental `+0x29120..+0x2B3DF` skeleton block.
- [ ] Recover every concrete `ReceiveTick` implementation for the ALuxBattleChara classes used in battle, preview, demo, and replay worlds before assigning base-tick ownership more narrowly.
- [ ] Inventory every producer of actor `+0x548` pending float delegates and determine whether callback identity/completion can be reconciled without replaying setup work.
- [ ] Design a selective confirmed presentation reconciler for root, hair/appendix, Soul Charge provider/material/sway, and visibility lanes without entering the callback queue or base tick.
- [ ] Strengthen presentation actor binding with verified class/vtable identity and expected `+0x6A0` target checks.
- [ ] Replace `presentation_transforms_finite` with a metric that cannot be read as semantic Lux/Unreal alignment; any actual comparison should use the restored primary-bank matrix/conversion result, not unrelated `PLAYER +0x94/+0xA0` scalars.

## Source locations

- `HorseMod/horselib/RollbackProductionRuntime.hpp`
  - dispatcher global RVA: around line 144;
  - presentation actor binding: lines 3320-3401;
  - vtable patch installation: around line 10308;
  - presentation accounting: around line 22535;
  - confirmed fighter root-transform reconciliation: lines 23117-23189;
  - confirmed commit: around line 23197;
  - audio hooks: around line 23475;
  - dispatcher slot hook: around line 23593.
- `HorseMod/horselib/NativeBinding.hpp`
  - current transform-builder alias/comment: lines 138-149;
  - vtable `+0x6A0` constant: around line 264;
  - narrow root-transform publisher: lines 441-490.
- `HorseMod/horselib/RollbackCharaAnimationState.hpp`
  - native Lux clip/runtime/cue-owner/scheduler offsets and extents: lines 25-58;
  - native pointer-graph capture: around lines 295-405;
  - native scalar/trigger restore: around lines 517-558.
- `HorseMod/horselib/ReplayAnimationPresentation.hpp`
  - replay actor/material/animation-override offsets: around lines 176-214;
  - replay presentation capture/restore: lines 303-691;
  - one-shot AnimInstance refresh fields: around lines 887-908.
- `HorseMod/horselib/ActorTickGate.hpp`
  - replay/freeze `ALuxBattleChara::TickActor` gate and sibling actor-tick sites: around lines 205-257.
- `HorseMod/horselib/RollbackHgCpuSnapshot.hpp`
  - primary/secondary matrix-bank offsets and control extent: lines 58-72;
  - primary matrix-bank capture/restore: around lines 2504-2740;
  - non-production timeline history reconstruction: lines 3123-3270;
  - native skeleton runtime base/extent: lines 84-89;
  - skeleton capture/restore paths: around lines 1433-1752.
- `HorseMod/horselib/RollbackStepHarness.hpp`
  - post-step motion timeline capture and history reconstruction calls: around lines 2438-2640.
- `HorseMod/horselib/RollbackMotionBankCanonical.hpp`
  - three-buffer bank contract and primary 0x1840-byte extent: lines 18-24.
- `HorseMod/horselib/RollbackVfxPresentation.hpp`
  - route enumeration and descriptors: lines 12-173;
  - normalized invocation and validation: after line 175.
- `HorseMod/horselib/RollbackSideEffectLedger.hpp`
  - stated policy: lines 1-6;
  - confirmation and commit accounting: around lines 464-568.
- `HorseMod/horselib/RollbackSnapshot.hpp`
  - stale eight-slot presentation capability: around lines 2033-2038.

## Historical Ghidra cleanup ledger (complete, 2026-08-05)

This ledger records the type-closure pass requested after the original investigations. It is static-analysis only. `Fixable` is the final `analyze_function_completeness` deduction after structural changes, comments, and direct-caller re-decompilation; Ghidra SSA/register artifacts are recorded instead of being force-typed. The final batch audit covers 119 unique ledger addresses, including expanded one-hop helpers. No function has more than ten fixable points; the maximum is exactly 10 on three reflected demo-human names whose exact tokens collide with their tagged variants.

| Address | Final function / prototype | Recovered types and globals | Fixable | Remaining uncertainty |
|---|---|---|---:|---|
| `0x1403C6B80` | `void __fastcall ALuxBattleSoundEventHandler_HandleCharaCueEvent(ALuxBattleSoundEventHandler_Partial*, FLuxCharaCueEventParam*)` | Provider/style remap labels, cue-family dispatch comments, reflected cue event globals | 3.87 | Terminal playback remains downstream of the synchronous semantic cue selection |
| `0x1403B5530` | `void __fastcall ALuxBattleVFxEventHandler_BeginPlay(ALuxBattleVFxEventHandler_Partial*)` | Manager cleanup delegates and tracked-id ownership | 4.49 | None affecting the recovered registration/cleanup order |
| `0x1403C79D0` | `void __fastcall ClearTrackedVfxManagerEntryIds(ALuxBattleVFxEventHandler_Partial*)` | Both tracked manager-id arrays | 0 | None |
| `0x1403C7A20` | `void __fastcall HandleVfxManagerCleanupDelegate(ALuxBattleVFxEventHandler_Partial*)` | Cleanup delegate ABI | 0 | None |
| `0x1403BA780` | `void __fastcall DestroyTrackedVfxManagerEntriesAcrossPools(ALuxBattleVFxEventHandler_Partial*)` | Manager pools at `+0xE90`, `+0xF00`, and `+0xF70` | 0 | Remaining deductions are compiler SSA structure only |
| `0x1403B4610` | `void __fastcall ALuxBattleSoundEventHandler_BeginPlay(ALuxBattleSoundEventHandler_Partial*)` | Twelve hub registrations, seven active-voice sets, authored DataTable keys, async `BattleData` callback/control-block types | 9.93 | One interior vtable-field audit defect and an authored row field at `+0x18` remain documented |
| `0x1403D54E0` | `void __fastcall InvokeBattleDataParseCallback(FLuxBattleDataParseCallbackStorage*, FLuxAsyncBattleDataResult_Partial*)` | Exact callback storage/vtable | 0 | None |
| `0x1403D54F0` | `void __fastcall CopyBattleDataParseCallbackStorage(FLuxBattleDataParseCallbackStorage*, FLuxBattleDataParseCallbackStorage*)` | Exact callback-copy ABI | 0 | None |
| `0x1403D5510` | `void* __fastcall GetBattleDataParseCallbackTypeInfo(void)` | RTTI descriptor global | 0 | None |
| `0x1403B0780` | `void __fastcall LuxDataTable_ParseBattleNameFromResult(FLuxBattleDataParseCallbackStorage*, FLuxAsyncBattleDataResult_Partial*)` | `FLuxAsyncBattleDataResult_Partial`, `FLuxFName_Partial`, handler `battleName` FString, parser field-name globals | 0 | Authored response payload remains a conservative partial overlay |
| `0x14216D8C0` | `void __fastcall DeleteLuxRefControlBlock(FLuxRefControlBlock_Partial*, bool)` | `FLuxRefControlBlockVtable_Partial` | 0 | Native MCP reports one pointer-return function-definition sizing defect; the exact target prototype is documented |
| `0x1403BA640` | `void __fastcall DestroyOwnedSubobjectAtControlBlockOffset10(FLuxRefControlBlock_Partial*)` | Owned subobject field at `+0x10` | 0 | None |
| `0x142ED0930` | `FLuxBattleDataRequestRef_Partial* __fastcall ConstructBattleDataRequestRef(FLuxBattleDataRequestRef_Partial*)` | Exact 0x18 request-reference layout | 0 | None |
| `0x142ED6360` | `void __fastcall DestroyBattleDataRequestRef(FLuxBattleDataRequestRef_Partial*)` | Request/ref-control cleanup | 1.93 | Compiler-projected cleanup temporary only |
| `0x14232C670` | `UClassCastMetadata_Partial* __fastcall GetDataTableClassMetadata(void)` | `g_pDataTableClassMetadata`, exact class-name strings | 0 | Structural decompiler artifacts only |
| `0x1423298C0` | `void __fastcall InitializeDataTableClassInstance(UDataTableObject_Partial*)` | `UDataTableObject_Partial` and class metadata | 0 | None affecting class construction |
| `0x1404247B0` | `UDataTableObject_Partial* __fastcall LuxAudio_LookupStageMaterialSoundTable(FLuxDataTableSubsystemRoot_Partial*)` | DataTable subsystem root, `StageMaterialSoundTable` lookup key, loader FName | 0 | None |
| `0x1403C90B0` | `void __fastcall RefreshBattleCharaSoulChargePresentationState(ALuxBattleChara_SoulChargePresentationOverlay*)` | Exact actor presentation overlay; exact 0x50 visibility/entity sets; `ALuxBattleManager_SoulChargeOverlay`; `ULuxWeaponMeshComponent_SoulChargeOverlay`; typed provider vtable slots `+0x10/+0xE0/+0xF8`; provider shared pointer; trace context | 7.23 | `pnRefCount` is a compiler HASH/SSA lifetime: decompiler resolves `int*`, but the variable API exposes stale `undefined8` storage and rejects mutation |
| `0x1403E79C0` | `void __fastcall InitializeLuxBattleManagerBeginPlayPipeline(ALuxBattleManager_BeginPlayOverlay*)` | Exact conservative 0x1640 BeginPlay overlay; 0x18-stride world/async-world entry arrays; 30+ spawned subsystem slots; world-mode shared identity; parameter-collection paths; VFxIDConvTable key; callback/task/counter storage | 3.07 | Raw `+0x388` async-hub and `+0x10` DataTable class accesses share one compiler-coalesced register lifetime and remain call-site documented |
| `0x1403F5300` | `void __fastcall HandleBattleManagerSequenceEvent(ALuxBattleManager_BeginPlayOverlay*, int*)` | Manager `+0x1490` and exact `+0xDB0` callback collection | 0 | None |
| `0x1403F51E0` | `void __fastcall DispatchBattleManagerEventE20(ALuxBattleManager_BeginPlayOverlay*, FLuxBattleManagerEventE20_Partial*)` | Exact `+0xE20` callback collection; opaque packet because this wrapper performs no field accesses | 0 | Packet fields remain intentionally opaque |
| `0x1403F5410` | `void __fastcall HandleBattleManagerAxisChangedEvent(ALuxBattleManager_BeginPlayOverlay*, byte, float, float)` | Embedded axis dispatcher at `+0x1280` | 0 | Numeric axis selector meaning remains unassigned |
| `0x1403F3080` | `void __fastcall UpdateBattleManagerEventCounters(ALuxBattleManager_BeginPlayOverlay*, FLuxBattleManagerEventCounterParam*)` | Exact 0x0C event packet and exact 0x70 counter block at `+0x15D0` | 0 | Five register SSA temporaries are structural only; numeric event-code names are intentionally conservative |
| `0x1403F41B0` | `void __fastcall AppendBattleManagerPendingByteEvent(ALuxBattleManager_BeginPlayOverlay*, byte*)` | Exact byte TArray at `+0x1470` | 0 | Two register-only resolved temporaries are structural only |
| `0x1403F5420` | `void __fastcall HandleBattleManagerD40Event(ALuxBattleManager_BeginPlayOverlay*, FLuxBattleManagerEventD40_Partial*)` | `+0xD40` collection, received flag `+0x1462`, state `+0x1594` | 0 | Packet fields remain opaque because the wrapper only forwards them |
| `0x1403F5D60` | `void __fastcall HandleBattleManagerE90Event(ALuxBattleManager_BeginPlayOverlay*, FLuxBattleManagerEventE90_Partial*)` | `+0xE90/+0xF00` collections and 60-tick counter at `+0x14F0` | 0 | Packet fields remain opaque because both broadcasts forward them unchanged |
| `0x1403A3930` | `ulonglong* __fastcall AddWeakUObjectByteTwoFloatCallback(FCallbackEntryCollection_Partial*, ulonglong*, UObject_Partial*, WeakUObjectByteTwoFloatCallbackFn*)` | Corrected `(target, byte, float, float)` callback ABI; exact 0x30 callback record; exact 0x40 entry insertion; exact byte/two-float constructing/final 14-slot vtables | 4.49 | Five scalar/destination values are register-only projections; raw entry offsets remain EOL-documented because the selected inline/heap destination is not an addressable local |
| `0x1403A3B70` | `ulonglong* __fastcall AddWeakUObjectCallbackWithBoundByte(FCallbackEntryCollection_Partial*, ulonglong*, UObject_Partial*, WeakUObjectCallbackWithByteFn*, byte)` | Separate `(target, pEvent, boundByte)` family; exact 0x30 callback record; exact bound-byte constructing/final 14-slot vtables | 4.49 | Shared `pEvent` payload remains intentionally generic; selected inline/heap destination is a register-only projection |
| `0x140399DF0` | `void __fastcall ProcessWeakCallbackEntryCompaction(FCallbackEntryCollection_Partial*, bool)` | Exact collection and entry layouts; common typed callback-object vtable with compactability predicate at `+0x30` | 3.07 | Selected entry and callback-object pointers are register-only projections; their verified offsets are EOL-documented |
| `0x1403BBE00` | `bool __fastcall TryExecuteWeakUObjectCallbackWithByte(FWeakUObjectCallbackWithByte_Partial*, void*)` | Exact bound-byte callback layout and typed invocation pointer | 8.87 | `pEvent` is deliberately shared across multiple hub payload families; callback `+0x28` remains unsupported |
| `0x1403BBE60` | `bool __fastcall TryExecuteWeakUObjectByteTwoFloatCallback(FWeakUObjectEventCallback_Partial*, byte, float, float)` | Corrected weak resolver/invoker ABI; exact callback-function signature and exact `+0x68` final vtable slot | 0 | Resolver/result values and the upper-DL phantom are register-only; callback `+0x18/+0x28` remain unsupported |
| `0x1403C4AC0` | `void __fastcall HandleBattleVFxDestroyDebrisAxisEvent(ALuxBattleVFxEventHandler_Partial*, byte, float, float)` | Exact shared player-track pairs; shared-reference controller; 0x24C track/hash overlay; 0x10 hash nodes; exact 0x18 named-effect command; typed mesh-slot group id and UTF-16 literal | 5.00 | Hash-bucket semantics beyond verified widths/selection remain conservative; downstream subsystem object is not yet fully closed |
| `0x1403C1D00` | `FLuxMoveTrackContainerSharedPtr_Partial* __fastcall GetLuxMoveTrackContainerByPlayerSide(FLuxMovePresentationTracks_Partial*, FLuxMoveTrackContainerSharedPtr_Partial*, int)` | Exact player-0/player-1 shared pointers at `+0x390/+0x3A0`; exact shared/weak controller counts | 0 | Four selection/refcount temporaries are register-only; effective completeness 100% |
| `0x1403BE320` | `FLuxMovePresentationTracks_Partial* __fastcall GetLuxMovePresentationTracksFromWorldContext(UObject_Partial*)` | Current/cached battle-manager selection; manager `+0x528` track owner; named common join label | 3.07 | Conservative UObject prototype does not claim `+0x98` for every UObject; candidate class and manager fields remain call-site documented |
| `0x1408A2520` | `void __fastcall EnqueueNamedEffectCommandForMatchingMoveSlots(FLuxMeshSlotSubsystemView*, FLuxMoveNamedEffectCommand_Partial*)` | Closed command-slot storage at `+0x3E8/+0x3F0`; exact 0xC0 slot record with typed actor, selector, match id, and unsigned group id; three exact 12-byte zero vectors | 1.45 | Slot-record bytes outside the independently used fields remain unsupported; selection and FName temporaries are register-only projections |
| `0x140898C20` | `void __fastcall ResetOrDestroyLuxMovePresentationEntries(FLuxMeshSlotSubsystemView*, bool)` | Closed presentation-entry storage at `+0x3F8/+0x400`; exact 0xC0 stride; typed actor reset fields through `+0xA10`; named in-place-removal loop | 0 | Actor virtual `+0x360` is documented by side effect but its wider vtable remains outside this overlay; effective completeness 100% |
| `0x141F75010` | `void __fastcall EnqueueLuxMoveNamedPresentationCommand(ALuxMovePresentationActor_CommandQueueOverlay*, FLuxFName_Partial, float, FVector3f_Partial*, FVector3f_Partial*, FVector3f_Partial*)` | Independent actor queue overlay at `+0x958`; exact 0x10 dynamic queue and 0x40 command record; verified type-5 payload fields | 0 | The fifth vector argument is ABI-real but unused by command kind 5; the record tail remains explicitly unknown after verified zero-initialization; effective completeness 100% |
| `0x140470470` | `void __fastcall ApplyBattleColorFadeLayerTimingUpdate(ALuxBattleColorFadeManager_Partial*, FLuxBattleColorFadeTimingUpdateEvent*)` | Exact 0x10 timing event and 0x34 queued-layer stride | 0 | Register-only layer/loop projections only; effective completeness 100% |
| `0x140480F70` | `void __fastcall ApplyBattleColorFadeMaterialParamsToPlayerLane(ALuxBattleColorFadeManager_Partial*, int, int, FLuxBattleColorFadeMaterialParams_Partial*)` | Exact 0x54 material block, sparse attached-entity storage, typed material-index arrays; corrected `int*` array-data names | 0 | Component/provider iteration temporaries remain register-only; effective completeness 100% |
| `0x140481400` | `void __fastcall ApplyBattleColorFadeParamsToMeshComponent(ALuxBattleColorFadeManager_Partial*, UMeshComponent_Partial*, FLuxInt32Array_Partial*, FLuxBattleColorFadeMaterialParams_Partial*)` | Typed mesh component/material indices and all six committed dynamic-material parameters | 3.07 | Two concrete UObject/material-interface interior fields remain conservative raw accesses with use-site documentation |
| `0x1404704F0` | `void __fastcall ApplyDisableVfxToBattleColorFadeState(ALuxBattleColorFadeManager_Partial*, FLuxDisableVFxParam*)` | Exact 0x10 disable packet, 0x20 lane and 0x18 active-entry layouts, authored 0x50 setting stride | 0 | Event keep/fadeout fields are verified unused by this subscriber; effective completeness 100% |
| `0x1404708A0` | `void __fastcall ApplyEnableVfxToBattleColorFadeState(ALuxBattleColorFadeManager_Partial*, FLuxEnableVFxParam*)` | Exact 0x4D enable packet, `ELuxEffectBankType`, six-lane jump table, active-entry and material-param layouts; exact `IMAGE_DOS_HEADER` retained as `g_abImageDosHeader` with fixed-image-base semantics | 0 | Remaining SIMD/register projections are structural; effective completeness 100% |
| `0x140470440` | `void __fastcall HandleBattleColorFadePhaseResetEvent(ALuxBattleColorFadeManager_Partial*, byte*)` | Typed phase-byte reset selector and manager reset route | 0 | None; effective completeness 100% |
| `0x140470750` | `void __fastcall HandleBattleColorFadeQueueEvent(ALuxBattleColorFadeManager_Partial*, FLuxBattleColorFadeQueueEvent*)` | Exact 0x28 queue event and 0x34 persistent queued layer; corrected pointer-array scratch naming | 0 | Sort temporaries are compiler projections; effective completeness 100% |
| `0x140471220` | `void __fastcall HandleBattleColorFadeStateResetEvent(ALuxBattleColorFadeManager_Partial*, FLuxCollection11ResetEvent*)` | Exact collection-11 reset callback ABI | 0 | Packet is intentionally field-opaque because the subscriber performs unconditional reset; effective completeness 100% |
| `0x1404007C0` | `void __fastcall HandleColorFadeLayerBroadcastFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxMoveEffect040BColorFadeRequest*)` | Exact MoveVM 0x040B request-to-callback conversion and collection-2 boundary | 0 | None; effective completeness 100% |
| `0x140400840` | `void __fastcall HandleColorFadeTimingUpdateBroadcastFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxBattleColorFadeTimingUpdateEvent*)` | Exact four-int callback copy and collection-3 offset at dispatcher `+0x158` | 0 | None; effective completeness 100% |
| `0x14044F2C0` | `void __fastcall InitializeLuxBattleColorFadeManagerBeginPlay(ALuxBattleColorFadeManager_BeginPlayOverlay*)` | New independent exact 0x3B0 BeginPlay overlay; typed cached BattleManager `+0x98`, auxiliary VFX actor `+0x538`, player states, lanes, settings, and FadePatternTex literal | 4.49 | Concrete setting/material-provider UObject classes and their virtual slots remain conservative raw accesses; register SSA projections are documented |
| `0x1403C51A0` | `void __fastcall InitializeLuxMoveEffectColorFadeAbsoluteParams(ALuxPresentationEnvelopeActor_Partial*, FLuxMoveEffectColorFadeParams_Partial*)` | Exact 0x40 absolute fade schedule at actor `+0x988` and cursor `+0x958` | 0 | Tail field remains explicitly unknown; effective completeness 100% |
| `0x1403C5080` | `void __fastcall InitializeLuxMoveEffectColorFadeFromDeltas(ALuxPresentationEnvelopeActor_Partial*, FLuxMoveEffectColorFadeParams_Partial*)` | Exact 0x40 delta schedule and neutral-one conversion for scale/color lanes | 0 | Tail field remains explicitly unknown; effective completeness 100% |
| `0x140460CD0` | `FLuxBattleColorFadeSettingEntry_Partial* __fastcall LookupBattleColorFadeSettingById(ALuxBattleColorFadeManager_Partial*, int)` | Exact 0x50 authored setting entry and manager setting-list traversal | 0 | Register-only iteration projections only; effective completeness 100% |
| `0x1403D2D10` | `void __fastcall LuxMove_Tick_EffectColorFade(ALuxPresentationEnvelopeActor_Partial*)` | Typed actor-owned fade cursor, exact schedule, interpolation, and material-slot publication state | 0 | Three scalar register projections are structural; effective completeness 100% |
| `0x140478D30` | `void __fastcall ResetBattleColorFadePlayerState(ALuxBattleColorFadeManager_Partial*, int)` | Exact 0x28 player state and 0x20 lane reset/array teardown paths | 0 | Reset worker temporaries are register-only; effective completeness 100% |
| `0x1403C5170` | `void __fastcall SetActiveLuxMoveEffectColorFadeDurations(ALuxPresentationEnvelopeActor_Partial*, FLuxMoveEffectColorFadeDurations_Partial*)` | Exact duration subset applied to the active actor schedule | 0 | None; effective completeness 100% |
| `0x1403A34F0` | `bool __fastcall SetupChara006WeaponAndInnerChestMaterial(ALuxBattleChara_Chara006WeaponSetupOverlay**)` | Exact 0x3C0 actor creation-component overlay; exact 0x918 weapon-mesh overlay; exact identity-component FName at `+0x18`; three authored component/material-name globals | 0 | One compiler structural temporary remains; it does not affect the always-true return ABI |
| `0x14260EBA0` | `UClassCastMetadata_Partial* __fastcall GetMaterialInterfaceClassMetadata(void)` | `g_pMaterialInterfaceClassMetadata`; exact `wchar_t[20]` class-name storage plus documented interior suffix; exact two-entry native-function table | 8.00 | The `MaterialInterface` suffix is an interior label at base `+2`; Ghidra cannot type it independently without destroying the authoritative overlapping `wchar_t[20]` array. Six registration staging values are register-only compiler artifacts |
| `0x14260B040` | `void __fastcall InitializeMaterialInterfaceClassDefaults(UObject_Partial**)` | Typed class-default UObject slot and shared UE initializer tail-call | 0 | None |
| `0x14260FAD0` | `void __fastcall RegisterMaterialInterfaceNativeFunctions(void)` | Exact `FNativeFunctionRegistrationEntry[2]` table | 0 | One register-only metadata-return temporary is structural only |
| `0x1403095B0` | `bool __fastcall ApplyOrClearBattleCharaSwayBoneParameters(LuxNativePlayerSwayBoneOverlay_Partial*, bool)` | Exact 0x70 descriptor and 0x06 linked-runtime layouts; exact 0x1C source record with seven sink-based field names; exact four-index rebuild packet; typed bone-data bank and move-data-table header; exact 765-record capacity; exact R1/R2/L1/L2 name table | 8.00 | Two Ghidra-created interior array labels remain intentionally untyped because independently defining their overlapping storage would destroy the authoritative `char*[4]`/`char[16]` arrays. Remaining variables are register/SSA artifacts documented in the plate |
| `0x1402D5C90` | `void __fastcall ApplySwayBonePresentationModeForBattleSlot(FLuxBattleSlotLookup*, bool)` | Exact 0x10 lookup context and typed native PLAYER selection boundary | 0 | One register-only resolved BST-entry pointer is structural only |
| `0x1402DA710` | `void __fastcall LuxBattle_ReinitCharaSlotForMove_SubsequentRound(int, FLuxBattleCharaInitMoveEntry_SubsequentRound_Partial*)` | Exact 0x158 subsequent-round entry; corrected exact 0x80 load packet and 0x60 nested payload; two exact 0x1C inline sway sources; exact 0x68 three-lane KHit output control; exact `int *[107]` move-slot table; exact two-lane 0x4000 KHit scratch storage | 8.00 | Two character-map interior member addresses inherit type from the authoritative exact 41-entry array but the global auditor reports them as untyped; remaining locals are register/SSA projections documented in the plate |
| `0x1403CFAC0` | `void __fastcall InitializeLuxBattleCharaSlotSubsequentRoundWrapper(void*, int, FLuxBattleCharaInitMoveEntry_SubsequentRound_Partial*)` | Direct table-wrapper ABI and exact setup-entry handoff | 8.87 | Unused dispatch context has no evidence-backed concrete type and remains `void*`; no contradictory cast reaches the target |
| `0x140312040` | `void __fastcall LoadBattleCharaMovesetEntriesBonesAndJapaneseVtb(ALuxBattleChara_Partial*, FLuxMoveDataLoadPacket_Partial*)` | Corrected exact 0x80 packet with exact 0x20 header and 0x60 move-data payload; authored 0x70 bone records; `dwPrimaryMatrixCount` writer; documented exact `Base`/`Hara` bone-name globals | 1.93 | One raw secondary-file header `+0x0C` access remains use-site documented; register-heavy loader staging remains structural |
| `0x14030B630` | `void __fastcall AdvanceCMatrixBankRingBuffer(CMatrixBankHeader_Partial*)` | Exact 0x38 header and verified `0 -> 2 -> 1 -> 0` current/previous slot rotation | 0 | None; effective completeness 100% |
| `0x14030B660` | `FMatrix64* __fastcall GetCMatrixBank32PreviousBoneMatrix(CMatrixBankHeader_Partial*, uint)` | Exact previous-slot selector and 0x40 matrix stride for the 32-matrix secondary bank | 0 | None; effective completeness 100% |
| `0x14030B680` | `FMatrix64* __fastcall GetCMatrixBank32CurrentBoneMatrix(CMatrixBankHeader_Partial*, uint)` | Exact current-slot selector and 0x40 matrix stride for the 32-matrix secondary bank | 0 | None; effective completeness 100% |
| `0x1402D2DC0` | `uint __fastcall GetPlayerPrimaryBoneMatrixCount(FLuxPlayerIndexCallbackContext_Partial*)` | Typed player selection and load-time `dwPrimaryMatrixCount` at PLAYER `+0x42550` | 0 | None; effective completeness 100% |
| `0x1402D2DE0` | `uint __fastcall CopyAllPlayerPrimaryBoneMatrices(FLuxPlayerIndexCallbackContext_Partial*, FMatrix64*)` | Typed PLAYER table, primary bank at `+0x35A0`, exact current-slot virtual and full advertised-count 0x40 matrix loop | 0 | Nine copy/register projections are structural; effective completeness 100% |
| `0x1402D2E70` | `uint __fastcall CopyPlayerBoneMatricesFromPrimaryBank(FLuxPlayerIndexCallbackContext_Partial*, FMatrix64*, uint, uint)` | Typed bounded-range copy from the current primary bank with exact matrix stride | 0 | None; effective completeness 100% |
| `0x1403D0590` | `void __fastcall ALuxBattleChara_TickActor(ALuxBattleCharaActorPresentation_Partial*, float, uint, FActorTickFunction_Partial*)` | Exact 0x618 actor presentation overlay; exact appendix actor/animation maps; typed queued-float delegate, AnimInstance refresh, Lux matrix/provider, root/hair/appendix transform, Soul Charge, visibility, and base-tick ownership boundaries | 5.80 | Large remaining SIMD/register projections are structural. The mixed world-mode/player-info scratch lifetime and two appendix pose-provider vectors remain explicitly conservative |
| `0x14047D0A0` | `void __fastcall SetScbattleAnimNodeWorldModeProvider(FLuxCharaAnimNode_SCBattle_Partial*, FScbattleWorldModeSharedPtr*)` | Exact 0x68 custom node, exact 0x10 shared world-mode pair, strong/weak control-block ownership | 0 | Three refcount pointer projections are register-only |
| `0x14047FBF0` | `void __fastcall BindScbattleAnimNodeToPlayerAndSkeleton(FLuxCharaAnimNode_SCBattle_Partial*, int, bool, TArrayHeader*)` | Exact `LuxSkeletonInterface_Partial` and 0x30-byte typed vtable; separate character/weapon bone-name and bone-count callbacks; exact bone-map/front-hair fields; both authored front-hair globals | 0 | Scalar loop/call-result projections are structural; interface vtable slots not used by this function remain unknown |
| `0x140480C50` | `void __fastcall RefreshScbattleAnimNodeBindings(ULuxCharaAnimInstance_Partial*, int, bool)` | Exact 0x4A0 Lux AnimInstance, proxy/node list, embedded 0x68 node, world-mode shared ownership, conservative UObject/UClass cast overlays | 1.93 | One register lifetime is coalesced between expected UClass metadata and a later node-list result; `+0x88/+0x90` are EOL-documented instead of forcing a contradictory type |
| `0x1409EA520` | `void __fastcall InvokeAppxAnimationStateChange(ULuxCharaAppxAnimInstance_Partial*, FAppxMeshAnimationParam_Partial*)` | Exact 0x0C threshold/event payload and reflected `OnChangeAnimationState` FName | 0 | Copy-loop and UFunction temporaries are register-only |
| `0x1409EA590` | `void __fastcall InvokeAppxTargetModeValueChange(ALuxCharaAppxActor_Partial*, int)` | Exact appendix actor and reflected `OnTargetModeValueChange` FName | 0 | One UFunction result projection is register-only |
| `0x141CA1B00` | `float __fastcall PlayAnimInstanceMontage(UAnimInstance_MontagePlayOverlay*, UAnimMontage_Partial*, float, byte, float)` | Independent exact 0x378 play overlay with current skeleton `+0x30`, active instances `+0x80`, lookup map `+0x90`, root-motion owner `+0x338`; exact montage and 0x190 instance layouts | 0 | Five allocation/return/control projections are register-only |
| `0x141CA2210` | `void __fastcall SetAnimInstanceMontagePosition(UAnimInstance_MontagePartial*, UAnimMontage_Partial*, float)` | Both all-active and selected-map paths now use `FAnimMontageInstance_Partial*`; position `+0x100`, marker caches `+0x98/+0xA0`, conservative bulk-seek eligibility float `+0x130` | 0 | The semantic UE name of `+0x130` is not proven; only its nonzero eligibility behavior is named. Three loop/map projections are register-only |
| `0x141DAEFF0` | `void __fastcall TickSkeletalMeshAnimation(USkeletalMeshComponent_AnimationPartial*, float, bool)` | Exact 0xFF0 skeletal-mesh animation overlay and `GlobalAnimRateScale +0xB60`; main/linked/post-process AnimInstance tick fan-out | 0 | Three indirect-call projections are register-only |
| `0x141DAE0A0` | `void __fastcall SetSceneComponentWorldTransform(USceneComponent_WorldTransformOverlay*, FTransform48*, bool, FHitResult136_Partial*, byte)` | Exact scene-component attachment/socket/absolute-channel overlay; current-parent socket query; world-to-relative transform conversion | 8.00 | Register-only transform math is structural; no unresolved ABI or field offset remains |
| `0x141DACB80` | `void __fastcall SetSceneComponentRelativeTransform(USceneComponent_WorldTransformOverlay*, FTransform48*, bool, FHitResult136_Partial*, byte)` | Typed relative location/quaternion extraction and separate scale virtual at `+0x3A8` | 0 | None |
| `0x141DABF80` | `void __fastcall SetSceneComponentRelativeLocationAndRotation(USceneComponent_WorldTransformOverlay*, FVector3f_Partial*, FVector4*, bool, FHitResult136_Partial*, byte)` | Exact movement arguments; typed FTransform/FMatrix/FVector temporaries; parent/socket composition; movement virtual `+0x438`; 12 exact SIMD globals; documented Y/Z interior element references without overlapping data | 8.00 | Ghidra still audits the two interior references of authoritative `g_flUnitScaleXYZ0: float[4]` as untyped pseudo-globals. Defining overlapping element data would destroy the required exact array; all remaining scalar names are register/SSA transform lanes |
| `0x141D94E10` | `FTransform48* __fastcall ComputeSceneComponentWorldTransformFromParent(USceneComponent_WorldTransformOverlay*, FTransform48*, FTransform48*, USceneComponent_WorldTransformOverlay*, FLuxFName_Partial)` | Correct five-argument Win64 ABI including stack-passed socket FName; exact parent-socket FTransform, composed FMatrix, quaternion normalization, and absolute-channel restoration | 0 | Thirty-seven compiler-expanded scalar lanes are structural only; effective completeness 100% |
| `0x1403D3030` | `void __fastcall UpdateSceneComponentToWorld(USceneComponent_WorldTransformOverlay*, byte, byte)` | Corrected scene-component owner; relative Euler cache at `+0x2CC/+0x2F0`, cached quaternion at `+0x2E0`, ComponentToWorld at `+0x270`; exact 180/360-degree SIMD globals | 5.00 | Remaining deductions are compiler projections |
| `0x141DB0600` | `void __fastcall RecomputeSceneComponentToWorldFromParent(USceneComponent_WorldTransformOverlay*, USceneComponent_WorldTransformOverlay*, FLuxFName_Partial, byte, FVector4*, byte)` | Exact 0x3E0 scene-component transform overlay; relative location `+0x2C0`, cached relative rotation `+0x2E0/+0x2F0`, relative scale `+0x300`, ComponentToWorld `+0x270`, attachment/socket fields, scoped-movement TArray; `FTransformMatrix64_Union` | 0 | Twenty-seven scalar SIMD lane projections and one high-register input are structural artifacts; effective completeness 100% |
| `0x141C15210` | `bool __fastcall AreTransformsNearlyEqual(FTransform48*, FTransform48*, float)` | Exact translation/rotation/scale comparison; runtime absolute-value mask `g_adFloatAbsoluteValueMask: uint[4]`; quaternion `q/-q` equivalence | 0 | Scalar quaternion lanes and MOVMSKPS input/output projections are register-only artifacts |
| `0x141DA79D0` | `void __fastcall PropagateSceneComponentTransformUpdate(USceneComponent_TransformPropagationOverlay*, bool, uint, byte)` | Exact 0x3E0 propagation overlay; attach-child and scoped-movement TArray headers; exact `FScopedMovementUpdate_Partial` flag field at `+0x10` | 0 | Upper seven bytes paired with the teleport enum are a register-only artifact |
| `0x141C2A1D0` | `bool __fastcall SetActorTransform(AActor_Partial*, FTransform48*, bool, FHitResult136_Partial*, byte)` | Typed root component, FTransform validation masks, exact 0x88 hit-result storage, scene-component world-transform call | 8.09 | FHitResult internals remain intentionally opaque; runtime-filled non-finite mask is statically zero-filled but exactly typed as `byte[16]` |
| `0x141C229E0` | `bool __fastcall ApplyActorTransformBlueprintSweepPolicy(AActor_Partial*, FTransform48*, bool, FHitResult136_Partial*, bool)` | Blueprint/K2 sweep-hit-result policy and tail-jump return ABI | 0 | One normalized-boolean register projection cannot be renamed; structural only |
| `0x1403CCFD0` | `void __fastcall ConvertFMatrixToTransform(FTransform48*, FMatrix64*)` | Exact 0x40 matrix and 0x30 transform layouts; typed quaternion/translation/scale vectors; exact determinant/sign, half, epsilon, and normalization SIMD globals | 7.23 | Ghidra splits the M30/M31/M32 translation copy into compiler-created float-bit projections. `translationXBits`, `translationYBits`, and `translationZBits` are documented from MOVSS/UNPCKLPS; the conflicting HASH storage prevents a stable decompiler float type for the Z projection |
| `0x1403CF790` | `void __fastcall InitializeBattleCharaProviderAndQueueSetupCallbacks(ALuxBattleChara_Partial*, ULuxBattlePlayerSetup_Partial*, int, LuxorOpcodeHandlerDelegateStoragePartial*)` | Corrected provider/setup owner; exact actor callback array at `+0x548`; exact 0x50 entries; exact 0x10 and 0x50 callback-target ownership; both exact 14-slot vtable bases; moved completion-delegate storage | 9.93 | Setup vslot `+0x230` returns an opaque allocation that is only freed, so `providerTemporaryStorage` intentionally remains `void*`. Delegate-target destroy vslot `+0x10` and one raw payload access are register-only/use-site-documented projections |
| `0x140455D00` | `int __fastcall SpawnOrReuseDemoHumanForPlayerSetup(ALuxDemoHumanManager_Partial*, ULuxBattlePlayerSetup_Partial*, ALuxBattleChara_Partial*)` | Exact 0x530 manager, 0x30 spawn parameters, 0x18 byte-buffer reference control, 0x20 completion target, exact two-slot/14-slot vtables, typed class cache and reflected class-name storage | 8.00 | Class hierarchy `UClass +0x30` remains outside the conservative class overlay; fifteen decompiler register/SSA projections are structural |
| `0x1404483F0` | `void __fastcall ProcessDemoHumanCreationCompletion(FLuxDemoHumanSetupCompletionContext_Partial*)` | Exact 0x18 capture, typed manager/world-mode crossing, exact 0x60 player-index-to-FName map and 0x18 entry, completion-task/listener ownership | 7.23 | Actor event `0x1276` has no recovered authored symbolic name; one stack home is compiler-reused for the world-mode guard and later tag payload |
| `0x140450470` | `void __fastcall InvokeDemoHumanCompletionDelegateThunk(FLuxDemoHumanSetupCompletionContext_Partial*)` | Exact completion-context tail handoff | 0 | Exact five-byte JMP thunk; no state |
| `0x140A4C9D0` | `void __fastcall InvokeReflectedDemoHumanCreation(ALuxDemoHumanManager_Partial*, FFrame_Partial*, int*)` | Typed reflected player-setup and optional actor stack arguments; exact integer result storage | 0 | One compiler-projected stack home is structural; effective completeness 100% |
| `0x140A21D60` | `UClass_Partial* __fastcall GetLuxDemoHumanManagerClassMetadata(void)` | `g_pLuxDemoHumanManagerClass`; exact 0x530 reflected size; exact class-name storage and registration callback | 8.00 | One overlapping interior wide-string view cannot receive an independent listing type; three dependency returns are register-only projections |
| `0x140A1F520` | `void __fastcall ProcessLuxDemoHumanManagerRegistrationCallback(void**)` | Leading registration target pointer and shared Lux actor constructor handoff | 0 | Registration context beyond its leading target remains opaque |
| `0x140455B90` | `void __fastcall InitializeDemoHumanActorAfterSetupReady(ALuxDemoHumanActorPresentation_Partial*)` | Exact 0x620 derived actor; exact 0xE2 setup/playback suboverlay; typed manager `+0x570`, player setup `+0x600`, playback/creation flags `+0x614/+0x615`, exact eight-byte tail | 0 | One resolved manager temporary is register-only; effective completeness 100% |
| `0x140456050` | `int __fastcall CreateDemoHumanFromProfile(ALuxDemoHumanManager_Partial*, ULuxCreationProfile_Partial*, ALuxBattleChara_Partial*)` | Exact reflected profile-to-setup wrapper and player-index return | 10.00 | Exact reflected name intentionally forms a token subset of the tagged variant |
| `0x140456080` | `bool __fastcall CreateDemoHumanFromProfileTagged(ALuxDemoHumanManager_Partial*, FLuxFName_Partial, ULuxCreationProfile_Partial*, ALuxBattleChara_Partial*)` | Exact eight-byte tag ABI and reflected profile-to-setup wrapper | 10.00 | Exact reflected name intentionally collides by token subset with the untagged variant |
| `0x1404560C0` | `bool __fastcall CreateDemoHumanTagged(ALuxDemoHumanManager_Partial*, FLuxFName_Partial, ULuxBattlePlayerSetup_Partial*, ALuxBattleChara_Partial*)` | Exact 0x50 tag-to-player map, exact 0x60 player-to-tag map, both exact 0x18 entries, exact eight-byte insertion scratch | 10.00 | Exact reflected name intentionally collides by token subset with the profile-tagged variant |
| `0x140456290` | `void __fastcall DestroyDemoHumanForPlayerIndex(ALuxDemoHumanManager_Partial*, int)` | Typed world-mode shared ownership, world-mode removal slot, conservative UObject/class identity overlay, player/actor map and completion-task cleanup | 0 | Five call/register projections are structural; effective completeness 100% |
| `0x140456390` | `void __fastcall RemoveExistingDemoHumanForTag(ALuxDemoHumanManager_Partial*, FLuxFName_Partial)` | Exact bidirectional tag/index map lookup and erase ordering | 0 | Three map-iteration projections are structural; effective completeness 100% |
| `0x1404865B0` | `void __fastcall ALuxDemoHumanActor_TickActor(ALuxDemoHumanActorPresentation_Partial*, float, uint, FActorTickFunction_Partial*)` | Exact 0x620 derived actor; exact setup/playback and base-presentation suboverlays; exact player-tag readiness byte; exact seven-entry playback-state switch table; typed tick context | 6.93 | Resolved auxiliary event target interface remains wider than the proven virtual `+0xE0`; remaining scalar/refcount values are register/SSA projections |
| `0x140A21C80` | `UClass_Partial* __fastcall GetLuxDemoHumanActorClassMetadata(void)` | `g_pLuxDemoHumanActorClass`; exact 0x620 reflected size; exact class-name storage and registration callback | 8.00 | One overlapping interior wide-string view cannot receive a second listing type; registration dependencies are register-only |
| `0x140A1F500` | `void __fastcall ProcessLuxDemoHumanActorRegistrationCallback(void**)` | Leading registration target pointer and shared Lux actor constructor handoff | 0 | Registration context after its leading target stays opaque |
| `0x1403B5940` | `bool __fastcall SetupBattleCharaWeaponCallbackThunk(ALuxBattleChara_Chara006WeaponSetupOverlay**)` | Typed tail-thunk handoff to the exact Chara006 weapon/material setup overlay | 0 | None; effective completeness 100% |
| `0x1403B5E40` | `bool __fastcall InvokeBattleCharaWeaponEquipCallbackThunk(FLuxWeaponEquipCallbackContext_Partial*)` | Typed weapon-equip callback context and tail-thunk ABI | 0 | None; effective completeness 100% |
| `0x1403BE870` | `ALuxBattleAuxEventActor_Partial* __fastcall GetLuxBattleVFxAuxEventActorFromContext(ALuxBattleVFxEventHandler_Partial*)` | Typed handler, current BattleManager lookup, and auxiliary VFX actor ownership | 3.87 | One current-world/context result remains a compiler projection |
| `0x1403BFDF0` | `byte __fastcall GetCurrentLuxVfxRemapMode(void)` | Typed current BattleManager/configuration lookup and byte return ABI | 1.93 | One register-only manager/configuration projection remains |
| `0x1403C5250` | `void __fastcall HandleDisableVfxEvent(ALuxBattleVFxEventHandler_Partial*, FLuxDisableVFxParam*, bool)` | Exact disable packet; optional current move-id resolution; both persistent presentation-slot tables and Blueprint publication boundary | 2.29 | Selected live-slot/register values remain compiler projections |
| `0x1403C7070` | `void __fastcall HandleCollection11VfxStateClear(ALuxBattleVFxEventHandler_Partial*, FLuxCollection11ResetEvent*)` | Exact collection-11 packet and state-clear tail route | 0 | Event contents are intentionally ignored after the forced zero selector; effective completeness 100% |
| `0x1403FFFF0` | `void __fastcall BroadcastHitCategoryVfxFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxHitCategoryVfxRequest*)` | Exact hub request conversion and callback-collection boundary | 2.29 | One converted-packet register projection remains |
| `0x140400240` | `void __fastcall BroadcastWeaponNodeAlphaChangeFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxWeaponNodeAlphaChangeEvent*)` | Exact weapon-node alpha packet and collection offset | 0 | None; effective completeness 100% |
| `0x1404002C0` | `void __fastcall BroadcastMoveCommandSlotCommitFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxMoveCommandSlotCommitEvent*)` | Exact command-slot commit packet and collection offset | 0 | None; effective completeness 100% |
| `0x1404003F0` | `void __fastcall BroadcastMoveEffect2AFAFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxMoveEffect2AFARequest*)` | Exact MoveVM request conversion and callback-collection boundary | 2.29 | One converted-packet register projection remains |
| `0x140400540` | `void __fastcall DispatchSecondaryVfxRequestFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxSecondaryVFXDispatchRequest_Partial*)` | Typed secondary VFX packet and direct subscriber boundary | 0 | None; effective completeness 100% |
| `0x140400590` | `void __fastcall BroadcastDisableVfxFromLuxEventHub(FLuxVfxDispatcher_Partial*, FLuxDisableVFxParam*)` | Exact disable packet and collection-10 broadcast boundary | 2.29 | One callback-collection register projection remains |
| `0x1409A9920` | `void __fastcall InvokeReceiveEnableVFxBlueprintEvent(FLuxSituationDescriptorOwnerView*, FLuxSecondaryVfxCallbackPacket_Partial*)` | Exact callback packet copy and reflected `ReceiveEnableVFx` ProcessEvent boundary | 0 | None; effective completeness 100% |
| `0x1403CD570` | `void __fastcall ApplySwayBonePresentationModeViaPlayerInfoWrapper(ScbattlePlayerInfoHandlerWrapper_Partial*, bool)` | Typed player-info wrapper and native secondary-sway presentation virtual | 0 | None; effective completeness 100% |

### Corrected conclusions from this cleanup batch

- Character-cue handling performs synchronous semantic work before audio playback: it resolves current player/style/provider state and remaps the cue index. Deferring the whole subscriber as terminal audio changes when that semantic lookup occurs.
- VFX handler `BeginPlay` does more than subscribe. Its manager cleanup delegates destroy tracked ids across two manager pools and clear/release the handler-owned id arrays; these ids are persistent presentation ownership state.
- Sound handler `BeginPlay` registers twelve semantic callbacks, loads authored conversion tables, starts an async `BattleData` request, and initializes seven active-voice tracking sets. The `BattleData` callback extracts the `battleName` field into handler storage; it is lifetime/configuration state, not a terminal sound command.
- `RefreshBattleCharaSoulChargePresentationState` is mixed ownership. Mesh visibility, weak-component material updates, selected-entity arrays, Maegami, and trace activation are UE presentation. The normal-player path then calls player-info wrapper `+0x268`, which mutates native `OPPAI_*` secondary-sway descriptors and resynchronizes that solver. Blanket classification as disposable VFX is therefore incorrect.
- The Soul Charge provider crossing is now represented as an exact 0x10-byte shared pointer whose provider vtable has typed slots at `+0x10`, `+0xE0`, and `+0xF8`. This removed the former raw virtual-offset interpretation without pretending that unrelated vtable gaps are known.
- Battle-manager BeginPlay creates a large process-local ownership graph: world-mode identity, subsystem actors, dependency edges, weak callbacks, authored material collections, and async task records. It is match/world lifetime state and cannot be reconstructed by restoring serialized pointers or by invoking BeginPlay during reconciliation.
- Several BeginPlay-installed callbacks are semantic manager transactions, not presentation endpoints. They append ordered bytes, mutate sequence/counter/flag state, and broadcast through multiple live collections with observable ordering. In particular, the `+0xE90` callback performs `E90 broadcast -> set 60-tick counter -> F00 broadcast`; collapsing or deferring that sequence changes what subscribers observe.
- `DestroyDebris` is not issued as an immediate particle-off call. The axis callback first fans a type-5 named command into every matching actor-local queue at actor `+0x958`, then synchronously deactivates actors, prunes weak registration ids, and removes 0xC0 presentation entries in place. Correct rollback handling must preserve that enqueue-before-topology-removal order and the actor/slot ownership observed at the event boundary.
- The color-fade corridor is now type-closed across its hub adapters, persistent manager lanes/queues, actor-owned tick schedule, and terminal material writes. Its BeginPlay-only overlay proves that callback registration and authored asset/default resolution are tied to the cached BattleManager and current UObject graph; replaying a source packet later does not recreate the source-frame lane, queue, component, or material ownership.
- The matrix-bank API distinction is now closed in the ledger: the root publication path reads matrix 0, but `CopyAllPlayerPrimaryBoneMatrices` copies every load-time-advertised matrix and performs no 97-matrix clamp. The rollback prefix is therefore sufficient for root publication but remains an unvalidated width contract for full skeleton/attachment consumers.
- `ConvertFMatrixToTransform` copies the native-to-UE render matrix translation row directly from M30/M31/M32 into `FTransform48.translation`; the remaining undefined-looking scalars are Ghidra HASH/SSA float-bit projections, not evidence of integer coordinates or an alternate axis conversion.
- Battle-character provider initialization owns three deferred 0x50-byte callback entries, not two. Provider vslot `+0x230` also returns an opaque allocation that is freed immediately; no persistent provider object or rollback state can be inferred from that temporary. The third entry owns a moved completion delegate and preserves the arbitrary-code/lifecycle boundary.
- The former `LuxMoveVM_Tick_ProcessInputAndDispatch` identification at `0x1404483F0` was wrong. Reflected registration, the exact completion-target capture, and the sole thunk caller prove that it completes `LuxDemoHumanManager::CreateDemoHuman` after three ordered provider/setup callbacks. It resolves the current weak manager and world-mode, mutates manager maps/tasks/listeners, creates the demo-human info handler, and publishes actor event `0x1276`; it is a deferred UE/Lux lifecycle transaction, not a per-frame MoveVM or terminal VFX boundary.
- Tagged demo-human ownership is bidirectional and persistent: manager `+0x410` maps exact FName tags to player indices, while `+0x460` maps player indices back to tags. The ready callback forwards the recovered tag with the player event; it does not retrieve a shared-reference guard from that map. Replacing, destroying, or completing a tagged actor therefore has observable map-erasure and listener-payload ordering.
- The derived actor's readiness gate at `0x140455B90` runs inside `ALuxDemoHumanActor_TickActor`. Once async setup is complete it reuses the actor in the manager transaction, sets the creation latch, and wires reciprocal shared ownership. Treating this as a generic move-provider or effect producer would miss both the derived-tick timing and the manager/actor lifetime mutation.
- `ALuxDemoHumanActor_TickActor` is a distinct `0x620`-byte derived-actor producer, not an alias of the `0x618` base battle-character overlay. Its registration, animation-track publication, authored playback-state decoding, weak-target event lanes, and visibility updates all execute before the base TickActor tail-call. Any rollback gate or reconciler placed only at the base tick boundary is therefore downstream of these mutations.
- The callback family formerly described as an unbound generic event packet is specifically `(UObject*, byte, float, float)`. The registration helper, weak executor, and final vtable slot all agree with `HandleBattleManagerAxisChangedEvent`; it must not be modeled as an arbitrary packet callback. The bound-byte family remains separate and genuinely forwards `(target, pEvent, boundByte)`.
- The VFX handler's selector-4/current-zero callback is a mixed presentation-ownership transaction, not a terminal debris visual. It reads both player-side shared track containers, requires track type `0x38` in phase state 1 or 2, emits the exact `DestroyDebris` named-effect command, and immediately enters the presentation subsystem's destructive cleanup path. Delaying this event can preserve actors, shared references, and track-owned state past the source frame, while world-context re-resolution can redirect it to a replacement battle manager.
- `SetupChara006WeaponAndInnerChestMaterial` is actor-lifetime presentation setup: it registers weapon traces, publishes style-related weapon identity, scans creation components by FName, synchronously resolves an authored `UMaterialInterface`, and assigns it through the matched component's material virtual. It always returns `true`; the return does not certify that the component or material lookup succeeded. Reinvoking it during rollback reconciliation can duplicate trace registration and repeat UObject/class/material setup.
- The material-class leg is now closed through `GetMaterialInterfaceClassMetadata`, its class-default initializer, and the exact two-entry native-function registrar. All 25 direct callers re-decompile with the return used as UE class metadata or as a material-class argument. This chain is one-time reflection state, not deterministic Lux battle state and not a safe reconciliation primitive.
- The secondary-sway source records at native PLAYER `+0x2B4C4` are not sourced from Unreal. The subsequent-round character initializer at `0x1402DA710` copies two exact 0x1C blocks from the current native move-table entry, while the constructor installs native defaults. The active/clear callbacks only project those restored/native authored values into four named 0x70-byte runtime descriptors and rebuild the solver.
- The subsequent-round entry boundary is broader than the sway copy. Its corrected exact `0x80` load packet includes a `0x60` move-data payload, and the initializer then installs AI/MoveVM state, motion and move banks, three KHit lists, three VTB stacks, and three LPD banks. Treating the two sway records as an isolated UE effect payload would discard their native content/load ordering and the surrounding move-entry ownership.
- This sharpens the Lux↔Unreal boundary: UE Soul Charge presentation can trigger the active/clear request through the player-info wrapper, but the data and mutation on the far side are native Lux secondary-motion state. Restoring UE mesh/material state alone cannot repair it; a rollback reconciler needs the intended active/clear mode applied after native move-table restoration, without rerunning actor lifetime setup.
- The SCBattle AnimGraph binding corridor is now closed through an exact typed skeleton-interface vtable. Character and weapon paths have distinct bone-count and bone-name callbacks, but both only rebuild node-local Lux-to-UE bone-index mappings and provider ownership. Neither path advances an AnimInstance clock or repairs montage state.
- Montage play validates the AnimInstance's current skeleton at `+0x30` against the montage skeleton before allocating the 0x190-byte instance. The bulk position setter also skips active instances whose conservative `+0x130` float is zero. Therefore a confirmed-state montage policy cannot be reduced to writing every instance's `+0x100` position: current skeleton ownership, active-instance eligibility, marker invalidation, lookup-map membership, delegates, and root-motion ownership are part of the UE transaction.
- The former `LuxChara_UpdateAimAngles_Normalized` identification at `0x1403D3030` was wrong. Its broad callers, exact offsets, and downstream parent-aware recomputation prove it is Unreal scene-component `UpdateComponentToWorld` behavior: it normalizes cached relative Euler degrees, rebuilds the cached quaternion, and recomputes ComponentToWorld through the live attachment hierarchy.
- `SetActorTransform` is not a terminal state overwrite. It validates the incoming FTransform, then routes through world-to-relative conversion, current parent/socket lookup, relative location/rotation movement, and a separate scale virtual. Even with `fSweep=false`, the final location/rotation leg invokes component movement virtual `+0x438`, while the scale leg invokes `+0x3A8`; engine child propagation and transform-side effects remain live.
- The TickActor call at `0x1403D0823` therefore consumes both restored Lux matrix-bank output and current Unreal attachment state. Reapplying the same nominal world FTransform after attachment/socket changes is not guaranteed to reproduce the same relative state, so rollback reconciliation must treat attachment ownership and ordering as part of the Lux-to-Unreal synchronization boundary.
- The corrected five-argument ABI of `ComputeSceneComponentWorldTransformFromParent` proves the socket FName is explicitly passed in the fifth Win64 stack slot. It is not stale stack data or an inferred field-only lookup. Absolute location, rotation, and scale bits are restored only after parent/socket composition.
- `RecomputeSceneComponentToWorldFromParent` publishes `ComponentToWorld` only when `AreTransformsNearlyEqual` rejects the cached value; the comparison treats quaternion `q` and `-q` as the same rotation. This makes transform publication and downstream fan-out conditional on the current cached Unreal state, rather than an unconditional projection of Lux output.
- Scene-component propagation has a second timing boundary after publication. With no active scoped movement update it immediately refreshes bounds, render/physics state, and attached children. With a live scope it suppresses that fan-out; teleport mode 1 instead sets bit `0x04` in the newest `FScopedMovementUpdate` record. A rollback path that invokes the same transform setter under a different scoped-movement ownership state can therefore produce different side-effect timing even when the resulting transform is identical.

## Change log

### 2026-08-04 — initial report

- Recorded the event-hub object model and the central boundary error.
- Added verified VFX-handler and sound-handler registration maps.
- Recorded the audio late-resolution defect, `+0x08` overread, shared-vtable risk, and accounting limitations.
- Recorded recovered functions, structures, and Ghidra documentation changes.
- Added the static follow-up investigation queue.

### 2026-08-04 — continued subscriber pass

- Proved collection 25 initializes persistent, tick-consumed color-fade state.
- Corrected its callback ABI from 0x3D to 0x40 and isolated indeterminate native padding.
- Reclassified slot `+0x100` as weapon-node alpha/visibility and recovered its live-state subscriber.
- Added the associated event and actor overlay types and Ghidra documentation.

### 2026-08-04 — collection-0 allocation pass

- Traced the secondary-VFX subscriber through current-state move/transform resolution and definition lookup.
- Recovered the particle-system and pooled ground-debris allocation branches.
- Proved both branches interleave terminal activation with persistent slot/delegate bookkeeping.
- Identified the post-allocation reflected `ReceiveEnableVFx` publication.
- Added the request/configuration, live actor, and persistent slot-record structures and documented the resolver, both allocators, lifecycle callback, and Blueprint wrapper in Ghidra.
- Completed the ground-debris allocator's variable/type audit and annotated its exact 0x50-byte owned-entry cleanup stride, activation flag, callback-name derivation, and registration failure path.
- Verified Ghidra completeness at 97% effective for the fallback allocator (two unfixable register-projected temporaries) and 100% for its lifecycle thunk.

### 2026-08-04 — collection-0 color-fade subscriber pass

- Replaced the generic 0x4D callback packet with the exact dumped `FLuxEnableVFxParam` and recovered the complete `ELuxEffectBankType` lane mapping.
- Expanded collection 0 from one known native subscriber to two: VFX actor allocation/Blueprint publication and persistent battle-color-fade state/material mutation.
- Proved that the color-fade subscriber consumes only `ID`, `BankType`, and `FollowPlayerNo`, but consults current setting assets, lane contents, weak component graphs, weapon state, material slots, and manager defaults.
- Traced first-entry initialization through attached-entity/weapon selection to committed `FadeType`, `FadeMidPoint`, `FadeMidValue`, `FadeOuterExp`, `FadePatternTex`, and `FadeTexParam` writes.
- Found a direct capture defect in HorseMod: collection 0 performs a guarded 0x50-byte read from an exact 0x44-byte request and persists the false 0x50 ABI in its journal record.

### 2026-08-04 — collection-4 positional sound pass

- Audited the rollback implementation's weak areas and ranked the blanket event-hub boundary, upstream audio hooks, incomplete semantic payloads, missing manager snapshots, request ABI assumptions, coverage gaps, shared-vtable lifetime assumptions, and accounting-only exactly-once status.
- Proved slot `+0x28` has a correct 0x28-byte source ABI but converts to a separate exact 0x18-byte callback value.
- Reclassified the route from positional VFX to a mixed positional contact-sound transaction.
- Traced the subscriber through current authored/player/style state, listener-relative `Pan`/`Distance`, terminal active-voice registration, reconciliation of seven persistent 0x50-byte tracking sets, and reflected `ReceivePlaySE` publication.
- Recovered and propagated the associated callback, manager audio view, handler base/context, player sound state, and active-voice tracking structures in Ghidra.
- Performed no runtime, game, or replay validation.
- Added exact/partial enum, request, setting, material, array, sparse-storage, and mesh-component types; corrected `FTransform48`; and completed the relevant Ghidra naming, typing, comments, and completeness checks without runtime validation.

### 2026-08-04 — collection-5 keyed sound-stop pass

- Reclassified HorseMod's `ModeAndUint` route as the stop half of the collection-4 contact-sound protocol.
- Proved the eight-byte callback carries only class/contact key and resolves `activeVoiceId` from the handler's mutable seven-lane tracking state at subscriber time.
- Traced native delivery through current class/shared player selection to a locked 0x18-byte command-queue record with opcode `2`.
- Proved reflected `ReceiveStopSE` still runs when the native tracking lookup misses, exposing a semantic split hidden by the current exactly-once counter.
- Added exact request/callback/queue-record/queue types and completed the relevant Ghidra naming, typing, comments, and completeness checks without runtime validation.

### 2026-08-04 — collection-11 reset pass

- Reclassified HorseMod's `uint-byte-60` route as a cross-system presentation-state reset.
- Recovered and documented the VFX clear thunk and the sound-handler reset subscriber.
- Confirmed that both known native subscribers ignore the five-byte packet and operate on current handler/manager/singleton state.

### 2026-08-04 — character-cue audio boundary pass

- Traced character cues from the hub subscriber through current player/style/provider remapping and the current battle-sound-manager cue-family hash.
- Separated semantic cue resolution from the five-instruction shared-player thunk and concrete CRI active-voice registration.
- Confirmed statically that active-voice ids are selected through the UCRT `rand` import and collision-checked against the live voice map.
- Recovered six partial audio/shared-reference structures and propagated them to the resolver and active-voice functions.
- Removed runtime-specific wording from the terminal function documentation; this pass used decompilation, disassembly, xrefs, and current HorseMod source only.

### 2026-08-04 — collection-1 DisableVFx pass

- Reclassified HorseMod's `FourUints10` route as the typed `FLuxDisableVFxParam` transaction.
- Traced the subscriber through current-state ID resolution, transition-list expansion, filter derivation, both persistent 0xC0-stride VFX slot tables, weak callback pruning, record compaction, and `ReceiveDisableVFx` publication.
- Recovered five request/transition/filter/subsystem/record structures and propagated them through seven functions.
- Expanded the route to its battle-color-fade-manager subscriber, recovered its 0x20-byte lane and 0x18-byte active-entry structures, and proved that it resets or mutates persistent fade state from the same packet.
- Proved that the color-fade subscriber ignores `keepFrame` and `FadeoutFrame`; current setting data and current lane state determine removal timing and whether entries are erased or transitioned.
- Kept the secondary actor's raw fields and virtual methods explicit instead of forcing a conflicting concrete actor overlay.
- Completed the native Ghidra naming, prototype, local-variable, comment, and completeness pass without runtime validation.

### 2026-08-04 — collection-2/3 battle-color-fade pass

- Reclassified HorseMod's `Transform18` and `FourUints20` routes as MoveVM effects `0x040B` and `0x040C`, a paired persistent color-fade queue/timing protocol.
- Proved that collection 2 inserts, initializes, and priority-sorts 0x34-byte manager-owned layers and that collection 3 rewrites matching layers' runtime timings.
- Added six exact/partial request, callback, layer, player-state, and manager structures and propagated them through the adapters, subscribers, and BeginPlay.
- Created three previously missing callback functions in Ghidra and recovered the collection-10/11 reset-all paths through `ResetBattleColorFadePlayerState`.
- Expanded the collection-11 route from two to three known native subscribers: VFX, sound, and battle-color-fade teardown.
- Verified all eight newly documented functions with no remaining fixable completeness deduction above ten points; no runtime validation was performed.

### 2026-08-04 — Lux-to-Unreal fighter synchronization pass

- Cross-checked HorseMod's confirmed fighter reconciliation against the stock `ALuxBattleChara_TickActor` call sequence.
- Proved vtable `+0x6A0` resolves an Unreal-world `FTransform` through the current `ScbattleWorldMode` and a player-info handler that reads native primary-bank matrix 0; it does not read native `PLAYER +0x94/+0xA0`.
- Confirmed HorseMod reproduces the narrow virtual/setter ABI correctly, but only checks call success and finite output; the separately sampled native pose is not used for semantic validation.
- Proved the stock sync continues beyond root publication. This pass initially classified the Soul Charge counter/selector/provider lane as rollback-relevant gameplay; the later ownership audit below retracts that classification.
- Proved the BattleManager world-mode pair is a BeginPlay-created lifetime identity and traced the handler's frame-varying transform source to native `PLAYER +0x35A0`.
- Cross-checked that exact matrix bank against `RollbackHgCpuSnapshot`: the current implementation restores its 0x38-byte control and the 0x1840-byte prefix of all three physical slots.
- Identified incomplete actor identity validation and the absence of a production actor-tick phase gate.
- Created and propagated the actor-specific 0x568-byte sync overlay plus world-mode/player-handler shared-pointer types, corrected the transform-builder/world-mode-getter/matrix-copy prototypes, and updated the relevant Ghidra function/global comments.
- Performed no runtime, game, or replay validation.

### 2026-08-04 — `ALuxBattleChara_TickActor` ownership correction

- Re-audited all 126 basic blocks and the concrete player-info wrapper vtable used by `ALuxBattleChara_TickActor`.
- Retracted the earlier gameplay move-table interpretation: actor `+0x53C` is a UE-tick Soul Charge fade counter, `+0x537` is a presentation selector, and actor `+0x4C0` is a 0x50-byte TSet-like provider container.
- Proved the stack pair previously called a weapon AnimInstance is overwritten with a `ScbattlePlayerInfoHandlerWrapper`; named and typed the wrapper gauge/move/state accessors.
- Reclassified `RefreshBattleCharaSoulChargePresentationState` as presentation plus one native secondary-sway sidecar, not MoveVM/hit/attack state.
- Traced wrapper `+0x268` through player-index resolution to four named `OPPAI_*` sway records and the native secondary-bone solver.
- Proved native HgCpuDirect skips the sway sidecar and HorseMod's supplemental skeleton snapshot ends immediately before it at `PLAYER +0x2B3DF`.
- Recovered the pending `+0x548` array as 0x50-byte float delegates; `TickActor` forwards `flDeltaSeconds` and removes an entry only on a true callback result.
- Confirmed the terminal base tick can dispatch arbitrary `ReceiveTick` code and UE tick dependencies, which remains the decisive reason not to call full `TickActor` during confirmed reconciliation.
- Added corrected Ghidra names, prototypes, variables, comments, function-pointer type, delegate/provider/sway structs, and ownership plates; no runtime validation was performed.

### 2026-08-04 — matrix-bank producer timing pass

- Recovered the exact primary and secondary three-slot physical layouts: 768 and 32 `FMatrix64` entries per slot respectively.
- Proved both banks rotate at the beginning of `LuxBattleChara_FinalizeTickPoseAndState`, before the primary pose solver writes the selected current slot, and that HorseMod production capture occurs after the owned native iteration returns.
- Proved rotation only changes controller pointers/index (`0 -> 2 -> 1 -> 0`) and does not copy, clear, or initialize the selected slot.
- Cross-checked native HgCpu and Horse coverage: native covers primary 97/current and secondary 32/current; Horse preserves those same prefixes across all three slots and restores them after the native reader.
- Recovered `PLAYER +0x42550` as a load-time authored primary-matrix count computed without a 97-matrix clamp. Matrix 0 root sync is covered, but wider matrix consumers rely on an unvalidated content contract.
- Found a separate static error in `RollbackRestoreMotionBankHistoryFromTimeline`: post-step historical frames are sourced through `provider_slot`, shifting restored prior-slot content one frame too old. This helper is diagnostic harness code, not production restore.
- Corrected the Ghidra bank header, secondary getters, matrix-count field/provenance, getter/copy helpers, initializer/rotation/finalizer comments, and relevant variable/prototype types; saved the program after verification.
- Updated documentation only; no runtime, game, replay, build, or test validation was performed.

### 2026-08-05 — animation and montage UE-boundary pass

- Separated the main SCBattle matrix-sampling AnimGraph bridge, appendix header-frame/threshold bridge, and UE-owned montage graph into three distinct ownership domains.
- Proved `RollbackCharaAnimationState` is a native Lux clip/scheduler snapshot despite its broad name and contains no AnimInstance, skeletal-mesh clock, appendix, or montage state.
- Recovered appendix `LastHeaderFrame`, `GlobalAnimRateScale`, target-mode updates, exact 0x0C threshold records, `IsUsed` re-arming, and synchronous `OnChangeAnimationState` Blueprint dispatch.
- Proved rewinds freeze the appendix graph and re-arm thresholds but do not set UE montage/animation position; the reflected `SetAnimationPosition(float)` event exists but is not called by the recovered native TickActor corridor.
- Recovered `UAnimInstance` active montage array/map/root-motion ownership and exact 0x190-byte montage-instance position/marker fields.
- Added confirmed defects for unowned appendix clock/event state and unreconciled montage state, while retaining parallel AnimGraph sampling as an explicitly unproven P2 concern.
- Added the relevant Ghidra structures, function/global names, prototypes, variable types, and comments; saved the program after final completeness review.
- Updated documentation only; no runtime, game, replay, build, or test validation was performed.

### 2026-08-05 — event-hub interception-boundary pass

- Proved every hooked hub adapter is a transaction over a live 0x70-byte weak-callback collection, including reverse registration order, weak-target resolution, recursion accounting, dirty detection, and post-broadcast compaction.
- Corrected two Ghidra helpers from asynchronous-task submission to synchronous weak-UObject callback registration and corrected the supposed async-pool compactor to callback-entry compaction.
- Corrected the former "unbound event" family to the exact byte/two-float ABI, retained the distinct bound-byte/generic-event-pointer family, and recovered both 0x30-byte callback layouts plus four exact typed 14-slot vtables.
- Traced the VFX byte/two-float consumer through selector/current-value gating, both player-side shared track containers, type-`0x38` hash lookup, the exact `DestroyDebris` command packet, and immediate presentation-entry destruction/reset.
- Proved HorseMod confirmation replays route bytes through the current dispatcher and current callback graph; it does not preserve source-frame subscriber identity, order, weak validity, bound registration arguments, or topology cleanup.
- Completed the full direct GetHub caller scan for collections 32/36 and found no direct native registrations while retaining indirect/Blueprint registration as open uncertainty.
- Re-audited collection 0 below the hub: current move/provider remap, authored descriptor construction, persistent slot allocation/ownership, particle/debris activation, completion delegates, and `ReceiveEnableVFx` are interleaved.
- Concluded that no single lower universal hook is supported by the binary; the durable design requires route-specific semantic ownership, shadow presentation state where necessary, and fully resolved terminal commands.
- Updated Ghidra names, prototypes, variables, globals, callback vtables, comments, and structures; updated documentation only and performed no runtime, game, replay, build, or test validation.

### 2026-08-05 — historical Ghidra cleanup and type-closure pass

- Re-audited all 119 unique historical ledger functions and required one-hop helpers; the final batch has no function above ten fixable completeness points.
- Corrected the last high-deduction function, `LuxBattle_ReinitCharaSlotForMove_SubsequentRound`, and propagated its exact subsequent-round entry type through the direct table wrapper.
- Corrected `FLuxMoveDataLoadPacket_Partial` from `0x78` to exact size `0x80`, recovered its exact `0x60` nested payload, and re-decompiled the loader and all direct callers affected by the type change.
- Reclassified init-entry `+0xAC/+0xC8` from byte/hit-scan ranges to exact inline `FLuxSwayBoneParameterSource_Partial` records and proved their publication to native PLAYER `+0x2B4C4/+0x2B4E0`.
- Recovered the exact `0x68` body/attack/hurtbox KHit output-control block, the exact 107-entry move-slot pointer table, and the exact two-lane `0x4000` subsequent-round KHit scratch pool.
- Typed and documented the fixed executable image base as `g_abImageDosHeader` while preserving the exact `IMAGE_DOS_HEADER` listing type; documented the two character-map interior member references without splitting the authoritative 41-entry array.
- Saved Ghidra after the verified subsystem and final cross-system audit. Updated this investigation only; no HorseMod source, build, game, replay, debugger, or runtime validation was used.

### 2026-08-05 — correctness-first rollback remediation pass

#### Executed plan

The implementation pass used the following ordering so that a narrow, proven
correction did not grow into a speculative Unreal-state serializer:

1. Remove the incorrect ownership claim at the event-hub boundary.
2. Keep only independently verified terminal presentation hooks.
3. Correct the concrete request-width, matrix-history, and matrix-capacity
   defects.
4. Strengthen fighter-presentation actor identity before invoking the stock
   virtual transform builder.
5. Rename qualification fields so their names match what they actually prove.
6. Update focused self-tests and the two-client acceptance policy.
7. Build, deploy, and run the required normal-renderer strict replay seek.

#### Changes made

- Production no longer installs the 38-slot listener-hub vtable interceptor or
  the upstream character-cue/audio adapters. Those callbacks remain stock,
  synchronous source-time transactions over the live weak-listener graph.
- The production presentation hook mask is now `0x7C`: FMemory allocation/free
  ownership, stage-wind tick/spawn, and camera vibration. These are the only
  boundaries in this pass that remain eligible for the terminal side-effect
  ledger.
- Dormant audio and VFX adapter functions are defensive pass-throughs. If an
  Audio or VFX event nevertheless reaches `commit_side_effect`, production
  fails closed with `semantic-side-effect-journal-entry-unsupported`.
- `kRollbackEventHubDeferralSupported` is explicitly `false`. The 38 route
  descriptors remain an RE/diagnostic inventory, not an authorization to
  capture and replay a hub packet.
- The exact collection-0 `FLuxEnableVFxParam` request width is now `0x44`, not
  `0x50`.
- `RollbackRestoreMotionBankHistoryFromTimeline` now reconstructs a post-step
  historical frame from `current_slot`. The old `provider_slot` use shifted
  each reconstructed prior slot one additional frame into the past.
- Rollback lifecycle admission now reads native PLAYER `+0x42550` for both
  fighters and rejects authored primary-matrix counts outside `1..97`. This
  prevents the fixed `0x1840` prefix from being certified for content whose
  full skeleton width exceeds HorseMod's captured 97 matrices.
- Confirmed root publication no longer samples unrelated native PLAYER
  `+0x94/+0xA0` scalars. It records only the transform returned by the exact
  stock world-mode builder.
- Presentation actor binding now requires one of the two constructor-proven
  vtables (`0x143268078` or `0x1432A7D98`), a non-null root component, and the
  exact `+0x6A0` target `0x1403C0200`. Reconciliation validity becomes false
  on identity, getter, setter, exception, or finite-transform failure.
- `presentation_transforms_finite` was replaced by
  `presentation_root_publish_valid`, and `presentation_exactly_once` was
  replaced by `presentation_accounting_consistent`. The latter proves only
  ledger conservation/accounting; it does not claim semantic exactly-once
  effects.
- The acceptance runner now compares only terminal camera and transition
  lanes. It requires `battle_event_hub_passthrough` and
  `audio_semantic_passthrough` and does not treat dormant hub-route counters as
  rollback convergence evidence.

#### Ghidra closure added during implementation

| Address | Final symbol/type | Evidence and completeness |
|---|---|---|
| `0x1403C0200` | `FTransform48 * BuildCharaActorWorldTransformFromBattleWorldMode(ALuxBattleCharaSyncActor_Partial *pChara, FTransform48 *pOutTransform)` | Re-decompiled after typing and naming the shared-ref/readiness projections. Exact base and demo vtable `+0x6A0` xrefs agree. Effective completeness is 89%; the remaining 11 fixable points are an audit false positive for the `translation` and `scale3d` interior members of the already typed `g_abIdentityTransform48`. The parent `FTransform48` was intentionally not split into overlapping data items. |
| `0x143268078` | `g_apfnALuxBattleCharaPresentationVtable` | Constructor-owned base actor vtable, typed as `ALuxBattleCharaPresentationVtable_Partial`. |
| `0x1432A7D98` | `g_apfnALuxDemoHumanActorPresentationVtable` | Constructor-owned derived demo-human vtable using the same verified presentation tail. |

`ALuxBattleCharaPresentationVtable_Partial` is an exact conservative
`0x6A8`-byte minimum overlay: 210 unknown virtual pointers through `+0x688`,
typed world-mode getters at `+0x690/+0x698`, and
`ALuxBattleCharaSyncWorldTransformBuilderFn *` at `+0x6A0`. Unsupported
earlier slots remain an exact pointer array rather than receiving speculative
names. Ghidra was saved after the final decompile, plate/PRE comment rebuild,
global typing, and completeness audit.

#### Deliberately unresolved capability gaps

This pass fixes unsafe behavior and false certification; it does not claim to
have completed presentation rollback:

- Source-time audio and VFX can still be observed from speculative frames.
  Eliminating duplicates requires route-specific hooks below semantic
  resolution and below the live weak-listener transaction.
- Collection-0 VFX allocation interleaves particle/debris activation,
  persistent slot/delegate bookkeeping, and reflected publication. A correct
  implementation still needs a route-specific shadow-state and terminal
  command design; the hub packet cannot be replayed safely.
- Battle-color-fade, debris topology, active-voice tracking, and other
  stateful UE managers are not snapshotted or reconstructed by this pass.
- Root publication remains intentionally narrower than full
  `ALuxBattleChara_TickActor`. Hair, weapon animation, materials, delegates,
  derived demo-human work, ReceiveTick, attachment/scoped-movement ownership,
  and arbitrary Blueprint dispatch remain outside reconciliation.
- Appendix animation clocks/thresholds and `UAnimInstance` montage graph state
  are still outside snapshot ownership. Writing a montage position alone is
  not a supported repair.

The next safe implementation milestone is therefore not another universal
dispatcher. It is one complete route at a time: prove the lower terminal API,
separate persistent semantic state from transient presentation output, define
its restore/reconcile rule, and only then add it to qualification.

#### Validation performed

- `HorseModRollbackSelfTestsRun`: 65/65 tests passed, including the updated
  VFX request-width and snapshot/matrix-history tests.
- `rollback_two_client_acceptance_run.py --selftest`: synthetic behavior and
  static-policy classification passed after terminal-only evidence changes.
- Full LTO-compatible HorseMod build completed and `main.dll` was deployed to
  the game install.
- Required strict replay seek passed with the normal renderer:
  run `20260805-054929-seek`, four 600-frame watch cases passed, 2,400/2,400
  state comparisons matched, resume rates were 59.0--60.4 ticks/s, and the
  maximum seek-validation interval was 0.42 seconds against the 0.50-second
  limit. The durable report is
  `reports/replay_tests/replay_seek_e2e_20260805-054929-seek.json`.

This replay run validates build/deploy integrity and ordinary seek/restore
behavior. It does not close the unsupported source-time audio/VFX duplication,
stateful UE-manager, appendix-clock, or montage-ownership gaps listed above.

### 2026-08-05 — rollback replay matrix/CRT correction and qualification pass

#### Defects exposed by Replay 100

The first two-client run did not reach active rollback because both stock
characters exceeded HorseMod's inferred 97-matrix limit. Live lifecycle
telemetry reported 231 primary matrices for Siegfried and 379 for Tira. This
is direct counterevidence to the proposed stock-content convention and agrees
with Ghidra's recovered 768-matrix physical allocation.

After complete-bank capture enabled rollback, successive runs failed closed
on previously unclassified UCRT `rand` callers. Ghidra caller/disassembly
audits proved that the exact sites were presentation-local and must not consume
the rollback gameplay CRT stream:

| Call site | Recovered owner | Proven random purpose |
|---:|---|---|
| `0x141F9BD5C` | `InitializeParticleEmitterInstance` | emitter-local random seed |
| `0x14054F91E` | `LuxAudio_RegisterActiveVoiceInstance` | collision-checked active voice id |
| `0x140895D6E`, `0x140896105` | `InitializeLuxGroundDebrisComponentRing` | debris base/yaw jitter |
| `0x141FA0932` | `InitializeParticleModuleRandomSeedPayload` | authored particle seed selection |
| `0x141FA5B0B`, `0x141FA5B52` | `InitializeParticleEmitterDurationState` | emitter delay/duration ranges |

These exact, executable-signature-validated sites now route to native UCRT.
All other owned callers remain fail-closed until individually classified.
Gameplay callers continue to use the snapshottable CRT state.

#### Ghidra type closure added for the CRT boundary

- `InitializeParticleEmitterInstance` is typed as
  `void __fastcall (FParticleEmitterInstance_Partial*)`; its GPU slack and
  tile-preallocation globals are named and typed.
- `LuxAudio_RegisterActiveVoiceInstance` is typed as
  `uint __fastcall (FLuxActiveVoiceOwner_Partial*, uint, int, uint)`.
- `InitializeLuxGroundDebrisComponentRing` retains the exact debris-ring and
  transform overlays; the signed-angle and degree-scale globals are named and
  typed.
- `InitializeParticleModuleRandomSeedPayload` now uses exact partial emitter,
  seed-payload, seed-info, and particle-component types, with its seed-index
  locals named and typed.
- The formerly unnamed `0x141FA5A20` is now
  `InitializeParticleEmitterDurationState`, typed as
  `void __fastcall (FParticleEmitterDurationState_Partial*)`. Conservative
  overlays were created for the 0x194-byte emitter duration state, required
  module duration fields, LOD entry/array, and component emitter-delay field.
  Unknown gaps and register/SSA phantoms remain explicitly documented rather
  than receiving speculative types.

Every changed function and direct caller was re-decompiled after structural
changes. The first four functions reached effective completeness 100%; the
duration initializer reached 95%, with the remaining five points limited to
documented compiler projections and raw nested accesses. Ghidra was saved
after each verified batch and after the cross-system audit.

#### Implementation corrections

- Primary matrix capacity is now exactly 768 matrices / `0xC000` bytes per
  physical slot. All three slots are captured and restored.
- Matrix authority receipt tracking widened from 16 to 64 bits, sufficient
  for the complete image's chunk count, with compile-time capacity checks.
- Gameplay CRT checkpoint fields now report the snapshottable internal state,
  round seed, draw ordinal, warm-up draws, and phase. These fields are compared
  bilaterally.
- Native presentation CRT sequence hashes and transaction-call totals are
  diagnostics only. They are process-local cumulative telemetry and can differ
  because audio/particle presentation work is client-local; treating them as
  deterministic rollback state caused false checkpoint failures.
- The replay-selection proof now verifies the requested client identity/slot
  separately from character ownership and requires at least one appended bit
  intersecting either replay-selected DLC character. Replay 100 proved the
  old role-to-character assumption wrong: sandbox slot 0 appended the missing
  remote Tira bit while host slot 1 locally owned Tira and appended no bit.

#### Qualification evidence

- `HorseModRollbackSelfTestsRun`: 65/65 passed after complete-bank and CRT
  changes.
- `rollback_two_client_acceptance_run.py --selftest`: passed after gameplay
  CRT comparison and DLC-proof corrections.
- Required normal-renderer strict seek report
  `reports/replay_tests/replay_seek_e2e_20260805-085024-seek.json`: 4/4 watch
  cases, 2,400/2,400 state comparisons, zero mismatches, and 0.42-second
  maximum seek validation against the 0.50-second limit.
- Replay 100 two-client report
  `reports/rollback_replay_corpus/replay100-gameplay-crt-checkpoint-clean-20260805.json`:
  PASS with four round generations, exact replay-input agreement, paired
  gameplay CRT checkpoints, 10,656 host saves, and 3,307 host rollback
  advances. No unknown CRT caller, deterministic checkpoint mismatch, replay
  input mismatch, or fatal occurred.

The 14-replay compatibility corpus is being executed through the SHA-bound,
same-artifact normal-oracle plus rollback-active two-client gate. Its aggregate
report is `reports/rollback_replay_corpus/gate-all14-compat-20260805.json`;
individual results and any newly exposed defect are appended after each
fail-fast/resumable case.

### 2026-08-05 — Replay 106 motion-pose and extra-bone cache correction

#### Failure localization

Replay 106 originally diverged on corrected logical frame 1146. The first
observable difference was P2 KHit list 1: the source matrix and payload lanes
0/1 differed, and the same frame showed a different P2 primary bone-25 matrix.
Temporary peer-breakdown hashes narrowed the producer to the nine persistent
extra-bone matrices in the P2 motion tail. The full character streams, motion
banks, providers, timers, skeleton state, explicit ranges, stage/wind state,
inputs, native round state, and gameplay CRT state otherwise matched.

An earlier frame-300 diagnostic was limited to expired stack residue in a
temporary pose transform. Deep sampler traces showed that every final corrected
sampling event, including transform 23 after each sample, matched the normal
control. Raw retained rotation/translation lanes therefore remain local
save/restore integrity state but are excluded from the cross-peer canonical
hash; canonical matrices and KHit state still expose any gameplay-observable
consequence.

#### Ghidra proof: nine uninitialized decision bytes

`LuxBattleChara_SolveBonePose` (`0x1402EDB90`) owns the 62-record sampled-pose
stack at `RBP+0xDF0`. Ghidra disassembly proved that the final extra-bone cache
exchange unconditionally reads nine stack bytes at `RBP+0x3721..0x3729`, or
sampled-pose offsets `+0x2931..+0x2939`, for bones 23 through 31. Function entry
does not initialize them. Only the native selector paths conditionally write
them:

- `0x1402EFE5A` sets reuse, and `0x1402EFE68` clears reuse;
- `0x1402F043F` sets reuse on the second selector path;
- `0x1402F095B`, `0x1402F09DE`, `0x1402F0A61`, `0x1402F0AE4`,
  `0x1402F0B67`, `0x1402F0BEA`, `0x1402F0C6D`, `0x1402F0CF0`, and
  `0x1402F0D73` perform the nine unconditional final reads.

A nonzero byte copies the persistent extra-bone cache into the current
collision matrix bank; zero refreshes the persistent cache from the current
matrix. The signed nine-entry source map is now typed and named
`anExtraBoneSourceMap23To31`; `-1` means no authored source. Plate, PRE, and EOL
comments document the decision-byte lifetime and each final cache exchange.

The apparent second 45-record stack bank at `RBP+0x1990` was also audited. Its
only address reference initializes scale lanes at `RBP+0x19B8` with a 0x30
stride; the pointer is discarded and the region is never read, passed, or
published. It is dead native stack initialization and is not rollback state.

#### Implementation and artifact compatibility

`RollbackMotionPoseResidueSnapshot` now carries the nine reuse bytes with the
62 retained transform rotation/translation records. The first sampler in each
solve restores both payloads, `LuxBone_WritebackScaledBoneTransforms`
(`0x1402F3690`) captures them while the SolveBonePose stack is still live, and
post-restore verification hashes the complete local rollback state. A missing
historical value bootstraps to reuse (`1`); native authored selector processing
overwrites that byte when refresh evidence exists.

Replay sidecars intentionally retain their fixed historical layout and do not
store the newly recovered bytes. Their existing pose hash therefore remains a
wire-compatibility hash over retained transforms plus validity/player fields.
The loader deterministically supplies nine reuse bytes for old artifacts, while
the in-memory rollback-state hash includes them. This separation fixed the
initial `consumed-input-sidecar-round-invalid` regression without changing the
sidecar schema or invalidating recorded normal-render oracles.

The diagnostic cache-hash multiplex used to localize the fault was removed
after proof; production peer-breakdown partition fields again retain their
defined meanings.

#### Verification

- `RollbackMotionPoseResidueSelfTest` and
  `RollbackReplayInputSidecarSelfTest` pass, covering bootstrap, capture,
  restore, flag-sensitive local hashing, and legacy sidecar loading.
- `reports/rollback_replay_corpus/replay106-extra-bone-flag-carry-fix2-20260805.json`:
  PASS through the prior frame-300 and frame-1146 boundaries with active
  rollback and deep corrected-frame comparison.
- `reports/rollback_replay_corpus/replay106-extra-bone-flag-carry-clean-repeat-20260805.json`:
  independent PASS after removing the temporary diagnostic multiplex.
- The fresh full-corpus aggregate is
  `reports/rollback_replay_corpus/gate-all14-extra-bone-fix-20260805.json`;
  it is updated after every normal-render oracle and two-client rollback case.

#### Replay 109 decoder-capacity baseline correction

The first fresh corpus attempt passed replays 100, 102, and 106, then failed
Replay 109 at generation 2 with `stock-online-baseline-mismatch`. The published
HgCpu, explicit, stage, wind, native-round, native-simulation canonical, input,
and pose-residue hashes all matched. Only the full `0x4A0` motion decode scratch
pair differed, so including that raw pair in `HashRollbackStepStateCanonical`
made otherwise-identical baselines unequal.

Ghidra re-audit corrected the ownership model:

- `LuxMotion_SampleKeyframeTransforms` (`0x1402E7780`) clears its two local
  fallback buffers, but at `0x1402E78B1`/`0x1402E78CC` substitutes a supplied
  caller pair without clearing it.
- `LuxMotion_DecodeHuffmanKeyframeData` (`0x1402E71E0`) loads the packed count
  at `0x1402E7289`, computes `componentCount = packed >> 1` at
  `0x1402E728E`, and bounds the base, delta, and next-frame output loops by
  that exact signed-word count. The copy/zero loop is
  `0x1402E72F0..0x1402E7309`.
- `LuxMotion_BlendKeyframeTransforms` (`0x1402E79C0`) consumes the authored
  prefix according to valid MOT selector metadata. It does not consume the
  remaining capacity tail.

The full pair remains local save/restore integrity state, but its unused tail
is now excluded from the cross-peer canonical digest. Canonical pose, matrix,
motion-tail, and KHit products still expose any observable decoder divergence.
Plate, PRE, and EOL comments record this capacity boundary in all three Ghidra
functions.

`reports/rollback_replay_corpus/replay109-decoder-capacity-canonical-fix-20260805.json`
passes with bilateral rearm through generation 2. The earlier aggregate
`gate-all14-extra-bone-fix-20260805.json` intentionally remains an authenticated
3-pass/1-failure record of the false invariant; testing after this correction
continues with a new artifact-bound corpus report.

#### Replay 155 auxiliary clip-section rebind correction

The post-Replay-109 corpus passed replays 116, 127, 131, 134, and 153, then
Replay 155 failed during generation 1 at rollback frame 1926 with the former
aggregate error `chara-animation-restore-preflight-failed`. A diagnostic-only
classification build reproduced the boundary and resolved the rejected field
to `chara-animation-preflight-clip-data-identity`; scheduler identity, circular
list topology, trigger count, and trigger-object ownership had not changed.
The diagnostic record is
`reports/rollback_replay_corpus/replay155-animation-preflight-diagnostic2-20260805.json`.

The prior rollback model treated `FLuxCharaAnimClipPlayer::pClipData` at
character `+0x95ED8` as immutable round-lifetime identity. Ghidra disproved
that assumption. The previously unnamed writer at `0x1402F77C0` is now
`LuxMoveVM_ApplyCharaAnimSlotEntry` with the exact prototype:

```text
void __fastcall LuxMoveVM_ApplyCharaAnimSlotEntry(
    FLuxCharaAnimSlotController_Partial *pController,
    int nBlendSlot,
    int nPackedEntryIndex,
    uint dwMotionVariant,
    float flTransitionCursor,
    float flTransitionLimit)
```

For packed entry type 1, the function calls `LuxPackedData_GetSection`, then
writes the returned interior authored-data pointer to controller `+0x18`.
The first 0x48-byte controller begins at character `+0x95EC0`, so controller
`+0x10/+0x18` exactly overlap the auxiliary clip player's
`pOwnerChara/pClipData` at character `+0x95ED0/+0x95ED8`. The pointer therefore
selects a clip section inside the stable controller-owned packed-data
allocation; it is not a heap object whose ownership ends on every selection.
The same transition can occur in the round-result corridor before the later
NewRound generation boundary.

`FLuxCharaAnimSlotController_Partial +0x18` is now typed
`FLuxCharaAnimClipTableHeader *pClipData`. The writer has a verified
calling convention, parameter ABI, resolved locals, PRE/EOL comments, direct
caller re-decompilation, and 100% function-completeness score. Ghidra was saved
after this batch.

The local animation snapshot now captures the controller's
`pPackedAnimData` owner identity. Preflight still rejects character,
controller-allocation, scheduler, list-head, node, trigger, control-block, and
vtable ownership changes, and it additionally requires the historical clip
header to remain readable. When those lifetime checks hold, restore writes the
historical `pClipData` section pointer together with the existing clip scalars
and owner runtime. Raw process pointers remain excluded from the peer-canonical
hash; the new packed-data owner participates only in local integrity.

`RollbackCharaAnimationStateSelfTest` now covers a valid same-allocation
section rebind and exact restoration while preserving the existing topology
reconfiguration refusal test. The final implementation artifact is
`3e16c4c1e9569dc327b94fac081c50bf3bff8b970e8e34086d259261bd4e3cb0`.

Verification results:

- `reports/rollback_replay_corpus/replay155-clip-section-restore-fix-20260805.json`:
  PASS through the former frame-1926 fatal boundary, round-result processing,
  bilateral rearm, and the next active round.
- `reports/rollback_replay_corpus/gate-remaining10-after109-fix-20260805.json`:
  authenticated passes for 116, 127, 131, 134, and 153 followed by the original
  Replay 155 failure; retained as failure evidence rather than rewritten.
- `reports/rollback_replay_corpus/gate-final4-after-replay155-fix-20260805.json`:
  PASS for replays 164, 165, 176, and 177, each with a same-artifact normal
  renderer oracle plus a clean two-client rollback correction/rearm case.

Together with the earlier passing reports for 100, 102, 106, 109, the five
passes before Replay 155, the corrected Replay 155 case, and the final four-case
gate, all 14 repository replays have now completed a normal-render oracle and a
two-client rollback case on the progressively corrected implementation. The
split reports preserve the exact artifact and failure history instead of
claiming that one pre-fix binary passed the whole corpus.

Final verification after the Replay 155 correction:

- Full CTest suite: 65/65 PASS, including
  `RollbackCharaAnimationStateSelfTest`, motion-pose residue, replay sidecar,
  snapshot/store, protocol, UDP, Gekko, and end-to-end rollback tests.
- `rollback_two_client_acceptance_run.py --selftest`: PASS for every synthetic
  runner policy and evidence classifier.
- Strict normal-render Replay 127 seek/watch:
  `reports/replay_tests/replay_seek_e2e_20260805-145812-seek.json` PASS. All
  four cross-round 600-frame watches passed (2,400/2,400 state comparisons,
  zero mismatches), resume rate was 59.9--60.4 ticks/s over each 120-tick
  window, and maximum seek validation time was 0.41 seconds against the
  0.50-second limit.

### 2026-08-05 — beta remaining native-boundary static pass

A subsequent Ghidra-only pass disproved the rollback input override's
interpretation of `ALuxBattleChara +0x324`. Three independent native consumers
show that the field is the current MoveVM move ID, not a temporary input-source
selector. Horse currently forces it to `1` across the whole native per-frame
tick and restores the entry value afterward. That changes scheduler branches
and can erase a legitimate move-ID transition written during the tick. All
rollback writes and restoration of `+0x324` must be removed before beta.

The same pass recovered a separate fixed-two-entry `TArray<ulonglong>` event
mask at MoveDispatch `+0x4A8/+0x4B0/+0x4B4`. Its registered event callback sets
bits and 20 native predicates consume them, but
`RollbackNativeInputCallbackSnapshot` currently stops at `+0x4A7`. The two
qwords must be captured, hashed, restored, and verified while retaining the
live TArray owner and metadata. This is a second confirmed pre-beta rollback
correctness defect.

The authoritative pre-callback input-pair injection order was independently
confirmed correct and should remain unchanged. Steam interface version and
relay-policy ownership are lower-severity issues. Full evidence, prototypes,
struct layouts, Ghidra change ledger, and minimal implementation guidance are
in
[`rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md`](rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md).
No runtime validation was performed during that pass.

### 2026-08-05 — event-mask lifetime and Lux input-router closure

The follow-up Ghidra pass closed two uncertainties left by the beta-boundary
report.

First, the full direct MoveDispatch `+0x4A8` surface is now classified. The
constructor allocates and zeroes exactly two qwords, the registered writer only
ORs validated bits 0..37, 20 predicates read the masks, and
`DestroyALuxBattleMoveDispatchSubobjects` (`0x14040A850`) frees the allocation
at object destruction. No per-frame or per-move clear, resize, or owner
replacement appears in that complete direct-reference surface. Two predicates
read bit 24 or bit 23 from slot `1 - request.slot`, proving the two qwords are
coupled gameplay state and both must be restored. The rollback omission cannot
be mitigated by assuming stock code clears speculative bits on the next frame.

Second, the upstream Lux input-router boundary is now explicit. The process
router owns a default `TArray<int32>` at `+0x00` and active owner assignments at
`+0x10`. `ProcessLuxInputKey` records a physical owner id per logical slot and
broadcasts logical slot, new owner, and accepted state. The weak callback ABI
passes all three values to `HandleLuxInputOwnerChanged`, but that handler uses
only slot 0/1 to invalidate the corresponding left/right processor cache. The
binding-change callback uses a distinct no-payload wrapper and invalidates both
caches. Neither path forwards physical owner identity into
`ALuxBattleFrameInput`; the 0x508 frame-input object samples a logical left or
right bitfield, computes exact 0x90-byte current/pressed/released/repeated/hold
records, and only those downstream logical values reach the rollback callback
pair.

Consequently, Horse's downstream logical-pair injection remains at the correct
deterministic boundary and does not need to snapshot the physical owner table.
Live device assignment still requires physical qualification, but it is not an
additional rollback-state hole. The detailed function/type/global ledger and
all remaining conservative uncertainties are recorded in the linked static
handoff report. This follow-up also performed no runtime validation.

The final input-router closure resolved the last raw router field and two
historical naming errors. `FLuxInputRouter_Partial +0x24` is a signed
slot-zero fallback owner ID, not an unknown uint: `-1` allows any otherwise
unmapped owner to resolve to an unassigned logical slot 0, while another value
allows only the matching owner. `ResolveLuxInputOwnerLogicalSlot`
(`0x1404C18A0`) proves the exact scan, capture-slot exclusion, and fallback
order.

Initialization constructs two default `-1` owner entries and registers two
connection callbacks. Both controller and secondary-input disconnection paths
reduce the physical owner ID to a logical slot and forward only that slot.
The shared dispatcher at `0x140415940` was therefore corrected from the
misleading historical `AsyncTaskPool_TickAll_CompactIfAnyFailed` name to
`DispatchIndexedCallbackEntriesAndCompact`; assembly preserves EDX and passes
the integer index to callback virtual slot `+0x68`. Shutdown restores defaults,
invalidates caches, removes both connection callbacks, and the guarded atexit
destructor frees the two owner arrays. None of this upstream identity or
lifecycle state crosses into `ALuxBattleFrameInput` or Horse's downstream
logical input-pair injection boundary.

### 2026-08-05 — MoveVM pump, scheduler pointer, and command-arena closure

The low-priority static closure pass found two additional confirmed rollback
defects downstream of the corrected input boundary.

Horse snapshots the full 0xC0-byte `FLuxMoveSchedState[2]` array at
`0x144715400`, including each record's owning `pSubVM` at `+0x50`. The generic
restore writes every byte even though the peer-canonical hash excludes pointer
offsets. Native command factories replace `pSubVM`, scalar-delete the previous
generation, and allocate class-dependent 0x68/0x70/0x78/0x80-byte mutable
objects. Restore can therefore republish a freed pointer, or combine restored
scheduler scalars with current-time SubVM bytes when the generation did not
change. Scheduler identity fields must be removed from raw restore, checked as
generation metadata, and paired with typed semantic SubVM restore or a
fail-closed gate.

The 0x88-byte `g_abLuxMoveSystemVMPumpState` at `0x144100C70` is also absent
from Horse rollback. Native state 3 owns a deterministic 60/120-coordinate
terminal wait. State 4 sets both fighters to move `0x2B`, rebuilds normal
SubVMs, publishes guarded meter changes, finalizes both lane hit/effect states,
and may end the transaction. The native round-init loader selectively restores
the pump while preserving live lane dispatch identities at `+0x10/+0x40`,
which is direct evidence that the other fields are serialized control state.
Begin, state 4, and End are explicit identity-generation boundaries.
Four adjacent general battle-interface slots expose dual Start, single Start,
End, and an exact `dwPumpEnabled != 0` query through `this + 8` adjustment
thunks. The surrounding interface also owns ordinary round/stage operations,
so the pump cannot be classified as a replay-only diagnostic. The enabled
query does, however, provide a precise fail-closed gate for a deliberately
narrow beta while full pump restore is unavailable.

Finally, the adjacent `g_abLuxMoveCommandPlayers` arena at `0x14470F390` is
exactly two 0x3038-byte records (0x6070 total) and is not captured by Horse or
the native HgCpu global serializer. Its MoveVM parser/opcode/reaction/hit/timer/
control/ring-out/RNG-derived fields are mutated by pump lanes and scheduler
routes. Static analysis has not proven that the intended human-versus-human
rollback admission window keeps every such route dormant. An unrestricted
beta therefore needs typed semantic coverage; a narrow beta may instead fail
closed whenever an arena-mutating route is active. A raw 0x6070-byte copy is
not safe because the arena contains current-round identity pointers.

The authoritative layouts, factories, AllGuard derived-object recovery,
Ghidra completeness ledger, and restrained implementation order are in
[`rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md`](rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md).
No runtime validation was performed.

### 2026-08-06 — Tira corpus policy and Replay 102 cross-mode closure

Horse's intentional gameplay-RNG contract changes Tira's move availability.
Base-game Tira recordings therefore remain useful as historical input files,
but they are not valid state oracles for the modified game. The four known
base-game Tira replays were moved, without deletion, to
`E:\myMods\ReplayExample\baseGameTiraReplays`. The active corpus contains ten
base-game-compatible replays. Corpus manifest schema 5 records this policy and
fails closed for style ID 16 with required replacement
`horse-rng-contract-state-oracle`. Beta coverage remains 14 cases: the ten
compatible recordings plus four Tira recordings generated under Horse's RNG
contract. Tira RNG state and its gameplay consequences remain exact
peer-canonical state; only the incompatible base-game oracle provenance is
rejected.

Replay 102 initially appeared to fail normal-render versus online rollback at
round 0 logical frame 755. The first difference was one IEEE-754 ULP in P1
authoritative X (`1.0290004` versus `1.02900028`); RNG, round control,
breakables, stage-wind gameplay, character-animation gameplay, inputs, and the
second fighter all matched. At logical frame 786 the expanded hit-cue image
reported the analogous one-ULP cached-world-Z difference. Both online peers
remained bit-identical.

Ghidra resolves the authority boundary in
`ApplyBattleCharaMotionSlotRootMotionDirectPositionWrite @ 0x140306530`.
Cached world X/Z at playback-slot `+0x70/+0x78` can be multiplied by time
dilation and added directly to authoritative `ALuxBattleChara +0xA0/+0xA8`
while a live transition marker exists, so these fields cannot be reclassified
as presentation. Conversely, `nActiveCue == -1` selects the cache-clear return
at `0x14030659F` before any node or pose-lane residue is read. Round-2 slot-2
clip/frame/blend differences were therefore dormant residue on two inactive
slots, not gameplay state.

The qualification fix is confined to cross-mode diagnostics:

- normal-versus-rollback float constituents use the existing `1e-5` semantic
  tolerance, including packed hit-cue floats;
- integer gates, cue IDs, move state, input, all RNG families, component
  digests, and active hit-cue semantics remain exact;
- the raw v12 gameplay aggregate remains reported as an encoding diagnostic,
  because it hashes the raw bits of the same fully exposed float fields;
- host-versus-sandbox `canonical_hash` remains exact on every compared frame.

The revised localizer classifies all 1,780 retained pair-confirmed Replay 102
frames as presentation-only divergence, with no primitive, RNG, or peer
divergence. Fresh attempt 3 completed correction and round rearm and produced
hash-bound traces. Re-evaluating those exact artifacts with the corrected
boolean normalization passed both the 1,200-frame main window and 64-frame
round prefixes with zero state mismatches; maximum cross-mode float error was
`6.527e-06`. The run's stored first-pass report remains failed because it was
written before the `1` versus `true` normalization correction; the retained
trace hashes and offline re-evaluation are evidence for the tool fix, not a
replacement for the remaining corpus and external release gates.

### 2026-08-07 — Beta remediation status and audio terminal proof

The deterministic camera blocker is closed in source and Ghidra. Horse now
captures the complete 308-byte native common projection using 41 verified
ranges, carries typed per-vtable derived state, validates all component/world/
lifecycle identities before the first write, restores source/common/derived/
published layers in native-consumer order, and recaptures before the first
resimulated camera consumer. The raw HgCpu camera-component restore was
removed. Native-session bootstrap is also epoch-bound to SC6's session,
connect, transport, and session-connection identities; Horse no longer calls
`AcceptP2PSessionWithUser`, and stale asynchronous attempts cannot publish
readiness into a replacement epoch.

The presentation boundary remains the sole manifest `PendingEvidence` release
blocker. Ghidra proves that `LuxAudio_ResolveCueNameAndRegisterActiveVoice @
0x14054F6E0` performs current-object cue-sheet/shared-reference and FString
resolution before calling `LuxAudio_RegisterActiveVoiceInstance @ 0x14054F8B0`.
The latter is the first shared irreversible audio terminal, returns a
rand-derived live voice-instance ID, and has at least 19 native callers. Later
tracking and stop paths consume that live ID. `LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr
@ 0x14054F6D0` directly unwraps its owner and jumps to the same terminal.

The create/stop dependency is now exact rather than inferred. Ghidra names
`LuxAudio_StartTrackedScheduledVoice @ 0x140542490` and
`LuxAudio_StopTrackedScheduledVoicesByKey @ 0x140542730`. The start path
resolves its cue through `cueSource +0x08 -> link +0x28 -> leaf +0x24`, calls
the registration terminal, and appends an exact 8-byte
`FLuxScheduledVoiceBinding_Partial { int nVoiceKey; uint dwActiveVoiceId; }`
record. Native failure `0xFFFFFFFF` produces no record. The stop path treats a
negative signed key as stop-all; otherwise it scans every binding, queues a
stop for the stored native ID, removes the matching record, compacts the array,
and rewinds the index so duplicate keys are all consumed. The typed
`FLuxScheduledVoiceTrackingState_Partial` places the shared player at `+0x148`,
cue source at `+0x158`, bindings at `+0x168`, count at `+0x170`, and capacity at
`+0x174`.

The first proven asynchronous stop terminals are also named and typed:
`AppendStopVoiceRecordToOwnerQueue @ 0x140560D60` appends opcode 2 with a
specific native ID, while `AppendStopAllVoicesRecordToOwnerQueue @
0x140560940` appends opcode 1 with ID zero. Both mutate the 0x18-byte
`FLuxAudioCommandRecord_Partial` queue at owner `+0x28/+0x30/+0x34` while
holding the critical section at `+0x88`; the audio worker consumes the queued
operation later. These internal owner entrypoints and the scheduled pair are
documented in the saved Ghidra program. This proves that journaling only the
original listener request cannot preserve native create/stop semantics: the
rollback layer needs an epoch-bound logical handle, confirmed native-ID
allocation, and mapping-aware stop commit.

The directly relevant Ghidra database now contains typed
`FLuxCueSheetEntryOwner_Partial`, `FLuxResolvedCueEntry_Partial`, and
`FSharedReferenceController_Partial` uses, the named bounded conversion helper
`ConvertWideCharsToAnsiWithReplacement @ 0x1404E70B0`, the immutable
`g_awEmptyString @ 0x14325B7C8`, and the imported CRI name resolver at
`0x1440E4E00`. These facts rule out the tempting four-argument journal hook:
safe speculative create/stop behavior needs lifecycle-bound logical voice IDs
and complete lookup/stop-consumer coverage, while semantic listener-hub work
must still run at its source frame.

A source-frame listener callback proves that the native ID escapes the audio
owner and becomes persistent battle-handler state. The function now named
`InitializeDeferredRankedSoundAndStoreVoiceId @ 0x1403C7660` checks the
current round mode, clears `ALuxBattleSoundEventHandler +0x3E8`, sets the
request byte at `+0x3E5`, selects context route `0x50`, calls
`LuxAudio_PlayPrimaryActiveContextVoice @ 0x140550600`, and stores the returned
native voice ID at handler `+0x400`. That callback is registered by
`ALuxBattleSoundEventHandler_BeginPlay`, so it runs inside the listener graph
whose semantic work must remain at the original simulation frame. The
constructor initializes `+0x400` to `0xFFFFFFFF`; the callback is its only
direct native non-constructor access in the 0x408-byte class. Ghidra therefore
now types and names it `uint dwDeferredRankedVoiceId`. Reflected Blueprint
access remains lifecycle-bound and cannot be excluded by native xrefs alone.

The current-context object used by that route is now an exact 0x28-byte
`FLuxAudioActiveContextPair_Partial`: primary shared-player and reference-
controller pointers at `+0x00/+0x08`, secondary equivalents at
`+0x10/+0x18`, and primary/secondary cue IDs at `+0x20/+0x24`.
`LuxAudio_PlayCueIdByByte @ 0x140550900` independently verifies both lanes;
`LuxAudio_PlayPrimaryActiveContextVoice` verifies the primary lane and live
reference retention. This gives the journal an evidence-backed stable owner
selector, but the pointers themselves remain lifecycle identities resolved at
confirmed commit and must never be snapshotted or peer-hashed.

The reflected BGM/jingle API closes another part of the ID-consumer graph and
corrects an earlier parameter interpretation. The checked-in SC6 SDK dump
declares `ULuxBGMPlayerUtil::PlayBGM(int cue_id, int start_time_ms, int
fade_in_frame)`, `PlayJingle(int cue_id)`, and
`StopJingleByPlaybackId(int playbackId)`. Their native implementations are now
named and typed in Ghidra as `LuxAudio_PlayBgm @ 0x14054FB20`,
`LuxAudio_PlayJingle @ 0x1405503C0`, and
`LuxAudio_StopJingleByPlaybackId @ 0x1405617B0`. The corresponding UFunction
thunks at `0x1409E2230`, `0x1409E2320`, and `0x1409E3730` copy the returned
playback ID to reflected output storage or pass the reflected stop ID back to
native code. Thus playback IDs escape through both native handler fields and
Blueprint-callable APIs.

These xrefs prove that the signed middle argument of
`LuxAudio_RegisterActiveVoiceInstance` is a cue ID/selector, not a generic
pitch-shift value. Its verified contract is now
`(owner, cueSheetId, cueId, playbackFlags)`. For `PlayBGM`, the fourth value is
the reflected `start_time_ms`; the separately reflected `fade_in_frame` is
written to BGM state `+0x5C` before the terminal call. Ghidra now contains the
exact 0x10-byte `FLuxSharedAudioPlayerRefPair_Partial` and verified 0x60-byte
`FLuxBgmPlaybackState_Partial`: dynamic BGM player/reference array `+0x00`,
count/capacity `+0x08/+0x0C`, a distinct dedicated jingle
player/reference pair `+0x10`, cue-sheet ID `+0x20`, one-shot suppression
`+0x48`, active lane `+0x4C`, lane gains `+0x50`, crossfade multiplier
`+0x58`, and fade-in frames `+0x5C`.

`LuxAudio_PlayBgmThroughCrossfadeState @ 0x14054FD10` proves that those fields
are semantic source-frame state: it may switch lanes, enqueue stop-all for the
replacement owner, reset that owner, update both gains and the multiplier,
publish the selected gain, and only then call the active-voice terminal.
`LuxAudio_PlayJingle @ 0x1405503C0`, `LuxAudio_StopJingle @ 0x140561720`, and
`LuxAudio_StopJingleByPlaybackId @ 0x1405617B0` validate the dynamic BGM lane
array but route their create/stop operation through the separate dedicated
jingle pair at state `+0x10`; they do not use dynamic BGM lane 1. The
ID-specific stop enqueues opcode 2 with the exact playback ID.
`LuxAudio_StopBgm @ 0x140560B60` enters a separate state-mutating BGM stop
routine. A correct rollback boundary must therefore preserve the BGM
crossfade state at its source frame and carry three distinct owner domains:
dynamic BGM lanes 0/1 and the dedicated jingle player.

The reflected system-sound stop surface is also now complete. Executable
UFunction registration-table entries bind `StopSE`, `StopSEByPlaybackId`,
`StopVoice`, and `StopVoiceByPlaybackId` to native functions
`0x140561870`, `0x1405618F0`, `0x140561A90`, and `0x140561B10`. The SE
pair resolves manager active-context `+0xA0` primary player `+0x00`; the
voice pair resolves the same context's secondary player `+0x10`. Stop-all
calls append opcode 1 through `AppendStopAllVoicesRecordToOwnerQueue`, while
ID-specific calls append opcode 2 through
`AppendStopVoiceRecordToOwnerQueue`. Ghidra now contains the exact
0x150-byte `FLuxCriAtomManager_Partial`, including BGM state/reference at
`+0x90/+0x98` and active-context/reference at `+0xA0/+0xA8`, and the
singleton/initialization-guard globals are typed and named.

Consequently, a correct implementation must snapshot or shadow every native
semantic field that receives a speculative logical voice ID, including the
handler `+0x400` field and the scheduled binding array, then translate logical
IDs to epoch-matched native IDs only at confirmed create/stop terminals. A
Horse-only logical table would leave speculative native IDs in restored game
objects and is therefore insufficient. The remaining ID consumers and owner
domains must be inventoried before production audio dispatch can be enabled.

The direct-terminal caller inventory also closes a previously omitted
scheduled-player owner family. `LuxAudio_ApplyScheduledPlayerCueSwitch @
0x140542210` is a callback installed by the native schedule driver at
`0x140555B90`. Ghidra now types its source as the 0xF4-byte
`FLuxScheduledAudioPlayerState_Partial`, with a 0x30-stride player array at
`+0xE0`, count/capacity at `+0xE8/+0xEC`, and source-frame current BGM cue at
`+0xF0`. Each `FLuxScheduledAudioPlayerEntry_Partial` has the shared player,
reference controller, and cue-sheet id at `+0x00/+0x08/+0x10`.

Normal selectors stop and recreate an indexed scheduled player; selector 10
performs a multi-owner transition that stops the special/default scheduled
lane and BGM state before publishing the replacement BGM cue. Therefore an
audio owner selector also needs a deterministic schedule identity plus player
index. An array address or UObject pointer is lifecycle metadata only and is
not a stable journal value. This new family remains an explicit admission
blocker until the schedule identity and its `+0xF0` state are captured and
restored.

The direct caller at `0x1405507E0` is now named and typed as
`LuxAudio_StopAndPlaySharedVoice(FLuxVoicePlayerState_Partial*, int, float)`.
Its only xref is `PlayPreviewHumanActorVoice @ 0x1404719E0`. The executable
UFunction table pairs that function's thunk with the literal `PlayVoice`, and
the adjacent table entries are `PlayMotionByMotionData` and `PlayTrace`,
matching the checked-in `APreviewHumanActor` SDK surface. Ghidra now has the
0x18-byte voice-player state and the independently accessed 0x6E0-byte actor
audio projection. This route is proven preview/UI passthrough outside an owned
BattleManager simulation iteration; it is not another battle owner domain.

The equivalent VFX chain is now grounded through its live UObject boundary.
`ALuxBattleVFxEventHandler_BeginPlay @ 0x1403B5530` registers source-frame
callbacks for enable, disable, grouped/side-track allocation, move-mask
allocation, single-slot allocation, hip-bone allocation, situation state,
collection-11 teardown, and a subsystem command. These callbacks are not
terminal presentation calls: they resolve current move/provider data, build
fully resolved spawn requests, allocate persistent numeric slot IDs, update
handler-owned ID and situation lists, and publish Blueprint callbacks.

All proven primary allocation routes converge on
`BattleMgrSubsystem_LookupOrAllocateMeshActorSlot @ 0x1408A2660`. It selects
the current normalizer and authored bucket, then branches to
`AllocateParticleSystemMeshActorSlot @ 0x1408A3C90` or
`AllocateGroundDebrisMeshActorSlot @ 0x1408A3300`. The primary branch creates
a particle component, sets its template/transform/parameters, binds
`OnParticleSystemFinished`, registers it in the persistent 0xC0-byte slot
table, and returns the slot ID. The fallback branch acquires a pooled debris
actor, initializes component-ring and transform state, activates it, binds
`OnGroundDebrisDeactivated`, registers another 0xC0-byte live-slot record, and
returns its slot ID.

The first primary UObject allocation is now named and typed as
`InitializeParticleSystemComponentFromTemplate @ 0x140898490`. It calls
`StaticConstructObject_Internal`, installs component creation flags, and calls
the corrected `SetParticleSystemComponentTemplateAndResetState @
0x141F82C70`. The latter was previously mislabeled as a skeletal-animation
helper; its actual behavior waits for particle-component async work, swaps the
particle template, rebuilds or activates render resources, clears dynamic
parameters, resets async-entry state, and dirties render state. Ghidra now has
the evidence-backed `UParticleSystemComponent_TemplateState_Partial` (0xA82),
`UParticleSystem_TemplateMetadata_Partial` (0x38), and
`FLuxParticleAsyncEntry_Partial` (0x24) projections, plus the lifecycle gate
`g_fParticleTemplateUpdatesEnabled @ 0x1440956B8`.

This rules out a null-return suppression hook at component construction: every
caller immediately configures, binds, registers, and publishes the returned
identity. It also rules out journaling only the original hub packet, because
the slot ID and resolved request depend on source-frame semantic state. A
correct speculative path needs a typed shadow slot/component identity that can
accept all downstream semantic writes without constructing or activating a UE
object; confirmed commit must materialize that shadow under the same lifecycle
epoch and bind the resulting native slot ID. The handler-owned ID lists and
manager live-slot tables must be included in rollback ownership or completely
redirected to that shadow path.

The slot-ID writer and disable consumer are now closed far enough to define
the first pointer-free shadow primitive. Ghidra names and types the mirrored
writers as `AppendParticleLiveMeshActorSlotRecord @ 0x140896520` and
`AppendGroundDebrisLiveMeshActorSlotRecord @ 0x140896410`. Both copy the
subsystem-wide signed counter at `+0x3E0`, append one 0xC0 record, increment the
same counter, and wrap `0x7FFFFFFF` to zero. Their dedicated arrays are
`+0x3E8/+0x3F0/+0x3F4` for particle components and
`+0x3F8/+0x400/+0x404` for debris actors. The counter proves its namespace and
wrap policy, but not that a sign-bit ID is usable. In fact,
`LuxMove_AllocateMeshActorSlots_WithRemap @ 0x1403C5830` and the other
source-frame producers register ownership only when the returned `int` is
nonnegative. Horse therefore reserves the nonnegative
`0x40000000..0x7FFFFFFF` band. Production admission must verify the native
counter is below that band and bound all confirmed allocations so it cannot
enter the reserved range during the lifecycle epoch.

`ApplyVfxDisableFilterToPrimarySlotTable @ 0x1408A1BB0` and
`ApplyVfxDisableFilterToSecondarySlotTable @ 0x1408A19F0` prove the exact
source-frame match projection: effect ID, kind tag, kind argument, group, and
the request `+0x14` time-scale selector, with signed wildcards and a
primary-only wildcard guard. Immediate
commands deactivate the live UObject, dispatch/prune weak slot callbacks, and
erase the compacted record. Retained secondary records instead mutate actor
fade/lifecycle clocks. `HandleDisableVfxEvent @ 0x1403C5250` resolves authored
transitions before applying the filter and only then publishes the Blueprint
event. Therefore a confirmed journal must contain the logical IDs selected at
the source frame; rerunning the filter later could affect objects that did not
exist when the command was issued.

`RollbackVfxSlotShadow` now implements that evidence-backed core without any
UObject or allocator pointer. It reserves deterministic nonnegative logical IDs,
stores the five-field match key and primary wildcard guard, resolves a filter
atomically to an exact logical-ID set, preserves create-before-disable commit
ordering, binds a nonnegative native ID under one lifecycle epoch, and retires
the mapping only after terminal success. Its attachment projection now stores
only one of `None`, `WeaponFrontEdge`, `SkeletonBone`, or
`PlayersChestMidpoint`, with the proven character role and authored selector
inputs. Invalid front-edge and bone selectors fail before shadow mutation.

This is scaffolding, not production admission. `ALuxVFxInstanceManager_TickActor
@ 0x1408A4510` proves that each retained 0xB0 request remains live after
allocation: `UpdateLiveMeshActorSlotTransform @ 0x1408A4B90` re-evaluates the
attachment/provider every tick, `ApplyLiveMeshActorTimeScaleOverrides @
0x1408A4C80` publishes current manager-table values, and
`ApplyLiveMeshActorVisibilityFilters @ 0x1408A4A00` applies current wildcard
filters recursively to both slot tables. These are direct per-tick terminal
bypasses. Lower allocator hooks, stable asset/config resolution, semantic
reconstruction of the retained provider, handler ID-list redirection, manager
time-scale/visibility ownership, and all remaining direct bypass paths still
have to be closed before VFX commits can be enabled.

One independent ledger defect was repaired during this pass. Terminal commit
callbacks now return success; confirmation counts an event only after its
native terminal succeeds. A per-bucket commit cursor retains the successful
prefix so retrying a failed bucket cannot replay already committed operations.
Audio and VFX remain fail-closed rather than being falsely advertised as
supported until their terminal/shadow paths are complete.

Production status now separates the installed allocator/wind/camera-vibration
support hooks from the still-incomplete terminal boundary. The legacy
`presentation_hooks_installed` field describes that five-hook support set;
`presentation_terminal_dispatch_complete` remains false and the public-beta
acceptance runner requires it. This removes the former possibility that the
0x7C support-hook mask could be interpreted as proof of audio/VFX ownership.

The current post-status-split Shipping DLL (`SHA-256
e59d7ed7c585426572497e1277ee1c8b274d756716e165515e5476ee5aebd48f`)
passes all 68 CTest cases, the stock-online policy lint, and the runner
self-test. The immediately preceding deterministic candidate (`SHA-256
2e17bbe5e39d9933f10a04afbb8fe5d886a1999308a165055b604d41da46a77a`) has
the artifact-bound normal-render strict replay run `20260807-032747-seek`,
which passes all four
600-frame watch cases: 2,400/2,400 state comparisons, zero mismatches, and a
maximum seek-land interval of 0.43 seconds against the 0.50-second limit. Its
report is `reports/replay_tests/replay_seek_e2e_20260807-032747-seek.json`
(`SHA-256
3f0c6333108d0ea9bfedbec7254c177f22a4323e710b39797f2f0129a6449922`).
This validates the implemented deterministic-state work; it does not exercise
the still-blocked speculative audio/VFX terminal contract.

After the manifest evidence was tightened to the proven audio-ID and VFX-slot
requirements, a clean rebuild exposed a stale self-test expectation: the
snapshot test still required two pending gameplay entries even though camera
ownership had already closed and presentation dispatch is now the sole
`PendingEvidence` entry. The test now requires exactly one pending entry and
also verifies its `PresentationDispatch` capability and exact manifest name.
The rebuilt Shipping artifact is `HorseMod.dll` SHA-256
`064c5eb670bdcfacb99deb9e7f8f9a268399306489bbcc0d2c61e319cd68cabb`.
All 68 rollback CTest cases, stock-online policy lint, and the runner synthetic
self-test pass. This artifact remains intentionally non-qualifying because its
presentation terminal capability is false.

After adding the pointer-free VFX slot shadow and atomic source-frame filter
selection, the Shipping DLL rebuilt as SHA-256
`a44e54d3c823c808faacfb33a1a55c33ac7dbd9543b60052afd16fe5a9304492`.
The focused VFX self-test and the full 68-test `rollback-fast` label pass. No
gameplay hook was enabled by this milestone, so the earlier strict replay
evidence remains historical context rather than qualification for this new
artifact.

The follow-up native request audit corrected the retained spawn request from a
flat 0x70-byte value to an exact 0xB0-byte request: a 0x70-byte value
projection followed by 0x38 bytes of polymorphic transform-provider storage
and 8 bytes of tail padding. `ALuxVFxInstanceManager_TickActor @ 0x1408A4510`
re-evaluates that provider for every live primary slot through
`UpdateLiveMeshActorSlotTransform @ 0x1408A4B90`, then publishes the manager's
current time-scale and visibility filters to both live slot tables. The Horse
shadow now retains only the proven semantic attachment forms (weapon
front-edge selector, skeleton-bone selector, or players' chest midpoint) and
rejects raw FName/UObject/callback identity. The current Shipping DLL is
SHA-256
`db12eb869d13dc8812540b17666a1dc2f7400b257e87578a96193c991dc16396`;
the focused VFX self-test, all 69 registered CTest cases, the stock-online
policy lint, and the runner synthetic self-test pass. This is still
non-qualifying: no lower terminal hook or confirmed-epoch provider
materializer exists yet, and the direct per-tick time-scale/visibility paths
remain outside rollback ownership. Because this milestone changed only the
fail-closed logical shadow and evidence, with no gameplay hook enabled, no new
strict replay run was claimed for this artifact.

### 2026-08-07 — Shared audio-terminal caller partition

The direct caller inventory for `LuxAudio_RegisterActiveVoiceInstance @
0x14054F8B0` proves that it cannot be intercepted as a battle-only terminal.
Ghidra now names, prototypes, and documents these independently registered
non-battle/reflected families:

- `LuxAudio_ApplyCharacterVolumeSettingPreview @ 0x14054E180`,
  `LuxAudio_ApplyNarrationVolumeSettingPreview @ 0x14054E330`, and
  `LuxAudio_ApplySeVolumeSettingPreview @ 0x14054E3C0` are the
  `LuxEBVModeSetting` option callbacks. They change a native category gain,
  stop the menu preview owner, and allocate a replacement preview voice.
- `LuxAudio_HandleStageBgmMenuEvent @ 0x14054EBA0` is registered as
  `LuxStageBGMSetting.OnReceiveMenuEvent`. It decodes `category`, `trigger`,
  and `locate`, selects a 0x10-stride menu owner entry, and reaches the same
  irreversible allocator.
- `LuxAudio_PlayAllManagerLanes @ 0x14054FA10` and the BGM/SE/voice lane
  wrappers at `0x14054FCC0`, `0x1405505B0`, and `0x1405508B0` are reflected
  controls sharing three lifecycle-local owner/cue-sheet pairs.
- `LuxAudio_StopAndPlayExplicitVoice @ 0x140550840` queues StopAll and then
  reaches the same allocator through another reflected `PlayVoice` path.

The corresponding Ghidra types are
`ULuxEBVModeSetting_Partial`, `ULuxStageBGMSetting_Partial`,
`FLuxAudioVoiceOwnerEntry_Partial`,
`FLuxAudioThreeLaneOwnerSet_Partial`,
`FLuxSingleVoiceOwnerFacade_Partial`, and the typed three-stage cue-sheet
handle chain ending in `FLuxAudioCueSheetIdentity_Partial::dwCueSheetId` at
`+0x24`. The common seven-step option multiplier at `0x1432D75FC` is now typed
and named `g_flLuxAudioOptionVolumeScale`; its exact float value is `1/7`.

This changes the hook contract. Calls outside Horse's owned complete battle
iteration must pass through stock so menu/settings audio remains functional.
Calls inside an owned iteration may be journaled only after the raw owner has
been resolved to an accepted lifecycle-local `RollbackAudioOwnerSelector`.
An unknown or invalid owner inside the owned iteration must fail closed because
the terminal mutates the active map and returns a playback ID consumed by later
semantic code. `SelectRollbackAudioTerminalRoute` and its focused self-test now
encode this partition. They are admission scaffolding only: no production
allocator/stop hook is enabled, and presentation remains the sole manifest
`PendingEvidence` entry.

The first lifecycle-identity primitive is now implemented as
`RollbackAudioOwnerResolver`. It is deliberately pointer-facing only at its
construction boundary: accepted native owner addresses are bound to stable
`RollbackAudioOwnerSelector` values under one nonzero lifecycle epoch, then
the table is sealed before activation. The resolver rejects publication before
seal, pointer/selector aliasing, capacity exhaustion, stale epochs, mutation
after seal, unknown owners, and use after revoke. Raw pointers never enter an
invocation, snapshot, canonical hash, or peer packet. Production still does
not install the shared terminal hook because the complete native owner graph
has not yet been populated and atomically preflighted; this primitive removes
one implementation ambiguity without weakening the existing fail-closed
`PendingEvidence` admission rule.

The directly relevant Ghidra program was saved through MCP after the function,
prototype, local/global type, partial-struct, and comment updates above. The
checked-in structured exporter was then attempted and failed closed before
export because the connected MCP schema did not expose the required read-only
`/run_script` endpoint (the other required program endpoints were present).
No refreshed structured export is claimed for this pass.

### 2026-08-07 — Character subscriber terminal closure

The complete direct listener inventory resolves the earlier collections
25-29/35 uncertainty. Collections 27 and 28 are synchronous read-only queries
and remain source-frame passthroughs. The remaining mutable character routes
are covered by ten hooks at the registered subscriber targets, preserving the
hub broadcast and callback topology while moving only stateful presentation
work behind confirmation. The targets are `0x1403B17CC`, `0x1403B17D8`,
`0x1403B17F0`, `0x1403B17FC`, `0x1403B1808`, `0x1403C6050`, `0x1403C51A0`,
`0x1403C4CD0`, `0x1403C4D50`, and `0x1403C5050`.

The journal record contains a semantic operation, character role, and an exact
0/8/12/64-byte native value. Confirmation resolves the actor from the sealed
two-character lifecycle identity and invokes the exact trampoline. Failed
resolution or invocation does not increment the committed count and fails the
owned session closed. This closes weapon setup, phase trace reset/activation,
Soul Charge publication, color fade, break/attack reset, player visibility,
weapon-node alpha, and material charge without serializing UObject identity.

`PresentationDispatch` is therefore no longer `PendingEvidence`; it is a
tested `DynamicSnapshot` capability. The manifest validator now reports live
coverage with zero pending entries. The same validation exposed and corrected
the camera entry's stale zero-address `ExplicitSnapshot` classification. The
focused character, semantic snapshot, ledger, and manifest tests pass, as do
all 71 CTest cases and the stock-online policy/synthetic runner checks.
