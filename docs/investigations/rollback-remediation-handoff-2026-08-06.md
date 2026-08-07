# Rollback remediation handoff — 2026-08-06

This handoff records the current source/Ghidra boundary after implementing the
first remediation slice. Addresses are for the supported `SoulcaliburVI.exe`
image base `0x140000000`.

## Implemented and statically grounded

| State | Native evidence | Ownership now | Peer policy | Restore order |
|---|---|---|---|---|
| MoveVM slot parameters | `g_abLuxMoveVMSlotParamArray @ 0x14470E0C0`, `FLuxMoveVMSlotParam[2]`, 0x2C stride; advanced by `LuxMoveVM_AdvanceSlotParamLerp @ 0x14032F780` at the tail of `LuxBattle_PerFrameTick @ 0x1402DBC60` | Typed two-lane capture; restore and recapture cover each authoritative +0x00..+0x27 prefix | Both 0x28-byte semantic prefixes canonical; initialization-only +0x28 padding excluded | Before the first resimulated MoveVM advance |
| Camera frame publication | `CopyLuxBattlePublishedCameraInfo @ 0x1402D7980` loads six float4 vectors from `0x14470D1A0`, yaw from `0x14470D0DC`, mode from `0x14470D198` | Typed vector/yaw/mode capture and restore | All values canonical | Published last |
| Camera director controls | `LuxBattle_UpdateBattleCameraSynthesis @ 0x14031EA50`, weight/blend/slow-motion callees | Transition/blend controls, published output, and proven component fields | Values canonical; identities local-only | director controls → component semantic fields → published output |
| FP control | scalar SSE paths in camera synthesis and MoveVM | Versioned MXCSR/x87 scope around the exact owned `SimulationLoop @ 0x1403FE520` trampoline | Policy ID/constants are in schema hash | caller environment restored after every owned iteration |

The camera/HgCpu timer alias is native-proven: `LuxCameraDirector_Initialize @
0x140321D90` publishes the same timer/action root through the timer config and
director `+0x7A0`. The existing HgCpu timer restore now preflights the indexed
table, all 16 component roots, vtables, writer slots, and every node backing
before its first write.

The restore corridor now also resolves lifecycle admission before its first
native write. Previously the live round/world token was checked only after
HgCpu, camera, and palette restoration had started; a stale-generation load
could therefore partially mutate the process before rejection. Required
palette-writer-registry admission and the live epoch/token check now precede
all component restores.

## Corrected native model

The previous `FLuxBattleCameraFramePublished_Partial` interpretation was too
large and conflated separate globals. Assembly at `0x1402D7980` proves:

- `0x14470D1A0..0x14470D1FF`: six float4 value groups (0x60 bytes);
- `g_flLuxBattleCameraYawTurns @ 0x14470D0DC`: float;
- `g_dwLuxBattleCameraMode @ 0x14470D198`: uint.

Ghidra now uses `FLuxBattleCameraFrameVectors` for the 0x60-byte bank and has
typed/labeled the two separate scalars.

## Steam P2P connection-failure terminal evidence

The `P2PSessionConnectFail` callback is now a first-class local lifecycle
terminal instead of a logging-only observation. Native evidence establishes
this ordering:

1. `HandleSteamP2PSessionConnectFailCallback @ 0x1429D10B0` copies the remote
   Steam ID and error byte into a queued 0x40-byte async event.
2. `DispatchSteamP2PSessionConnectFailEvent @ 0x1429BD390` resolves the live
   Steam subsystem when that event is dispatched.
3. `HandleSteamP2PSessionConnectFailForTrackedConnections @ 0x1429B6BB0`
   tears down every matching `SteamNetConnection` and removes the failed
   remote from native P2P tracking.

Horse therefore publishes a bounded value-only observation from the first
callback before invoking SC6. Each record contains sequence, current local
native lifecycle serial, remote Steam ID, and error byte. The game thread
accepts it as terminal evidence only when both lifecycle serial and current
peer Steam ID match. Old-lifecycle and unrelated-peer callbacks are ignored;
an overwritten callback stream fails closed for an active peer because the
lost record cannot safely be classified. No native connection pointer,
callback object, or other generation-dependent identity crosses threads.
The controller services this lightweight handoff before publishing the Steam
identity on every production game-thread tick, including the local
qualification lane after its heavy setup observer has stopped at `Active`.

Ghidra records the callback data/event types, the complete callback-to-teardown
chain, and the named/typed
`g_apfnSteamP2PSessionConnectFailEventVTable @ 0x143BAB420`.

## Transactional Load and client-local stall counters

Production `Load` now separates preparation, native mutation, and local commit.
Confirmed-summary, terminal-candidate, restored-terminal-queue, and deferred
notification conditions are checked before the first native write. A dedicated
`RollbackStepState` with the frozen baseline's exact capacities captures the
complete pre-Load state without allocation. Internal restore failures recover
through that snapshot; canonical-recapture or derived-cache repair failures
perform the same undo and recapture before failing closed. Presentation-ledger,
summary, terminal, and fixture bookkeeping is committed only after target
restore and repair succeed.

Stage-wind restore now preflights graph-pool ownership plus the complete live
list-node/emitter identity sequence before writing output-active, combined RNG,
graph, or emitter state. Mutable graph/emitter values are intentionally not
validated as ownership because speculative values are precisely what Load
replaces.

`UpdateLuxBattleOnlineFrameStallCounters @ 0x1403FDEC0` writes the client-local
diagnostic counters at `BattleManager +0x1638/+0x163C`. Horse captures and
restores those two values around rollback resimulation calls only. Ordinary
owned forward iterations retain SC6's native updates, and the counters remain
outside snapshots and peer hashes.

## Intentional production blockers

Production remains fail-closed with one `PendingEvidence` manifest entry:

1. Presentation terminal dispatch: current 38 listener-hub hooks pass through
   at source time, while audio/VFX journal commits are rejected. Descriptor
   normalization is not terminal suppression or exactly-once confirmed commit.

### 2026-08-07 listener-graph ownership update

The source now performs an activation-time inventory of all 41 callback
collections rooted at `ULuxBattleEventListenerHub +0x30` (0x70-byte stride).
It resolves every weak-object target, requires every callback dispatcher and
bound function to belong to the supported executable, and incorporates the
entry, target-storage, resolved-object, vtable, and effective-function
identities into an aggregate topology digest. Collections 8, 9, 21, 32, 33,
36, and 37 must be empty because the complete direct native registration pass
found no producers for them. Any stale weak target, over-capacity collection,
unexpected callback in a required-empty collection, or identity change keeps
activation fail-closed.

This pass also resolved three concrete reversible-state owners:

- `HandleLuxRoundPhaseAudioEvent @ 0x1405106A0`, collection 10, writes
  `ALuxBattleRoundPresentationActor_Partial::fRoundPresentationActive @ +0x489`;
- `HandleLuxRoundStateBgmEvent @ 0x140510AF0`, collection 20, writes
  `ALuxBattleRoundPresentationActor_Partial::fStateBgmUpdatePending @ +0x494`;
- `UpdateLuxActorActiveFlagFromBattlePhaseEvent @ 0x140470450`, collection 10,
  writes `ALuxBattlePhaseActiveActor_Partial::fActive @ +0x4B8`.

Ghidra now has the exact one-byte `FLuxBattlePhaseEvent_Partial`, the minimal
1209-byte `ALuxBattlePhaseActiveActor_Partial`, and the typed/renamed
`InitializeLuxBattlePhaseActiveActorOnBeginPlay @ 0x14044F5F0`. The callback
is documented at 100% completeness and its BeginPlay owner at 92.98% (the
remaining deduction is compiler-projected state). These three fields are not
yet restored by Horse; their identities are now proven and bound so typed,
atomic restore can be added without guessing UObject ownership.

The remaining blocker is therefore narrower but still real: add lifecycle-
preflighted capture/restore for these fields, then close collections 25-29 and
35 plus the remaining direct terminal/commit-failure paths before changing
`PresentationDispatch` from `PendingEvidence`.

Gameplay camera state is now an `ExplicitSnapshot`: every live component uses
the native serializer selected by vtable `+0x100`, including the common
308-byte projection and the proven Game/Great, PlayerWatch, Attention, and
Stay/Free subtype payloads. Do not change the remaining presentation entry to
complete until its native boundaries are closed. The manifest states the actual
behavior and cannot qualify production through the former VFX descriptor-only
self-test.

## Verification completed

- `RollbackLuxMoveStateSelfTest`: two-lane semantic mutation/restore/canonical
  test, including proof that +0x28 stride padding is untouched and excluded.
- `RollbackBattleCameraSnapshotSelfTest`: canonical mutation, identity drift,
  no-write atomic rejection, restore ordering, and recapture.
- `RollbackFloatingPointEnvironmentSelfTest`: perturbation, nesting rejection,
  policy installation, and exact caller restoration.
- `RollbackSnapshotSelfTest`: expects and proves the one-entry production
  fail-closed gate.
- Full HorseMod `LessEqual421__Shipping__Win64` build and rollback ON/OFF
  matrix succeed with LTO enabled.
- The complete rollback CTest label passes 69/69 tests.
- Required normal-render strict replay seek passed in run
  `20260807-103220-seek`: 4/4 cases, 2400/2400 comparisons, zero mismatches,
  and 0.42 s maximum seek. Reports are under
  `reports/replay_tests/replay_seek_e2e_20260807-103220-seek.{json,txt}`.
  Candidate DLL SHA-256:
  `29D03DF8BF97DEBBF46C0CC0AC3684048085C66E3022C335A6C937A1CAC1DCEA`.
  Replay SHA-256:
  `95E12E394D35C13D5E0DD3DCE692F9E0A4022E2A84205A9EC75F2FA6726D7879`.

Broader runtime qualification remains intentionally blocked. The strict replay
pass validates this candidate's exercised replay corridor, but cannot convert
unresolved presentation terminals into production coverage; two-client,
corpus, lifecycle, fault, soak, and physical-machine release gates therefore
remain outstanding.

## 2026-08-07 presentation closure and live manifest transition

The former presentation blocker is now closed in source and in the native
evidence model. The listener hub itself remains unpatched: all 38 broadcast
routes and native reverse-order subscriber traversal execute at the source
frame. Ghidra proves collections 27 and 28 are read-only custom-root and bone
transform queries. Ten stateful or irreversible character subscribers are now
intercepted at their registered native callback targets instead:

- weapon-bone and weapon-actor setup virtuals at `0x1403B17CC` and
  `0x1403B17D8`;
- break/attack reset and phase trace callbacks at `0x1403B17F0`,
  `0x1403B17FC`, and `0x1403B1808`;
- Soul Charge, effect-color fade, visibility, weapon-node alpha, and material
  charge targets at `0x1403C6050`, `0x1403C51A0`, `0x1403C4CD0`,
  `0x1403C4D50`, and `0x1403C5050`.

Owned speculative calls are suppressed and encoded as a fixed 68-byte
`RollbackCharaPresentationInvocation`: operation, player role, exact native
value length, and at most 64 value bytes. No actor, component, callback, or
vtable pointer enters the ledger. Confirmation revalidates the sealed
presentation epoch, resolves the current character by role and exact vtable,
selects the corresponding trampoline, and reports success only after the
native call completes. The lane is bounded, ordered, deduplicated by native
frame ordinal, discarded on rollback, and fails closed on identity, queue, or
commit failure.

Ghidra now contains `FLuxBattleCharaMaterialChargeEvent` (0x0C),
`FLuxBattleCharaSoulChargeStateEvent` (0x08),
`FCallbackEntryCollection_Partial[41]`, and
`FLuxBattleEventDispatcherView` (0x11F8), plus typed dispatcher adapters,
named callback targets, corrected locals, and PRE/EOL/plate documentation.
The program was saved through MCP. The checked-in structured exporter was
attempted and failed closed because this connected MCP schema does not expose
the required `/run_script` endpoint; no refreshed export is claimed.

`PresentationDispatch` is now a complete `DynamicSnapshot` capability and the
production manifest has zero `Unknown` or `PendingEvidence` entries. During
that transition the live manifest test uncovered another independent stale
declaration: the already-implemented typed camera snapshot had been labeled as
a zero-address `ExplicitSnapshot`, which made validation fail with
`invalid-manifest-entry`. It is now correctly declared `DynamicSnapshot`.
Compatibility moved once to runtime ABI 48, canonical schema 35, and runner
snapshot version 38.

The complete Shipping build and all 71 registered CTest cases pass. The
stock-online source policy lint and runner synthetic self-test also pass. A
new strict normal-render replay and local/physical two-client qualification
must still be bound to the final immutable DLL before public-beta release.
