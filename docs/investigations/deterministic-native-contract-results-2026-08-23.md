# Deterministic native contract results

## 2026-08-24 bounded seek/resume integration (schema 17)

- Replay capture now retains an immutable, pointer-free canonical hash for
  every captured frame in a preallocated 16 MiB timeline. The budget is
  deducted from the existing 512 MiB replay cap; opaque reconstruction images
  remain local and outside the canonical history.
- `ExecuteOwnedStateSeek` now acquires native restore ownership, requires both
  target and source-end hashes, restores transactionally, verifies the landing
  hash, rebases the runtime cursor, and enters a resume-validation state.
- While resuming, authoritative input must match the immutable baseline,
  batch/checkpoint stores are not duplicated, and each live frame is recaptured
  and compared to its original canonical hash. Capture resumes only after the
  original source-end coordinate is reached at an outer-batch boundary.
- HorseMod exposes a test-only in-process request/status boundary. Requests are
  queued from qualification tooling but executed only on the verified
  simulation outer-tick fencepost. ReplayQualificationMod request schema 3 can
  exercise ordered 10/25/50/75-percent seeks without adding a launcher,
  transport, filesystem poller, or production fault injection to HorseMod.
- Local evidence before runtime deployment: all three CTest targets pass and
  all 14 deterministic qualification Python tests pass. Normal-render runtime
  evidence is still required before this slice can be called qualified.

## Build and Ghidra identity

- Executable identity/hash: SHA-256 `F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`
- Ghidra program: `SoulcaliburVI.exe` (`x86:LE:64:default`, image base `0x140000000`)
- Analysis date and agent: 2026-08-23 through 2026-08-24, Codex
- Runtime evidence generated in this pass: the normal-render 600-frame HgCpu coverage report at `reports/deterministic/hgcpu_coverage_runtime.md`, SHA-256 `684F6FC9A1A5D26C8B9030B02DB6CA74AF51005C0F38147BD6C00FCA780675EB`, was produced by the deployed qualification build and is summarized below. Historical corroboration only: `reports/replay_tests/replay_seek_e2e_20260806-042205-seek.json`, SHA-256 `A2ABAD8B71671ECB47E367B502B43C876F503D129D9DC7496ED21DC5EA6E4B0F`, records the retired ABI-45/schema-32 MoveCommand implementation passing 4/4 normal-render cases and 2,400/2,400 comparisons for DLL `534DAC013161D0A84B0B39B0089BB490FDEB59275A56AC56F0AC0142ECBB7357`. It is not qualification for a new adapter artifact.
- Extended fencepost diagnostic: commit `C8CFECBEBE92D80B9F5BCBE5868CCF7A41CAA2F3`, DLL SHA-256 `232F1F704A257E77FFBCFB8EA143CE58B188245A218622462EA54D5160F18F4B`, and generated schema SHA-256 `26C05E04388D88BC1CB7C19F583B1DA59D7A239738D589470D904833F1CE1E18` completed 600 normal-render coordinates on `REPLAY_12744704008398858106.bin`. The `0xFFF` observation surface remained complete, checkpoints landed at frames 1, 30, 300, and 600, and the bounded summary reported one native repeat request, three same-native-time coordinates, zero manager/InputLog cursor mismatches, and no hook or checkpoint failure. The non-certifying dirty-tree report is `tools/deterministic_qualification/output/replay-entry-report.json`, SHA-256 `768E0FCE9DC91348592B70C9306DB777F9BA1AABA2A4683B2130BAC73F84A48A`; it is runtime evidence only, not release qualification.
- Governing sources: [agent plan](deterministic-native-contract-agent-plan-2026-08-23.md), [simulation contract](deterministic-simulation-contract-2026-08-23.md), [Interfaces.hpp](../../HorseMod/horselib/deterministic/Interfaces.hpp), [Types.hpp](../../HorseMod/horselib/deterministic/Types.hpp), and [Schema.hpp](../../HorseMod/horselib/deterministic/Schema.hpp)

## Executive status

### Implementation follow-up (2026-08-24)

The ten statically closed candidate regions are implemented as inactive,
compiled components in `HorseMod/horselib/deterministic/NativeCandidateRegions.*`.
The implementation stores no native address in its value image, validates all
local identities before the first write, restores only the semantic banks in
the table below, recaptures exact semantic state, and applies an exact
reverse-order undo after a partial write or verification failure. It remains
absent from `Schema::production_regions`.

Checkpoint schema v5 extends that inactive candidate with the coordinate and
input-production boundary required before native replay resimulation can be
wired. The value image now includes the global Lux frame counter, InputLog and
manager round/time cursors, round-state frame, unpause/repeat/move-state
scalars, the two previous-input words, both current/prior post-filter input
pairs, the pointer-free `+0x390..+0x3BF` InputLog scalar bank, and all 1,024
semantic cache rows. Each cache row serializes only `{GameRound, FrameIndex,
InputValue, Filled}`; the three initialized-reserved bytes at `+0x0D..+0x0F`
are preserved from the live allocation and excluded from restore/hash. Binding
requires the same InputLog UObject/class and the same three manager-owned input
array allocations with active-player count exactly two. Restore never writes
their headers or pointers. Structural tests mutate every new bank, prove exact
transactional restoration, prove reserved-row preservation, and prove that the
InputLog and array owner addresses do not enter canonical bytes. This closes
the value/identity mechanics only; FrameInputSync transport lifetime, the
remaining fighter/hit/camera state, and live resimulation are still gated.

The first schema-v5 normal-render replay probe rejected every checkpoint before
mutation and exposed an incorrect prior-pair owner offset. `BattleManager+0x14A8`
is the current-pair `TArray` header: pointer at `+0x14A8`, count at `+0x14B0`,
and capacity at `+0x14B4`. The prior-pair header begins at `+0x14B8`, with count
and capacity at `+0x14C0/+0x14C4`. Existing executable-derived
`ALuxInputKeyEventListenerTickActor @ 0x1403FC2B0` documentation independently
records the `+0x14B8/+0x14C0` prior-input owner/count, while
`LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`
consumes the current header at `+0x14A8`. The binding now preflights pointer,
two-player count, and sufficient capacity for the previous-word, current-pair,
and prior-pair arrays, and emits a bounded typed validation diagnostic on
failure. A five-frame normal-render regression probe then captured frame-0 and
frame-18 batch entries and frame-1/frame-30 landings without checkpoint failure.
The Ghidra MCP instance was discoverable during this correction but refused the
client connection, so no new database mutation or saved comment is claimed;
the immutable GhidraCalibur export was used as the executable-derived source.

The proven nine-slot HgCpu buffer ABI is implemented in
`HorseMod/horselib/deterministic/HgCpuStream.*` with the exact `0x28018` bound,
build/schema/session/round/fighter/camera generation metadata, exact cursor,
and local integrity checksum. The writer at `0x1403841E0` is now invoked only
for diagnostic checkpoint capture during ordinary forward replay playback. The
reader at `0x140384540` remains disconnected from runtime activation: the
enclosing fighter, camera, timer, supplemental-region, thread, and lifecycle
preflights below are still open.

`NativeCandidateRegionsSelfTest` covers semantic restoration, excluded-byte
preservation, identity drift, invalid container metadata, unknown SubVM class,
partial-write undo, HgCpu cursor/integrity/generation checks, and stream
overflow. It and `DeterministicCoreSelfTest` pass under the `deterministic`
CTest label.

`CandidateCheckpointCodec` now encodes the typed candidate image and the exact
same-generation HgCpu local reconstruction image in a versioned pointer-free
envelope. Its SHA-256 covers only the explicitly canonical candidate fields;
the opaque HgCpu stream is separately integrity-checked and remains excluded
from peer-canonical hashing. `Sc6CandidateCheckpointCapture` resolves the live
MoveDispatch owner by validating the exact callback RVA, finalized weak-callback
vtable, serial-checked weak UObject identity, bounded `BattleManager +0x1210`
collection count/capacity, and uniqueness. This layout is backed by
`InitializeLuxBattleCharaFrameActionAndRegisterCallbacks @ 0x1404157D0` and
`RegisterAdjustedWeakUObjectCallbackInCollection @ 0x142EC8D70`; no UObject
address is persisted in a snapshot. A normal-render replay run captured
checkpoints at generation 1 frames 1, 30, 60, and 90, growing by exactly
134,754 bytes per checkpoint. Input and checkpoint budgets are generated from
the C++ schema and total 512 MiB. The capture path remains inactive for restore,
seek, production admission, and peer hashing until the enclosing blockers below
are closed.

`CandidateGameStateAdapter` now implements the real `IGameStateAdapter` capture,
preflight, restore, derived-repair, and recapture-verification contract over the
typed candidate regions plus the local HgCpu image. Restore reconstructs the
opaque same-generation image first and writes the explicit typed canonical
fields last. `SimulationSession` remains the sole transaction owner: it captures
the combined undo image before mutation and restores and verifies that image if
either the HgCpu reader or a later native write fails. The real adapter tests
cover both failure boundaries and prove exact native-plus-HgCpu undo. Its frame
advance and presentation callbacks are deliberately unconfigured, and the
adapter is not connected to replay seek or online rollback.

- Closed gates: fourteen native regions pass the static six-gate audit and are listed in restore order below: four explicit Lux RNG streams, one MoveDispatch mask bank, one VMPump semantic image, two scheduler semantic images, two same-generation allowlisted SubVM images, two `FLuxMoveCommandPlayerRollbackOverlay_Partial` semantic images, and two `FLuxMoveVMSlotParam` semantic prefixes. The paired HgCpuDirect writer/reader is additionally proven as a same-generation local reconstruction primitive, but its opaque stream is not peer-canonical and is not itself an admitted region.
- Open blockers: SubVM generation crossing, pending dynamic hit/list generations, downstream state mutated by the now runtime-signed manager callback collections, stage/wind topology, owned UCRT restore/resimulation failure injection, full camera/presentation terminal coverage, and runtime lifecycle qualification. The fighter/HgCpu supplement, callback-topology, UCRT capture, and FP-scope gaps are closed for the measured replay workload only; other content and native generations still require the same bounded proof before admission.
- Safe implementation work now unlocked: exact MoveDispatch-mask, VMPump, allowlisted same-generation SubVM, MoveCommand, and slot-parameter capture/hash/restore; a bounded `0x28018` HgCpuDirect buffer shim behind an inactive capability; fail-closed resolver/generation scaffolding; and value-only journal interfaces. Each must remain disabled until the enclosing adapter preflights every earlier/later dependency.
- Work still forbidden: editing `Schema::production_regions`, activating native replay seek/rollback or networking, treating the HgCpuDirect byte stream as a peer hash, crossing an allocation generation, or restoring a raw native pointer.

### Live gate checklist

- [x] FrameInputSync packet/cache writers and input filtering/consumption state reduced to the `+0x1210`/`+0x8E0` callback-set and remaining filter-writer blockers.
- [x] RNG and floating-point work reduced to checkpoint integration of the qualified UCRT shadow stream, admitted draw ordering across resimulation, and failure-injected restore proof.
- [x] MoveDispatch masks, VMPump, scheduler scalars, an exact same-generation SubVM class allowlist, MoveCommand, and slot-parameter ownership closed into ten production-candidate rows; the measured replay's fighter/HgCpu deltas are fully classified, while unsupported SubVM classes/generation crossing, dynamic hit/list generations, other content, and allocation routes remain explicit blockers.
- [x] Camera, stage/barriers, wind, destructibles, and dynamic allocation work reduced to an empty stage allowlist with concrete topology/generation prerequisites.
- [x] Audio, VFX, callbacks, UI/cinematic, and presentation work reduced to explicit mixed-boundary blockers and terminal-specific journal requirements.
- [x] Match, round, scene, replay, disconnect, and teardown work reduced to a monotonic generation policy and exact missing authoritative signals/runtime tests.

## Ghidra change ledger

| Kind | Address | Old | New | Evidence/reason |
|---|---:|---|---|---|
| Plate comment | `0x1403FE520` | Short simulation-loop description without the proven vtable targets or complete transaction fence | Exact `+0x68/+0x80/+0x88` target identities and the requirement that the adapter transaction include manager post-frame callbacks, round sequencing, active-track update, and any `+0x1462` repeat | Current decompile, vtable data at `0x14327B610`, constructor `0x1403DC7F0`, and tail frame-counter instruction in `0x1402DBC60` |
| Globals/type/comments | `0x14470E870`, `0x144844170`, `0x144845220`, `0x144845218`, `0x1448462C8`, `0x1448462D0`, `0x144846330`, `0x144846470..0x144846488`, `0x1448545A0`, `0x1448545F8` | Untyped or incompletely documented HgCpuDirect stream ranges, frame contexts, terrain flags, mode state, and relocated identities | Exact existing structure types or bounded byte extents; pointer slots typed as local identities; comments state relocation, generation, hashing, and stream-policy rules | Paired writer/reader `0x1403841E0/0x140384540`, global range pair `0x14031DC00/0x14031DE50`, exact `0x28018` allocation/stride xrefs |
| Plate comments | `0x140364950`, `0x140364BC0`, `0x140364C90` | CPU-personality notes still said semantic consumers/generation were unresolved | Exact membership in the `+0x348/0x860` semantic bank; identity qwords separated and excluded from restore/hash | Current rollback overlay, ordinary MoveVM driver, predicate/personality/reaction consumers |
| Plate comments | `0x1403841E0`, `0x140384540` | Native stream order described without the complete adapter preflight/undo policy | Exact same-generation preflight, `0x28018` bound, cursor/integrity checks, relocation-token rule, omissions, undo, and post-restore verification | Current writer/reader, buffer allocation sites, direct callers, and paired section order |
| Plate comment | `0x1402E26A0` | Factory/replacement contract without the final same-generation semantic snapshot rule | Exact 77-entry RVA/extent allowlist policy, semantic prefixes, identity/gap/tail exclusions, and hard rejection of unknown or replaced allocations | Current factory jump table/decompile, `CCpuDirectCommand_Partial`, `CCpuDirectAllGuardCount_TypedPartial`, vtable globals, swap/destructor paths |
| Struct | `FLuxMoveSchedState` | Four-byte `void **` vtable field and unresolved residue names | Eight-byte vtable identity; `+0x0C` and `+0x5C` allocator residue; `+0x18/0x18` constructor-cleared reserved bytes | Complete static-root xrefs plus constructor/reset/tick/commit/training/destructor paths; none of the excluded bytes has a semantic consumer |
| Struct | `ULuxReplayListContainer_Native` | Partial `0x1D28` layout ending before the two native profile records | Added `FLuxorBlueprintUserProfileData` records at `+0x1A80` and `+0x1BC0`, plus the unknown `0x18`-byte tail, for an exact `0x1D40` extent | `GetClass_ULuxReplayListContainer @ 0x140B77900` registers class size `0x1D40`; `ULuxReplayListContainer_RequestPlayerProfile @ 0x1405E90C0` writes the two profile slots and requires the container to remain alive through its asynchronous request |
| Prototype/comment | `0x1405E90C0` | Generic `void *` container parameter and incomplete lifetime note | Typed `ULuxReplayListContainer_Native *` parameter and documented rooted-container/asynchronous profile ownership | Direct field writes, request callbacks, class size, and the consumer below |
| Replay entry contract | `0x1405E3010` | Replay-list play request understood only as a menu callback | `ULuxReplayListContainer_OnRequestPlay` is the mandatory ownership-transfer step: it copies the current replay item to the replay save manager and publishes both profile/style/rank records into live match data before ReplaySetup launch | Current decompile and xrefs from the native replay-list flow; skipping this step reproduced a clean process exit immediately after `LuxBattleLauncherStartHook` |
| Replay profile fallback | `0x142DC0270`, `0x1404EEED0`, `0x1404F1CF0` | Historical synthetic-profile path not admitted as current evidence | Exact `FLuxorBlueprintUserProfileData` initialize/destroy/deep-copy triplet, with region `+0x18`, language `+0x1A`, owned `FString` name `+0x30`, and validity `+0xF9`; the qualification bridge may construct bounded `P1`/`P2` records only when the native replay profile request rejects all retries | Current decompiles and exact entry signatures. Runtime on 2026-08-24 reached `ReplayListScene_C` but the native profile request returned false three times; the bridge failed closed before this fallback was added. This is qualification-only presentation data and is not canonical simulation state. |
| Replay asset/launch order | Reflected `ApplyReplayToBattleSetup`; native stage-map request `SetActiveStageMapPathByHexStageNumber @ 0x140550D70`; stock `ReplaySetupScene` launcher flow | Bridge first queued native assets during initial import, then later attempted another asset request or manual launch after entering ReplaySetup | In ReplayList, complete profile population and `ULuxReplayListContainer_OnRequestPlay`, then call `ApplyReplayToBattleSetup` before allowing the transition to ReplaySetup. Do not queue stage assets or invoke manual launch: normal-render trace at commit `a1b39c8c` proved stock ReplaySetup invokes `ULuxUIBattleLauncher::Start` itself within 0.38 seconds and reaches `ReplayBattleScene`. | Current decompile and normal-render traces. The redundant stage request raced state already consumed by the stock launcher and raised `native_stage_map_request_failed`; removing that request reached `ReplayBattleScene` cleanly. Runtime simulation-fencepost proof remains required. |
| Replay save ownership | `ULuxReplayListContainer_OnRequestPlay @ 0x1405E3010`, `CopyBattleReplayData @ 0x140538580`, `ALuxBattleReplayPlayer_LoadStagedReplayRecordingData @ 0x140428040` | Qualification importer pre-wrote guessed replay-save offsets before invoking the native ownership transfer | Only deserialize and deep-copy the complete list item into `ULuxReplayListContainer_Native.currentReplayItem`; `OnRequestPlay` exclusively copies its `battleReplayData` into `ReplaySave+0x40`, including the `+0x1940` reset-round and `+0x1950` recording arrays consumed by ReplayPlayer. `pNewReplayRows` is a separate UI/list-row array. | Ghidra decompilation and runtime trace at commit `17de27c3`: stock launch reached `ReplayBattleScene` but the global Lux frame stayed zero, proving the scene shell alone is not simulation evidence. Guessed writes at `ReplaySave+0x19E0` and beyond were removed. |

## Per-frame call order

| Order | Function/address | Thread | Reads | Writes | External effects |
|---:|---|---|---|---|---|
| 0 | `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520` entry gate | Unknown/blocker: static code does not establish calling-thread identity | manager `+0x478`, `+0x12F3`, pending-move byte `+0x1463` | clears a move-dispatch latch | May skip the whole native coordinate when the log is null or manager pause is set. **Proven-static.** |
| 1 | `ProcessLuxBattleManagerMoveStateAndDispatchCallbacks @ 0x1403F7D70` (conditional) | same caller thread | pending move state | manager state/callback-owned state | Move-state 3 can cause an early world-mode traversal. **Proven-static; reached graph open.** |
| 2 | Manager clock admission in `0x1403FE520` | same caller thread | FrameInputLog GameRound `+0x3A0`, GameTime `+0x3A4`; manager cursors `+0x1488/+0x148C` | manager cursors; two event countdowns `+0x1468/+0x146C` | Computes a nonnegative native delta, or forces one traversal for the move-state path. **Proven-static.** |
| 3 | `UpdateBattleManagerCommandInputFromFrameInputLog @ 0x1403FE960` | same caller thread | FrameInputLog/current command state | manager camera/command input | Input-derived state; writer graph remains Phase 1 open. |
| 4 | `UpdateLuxBattleOnlineFrameStallCounters @ 0x1403FDEC0` | same caller thread | admitted delta and online state | online stall bookkeeping | Gameplay exclusion not yet proved. |
| 5 | `InitializeLuxBattlePerFrameTickArgs @ 0x1402DD100` | same caller thread | none material identified | value-only stack payload | Initializes one traversal payload. **Proven-static.** |
| 6 | `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`, P1 then P2 | same caller thread | routed cached/raw words and suppression state | manager-owned 8-byte input-pair lane | Produces held/rising pair. **Proven-static; writer closure open.** |
| 7 | `ProcessAndCompactCallbackEntries @ 0x141D38300`, manager collection `+0x1210` | same caller thread | produced pair and callback collection | pair and collection compaction | Gameplay-authoritative input filtering. Concrete callback set/lifetime remains Phase 1 open. |
| 8 | `ForwardScbattleWorldModeReadyQuery @ 0x1403C3B80`, provider vtable `+0x80` | same caller thread | owned ScbattleWorldMode target | provider-dependent | May gate input event dispatch. **Proven-static target; transitive state open.** |
| 9 | `ForwardScbattleWorldModeInputEvent @ 0x1403CFE70`, provider vtable `+0x88` (conditional) | same caller thread | input flags/online state | provider-dependent | Gameplay/presentation classification remains open. |
| 10 | `LuxBattleChara_VTableThunk_PerFrameTick @ 0x1403D2A20`, provider vtable `+0x68` | same caller thread | `FLuxBattlePerFrameTickArgs` | none in thunk | Exact bytes tail-adapt RDX to RCX and call `LuxBattle_PerFrameTick`. Vtable entry is `0x14327B678`; constructor `InitializeALuxBattleManagerObject @ 0x1403DC7F0` installs the 8-byte provider at manager `+0x1450`. **Proven-static.** |
| 11 | `LuxBattle_PerFrameTick @ 0x1402DBC60`: publish input/camera | same caller thread | two input words and camera payload | `g_qwLuxBattleLatestEngineInputPerPlayer @ 0x144855700`, `g_abLuxBattleCameraInputPublished @ 0x14470D100` | First state mutation inside the coordinate. **Proven-static.** |
| 12 | `LuxBattle_AdvanceWorldModePump @ 0x1402D9CD0`; freeze/anomaly publication | same caller thread | world-mode pump/freeze state | pump state and `g_bLuxBattleBattleAdvanceFlag @ 0x14470D0C0` | Canonical/derived boundary remains Phase 2 open. |
| 13 | MoveVM outer pump selected from states 0–4 | same caller thread | `g_abLuxMoveSystemVMPumpState @ 0x144100C70` | pump/fighter/HgCpu/SubVM state | Calls `0x14031CC00`, `0x14031D380`, `0x14031D460`, `0x14031D530`, or `0x14031D5B0`. **Proven-static dispatch; reachable graphs open.** |
| 14 | `LuxMoveVM_TickCharaCommandScheduler @ 0x1402E52D0` then `LuxBattle_TickCharaInput @ 0x140312510`, P1 then P2 | same caller thread | CPU-command, fighter, input-transform state | fighter command/input rings and latches | Core gameplay. **Proven-static order; state closure open.** |
| 15 | `LuxMoveVM_AdvanceCharaAnimClipPlayer @ 0x14037C2F0`, P1 then P2 | same caller thread | fighter clip runtime | clip time/runtime state | Collision/motion dependency remains Phase 2 open. |
| 16 | `LuxAudio_TickCharaEventCueScheduler @ 0x14038BD60`, P1 then P2; conditional VFX pose-bank copy | same caller thread | character event/clip/VFX pose state | scheduler and character pose mirror | Mixed gameplay/presentation boundary remains Phase 5 open. |
| 17 | `LuxBattle_PreTickStateSnapshotAndRoundDecision @ 0x14034FCE0` | same caller thread | round/global/fighter state | native pre-main snapshot/decision state | Core gameplay. Open. |
| 18 | `LuxBattle_TickCharaMainSimulation @ 0x14034DA70`, P1 then P2 | same caller thread | fighters, stage, MoveVM, hit, animation/motion | fighter/opponent/world state | Core gameplay graph. Open. |
| 19 | `LuxBattle_SyncLauncherHitToOpponentState @ 0x1402FF530` | same caller thread | launcher/hit state | opponent hit state | Core gameplay. Open. |
| 20 | `LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0` | same caller thread | both fighters and collision world | serialized hit/body results | Core gameplay. Open. |
| 21 | `LuxBattle_TickCharaSecondaryAndDecorators @ 0x140341CB0`, P1 then P2 | same caller thread | fighter/hit/reaction/decorator state | secondary simulation state | Mixed graph. Open. |
| 22 | `LuxBattle_TickPostSimulationFreezeAndRoundState @ 0x14034D500` | same caller thread | simulation/round state | freeze/round state | Core gameplay. Open. |
| 23 | `LuxBattle_FireVFXOnStateEdges @ 0x1402DD1F0` | same caller thread | gameplay edge flags | VFX-related state | Irreversible terminal boundary remains Phase 5 open. |
| 24 | `LuxBattle_RetireFinishedControllerSlotObjects @ 0x140323530`, `LuxBattle_UpdateBattleCameraSynthesis @ 0x14031EA50`, `LuxBattle_TickCameraFadeSchedules @ 0x140323620`, then camera-interface vtable `+0xB0/+0xA8/+0xA0` | same caller thread | camera/controller actions/fighters/stage/RNG | camera roots and published vectors | Indirect concrete targets and gameplay feedback remain Phase 4/5 open. The former `ProcessLuxCameraActiveActions` label for `0x140323530` was stale: the exact body walks controller slots, destroys completed objects, and then processes vibration. |
| 25 | `LuxMoveVM_AdvanceSlotParamLerp @ 0x14032F780`, lane 0 then lane 1 | same caller thread | two `FLuxMoveVMSlotParam` records | semantic `+0x00..+0x27` of each lane | Exact two-record order. **Proven-static; admission awaits complete writers/lifetime.** |
| 26 | `AdvanceLuxBattleStageWindEmitter @ 0x140334960`, list order, then `LuxBattle_TickStageWindAndAccumulateForces @ 0x140333FD0` | same caller thread | wind list/root/RNG | emitter and root wind state | Dynamic topology and allocation generation remain Phase 4 open. |
| 27 | `LuxBattle_EvaluateMatchAutoAdvanceCondition @ 0x14034EE40` (optional) | same caller thread | watcher and match state | watcher/transition state | Session identity/lifecycle open. |
| 28 | `INC dword ptr [0x14470D0C4]` at `0x1402DC34C` | same caller thread | `g_dwLuxBattleFrameCounter` | same global | Definitive coordinate commit: exactly one increment per completed `LuxBattle_PerFrameTick`. **Proven-static by instruction bytes.** |
| 29 | Manager collection `+0xA30` simulation callbacks, `LuxBattleManager_Tick_ProcessRoundStateSequence @ 0x1403FCE80`, and `UpdateLuxBattleActiveStateFromTrackEntries @ 0x1403FCA60` | same caller thread | frame payload/round/track state | callback targets, round and active state | Full-function audit of `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520` confirms the collection at `0x1403FE816`, round sequencing, and active-track update are one manager-owned transaction. `UpdateLuxBattleActiveStateFromTrackEntries` has this worker as its sole caller. Return from call site `0x1403FE82D` is the first reusable post-coordinate fencepost before the repeat test. Calling `LuxBattle_PerFrameTick` directly would omit this required tail and is forbidden. Callback storage is generation identity, never snapshot bytes. **Proven-static fencepost; extended runtime proof required.** |
| 30 | Manager `+0x1462` repeat | same caller thread | repeat byte | clears repeat byte | Re-enters orders 10–29 without rebuilding the P1/P2 pair. **Proven-static.** |
| 31 | Conditional manager unpause callbacks and `ProcessLuxStagePendingMoveEvents @ 0x140428D30` | same caller thread | unpause counter/stage queue | callback collection and stage state | Post-frame lifecycle/presentation surface remains open. |
| 32 | Return to sole caller `LuxBattleManager_Tick_MainStateMachine_At1461 @ 0x1403FBF30` | same caller thread | round-over predicates, manager `+0x1480/+0x1440`, actor state | outer round state, online-sync counter, parent actor tick state | The caller evaluates round completion only after the complete admitted batch returns, then advances/resets online-sync state and tail-calls `AActor_TickActor`. This work is once per batch, not once per inner coordinate. **Proven-static outer lifecycle ordering; transitive state and runtime thread proof open.** |

### Phase 0 call-target disposition

| Target/dispatch | Disposition | Reason |
|---|---|---|
| Direct callees of `0x1403FE520` | **Open by subsystem**, except stack initialization/free helpers | All 14 named direct callees were enumerated; callback and state-machine descendants are assigned to Phases 1, 4, 5, or 6. `FMemory_Free`, `LuxMoveSlot_InitStruct`, and `LuxMoveEntry_MoveAssign_With3Arrays` are bounded helper calls on the forced-move path but their allocated-object identity still prevents region admission. |
| Direct callees of `0x1402DBC60` | **Open by subsystem** | All 25 named direct callees were enumerated. None is silently declared presentation-only; each is assigned above to fighter/MoveVM, stage, RNG, or presentation closure. |
| ScbattleWorldMode vtable `+0x68` | **Closed target identity** | `g_apfnScbattleWorldModeVtable + 0x68` at `0x14327B678` contains `0x1403D2A20`, whose thunk calls `0x1402DBC60`. Constructor evidence is `0x1403DC7F0`. |
| ScbattleWorldMode vtable `+0x80/+0x88` | **Closed immediate targets; transitive behavior open** | Entries resolve to `ForwardScbattleWorldModeReadyQuery @ 0x1403C3B80` and `ForwardScbattleWorldModeInputEvent @ 0x1403CFE70`. |
| Camera interface vtable `+0xB0/+0xA8/+0xA0` | **Proven canonical read-only accessors** | The initialized SynthesisCamera scratch-interface vtable is `0x143E87A58`. Slots `+0xA0/+0xA8/+0xB0` are `GetLuxCameraVectorProviderX/Y/Z @ 0x14033FB70/80/90`; they read provider floats `+0x20/+0x24/+0x28` and have no mutation or UE publication side effect. The adjacent shared vector-copy leaf is `CopyLuxCameraVectorProviderPrimaryVector @ 0x14033FBA0`. |
| Manager callback collection `+0x1210` | **Closed for current binary** | The sole exact-field registration is `InitializeLuxBattleCharaFrameActionAndRegisterCallbacks @ 0x1404157D0`, installing `FilterALuxBattleMoveDispatchInputPairByFrameSlot`; `LuxActor_EndPlay_RemoveDelegates_Sub0x9d0_Chara0x1210_Clear8e @ 0x14041D750` removes the same owner. Consumer is `0x1403FE708`. No second exact `+0x1210` registration reference exists. |
| Manager callback collection `+0x8E0` | **Closed producer and known owner lifecycles; not an input producer** | `ProcessLuxBattleManagerMoveStateAndDispatchCallbacks @ 0x1403F7D70` broadcasts it after move-state application and before cleanup. `ALuxBattleChara_DispatchBeginPlay @ 0x1403B4310` installs the linked-actor/Maegami gameplay callback. `LuxActor_BeginPlay_CreateThreeAsyncTasks_At0x8e0_0xdb0_0xe20 @ 0x1404156F0` installs the auxiliary virtual callback, with matching owner removal at `0x14041D6E0`. The collection is generation-bound callback topology and executes during resimulation; it is not serialized. |
| Manager simulation collection `+0xA30` | **Closed registration/removal surface for current binary** | Dispatches occur at `0x1403FE816` and on move-state 3 in `0x1403F7D70`. `ALuxBattleChara_BeginPlay_D @ 0x1403B4560` installs `RefreshALuxActorMoveProviderDerivedState`; `LuxActor_BeginPlay_CreateAsyncTask_AtOffset0xa30 @ 0x140414B60` installs `InvokeUE4BoundObjectVirtual5F8NoArgs`. Matching owner removals are `0x1403BB6F0` and `0x14041D0C0`. The earlier `+0xB18` claim was a stale struct-label error. |
| Manager round collection `+0xB80` | **Closed registration/removal surface for current binary; downstream fighter state remains open** | Eight owner families register: character phase linked-actor handling (`0x1403B4310`), character virtual/no-op handling (`0x1403B4560`), FrameInputLog round reset (`0x1403E6EF0`), character round-widget/actor activation (`0x1403E7380`), MoveProvider propagation (`0x1403E8690`), exact `+0x390..+0x3A0` actor reset (`0x1403E8740`), active-player selection (`0x1403E9150`), and MoveDispatch pending-window reset (`0x140414F40`). Matching removals are `0x1403BB550`, `0x1403BB6F0`, `0x1403EDF30`, `0x1403EE1A0`, `0x1403EE6B0`, `0x1403EE710`, `0x1403EEB10`, and `0x14041D2A0`. Actual insertion order is a runtime generation signature and must be validated, not restored. |
| Manager unpause collection `+0xF70` | **Closed registration/removal surface for current binary** | It dispatches only when countdown `+0x14F0` reaches zero at `0x1403FE859`. `ALuxBattleChara_BeginPlay_B @ 0x1403B3990` installs `DispatchLuxBattleCharaVirtual620Thunk @ 0x1403B17C0`; `ALuxBattleVFxEventHandler_BeginPlay @ 0x1403B5530` installs `ClearTrackedVfxManagerEntryIds`. Matching removals are `0x1403BB350` and `0x1403BBA60`. The character thunk and its sibling `+0x610/+0x618` thunks are typed, documented, and saved in Ghidra. |
| Sibling ScbattleWorldMode vtable `+0xC8` | **Out of the admitted coordinate only** | Immediate table target is `HandleLuxBattleCharaPerTickAdvanceAllThunk @ 0x1403D3180`; `0x1402DBC60` does not invoke it. Its external dispatcher/lifecycle still belongs to Phase 6 qualification, so this is not a global exclusion. |

## Open-question ledger

| Phase | Question | Current status | Exact next action |
|---:|---|---|---|
| 0 | What is the complete direct and indirect call order for one admitted native coordinate? | **Closed for immediate boundary; descendants assigned to subsystem blockers.** | Re-open only if a subsystem finds a new indirect target reachable from `0x1403FE520`/`0x1402DBC60`. |
| 0 | What exact native round/frame identity is stable at the post-filter, pre-consumption boundary? | **Static landing fencepost and a 600-coordinate normal-render runtime traversal are closed for the measured replay generation.** The sole-caller return from `UpdateLuxBattleActiveStateFromTrackEntries` at `0x1403FE82D` follows simulation callbacks and round sequencing and precedes `+0x1462` repeat handling; `g_dwLuxBattleFrameCounter` increments once in each preceding `LuxBattle_PerFrameTick`. The signature-gated `DeterministicHookSet` observed this boundary on the UE game thread during the exact qualification replay on 2026-08-24, validated manager/InputLog/input-pair identities, and captured exact post-filter pairs without mutating simulation. The read-only observation also captures manager round/time cursors (`+0x1488/+0x148C`), round-state frame (`+0x1490`), unpause countdown (`+0x14F0`), repeat (`+0x1462`), and pending move state (`+0x1463`) under one schema-owned `0xFFF` completeness mask. The measured window included one native repeat and three same-native-time coordinates with zero cursor mismatch. | Extend qualification across round replacement, replay stop/re-entry, and manager/fighter/stage identity changes. Keep checkpoint restore disabled until the canonical subsystem blockers below close. |
| 1 | Which writers can alter the post-filter input pair? | **Closed for the current binary and measured replay generation.** `+0x1210` and its MoveDispatch writer family are closed; `+0x8E0`, simulation `+0xA30`, round `+0xB80`, and unpause `+0xF70` are later fenceposts with complete static registration/removal sets. `CallbackTopologyProbe` now binds all five collections as ordered, pointer-free records containing only role, collection/dispatch order, weak UObject index/serial generation, owner-class token, wrapper-vtable RVA, and callback RVA. Every candidate capture recaptures that topology and atomically releases the binding on any mismatch. A 600-frame normal-render replay probe retained the binding and captured landing checkpoints at frames 1 and every 30 frames through 600 plus 35 batch-entry checkpoints with no topology or identity failure. Source commit `8c351ca02d298b2e7e8b22c489070bc91245a5e7`; deployed DLL SHA-256 `52E4405AB47C4A74BF64CC1389E9D187A31AF70D6B310748B5F031CDE089777C`; replay SHA-256 `95E12E394D35C13D5E0DD3DCE692F9E0A4022E2A84205A9EC75F2FA6726D7879`. | Repeat the same binding proof across round replacement, replay re-entry, and each content candidate. Downstream state mutated by these callbacks remains assigned to fighter, stage, round, and presentation blockers. |
| 2 | Can both fighter execution graphs be snapshotted without pointer restoration? | **No with present coverage; all ActiveBattle routes explicitly unsupported.** | Recover per-class semantic fields, factories/destructors, derived repairs and allocation tokens from the exact roots listed below. |
| 3 | Can all shared RNG and caller FP state be restored with stable ordering? | **No statically complete contract; explicit UCRT/thread/FP blocker.** | Runtime qualify the scoped broker, draw ordering, thread, MXCSR/x87 and all failure exits. |
| 4 | Which stage/world graphs are bounded and generation-stable? | **No admitted content; all stages explicitly unsupported.** | Qualify an exact stage ID by concrete class/topology/factory/destructor enumeration and runtime emptiness/count assertions. |
| 5 | Where is each first irreversible presentation terminal? | **Partly proven; all incomplete families explicitly unsupported.** | Continue from exact audio/VFX terminals and unresolved camera/material/UI targets in the blocker table. |
| 6 | Which native transitions invalidate all snapshot identities? | **Hard BeginPlay/EndPlay/round/match/registry boundaries and the replay-stop admission boundary are proven statically; earliest scene/disconnect remains an explicit blocker.** `LuxBattle_Replay_PostTick @ 0x1403829F0` tests `dwExitGuard` before its first camera write. The centralized hook set now invalidates its observed native identity before the original function when that guard is zero. | Exercise replay stop/restart under the source-bound normal-render workload, prove game-thread affinity and fresh identities after re-entry, then close earliest world/online invalidation signals and validate reverse Horse teardown. |

## State-region ledger

| Region | Owner/resolver | Type/extent | Writers | Readers | Lifetime |
|---|---|---|---|---|---|
| FrameInputLog clocks/current inputs/cache | Live `ALuxBattleFrameInputLog_Runtime *` from BattleManager `+0x478`; class identity must be base Log or derived Sync | Semantic fields `+0x390`, `+0x394`, `+0x398`, `+0x39C`, `+0x3A0..+0x3BF`; 2 × 512 cache rows at `+0x3C0`, stride `0x10`, ending at `+0x43C0`. Each row semantically contains `{nGameRound, dwFrameIndex, dwInputValue, bFilled}`; exclude the three trailing initialized-reserved bytes from canonical hashing. | `InitializeFrameInputLogCacheForRound @ 0x1403F7C60`; `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30`; `UpdateFrameInputLogCacheLocalMode @ 0x1403F2AB0`; derived online/local writer `0x1403F2B60`; replay decoder `0x1403F63B0`; game-thread packet drain `0x1403F6770`; clock pipelines named below. | `GetCurrentInputForFrameInputLogSlot @ 0x1403F0680`; `GetCachedInputForFrameInputLogSlot @ 0x1403F0720`; pair producer `0x1403FCD10`; replay drain `0x1403F6600`; ready-row scanners `0x1403FE8F0/0x1403FEBA0`; manager simulation loop. | Round epoch established by `0x1403F7C60` or `ResetFrameInputSyncRoundState @ 0x1403F7D20`; UObject replacement/end-play invalidates resolver. **Proven-static layout/writers listed; enclosing region still blocked.** |
| FrameInputSync receive bookkeeping | Derived Sync object from the same manager root | Remote/User flags and Sync tail fields, receive deque, lock, packet wrappers, sent bitmap; several pointer-owning containers | Network callback enqueues through `EnqueueFrameInputSyncReceivedPacket @ 0x1403F4BE0`; cache mutation occurs only in the game-thread drain `0x1403F6770`; round reset `0x1403F7D20`; transport init/destructors | drain, handshake, ready-row and send paths | Sync UObject lifetime and transport session lifetime. **Client-local/identity mix; raw capture forbidden.** |
| Manager-produced input pairs | BattleManager current object | Previous-word `TArray` header `+0x1498/+0x14A0/+0x14A4`; current 8-byte `{held,rising}` pair header `+0x14A8/+0x14B0/+0x14B4`; prior-pair mirror header `+0x14B8/+0x14C0/+0x14C4`; suppression mask, round/game-time cursors and countdowns. Backing arrays are process-local owners and must be validated, never restored as headers. | `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`; callback collection `+0x1210`, principally `FilterALuxBattleMoveDispatchInputPairByFrameSlot @ 0x140427940`; reset/constructor paths | `LuxBattle_PerFrameTick @ 0x1402DBC60`, scheduler and `LuxBattle_TickCharaInput @ 0x140312510` | BattleManager/match generation; all three headers must retain non-null allocation identity, two-player count, and sufficient capacity. Callback registrations add a MoveDispatch object-generation dependency. **Canonical but blocked by callback-set closure.** |
| MoveDispatch input-filter state | Weak-UObject callback target registered by `InitializeLuxBattleCharaFrameActionAndRegisterCallbacks @ 0x1404157D0` | Authored table identity `+0x470`; indices `+0x478/+0x47C`; tagged phase union `+0x480..+0x48F`; saved input/gates `+0x490..+0x493`; completion delay `+0x494`; subelement-runtime owner `+0x498`; separately owned fixed-two-qword event-mask values behind header `+0x4A8/+0x4B0/+0x4B4`. In action-mode phase, `+0x480` is mode/counter. In pending-window phase (`+0x490 != 0`), `+0x480/+0x488/+0x48C` is a live stride-`0x20` array header/count/capacity. Do not capture either container header raw. | Initializer `0x1404157D0`; slot setters/advancers `0x140433F80/0x140434000/0x140434270/0x140438210`; action-mode driver `0x140438780`; subelement driver `0x140438370`; pending-window append/end writers `0x140427AB0/0x1404287C0`; manager callbacks reset/arm/clear at `0x140427D60/0x140428450/0x140427D50`; per-tick consumer/cleanup `0x140435E90`; filter `0x140427940`; event-mask writer `0x1404274E0`. Reflected SetSubFrameIndex calls funnel through `0x1409E3650` into `0x140434270`. | Filter, action-slot/event-window traversal, and 20 named event-mask predicates in `0x140415EF0..0x140417960` | MoveDispatch UObject generation plus authored table and live pending/subelement allocation generations. `DestroyALuxBattleMoveDispatchSubobjects @ 0x14040A850` frees owned arrays. **Writer family and same-phase semantic adapter are implemented; allocation/phase drift invalidates the binding atomically.** |
| Round-state sequence | BattleManager current object | Owned byte `TArray` header at `+0x1470/+0x1478/+0x147C`, current state `+0x1480`, frame counter `+0x1490`, cached actor `+0x1448`, unpause countdown and branch-specific state | `LuxBattleManager_PushByteToGrowableArray_At1470 @ 0x1403F41B0` is the sole exact-field append primitive; `LuxBattleManager_Tick_ProcessRoundStateSequence @ 0x1403FCE80` commits every changed byte and drains count to zero; constructor `InitializeALuxBattleManagerObject @ 0x1403DC7F0` reserves eight bytes | Same function, track/world-mode/game-flow consumers | Manager/match generation; state-entry callbacks can bind round actors. Candidate adapter retains the backing pointer/capacity as identities, accepts at most 32 queued state bytes, restores only bytes/count/current state, and fails closed without allocating above that ceiling. **Bounded semantic queue candidate implemented; callback-mutated round actors and presentation/game-flow terminals remain enclosing blockers.** |

| Capture phase | Restore order | Repair | Verify | Hash/classification | Status/evidence |
|---|---|---|---|---|---|
| Immediately after receive-drain/cache production and immediately before pair production for external authoritative inputs; snapshot landing boundary must include post-frame manager callbacks, not merely the tail of `LuxBattle_PerFrameTick` | Validate BattleManager, InputLog/Sync class, GameRound, MoveDispatch object and all backing-owner identities; restore semantic cache rows/clocks; restore prior raw words, masks, filter scalars and exactly two event-mask qwords; publish desired pair before `+0x1210`; execute stock filter; verify post-filter pair and all restored semantic fields | No container reconstruction is currently admitted. Derived current-input mirrors may be recomputed only through the verified stock producer for the same coordinate. | Exact round/full-frame cache tags, clocks, previous words, filter state, event masks, and post-filter pair; any owner/count/capacity mismatch fails atomically | Canonical hash includes semantic scalar values, exact cache row fields, produced pairs, filter scalars, and two event-mask qwords; excludes UObject/pointers, TArray headers, lock/deque/packet wrappers, and row reserved bytes | **Unknown/blocker for production.** Callback membership and MoveDispatch writers are closed, but complete FrameInputSync packet/cache mutation, the callback-mutated fighter/round graphs, and runtime owner/order topology validation are not yet admitted. |

### Phase 1 writer and packet semantics

| Path | Current-binary result | Confidence |
|---|---|---|
| Local current input | `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30` zeroes both mirrors, then samples enabled logical slots through `GetCurrentInputForFrameInputLogSlot @ 0x1403F0680`. Physical device identity is upstream and absent from this function. | **Proven-static.** |
| Local/offline cache | `UpdateFrameInputLogCacheLocalMode @ 0x1403F2AB0` writes local-flagged slots at `GameTime & 0x1FF`; an already-filled exact `{round,frame}` row is immutable. | **Proven-static.** |
| Local/online cache | `UpdateFrameInputLogCacheOnlineOrLocal @ 0x1403F2B60` uses `SendTime`, not `GameTime`, and writes only `LocalPlayerFlags` slots; offline delegates to `0x1403F2AB0`. | **Proven-static.** |
| Network callback thread | `EnqueueFrameInputSyncReceivedPacket @ 0x1403F4BE0` locks and pushes a packet wrapper only. It does not parse opcodes or write cache rows; count above 100 drops the oldest wrapper. | **Proven-static.** Calling-thread identity still needs runtime proof. |
| Game-thread receive drain | `ProcessFrameInputSyncReceivedPacketQueue @ 0x1403F6770` drains under the same lock. Opcode 1 derives row count as `(packetBytes - 10) / 2`; each non-stale row is tagged with wire round nibble/full uint frame and written only if the exact row is not already filled. | **Proven-static.** Game-thread identity is a scheduling inference until runtime traced. |
| Duplicate/stale behavior | Rows below local `SyncTime` are ignored individually; future rows may land ahead of a gap; exact filled duplicates/conflicts are first-writer-wins; `SyncTime` advances only across contiguous ready `UserPlayerFlags` rows. | **Proven-static.** |
| Replay/file source | `DecodeFrameInputSyncReplayRecordsIntoCache @ 0x1403F63B0` requires full local GameRound equality, cursor at least SyncTime, and cursor below `GameTime + 512`; it widens two ushort inputs and observes exact-row immutability. | **Proven-static.** |
| Cache read | `GetCachedInputForFrameInputLogSlot @ 0x1403F0720` rejects slot ≥2 and negative rebased frames, selects `frame & 0x1FF`, then requires both exact round and full frame. It does not rely on the ring index alone. | **Proven-static.** |
| Pair production | `0x1403FCD10` copies current pair to prior pair, reads cache at `(GameTime - nFramesBack) - 1`, advances the per-slot previous raw word, computes rising as `(previous ^ current) & current`, then applies manager and gameplay suppression masks. | **Proven-static.** |
| Post-filter | `0x140427940` may clear both words, replace both with saved `+0x490` action data, and advance the pending-window state. Desired input must be injected before this callback, never after it. | **Proven-static.** |
| Observational timeline capture | The admitted fencepost reads the two exact 8-byte `{held,rising}` records through `BattleManager+0x14A8` after `+0x1210` filtering, validates active-player count `+0x14B0 == 2`, records InputLog identity/round/time, manager consumed cursors, round-state frame, unpause countdown, repeat request, and pending move state, and keys the consumed pair by `g_dwLuxBattleFrameCounter`. The in-process `Sc6ReplayRuntime` starts a fresh generation on manager, InputLog, round, or counter reset and retains earlier generations. Cursor divergence is counted as evidence rather than rejected because the manager cursors can describe a catch-up batch boundary. | **Implemented and proven for one 600-coordinate normal-render generation.** The schema-owned `0xFFF` surface stayed complete through frame 600, including one repeat and three same-native-time observations; checkpoints captured every 30 coordinates without failure. Round replacement and replay stop/re-entry qualification remain. |
| Transport exclusion | Packet wrappers, receive-deque indices/storage, lock state, shared transport controls, sent bitmap, and acknowledgements are not safe snapshot bytes. They can be excluded from canonical gameplay only if rollback owns the pair/cache boundary and never rewinds native transport delivery. A future adapter must drain once, externalize authoritative inputs, and leave transport monotonically forward. | **Bounded-inference; architecture requirement, not yet production proof.** |

### Exact outer-batch timeline and replay continuation

The signature-gated outer hook at `LuxBattleManager_Tick_MainStateMachine_At1461 @ 0x1403FBF30` now records one value-only envelope around each original call and invokes the original exactly once. Each completed coordinate observed at the inner `0x1403FE82D` fencepost is assigned its exact outer batch ID and offset. The stored envelope contains only coordinates, `DeltaSeconds`, native clocks/cursors, round/main states, repeat counts, and generation-change facts; manager, InputLog, fighter, stage, and heap addresses remain binding identities and never enter the timeline.

The normal-render replay workload at commit `e9958281ce085353ac7555d1540b24cc0811c5b1` produced 600 coordinates in 597 outer batches, including two multi-coordinate batches, maximum width 3, one repeat coordinate, three same-input-time coordinates, and zero batch/accounting mismatches. Landing checkpoints remained exact at frame 1 and every frame divisible by 30. The first version of the batch-entry scheduler measured a maximum checkpoint gap and resimulation distance of 30, contradicting the required 29-frame ceiling. That result was rejected rather than normalized away.

The corrected scheduler plans against the schema's full supported batch width of 12. It retains a prior batch-entry checkpoint only when that checkpoint plus the widest possible next batch remains within 29 coordinates. The first corrected run captured entry bases at frames 3, 21, 39, and then every 18 coordinates through frame 597. Runtime measurements were `entry_gap_max=18`, `resim_distance_max=18`, and `entry_uncovered=3`; this exposed that generation 1 was being published only after the original three-coordinate batch.

Commit `a592b08531355698fb11cf11e0b6b94e40134595` closes that initial-generation gap for the measured replay route by publishing manager, InputLog, thread, round, clock, and frame identities from the complete outer pre-read and capturing the first batch-entry checkpoint at generation 1/frame 0 before calling stock. The 600-coordinate normal-render proof recorded frame-0 entry bytes, frame-1 landing bytes, `entry_uncovered=0`, `entry_gap_max=19`, `resim_distance_max=19`, the same 597/2/3 outer-batch topology, and zero cursor or accounting mismatches. Its deployed DLL SHA-256 is `379798F698A16F6296746480261677D6AF5DE3AE30E8674CC441EB9652311C66`, ReplayQualificationMod SHA-256 is `548D05C2F827E193CB4D73333EBD10F6CF3E074A8D31064271CDD9E4F12AD6C8`, and non-certifying dirty-tree report SHA-256 is `537885D87B1F23D1600535A7FBF6013E696CB7240A3B35D4C9E31899FC73CA10`. Cross-round and replacement generations still require their own native materialization fencepost and cannot infer a baseline from this initial-entry result.

The run used the deployed DLL SHA-256 `F801BEABB5C2809B5F976B44F8B0D436A5C2862E4E33031D682E7AF078143D1E`, ReplayQualificationMod SHA-256 `7764E503092DE1157CDA9AE278ED4DCBFF5A6DD82520077AB4C41FFFF90E8A0E`, generated schema SHA-256 `BD05F4512EF829EFB0AD5CE6893C20F49142A7FA6001D5F6361E1F42EBC61FFE`, and report SHA-256 `D0AAC3409F7F65B4B7F673559B01C9B4002053FA0B55EA2E315FBBC9697C2D2D`. The report is non-certifying solely because unrelated user moveset-parser edits kept the source identity dirty; its reported runtime commit exactly matches `e9958281`.

Mid-batch seeking never jumps into `0x1403FE832` or fabricates the native stack/register continuation. The batch-aware planner selects a same-generation entry checkpoint at or before the target's enclosing batch, rejects a missing or over-distance base, and records the enclosing batch plus landing offset. Execution must restore that entry, replay complete native outer batches with ephemeral presentation suppressed through the landing coordinate, capture the exact landing fencepost, finish the enclosing batch, and restore the landing image for the paused view. Resume must restore the same batch entry and replay the enclosing batch again, suppressing through the landing offset and allowing only later coordinates to publish. An exact entry checkpoint at the target resumes directly at the following batch. This planner is implemented and structurally tested; native restore/execution remains disconnected until the initial-generation baseline and enclosing adapter gates close.

## Pointer and allocation generations

| Object | Factory | Replacement/destructor | Stable identity check | Restore policy |
|---|---|---|---|---|
| ScbattleWorldMode provider | Allocated as an 8-byte target by `InitializeALuxBattleManagerObject @ 0x1403DC7F0`; table `0x14327B610` | Shared-control destruction with BattleManager lifetime | Same BattleManager, target pointer, control pointer, and vtable RVA | Identity only; never restore target/control bytes. |
| FrameInputSync receive deque and packet wrappers | `InitializeFrameInputSyncTransportBuffers @ 0x1403FA810`; transport/deque helpers | `DestroyALuxBattleFrameInputSyncRuntime @ 0x1403DE970`, `DestroyALuxBattleFrameInputSyncObject @ 0x1403E1780`, per-wrapper release in enqueue/drain | Same Sync UObject/transport generation; deque remains monotonically forward | Excluded from snapshots. Externalize authoritative input after drain; never rewind wrappers, lock, indices, or shared refs. |
| MoveDispatch event-mask array | `ALuxBattleMoveDispatch_Constructor @ 0x1404049E0` reserves and initializes exactly two qwords | `DestroyALuxBattleMoveDispatchSubobjects @ 0x14040A850` frees it; no verified resize after construction | Same MoveDispatch UObject and data owner; count exactly 2; capacity ≥2 within a small fixed bound | Restore only two qword values. Header is validation metadata, never written or peer-hashed. |
| VMPump lane fighter references | Bound by `LuxMoveSystem_BeginVMPump @ 0x14031C950` | Existing pump is ended before rebind; `LuxMoveSystem_EndVMPump @ 0x14031CAC0` ends logical generation | Same current-round primary/opponent, lane pointers, player slots, timer config, scheduler objects | Pointers are identity only. Scalar pump restore is allowed only inside the unchanged Begin–End generation. |
| Per-player scheduler SubVM | `CCpuCommand_ResetSubVMState @ 0x1402E25A0`, `LuxMoveVM_CreateCpuDirectState @ 0x1402E26A0`, `LuxMoveVM_CreateHgCpuDirectMoveSubVM @ 0x1402E5220` | `LuxMoveVM_SwapSubVM @ 0x1402E57D0` publishes replacement then scalar-deletes old; normal-move init `0x1402E5710` can replace during traversal | Same scheduler/fighter, same live SubVM pointer and vtable RVA, recognized allocation class/size | Never restore `FLuxMoveSchedState +0x10/+0x50` pointers. Same-generation semantic restore requires per-class field map; crossing replacement requires verified factory reconstruction or fail closed. |
| CPU-direct derived SubVM | Factory jump table in `0x1402E26A0`; observed sizes `0x68`, `0x70`, `0x78`, `0x80`; common base is `CCpuDirectCommand_Partial` | vtable scalar delete on replacement/reset | Same live object generation; exact vtable RVA is in the 77-entry allowlist below; extent matches; current fighter/opponent/scheduler back-pointers match | Same-generation candidate: restore `+0x08/4`, `+0x20/0x3C`, and the extent-specific derived prefix. Exclude vtable, identities, gaps `+0x0C/4` and `+0x5C/4`, and `0x80` tail `+0x7C/4`. Unknown classes or replacement fail closed. |
| MoveCommand player arena | Static `g_abLuxMoveCommandPlayers @ 0x14470F390`; two exact `0x3038` slots (`FLuxMoveCommandPlayerRollbackOverlay_Partial[2]`) | Reset/rebound by `LuxBattle_ResetRoundCountersAndCommandSlots @ 0x140302930` and initialized by `LuxBattle_InitCommandPlayerState @ 0x1402DED20`; ordinary writers are the typed MoveVM driver, opcode executor, predicate refresh, personality producers, and reaction consumers | Same arena address; both self/opponent owners; all 17 identity qwords per slot byte-equal; same match/round generation | Restore only nine semantic banks (12,076 bytes/slot). Exclude 17 identity qwords, `+0x2A28/0x80` diagnostic text, and `+0x3034/4` uninitialized tail from restore/hash. **Production candidate; raw `0x6070` copy forbidden.** |
| HgCpu buffer/snapshot graph | Native allocation/stride is exactly `0x28018`; writer `0x1403841E0` and reader `0x140384540` use a two-segment P1/P2 relocation table with `0x973F0` extents | Writer callers are interactive replay reset, round-result cinematic, and palette-variant capture; reader callers are round-result cinematic and palette restore | Same P1/P2 roots/classes/allocation generation, same optional-camera presence/root and embedded interface class, same timer/config roots; all relocation tokens resolve inside current segments; exact saved cursor and integrity | Use as a bounded, same-generation local reconstruction primitive behind an adapter-owned stream implementation. Require exact reader/writer cursor equality, undo on failure, then restore supplements and recapture. Never peer-hash the opaque stream or treat it as complete fighter state. |
| Pending hit/list nodes | `LuxBattle_AllocAndLinkListNode @ 0x1402F8BA0` reached from hit resolution | The two `LuxKHitFrameContext` list sentinels and all collected event nodes are stack-owned per-call temporaries. `LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0` allocates both sentinels, passes the lists only to `LuxBattleChara_UpdateAllKHitWorldCenters`, then unlinks/frees every node and both sentinels before either classifier and before return. The persistent one-slot pending transition is separate: `{moveId @ 0x14485E738, facingDelta @ +4, attacker @ +8, flags @ +0x10}` plus launcher-sync byte `0x14470F38D`. | Same fighter pair/session/round; pending attacker must be null, P1, or P2 and is encoded as slot `0/1/2`. The temporary lists have no generation at the completed coordinate because they cannot survive the call. | Never restore list links or allocator metadata. The typed candidate captures only the four scalar pending values and fighter-relative slot, reconstructs the raw pointer against the already-bound roots, and verifies by semantic recapture. **Temporary-list exclusion and persistent-slot contract proven statically; candidate implemented and unit-tested.** |

### Phase 2 fighter/MoveVM/HgCpu findings

| Region/path | Evidence | Restore consequence | Status |
|---|---|---|---|
| VMPump control | `FLuxMoveSystemVMPumpState_Partial` is exact `0x88`: six identities at `+0x00/+0x08/+0x10/+0x18/+0x40/+0x48`; scalar lanes `+0x20/0x1C` and `+0x50/0x1C`; excluded unknown tails `+0x3C/4` and `+0x6C/4`; controls `+0x70/0x18`. Begin saves fighter `+0x324/+0x34BC/+0x1E50`; End restores them, finalizes hit/effect lanes, rebuilds normal SubVMs, restores HgCpu `+0x13C`, then disables. | One indivisible Begin–End generation. Preflight all six identities and state `0..4`/enabled `0..1`; restore/hash only the two scalar lanes and controls. Reject across Begin/End, rebinding, state-4 SubVM replacement, or round/session reconstruction. | **Proven-static same-generation production candidate.** |
| Scheduler | `FLuxMoveSchedState[2] @ 0x144715400`, `0x60` each. Canonical banks are `+0x08/4`, `+0x30/0x20`, and `+0x58/4`. Vtable `+0x00`, fighter `+0x10`, and owned SubVM `+0x50` are identities. Constructor/reset/xref closure classifies `+0x0C/4` and `+0x5C/4` as allocator residue and `+0x18/0x18` as constructor-cleared reserved bytes with no semantic consumer. | Validate all three identities for both lanes before the first write; restore/hash only the three canonical banks. Never copy the raw `0xC0` array or cross a reset/factory replacement. | **Proven-static same-generation production candidate.** |
| CPU-direct SubVM | Factory `0x1402E26A0` can replace during ordinary simulation based on fighter current move ID `+0x324`; allocation failure publishes null and deletes old. The current factory's 77 recognized vtable-RVA/allocation-extent pairs have an exact common/derived semantic partition. | Snapshot encodes class and initialized semantics. Same-generation restore is admitted only when the live pointer, allowlisted vtable RVA/extent, fighter/opponent identities, and owner scheduler all still match. Unknown classes and every replacement/generation crossing fail closed. | **Proven-static same-generation production candidate for the exact allowlist; replacement and unknown classes remain blockers.** |
| Slot parameters | `FLuxMoveVMSlotParam[2] @ 0x14470E0C0`, stride `0x2C`; `LuxMoveVM_AdvanceSlotParamLerp @ 0x14032F780` reads/writes only semantic `+0x00..+0x27`, P1 then P2; initializer/xref audit leaves `+0x28` as initialization-only stride residue. | Capture/restore/hash each 0x28-byte semantic prefix; leave both padding dwords untouched. Preflight the static root and match/round generation, restore before the first resimulated call, then byte-recapture both prefixes. | **Proven-static production candidate.** |
| Command-player arena | `FLuxMoveCommandPlayerRollbackOverlay_Partial` is exact `0x3038`; current ordinary scheduler/opcode/predicate/personality/reaction consumers partition every byte into nine semantic banks, 17 identity qwords, diagnostic text, or uninitialized tail. | Preflight every identity in both slots before the first write; restore/hash only the nine banks; leave all exclusions untouched; recapture and compare the semantic image. A generation mismatch rejects atomically. | **Proven-static production candidate.** |
| Fighter root | The current `ALuxBattleChara_VerifiedPartial` is `0x9751C` and deliberately quarantined because overlapping overlays remain. Main simulation `0x14034DA70` reaches hit, damage, physics, stance, terrain, MoveVM opcodes, bone pose, and presentation dispatch. | A whole-fighter memcpy is prohibited. Build a pointer-free field map from the main/secondary/hit/motion access graph, with derived repairs and exact writer/lifetime closure. | **Unknown/blocker; all ordinary ActiveBattle fighter routes unsupported.** |
| Hit/body graph | `LuxBattle_TickHitResolutionAndBodyCollision @ 0x14033CCA0` reaches hit collection, attack/hurtbox resolution, mutual arbitration, damage application, root-motion carry, physics/body collision, pending node allocation and free. The pending transition producer is `LuxMoveVM_StagePendingHitTransitionAndSnapFacing @ 0x1402FF3E0`; `LuxBattle_ApplyDamageFromPendingHit @ 0x1402FF620` consumes and clears its attacker pointer before damage math. Early world/hit gates can leave that one slot live across the coordinate. | The per-call event lists are excluded because their complete allocation/use/free lifetime is nested inside `0x14033CCA0`. Candidate capture now represents the persistent pending attacker as fighter-relative slot `0/1/2` and rejects every foreign pointer; paired HgCpu remains responsible for fighter-inline hit/body state. Collision proxies, authored stage geometry, derived repair, and presentation terminals still require closure. | **Pending-slot/list-lifetime subgraph closed and implemented; enclosing fighter/stage/presentation graph remains blocked.** |
| Animation/motion | `LuxMoveVM_AdvanceCharaAnimClipPlayer @ 0x14037C2F0` can end, initialize, advance time-dilated clip state, and set animation slot weights. Main/secondary paths consume pose and bone transforms for hit/motion. | Clip identity, section/index/time, active masks, root motion and collision-dependent pose caches must be restored semantically; cache rebuild routines are not yet proved complete. | **Unknown/blocker.** |

Phase 2 conclusion: VMPump, scheduler scalars, the exact same-generation SubVM allowlist, MoveCommand, slot parameters, and the persistent pending-hit slot are complete, and the paired HgCpuDirect stream is a proven local reconstruction primitive. A bounded normal-render run observed 600 consecutive replay frames with 2,567 retained source spans per sample and zero source-span truncation. Of 13,836,916 changed-byte observations outside direct source spans, 7,644,825 fall in the proven `+0x45700..+0x95700` KMot inline working buffer, 6,187,659 in the exact `CMatrixBankImpl_768` triple buffers, 33,127 in `CMatrixBankImpl_32`, 4,428 in the typed `FLuxBoneDataBank_Partial`/`FLuxSkeletonMotionState_Partial` derived animation caches, and four in the page containing relocated identity fields `+0x97190/+0x97198/+0x971A0`. The event-node lists previously grouped with the dynamic hit blocker are now proven call-local temporaries that are fully destroyed before the checkpoint fence, so they are explicitly excluded rather than reconstructed. This closes the fighter/HgCpu supplement coverage gap for that replay workload: every observed byte is a native direct source, proven derived cache/working buffer, or transformed relocation identity. It does not qualify other content or close unsupported SubVM replacement, collision-proxy/stage generation, camera, RNG/FP, presentation, or lifecycle routes, so production activation remains forbidden.

### Phase 4 stage, wind, barrier, camera, and world topology

| Object/path | Current-binary evidence | Snapshot/generation rule | Status |
|---|---|---|---|
| Stage identity and authored geometry | Current stage data resolves through the live MoveProvider/StageInfo graph; `GetCurrentStageRingEdgeAvailable @ 0x140426A80` reads `LuxBattleStageInfoTableRow +0x40`. `LuxBattle_TickCharaMainSimulation @ 0x14034DA70` reaches terrain support and contact blending; hit resolution reaches physics/body collision. | Validate MoveProvider, StageInfo row, stage ID, world and round configuration as generation identities. Static authored tables may be referenced by stable logical IDs, never by raw addresses. Exact wall/ring/floor proxy topology and all mutators are not yet closed. | **Unknown/blocker; every stage remains unsupported.** |
| Runtime wind-emitter list | `LuxBattle_RebuildStageWindEmitterList @ 0x1402D9F30` clears current nodes, walks ordered templates, allocates/initializes shared `0xB0` emitters, appends in order, increments checked count, then consumes the template list. Clear functions are `0x1402DD840/0x1402DD950`. | Rebuild is a generation boundary. Validate sentinel, bounded count, node/ref-control ownership, emitter identity and exact template ordering. Do not restore list links or ref controls. | **Proven-static topology lifecycle; restore reconstruction not proved complete.** |
| Per-frame emitter spawning | `AdvanceLuxBattleStageWindEmitter @ 0x140334960` updates timers, consumes shared LFSR, allocates/links a `0x1E0` ring-in node, initializes lifecycle state and clears emitter active/count. `InitializeIwWindRingInObject @ 0x140332D60` initializes the ring-in oscillator subset. It is gameplay/RNG state, not a particle-only terminal. | Must execute on every resimulated coordinate. A checkpoint crossing allocation/link/unlink needs semantic node topology plus generation; allocator residue at wind base `+0x34..+0x3F` is excluded. | **Proven-static concrete type/lifecycle surface; allocation reconstruction remains a blocker.** |
| Wind root | `LuxBattle_TickStageWindAndAccumulateForces @ 0x140333FD0` flips/dispatches callback banks, prepares/updates nodes, unlinks/frees expired nodes, samples stage/P1/P2, and writes twelve force lanes to the root and fighters. `g_fIwWindOutputActive` suppresses sampling only, not lifecycle/RNG. Assembly at `0x14033450C` and `0x1403345AA` proves both parallel and ring-out allocations are `0x130`; ring-in is `0x1E0`, and shockwave is `0x180`. The concrete vtable allowlist is parallel `RVA 0x3E88C88`, ring-out `0x3E88CB8`, ring-in `0x3E88CE8`, and shockwave `0x3E88D18`. `IwWind_SampleRingInForce @ 0x140333C20` proves node `+0x40..+0x4F` is sampled presentation force and `+0x50..+0x5F` is its presentation source/output; neither controls lifetime or shared-LFSR admission. | `StageWindTopologyProbe` validates the generation-stable root, callback RVAs, callback-bank bounds, exact class/size allowlist, root ownership, forward/reverse links, cycles, and a 64-node ceiling. Its canonical form contains type tags and scheduling semantics. Node force vectors `+0x40..+0x5F` and the root's 48 accumulated force bytes remain in the local reconstruction image for visual continuity but are excluded from canonical peer truth; life `+0x30`, oscillator tick `+0x60`, prepared `+0x68`, active `+0x6C`, and verified class scheduling fields remain canonical. Root/node/link pointers and documented allocator-residue ranges are absent. `StageWindGraphTransaction` proves replacement allocation, value scattering, vtable/link reconstruction, atomic root publication, post-write recapture, old-graph retirement after verification, and mutation-free allocation/publication failure against fake memory/allocator. | **Pointer-free capture and structural transaction implemented and unit-tested; sampled/output force classification verified against current-executable Ghidra evidence and runtime byte localization.** |
| Breakable barrier gameplay | `ConstructLuxStageBreakableBarrierActor @ 0x1405347B0` initializes barrier ID `+0x420`, endurance `+0x424`, current hit count `+0x468`. `HandleStageBreakableBarrierHit @ 0x140549F40` rejects at endurance, increments `+0x468`, then performs material/particle/component work and synchronously dispatches stage-break listeners. `ApplyLuxStageBreakableBarrierPresentationState @ 0x14054A960` also writes `+0x468`. | Actor identity is scene/round generation. `+0x468` is canonical gameplay, while component/UObject effect pointers `+0x470/+0x478` and transient arrays are local presentation identities. A restore must set semantic state then reconcile presentation; raw actor/component capture is forbidden. | **Bounded field proof, but all writers/listeners and barrier registry topology are not closed; unsupported.** |
| Destructibles/hazards | Class/function search found the barrier family above but did not establish a closed generic destructible/hazard owner graph. Absence of a friendly name is not dormancy evidence. | Qualification must enumerate concrete stage classes for each allowlisted stage, registry insertion/removal, capacity, hit writers, teardown, and collision-proxy repair. | **Unknown/blocker; no content allowlist yet.** |
| Camera action root | `ProcessLuxCameraActiveActions @ 0x140323530` and the extensive `LuxCameraAction_*` family run inside the coordinate; current camera state consumes LFSR/xorshift and fighter/stage transforms. `LuxBattle_PerFrameTick` invokes camera scratch-interface vtable `+0xB0/+0xA8/+0xA0` before publishing final vectors. Those slots are now proved to be the read-only Z/Y/X accessors at `0x14033FB90/80/70` over the SynthesisCamera provider vector `+0x20..+0x28`. | Camera cannot be excluded: orientation feeds side-relative gameplay/MoveVM and shares RNG. Restore the bounded camera owner/action state and compare `CameraPublicationState` bitwise. Do not suppress or journal the accessor corridor as ephemeral presentation. | **Concrete publication accessor blocker closed; per-class action semantic maps remain lifecycle-validation work.** |

Stage/content qualification presently supports no stage. A future narrow allowlist must prove an exact stage ID plus static authored topology, zero unmodeled hazards/destructibles, bounded wind lists, unchanged allocation generations, and concrete camera-action classes. A runtime assertion of emptiness/dormancy is required even where static analysis finds no named route.

The read-only wind admission probe passed a source-bound 600-frame normal-render
run at commit `b81ea65a76d578bb916a32a32ca4c20a293ee874`. Both landing and
batch-entry capture remained available with no topology failure. The observed
ordered graph grew through the legitimate native lifecycle from two nodes at
frame 0/1, to three by frame 18, and four by frame 108; it remained at four
through landing frame 600. This proves that the allowlist, link validation, and
dynamic traversal accept the measured workload rather than merely asserting an
empty graph. It does not prove restore: no native node/link pointer is present in
the canonical probe bytes, and no allocation is created, destroyed, or relinked
by HorseMod. The DLL SHA-256 is
`10DD33BBFB3847B30FE27376E1054A102E8DF1E77D1E095A8F04E7EFCBE272BF`;
the generated schema SHA-256 is
`CED7084AB7DC4C57F8CA63B1BE484844C45E3CA27FCC72107EF7ADE7181E171E`.

### Wind allocation and reconstruction contract

The current binary has one creation path per concrete vtable. Vtable xrefs and
initializer callers close the following surface:

| Class | Allocation and creation path | Initialization/RNG | Retirement/reconstruction consequence |
|---|---|---|---|
| Parallel | `0x130`, `IwWind_CreateWindEffectPair @ 0x140334430`; vtable xrefs only at `0x14033455C/0x140334563` | `IwWind_InitParallelObject @ 0x1403311F0`; consumes combined wind RNG, battle LFSR, and the Park-Miller LCG | Link is published before initialization. Restore must allocate raw storage, write captured values, then rebuild links; calling the initializer would consume RNG twice. Parallel update does not expire the node, so it may survive round initialization. |
| Ring-out | `0x130` in the same pair constructor (`MOV ECX,0x130 @ 0x1403345AA`); vtable xrefs only at `0x14033461A/0x140334621` | `IwWind_InitRingOutObject @ 0x1403315A0`; consumes combined wind RNG, battle LFSR, and LCG, after additional pair-constructor draws. The initializer writes `+0x70..+0xDF`; the shared update writes `+0x120..+0x12B`. No creation or virtual path owns `+0xE0..+0x11F`, so it is excluded allocator residue. The earlier inferred `0x180` extent and derived-field overlay were wrong and have been removed. | Same raw-allocation/value-rebuild rule. The native pair constructor can partially publish the first node when the second allocation fails, so it is not a transactional restore primitive. |
| Ring-in/stage | `0x1E0`, `AdvanceLuxBattleStageWindEmitter @ 0x140334960`; vtable xrefs only at `0x140334989/0x140334A5B` | `IwWind_ConfigureRingInParams @ 0x140333E80` calls `InitializeIwWindRingInObject @ 0x140332D60`; emitter expiry consumes battle LFSR before allocation | Restore must not re-run the emitter or initializer. Emitter timer/count and RNG are restored separately from the graph. Untouched allocator gaps and copied residue lanes are not peer authority. |
| Shockwave | `0x180`, `LuxMoveVM_SpawnAttackWindowWindShockwave @ 0x1402FC5B0`; vtable xrefs only at `0x1402FC767/0x1402FC76E` | `IwWind_InitShockWaveObject @ 0x140331E90`; consumes combined wind RNG, battle LFSR, and LCG | Attack-window corrections can cross creation/retirement, so a topology generation barrier alone cannot satisfy rollback. Raw allocation plus semantic reconstruction is required. |

Current-executable replay catch-up localized a false canonical divergence to packed ShockWave semantic chunk 8. `IwWind_PrepareShockWaveFrame @ 0x1403322C0`, `IwWind_UpdateShockWaveOscillation @ 0x140332400`, and `IwWind_SampleShockWaveForce @ 0x140332B60` independently exclude object bytes `+0xE4..+0xEF`. Those 12 bytes are allocator residue: snapshot schema 34 retains/restores them as local derived bytes but excludes them from canonical hashing and peer authority.

All four classes insert at the root head, set the concrete vtable before
linking, and are retired only by the root tick after their query-active virtual
returns zero. Retirement overwrites the base vtable, unlinks both neighbors and
the root head, then calls `UE_FMemory_Free` with only the node address. The sole
write to `g_pIwWindRoot @ 0x14470E038` is match initialization
`scbattle_WorldMode_InitMatch @ 0x1402DA1B0`, which binds process-lifetime static
root storage. `LuxBattle_InitializeMatchRoundState @ 0x1402DB3A0` does not clear
or reconstruct the intrusive list, so neither match-round initialization nor a
logical round boundary is an undo mechanism.

The structural transaction therefore keeps the old graph linked while it
allocates and populates every replacement off-list. It patches concrete
vtables, root ownership, and ordered forward/reverse edges only from local
addresses, publishes the new head and root value state as one write, recaptures
the pointer-free image, and frees the detached graph only after exact
verification. Allocation or pre-publication write failure leaves the root
untouched; post-publication verification failure restores the saved root image
before freeing replacements. This proves the transaction shape, but not live
admission: zero-filled non-canonical fields are unsafe until every virtual
reader is classified and persistent presentation state has a reconciliation
routine. Production integration therefore remains disabled and the runtime
content allowlist remains empty.

The ring-in virtual-reader audit additionally closes the local derived-state
surface. `IwWind_PrepareRingInFrame @ 0x140333130`,
`IwWind_UpdateRingInOscillation @ 0x140333550`, and
`IwWind_SampleRingInForce @ 0x140333C20` read or write current angles
`+0x120..+0x12F`, travel accumulator `+0x134..+0x143`, path scale
`+0x150..+0x15F`, orientation matrix `+0x160..+0x19F`, and path matrix
`+0x1A0..+0x1DF`. These ranges are now captured as a distinct local-derived
bank: the graph transaction restores them byte-exactly, while
`StageWindTopologyProbe::CanonicalBytes` excludes them from peer authority.
Verified gaps `+0x144..+0x147` and `+0x14C..+0x14F` remain excluded. The
shockwave sample/prepare/update virtuals (`0x140332B60`, `0x1403322C0`,
`0x140332400`) are already covered by the existing semantic ranges and never
touch their documented residue gaps. Structural tests now use a ring-in graph
and prove that changing local matrices/travel values does not change canonical
bytes, while restore still reproduces the complete captured local image.

Candidate checkpoint format v5 retains the v4 wind image instead of discarding
the probe result after logging. Its canonical SHA-256 domain appends only the
pointer-free wind canonical bytes. The local payload separately serializes the
derived bank and protects it with a bounded FNV-1a integrity checksum; corrupt
local matrices therefore fail decoding without promoting them to peer
authority. Decode validates the exact class layout, per-bank sizes, node count,
generation, callback bank, and pending count. The real candidate adapter
captures, transactionally restores, outer-undoes, and recaptures the wind image.
Its SC6 allocator binding admits only the assembly-proven sizes `0x130`,
`0x180`, and `0x1E0`, calls `FMemory_Malloc @ RVA 0x4A61C0` and
`UE_FMemory_Free @ RVA 0x1F90000`, and refuses allocation away from the bound
simulation thread. This path remains inactive with the production allowlist
empty; live normal-render capture is required before any restore exercise.

The historical format-v4 capture path subsequently passed a source-bound, normal-render
replay-entry run for 600 requested frames at commit
`fd7a7710a71c0d911a5aabda3f5e6a54f7a1c9c6`. It retained 21 landing
checkpoints through frame 600 and 35 batch-entry checkpoints through frame 613;
the graph grew from two to four nodes without a capture, identity, checksum, or
capacity failure. Deployed HorseMod SHA-256 was
`B4213C7CD39D98C594C6A4B253E80A3AF08E65F5BDB81D8AD3F44A7C34127B25` and
generated schema SHA-256 was
`70D0D5B73F547874CAD94E0354E75D8E91B730ADF64D2ECA04A8DF3997E7FFA7`.
This is evidence for live capture and format ownership only; it does not claim
that the FMemory-backed restore transaction has executed safely in SC6.

## RNG and FP contract

### Explicit Lux RNG families

| Stream | Owner/extent | Writers and representative reachable consumers | Restore/hash rule | Status |
|---|---|---|---|---|
| Park–Miller LCG | `g_dwLuxBattleLcgRngState @ 0x14485EB28`, `uint` | Init `0x14034F610`; helper `LuxMoveVM_GetRandLCG @ 0x14034F550`; direct/inlined writers in AI palette and wind constructors `0x1402FA080`, `0x1402F9E10`, `0x1403311F0`, `0x1403315A0`, `0x140331E90`, `InitializeIwWindRingInObject @ 0x140332D60`, `0x140334430` | Restore/hash the scalar independently; never infer stream ownership from helper callers alone because inlined recurrence writers exist. | **Proven-static direct xref surface; reachability tied to unsupported AI/wind routes.** |
| Battle LFSR | `g_adLuxBattleLfsrState @ 0x14485EB30`, 25 `uint` (`0x64`), plus `g_dwLuxBattleLfsrIndex @ 0x14485EB94` | Init and `LuxMoveVM_GetRandU32 @ 0x14034F130` are the only direct global writers. Its current-binary caller list spans hit/reaction, CPU-direct, camera shake and wind construction/update. | Restore/hash all 25 words plus index as one stream. Preserve draw order; presentation-looking callers cannot be skipped before their irreversible terminal. | **Proven-static state/writer closure; admitted traversal ordering remains blocked by reachable caller closure.** |
| Gameplay xorshift96 | `g_stLuxBattleXorshift96State @ 0x14470E2C8`, 12 bytes | Init; `LuxMoveVM_GetRandXorshift96Gameplay @ 0x14034F1F0`; native save/restore `0x1402FBB48/0x1402FBDFE`; HgCpu direct executors `0x1403843B9/0x140384727`. Callers include hit/damage/block, MoveVM probability, intro/round pose, camera and effect variants. | Restore/hash 12 bytes; native round snapshot is evidence but does not cover LFSR/UCRT/wind. | **Proven-static state; shared-order reachability open.** |
| Wind combined RNG | `g_stLuxBattleWindCombinedRngState @ 0x14470E2B0`, 24 bytes | Init and `IwWind_GetRandCombinedRngU32 @ 0x14034F3C0`; seven direct wind constructors/oscillator consumers | Restore only with a validated wind topology and generation. | **Proven-static stream; blocked by Phase 4 graph.** |
| Lux MT | `g_stLuxBattleMtState @ 0x144100EA0`, 5000 bytes | `LuxBattle_MT_Seed`, generator/refresh, initialization, attention camera and replay entry; one unexplained write at `0x14010451E` remains outside the named consumer surface | Restore/hash only if a reachable admitted path uses it and the unexplained writer is resolved. | **Unknown/blocker.** |

The inactive schema-v2 candidate image now captures, hashes, decodes, transactionally restores, recaptures, and exactly undoes the LCG, 25-word LFSR plus cursor, gameplay xorshift96, and wind-combined streams. `LuxMoveVM_GetRandU32 @ 0x14034F130` proves cursor value 25 is the valid between-draw refill sentinel; only values above 25 fail closed. Commit `fe64edb520a3306369d0232c54f165fc8039409a` corrected the initial `< 25` implementation error. A source-bound 600-frame normal-render run at commit `2ab1226fb52a9a695673b482eb460189b1f105ce` captured through frame 600 with no candidate/topology failure. DLL SHA-256 is `46C500E94D832E327BA55599217FBC6CAD9ABCC39F8B30812B09E01C91C112B4`; generated schema SHA-256 is `70B64E1BB2B25CB1B1A175868F88657660E80DEA79E4F7B30D09FBE2335CC414`. These four streams remain inactive because caller ordering, wind topology, UCRT, MT reachability, and FP scope are enclosing blockers.

`LuxBattle_InitRngAndHashPrimes @ 0x14034F610` is one coupled generation transaction. Assembly proves UCRT `srand(seed >> 4)` at `0x14034F62E`, Lux MT seeding with the full seed, exactly `seed & 0xFFF` UCRT/MT warm-up iterations with UCRT draw at `0x14034F652`, then LCG/LFSR derivation, gameplay-xorshift seeding, and one or more gameplay-xorshift draws to derive the separate wind stream. Match initialization and NewRound entry therefore invalidate all previous RNG snapshots together. **Proven-static.**

### UCRT `rand` blocker and broker contract

The call target is the IAT entry at `0x14322D800`; `srand` is `0x14322D818`. The current binary contains a gameplay-reachable imported-UCRT draw in MoveVM opcode `0x50006` inside `LuxMoveVM_ExecuteAndDumpOpcode @ 0x140365900` (call site `0x140366FEE`), plus the initialization warm-up at `0x14034F652`. The same thread-local stream is also consumed by many unrelated camera, audio, particle, stage/debris, auxiliary-actor and engine functions. A raw Lux global cannot restore it.

No safe native getter/setter for the current thread's UCRT seed was found. Therefore any future adapter must either fail closed on every route that can execute opcode `0x50006`, or install a scoped broker with this minimum contract:

1. Intercept the exact IAT-backed `rand`/`srand` calls, not all randomness APIs.
2. Require the admitted simulation thread identity captured at binding; reject migration or nested ownership from another thread.
3. Maintain a private deterministic stream only for a versioned return-address allowlist beginning with `0x14034F658` (initialization warm-up return) and the return after `0x140366FEE` (MoveVM RAND).
4. Preserve the caller's real UCRT stream by forwarding every non-allowlisted call unchanged. Presentation reconciliation cannot borrow the gameplay broker stream.

The inactive broker now implements this contract at commits `aa4393ea` (value-only broker), `6f8eb4d0` (atomic IAT hook ownership), and `a71aee5c` (bind the owner thread at the exact RNG-initialization `srand` callsite instead of guessing from UE4SS initialization). The IAT detours forward every call to the real CRT exactly once. Only relocated return RVAs `0x34F658` and `0x366FF4` advance the private Microsoft-CRT-compatible stream, and only `0x34F634` seeds and binds it. Calls from every other return address remain untouched; a later allowlisted thread migration is terminal.

The first normal-render proof correctly failed closed with `wrong_thread` when the broker was incorrectly bound to the UE4SS initialization thread. After the native-seed binding correction, the source-bound 600-frame replay at commit `a71aee5cf627c3084b471c090a7b3b230eb69d45` completed with 21 landing checkpoints, no candidate/topology failure, broker mode `Observing`, failure `none`, an authoritative seed, 734 allowlisted draws, and final private state `0xA99E413B`.

Commit `a76b2189f370cfe58c275d9e53422d1b4cd8ffa7` promotes that shadow state into candidate checkpoint schema v3. The canonical hash and pointer-free codec now include algorithm version, return-allowlist version, state, draw count, and seeded flag. The adapter binds the broker to the measured simulation thread, requires `Owned` mode before restore, captures a complete undo image, restores HgCpu/native/UCRT in consumer order, and reverses UCRT/native/HgCpu on failure. CTests prove codec round-trip, exact successful UCRT restore, and preservation of the existing native/HgCpu undo guarantees. Its exact normal-render run again captured 21 landing checkpoints through frame 600 with 734 draws, broker failure `none`, and no candidate/topology failure. HorseMod DLL SHA-256 is `FAF559025475BF0F15216F7242B587C407845221E60A9B745CE36B8C8C564C6C`; generated schema SHA-256 is `CED7084AB7DC4C57F8CA63B1BE484844C45E3CA27FCC72107EF7ADE7181E171E`. Stock CRT state is still forwarded and excluded. Activation remains blocked until ownership is acquired only at a frozen seek/rollback baseline and actual SC6 restore/resimulation failure injection proves convergence and exact undo.
5. Use an exception-safe nesting counter and restore interception context on normal return, native failure, C++ exception/unwind, adapter abort, scene invalidation, and module teardown.
6. Record and hash the broker algorithm/version/state and the allowlist version; peers with different versions fail handshake.

This design remains **bounded-inference**, not production proof, until runtime establishes thread identity, exact return addresses after relocation/patching, nested behavior, and stable draw counts for the qualified content.

### Floating-point environment

A full-program mnemonic scan of 11,640,055 decoded instructions found 26 `LDMXCSR` and 13 `STMXCSR`, all outside the Lux battle/MoveVM address ranges reached above (the closest groups are at `0x1420...` and `0x14319...`). It found no `FLDCW`, `FNSTCW`, or `FSTCW` instruction anywhere in the decoded executable. Thus no explicit MXCSR or x87 control-word mutation has been found in the statically inspected Lux traversal. **Proven-static negative result for decoded instructions.**

This does not prove a fixed caller environment or game-thread affinity. The adapter contract must capture MXCSR and x87 control/status at owned-entry, execute with the caller's values unless a separately qualified versioned normalization policy is established, and restore the exact caller values on every exit. No process-wide mutation is permitted. The read-only `FXSAVE64` probe at commit `a9a986b3fe3cbdcb121ae96d430ea4bfe1bbdd5d` sampled the same bound game thread across 597 outer native batches in the normal-render replay workload. Control fields had zero entry/exit mismatches; x87 status also had zero mismatches. MXCSR sticky status differed across 359 batches, proving that exact MXCSR restoration is required around resimulation even though rounding/masks/FTZ/DAZ and all observed x87 state were stable. Commit `58d47a6dbdba1927dc0961d2c822a87311d2cf21` implements the resulting narrow scope around every candidate adapter capture, restore, derived rebuild, advance, and presentation reconcile: it restores raw MXCSR on every normal/destructor exit, verifies the complete caller environment afterward, and fails closed if x87 control/status ever changes instead of using a broad `FXRSTOR` that could overwrite SIMD register state. Its source-bound normal-render 600-frame run retained all candidate checkpoints with no capture/topology failure; the outer stock transaction again showed zero control/x87 drift and 362 MXCSR-status changes. DLL SHA-256 is `B66DE0D26F1FF12A91330344E9EDC8DDC5993AAE78179AD1F08AE6997AE2BAAF`. Production activation remains forbidden until this scope passes actual restore/resimulation failure injection in SC6 and the UCRT broker is qualified.

## Presentation journal contract

The suppression point is the first irreversible terminal, never the listener hub or a mixed semantic handler. `DispatchSecondaryVfxRequestFromLuxEventHub @ 0x140400540` proves why: collection 0 synchronously reaches a callback that resets/reactivates trace lifecycle and can write character state. Audio creation at `LuxAudio_RegisterActiveVoiceInstance @ 0x14054F8B0` consumes UCRT `rand`, mutates the active-voice map, and returns an ID later consumed by stop paths. Both routes must retain their reversible/gameplay work during resimulation.

| Family | Last reversible/mixed boundary | First proven terminal | RNG/gameplay feedback | Required handling | Status |
|---|---|---|---|---|---|
| Audio create | Cue conversion/dispatch; scheduled binding producer `0x140542490` | Active voice/CRI creation `0x14054F8B0` | UCRT `rand`; returned ID is saved for later stop | Allocate a logical ID speculatively; on confirmation re-resolve the same owner generation, create once, and map logical to native ID | **Unknown/blocker:** full create/update/stop hook surface and UCRT broker are unqualified. |
| Audio stop | Signed-key scan/removal `0x140542730` | Stop queue `0x140560D60`; stop-all `0x140560940` | Reads the prior native ID; binding removal is local semantic state | Mutate a logical shadow during speculation; confirm through the current-generation mapping; discard stale mappings | **Bounded-inference; terminal targets proven-static, route set open.** |
| BGM/jingle | BGM lane/crossfade and cue selection | Shared create `0x14054F8B0`; BGM/ID stops `0x140560B60`/`0x1405617B0` | Same UCRT/native-ID dependency | Journal semantic start/stop/parameter values and reconcile persistent lane state at landing | **Unknown/blocker.** |
| Secondary VFX | Hub conversion/broadcast `0x140400540`, exact `0x44 -> 0x4D` payloads | Route-dependent component creation; proven particle helper `0x1408A3920` | Hub collection 0 has a gameplay consumer | Execute hub and semantic callbacks every resimulated coordinate; journal only concrete create/destroy/material terminals | **Unknown/blocker:** callback/terminal route coverage incomplete. |
| Breakable presentation | Barrier hit `0x140549F40` increments gameplay count and notifies listeners | Particle helper `0x1408A3920` and downstream material/component operations | Barrier state and listener effects are not presentation-only | Run gameplay mutation/listeners; journal asset/transform/material values; reconcile from canonical barrier state | **Unknown/blocker.** |
| Color fade | BeginPlay/subscriber/lane ownership `0x14044F2C0` | Concrete material writes unresolved | Persistent queues are read on later ticks | Snapshot only after lane semantics close; journal material writes and rebuild displayed state | **Unknown/blocker.** |
| Camera/cinematic | Camera actions and synthesis inside `0x1402DBC60` | UE view/shake publication unresolved | Shared RNG and side-relative gameplay reads | Keep camera semantics canonical; journal only final publication after concrete vtable closure | **Unknown/blocker.** |
| UI/round/result/replay | Manager round sequencing and listener callbacks | Route-specific widget/announcement terminal unresolved | Broadcasts can invoke stateful callbacks | Never suppress manager collections; journal only proven terminal calls | **Unknown/blocker.** |

Every eventual record needs `{sessionGeneration, roundGeneration, coordinate, sequenceWithinCoordinate, family, logicalObjectId, valuePayload}`. The exactly-once key is the first five identity/order fields plus `logicalObjectId`. No pointer, UObject weak index, callback address, native playback ID, container header, or allocator token may enter peer-visible data. Rewind discards speculative records; confirmation commits in original order. Failed preflight or generation change drops all pending records and triggers persistent-state reconciliation.

## Lifecycle and teardown contract

The adapter owns a monotonically increasing `sessionGeneration`; `roundGeneration` is subordinate. Increment before publishing new native resolvers whenever a hard boundary begins. Checkpoints, cached resolvers, logical presentation IDs, pending events, and allocation tokens carry the generation. Any mismatch invalidates the checkpoint atomically; addresses never define a generation.

| Transition/signal | Proven native sequence | Objects invalidated | Adapter action/status |
|---|---|---|---|
| Manager BeginPlay | `InitializeLuxBattleManagerBeginPlayPipeline @ 0x1403E79C0` creates the world-mode shared pair, spawns manager actors, registers callback/dependency topology, and loads authored assets | Manager subactors, providers, callback handles, async ownership, presentation managers | Bind only after return and complete preflight; never call BeginPlay as repair. **Proven-static.** |
| FrameInput BeginPlay | Base `0x1403E6EF0` binds manager `+0xB80/+0xBF0`; Sync `0x1403E6FB0` registers channel 4, runs base BeginPlay, then binds `+0xAA0/+0xB10/+0x1050/+0xDB0` | Input object/cache, delegates, transport handle/deque | Publish resolver only after full registration; partial/no-manager setup unsupported. **Proven-static.** |
| Round init/reset | Manager `0x1403FB660`; provider reset `0x1403F4E00`/Sync thunk `0x1403F4EC0`; cache reset `0x1403F7C60`/`0x1403F7D20` | Round cache/clocks, timers, relation/stage setup, logical presentation IDs | Increment `roundGeneration`; baseline only after reset callbacks complete. **Boundaries proven-static; complete caller order open.** |
| Replay transition | Entry `0x1403014A0`/`0x140382560`; active-mode tick `0x140382770`; destructive stop/next-mode selection `LuxBattle_Replay_PostTick @ 0x1403829F0`; ReplayPlayer round-image producer `0x140435C20`; state-4 consumer `ProcessLuxBattleManagerMoveStateAndDispatchCallbacks @ 0x1403F7D70`; decoder `0x1403F63B0` | Replay cursor/cache, MoveVM replay and camera/round state, live world-mode handler and callback topology | Stop admission before PostTick transition. For cross-round selection, let the ReplayPlayer copy its 0xC0 image and request state 4; wait through handler `+0xB0/+0xA8/+0xB8`, manager `+0x8E0`, and common cleanup; then validate newly published battle/fighter/stage identities and bind a new generation. Never restore prior-generation pointers. **Proven-static ordering; runtime fencepost/identity proof required.** |
| FrameInput EndPlay | Sync `0x1403EDF90` removes four Sync delegates, two base delegates, forwards EndPlay, unregisters channel 4, then clears online; base Log `0x1403EDF30` removes its two delegates first | Input/cache/transport resolvers and queued work | Invalidate before removal starts. **Proven-static order.** |
| FrameInput destruction | Runtime destructor `0x1403DE970` deletes lock, drains/frees deque, destroys dispatch arrays, then inherited state; scalar wrapper `0x1403E1780` may free object | Lock/deque/arrays/object identity | No checkpoint or callback may retain an address. **Proven-static.** |
| Match teardown | `LuxBattle_TeardownMatchState_OnEnd @ 0x1402DC380` releases frame resources, resets MoveVM/fighters/effect/camera/shared owner, enters idle, clears finalization/time-dilation | Every match-scoped simulation and presentation identity | Increment generation at entry; fail closed until fresh BeginPlay/baseline. **Proven-static hard boundary.** |
| Fighter registry teardown | `LuxBattle_DestroyCharaRegistryAndDeactivateRuntime @ 0x1402DCE70` frees all nodes, resets sentinel/count, clears active flag | Fighters and registry-derived identities | Invalidate before first free. Wind is independent and needs its own signal. **Proven-static.** |
| Scene/disconnect/lobby | Earliest authoritative signal not closed; paths converge on EndPlay/teardown but may begin earlier | World, manager, online registrar, fighters, stage, presentation owners | Resolver/world mismatch invalidates immediately; exact hooks and physical recovery remain required. **Unknown/blocker.** |
| Module/process cleanup | Horse ownership lies outside this binary; native callbacks can remain reachable during destruction | Hooks, callbacks, journal, workers | Stop admission, invalidate, cancel/drain work, remove hooks in reverse order, release storage. **Design requirement; runtime proof required.** |

FrameInput registration/removal is asymmetric: online registration precedes base and Sync manager binding; EndPlay removes Sync delegates, removes base delegates, forwards actor EndPlay, then unregisters online. Horse needs its own teardown gate before native EndPlay and cannot rely on channel removal alone to exclude late weak-owner dispatch.

### Replay cross-generation materialization contract

Cross-round replay seek is a native rebuild followed by an ordinary same-generation restore. It is not a restore across native object generations.

1. `ALuxBattleReplayPlayer_Tick_CopyRoundResetSnapshotAndSetMoveState4 @ 0x140435C20` validates replay enablement and BattleManager status, changes the selected round cursor, computes `ReplayPlayer+0x3A8 + cursor*0xC0`, copies exactly twelve 16-byte `MOVUPS` chunks into `BattleManager+0x1360..+0x141F`, then sets manager move state `REPLAY_ROUND_RESET` (4). The dedicated `ALuxBattleManagerReplayRoundState_Partial` Ghidra view records this exact base. The older broad manager partial placed the field at `+0x135C`, which was a four-byte type error and must not be used for this operation.
2. On the manager tick, `ProcessLuxBattleManagerMoveStateAndDispatchCallbacks @ 0x1403F7D70` resolves and retains the current world-mode handler, dispatches `+0x950` and `+0x870`, constructs stage/velocity parameters, applies the image through handler virtuals `+0xB0`, `+0xA8`, and `+0xB8`, sets the completion byte at manager `+0x1465`, dispatches `+0x8E0`, resets color-fade and six managed actor slots, then atomically changes manager `+0x1463/+0x1464` to `{0,1}`.
3. Horse may treat the return from that manager transaction as a materialization fencepost only after validating the requested round key and every newly published battle, fighter, stage, allocator, handler, and callback identity. An unchanged address is not proof of an unchanged generation.
4. The deterministic session releases its old binding without deleting recorded inputs/checkpoints, binds the new `NativeContext`, captures a fresh undo/baseline image, then restores the nearest checkpoint in that same new generation and resimulates to the target.
5. A request mismatch, early lifecycle exit, invalid handler, missing fencepost, identity mismatch, capture failure, restore failure, or verification failure terminates the seek. No old-generation write is attempted and the native replay remains at the last fully materialized round.

The inactive production bridge now implements only the proven request half of this contract. It resolves the ReplayPlayer, BattleManager, both fighters, and stage on the game thread; validates replay enablement, manager status 2, idle move state, bounded round count/capacity, and the exact move-state-setter signature; captures the existing `0xC0` manager image and move state; copies the selected native `0xC0` round image; requests state 4 through the native setter; and verifies both the copied bytes and state. Any post-write failure restores and verifies the exact prior image and state. Its 64-bit FNV-1a image identity is a local stale-request guard only, not the canonical simulation hash or an authentication primitive. The bridge is not connected to production lifecycle/hook ownership, and completion still requires the state-4 fencepost and full post-materialization identity validation described above.

`LuxBattle_Replay_PostTick @ 0x1403829F0` is the separate destructive stop/next-mode boundary. With `dwExitGuard == 0`, it restores camera/timer configuration, clears both fighters' replay/movement flags, and queues RoundResult or NewRound. Mode ID 20 (`HgBattleModeShortReplay`, vtable `0x143E87588`) always queues NewRound. The ordinary replay vtable is `0x143E87210`. Horse must invalidate admission before this PostTick transition and must not interpret the shared `GetLuxTypeId4 @ 0x140301490` vtable entry as an exit callback.

## Production candidate regions in restore order

These rows pass the current-binary **static** region audit. They are implementation-ready components, not authorization to activate the adapter: activation still requires all enclosing fighter/input/RNG/world/presentation/lifecycle blockers to close and an independent review before `Schema::production_regions` changes.

Before any row below is written, the adapter must validate the common build/session/round roots, validate every row-specific identity, capture an exact undo image for every region it may mutate, and verify that the transaction is outside a native RoundResult HgCpu restore-capable state. Failure before the first write is a no-op. Failure after the first write restores all prior rows from the undo image in reverse mutation order, recaptures them, invalidates the checkpoint generation, and disables native restore.

| Restore order | Region/owner | Semantic extent and classification | Capture phase | Atomic preflight | Restore/repair/verify | Canonical hash and exclusions | Lifetime/evidence |
|---:|---|---|---|---|---|---|---|
| 1 | MoveDispatch event masks behind `LuxBattleCharaFrameActionRuntimeV2_Partial +0x4A8` | Two qwords, exactly 0x10 bytes, object-lifetime canonical gameplay/event state | Completed outer manager transaction, after `+0x1210` filtering | Same MoveDispatch UObject/owner; data pointer unchanged; count exactly 2; capacity at least 2 and within the constructor-proven fixed bound; same session/round | Write the two qwords only; never write the `TArray` header; recapture values. Undo is the same 0x10 bytes | Hash lane/object role plus both qwords. Exclude owner pointer, header/count/capacity, weak callback identity | Constructor `0x1404049E0` allocates/zeros exactly two; OR writer `0x1404274E0`; 20 predicates `0x140415EF0..0x140417960`; destructor `0x14040A850`; filter registration/removal `0x1404157D0/0x14041D750`. **Proven-static.** |
| 2 | `g_abLuxMoveSystemVMPumpState @ 0x144100C70` | `+0x20/0x1C`, `+0x50/0x1C`, `+0x70/0x18`, total 0x50 semantic bytes; canonical gameplay | Completed outer manager transaction | Same six identities at `+00,+08,+10,+18,+40,+48`; state `0..4`; enabled `0..1`; same Begin–End/round generation; no state-4 replacement crossed | Write the two lanes then controls; no factory call; recapture the three banks before the next pump dispatch. Undo uses the same banks | Hash the three banks in fixed order. Exclude all six identities and unknown tails `+3C/4`, `+6C/4` | Begin `0x14031C950`; loader `0x1402DBB20`; pump states `0x14031CC00..0x14031D5B0`; End `0x14031CAC0`. **Proven-static.** |
| 3 | P1 `FLuxMoveSchedState` at `g_abLuxBattleCpuCommandStates + 0x00` | `+0x08/4`, `+0x30/0x20`, `+0x58/4`, exactly 0x28 pointer-free canonical bytes | Completed outer manager transaction | Same static slot, vtable `+0x00`, fighter `+0x10`, owned SubVM `+0x50`, session/round and active slot `0..1`; all P1/P2 checks finish before writes | Write the three banks and recapture; no reset/factory call. Undo uses the same banks | Hash lane plus the three banks. Exclude identities and `+0x0C/4`, `+0x18/0x18`, `+0x5C/4` | Static init/dtor `0x140103FA0/0x1431EAEF0`; constructor/reset/tick/commit/training paths `0x1402E24C0`, `0x1402E25A0`, `0x1402E52D0`, `0x1402E5660`, `0x1402D3E50..0x1402D4004`. **Proven-static same-generation.** |
| 4 | P2 `FLuxMoveSchedState` at `g_abLuxBattleCpuCommandStates + 0x60` | Same three banks and classification as P1 | Same capture phase | Same checks independently for P2 | Same restore/recapture; both lanes form one atomic component | Same hash with distinct lane tag and exclusions | Same complete writer/reader/lifetime evidence. **Proven-static same-generation.** |
| 5 | P1 current `CCpuDirectCommand_Partial`/derived SubVM from scheduler `g_abLuxBattleCpuCommandStatePerPlayer +0x50` | `+0x08/4`, `+0x20/0x3C`, plus derived `0`, `8`, `0x10`, or `0x14` bytes for extents `0x68`, `0x70`, `0x78`, or `0x80`; class RVA and extent are canonical metadata | Completed outer manager transaction | Same scheduler, SubVM address, allowlisted vtable RVA/extent, fighter `+0x10`, opponent `+0x18`, owner scheduler `+0x60`; no factory/swap crossed | Write common semantic banks and canonical derived prefix only; do not invoke factory; recapture class/identities/bytes. Undo uses the same semantic bytes | Hash lane, vtable RVA, extent, semantic bytes. Exclude vtable address, raw identities, `+0x0C/4`, `+0x5C/4`, and for `0x80`, `+0x7C/4` | Factory/reset/init/swap `0x1402E25A0`, `0x1402E26A0`, `0x1402E5220`, `0x1402E5710`, `0x1402E57D0`; exact allowlist below. **Proven-static for allowlisted current generation.** |
| 6 | P2 current allowlisted SubVM | Same class-dependent semantic contract as P1 | Same capture phase | Same checks independently for P2, completed before P1 is written | Same restore and recapture; both lanes are one atomic component | Same hash with distinct lane tag | Same evidence and hard rejection of unknown class/replacement. **Proven-static.** |
| 7 | P1 MoveCommand slot at `g_abLuxMoveCommandPlayers + 0x0000` | Nine banks totaling 12,076 bytes: `+0x0000/8`, `+0x0018/0x10`, `+0x0038/0x308`, `+0x0348/0x860`, `+0x0BE8/0xE0`, `+0x0CD0/8`, `+0x0CE8/0xCB0`, `+0x19A0/0x1088`, `+0x2AA8/0x58C`; canonical gameplay | At the completed outer manager transaction boundary, after all callbacks/round sequencing for the coordinate | Exact global root; current P1/P2 owner identities; all 17 slot identity qwords equal the captured generation; same match/round; no reset/rebind or native cinematic restore in progress | Write the nine banks only, after fighter/scheduler identities have been restored/repaired; do not run personality initialization; byte-recapture all banks and compare. Undo is the same nine-bank image | Hash ordered `{bankId,size,bytes}`. Exclude identities at `+08,+10,+28,+30,+340,+BA8,+BB0,+BB8,+BC0,+BC8,+BD0,+BD8,+BE0,+CC8,+CD8,+CE0,+1998`, diagnostic `+2A28/0x80`, and tail `+3034/4` | Static arena; reset/rebind boundary `0x140302930`; init `0x1402DED20`; ordinary driver `0x1403656B0`; predicate `0x140364D10`; personality `0x140364950/BC0/C90`; current exact overlay size `0x3038`. **Proven-static.** |
| 8 | P2 MoveCommand slot at `g_abLuxMoveCommandPlayers + 0x3038` | Same nine semantic banks and classification as P1 | Same capture phase | Same checks, independently applied to P2's 17 identities before P1 is written | Same nine-bank write and recapture; P1 and P2 are one atomic component for undo/failure | Same field-order hash with an explicit lane tag; same exclusions | Same static arena and writer/lifetime evidence. **Proven-static.** |
| 9 | P1 `FLuxMoveVMSlotParam` at `g_abLuxMoveVMSlotParamArray + 0x00` | `+0x00..+0x27`, exactly 0x28 bytes, pointer-free canonical gameplay | Same completed outer transaction boundary | Exact static root; same match/round; native RoundResult state is not restore-capable; candidate build/type version matches | Write 0x28 bytes after MoveCommand; no derived repair; recapture exact bytes before the first resimulated `LuxMoveVM_AdvanceSlotParamLerp` | Hash lane tag plus 0x28 semantic bytes; exclude stride padding `+0x28/4` | Init `LuxMoveVM_InitSlotParamTables @ 0x1402E5910`; effect writers in `LuxMoveVM_DispatchEffectOp @ 0x140376B20`; native RoundResult writer `0x14037D670`; per-frame reader/writer `0x14032F780`. **Proven-static.** |
| 10 | P2 `FLuxMoveVMSlotParam` at `g_abLuxMoveVMSlotParamArray + 0x2C` | `+0x00..+0x27` lane-relative, exactly 0x28 bytes, pointer-free canonical gameplay | Same capture phase | Same checks as P1 | Write after P1; recapture both lanes as one atomic component | Hash distinct lane tag plus 0x28 bytes; exclude lane-relative `+0x28/4` | Same complete writer/reader/lifetime set; native advancement order is P1 then P2. **Proven-static.** |
| 11 | Persistent pending-hit transition globals | `{dwReactionMoveId @ 0x14485E738, flLauncherFacingDelta @ +4, pAttacker @ +8, dwTransitionFlags @ +0x10}` and `bLauncherSync @ 0x14470F38D`; scalar gameplay except the raw attacker identity | Completed outer manager transaction; the slot may remain live only when `0x14033CCA0` takes an early world/hit gate | Same P1/P2 roots and session/round; attacker is exactly null/P1/P2; launcher-sync is `0/1`; all checks complete before writes | Write the scalar fields and reconstruct attacker only from slot `0/1/2`; recapture to the same semantic slot. Undo uses the same typed image in reverse order | Hash move ID, facing delta bits, flags, attacker slot, and sync byte. Exclude the raw fighter pointer | Producer `0x1402FF3E0`; launcher consumer `0x1402FF530`; damage consumer/clear `0x1402FF620`; full-frame consumer `0x14033CCA0`; round initialization `0x1402E16F0`. Per-frame event lists are allocated and completely freed inside `0x14033CCA0` and never exist at capture. **Proven-static candidate; implemented and unit-tested.** |
| 12 | BattleManager round-state byte sequence at `+0x1470` and current state `+0x1480` | Up to 32 ordered byte state IDs, exact count, and current-state byte; canonical gameplay/round state | Batch-entry or completed outer transaction checkpoint | Same manager/session/round and backing pointer; current capacity unchanged, at least eight and sufficient for the saved count; saved/current counts `0..32`; no allocation or header replacement crossed | Write saved bytes, count, and current state only; never write pointer/capacity or call reserve. Recapture the typed sequence. Undo writes the prior typed sequence in reverse transaction order | Hash count, current state, and exactly count bytes. Exclude unused capacity bytes, backing pointer, and capacity | Constructor `0x1403DC7F0` reserves eight; sole exact append primitive `0x1403F41B0`; consumer `0x1403FCE80` processes in order and resets count only after exhaustion. The 32-byte ceiling is an adapter admission bound, not a native maximum; excess disables capture. **Proven-static bounded candidate; implemented and unit-tested, runtime cross-round population proof pending.** |
| 13 | PlayerWatch camera-action distance history in each exact `CCameraPlayerWatchClass` slot (vtable RVA `0x3E87EB0`) | Sixteen IEEE-754 value words at action `+0x25C..+0x29B`, signed sample count at `+0x29C`, and ring cursor at `+0x2A0`; canonical historical camera input | Completed outer transaction after camera update/synthesis | Same `0x41E0` backing allocation, exact 17-slot topology and vtable identities; only exact PlayerWatch slots are present; count nonnegative and cursor `0..15`; no reset/class/backing generation crossed | Write only the 16 value words, count, and cursor for present PlayerWatch slots; never copy a whole `0x3E0` action. Recapture bit-exactly. Undo writes the same three banks in reverse transaction order | Hash per-slot presence followed by exact float payload bits, count, and cursor. Exclude backing/slot pointers, vtables, owner/list identities, cached type, every other action field, and every unsupported class | `LuxEffectCamera_ResetGameCameraData @ 0x14031A2B0` zeros the ring/count/cursor; `LuxEffectCamera_UpdateDistanceWithHistory @ 0x14031B1C0` is the bounded writer and saturates signed count; `LuxEffectCamera_GetAveragedDistanceFromHistory @ 0x14031B0C0` reads the current and preceding values modulo 16 with a five-sample bound; `LuxEffectCamera_UpdatePlayerWatchCameraState @ 0x14031A490` consumes it. Exact executable vtable xrefs bind this implementation at vtable `0x143E87EB0` (`+0x118` update, `+0xD0` reset). **Proven-static same-class candidate; implemented and unit-tested, normal-render class reachability/capture pending.** |

### Exact same-generation SubVM allowlist

The current `LuxMoveVM_CreateCpuDirectState @ 0x1402E26A0` decompilation still contains the four allocation extents and the named factory branches represented by this list. Entries are `vtable RVA:allocation extent`; anything absent is a hard admission failure:

```text
3E863D0:68 3E85608:78 3E868F0:78 3E85698:78 3E85D10:68 3E857B8:68
3E86C50:68 3E86BC0:68 3E865D8:68 3E860B8:68 3E85C38:70 3E862F8:68
3E85C80:68 3E85770:68 3E86418:68 3E85E30:68 3E86028:78 3E85F08:78
3E861D8:78 3E86788:68 3E86D28:68 3E86FF8:68 3E86818:70 3E85D58:68
3E86190:68 3E864F0:68 3E85A40:68 3E86B78:78 3E86938:68 3E86548:68
3E858D8:68 3E85848:68 3E866B0:78 3E86100:78 3E85DA0:78 3E85578:78
3E85CC8:78 3E85F50:68 3E86860:68 3E859F8:70 3E868A8:68 3E86F68:68
3E85BA8:68 3E856E0:68 3E85AD0:68 3E864A8:68 3E85DE8:68 3E86668:68
3E867D0:68 3E86070:78 3E869C8:78 3E86D70:70 3E85A88:78 3E85B18:78
3E86C98:68 3E859B0:68 3E86E90:68 3E86DB8:68 3E86E48:68 3E866F8:68
3E85E78:68 3E86CE0:68 3E86620:68 3E86220:78 3E85920:78 3E86B30:68
3E855C0:68 3E86A10:68 3E86340:68 3E86590:68 3E86E00:68 3E86268:68
3E86FB0:68 3E85BF0:68 3E891B8:80 3E89248:80 3E85EC0:70
```

The `0x80` cases use `CCpuDirectAllGuardCount_TypedPartial`: `+0x78..+0x7B` is the verified guard threshold and `+0x7C..+0x7F` is constructor-uninitialized tail. The allowlist authorizes only a load into the exact same live allocation. Cross-generation reconstruction remains forbidden until a separate transactional factory contract is proved.

### Native paired restore primitive (not a production region)

`LuxBattle_HgCpuDirect_ExecMoveChangeAndPost @ 0x1403841E0` and `LuxBattle_HgCpuDirect_ExecFinalizeAndPost @ 0x140384540` are a proven inverse pair for a bounded local stream. Native allocation and ring arithmetic use exact stride/capacity `0x28018`. The writer serializes P1/P2 fighter ranges through relocation tokens, xorshift96, five global ranges, a `0xBF0` MoveVM bank, optional `0x360` camera state, timer configuration, active frame bounds/transforms, physics, terrain flags, and VFX; the reader consumes the same order, rebuilds current-segment pointers, relinks opponents, and recanonicalizes native anchors.

The adapter may implement a bounded `FLuxHgCpuBuffer`-compatible shim and call the pair only under this contract:

1. Capture and store `{buildId, schemaId, sessionGeneration, roundGeneration, P1Generation, P2Generation, cameraGeneration, exactCursor, checksum}`.
2. Reject capture overflow above `0x28018`; reject load unless the exact current P1/P2 roots and `0x973F0` allocation extents, fighter classes, optional-camera presence/root and embedded interface class, timer/config roots, and all supplemental-region identities match. For the exact supported build, `g_pLuxEffectCameraComponentList @ 0x14470DEE8` is either null or the static `g_LuxCameraDirectorEffectState @ 0x14470E9F0`; `LuxCameraDirector_Initialize @ 0x140321D90` installs outer/interface vtables `0x143E85568` and `0x143E87A58` at `+0x00/+0x10`, and the native pair conditionally transfers exactly the readable `0x360`-byte prefix. The same initializer constructs the `LuxCameraFuncList_Partial @ 0x14470EE90`, whose owner is the static camera manager at `0x14470ED50`; `LuxEffectSystem_ResetCameraState @ 0x1403234A0` owns one `0x41E0` allocation containing exactly 17 in-place `0x3E0` action records, and `LuxCameraFuncList_CreateNothingSlot @ 0x14032F220` proves every published slot is `backing + index*0x3E0` with common `{slotIndex, owner, list, cachedType}` identities at `+0x08/+0x10/+0x18/+0x20`. HorseMod now records the director root as the local `camera_generation`, revalidates its root/vtables/readable extent plus the action backing extent, all 17 exact slot pointers, common owner/index/list identities, bounded type IDs, and action vtable identities before every capture, and fails closed when an action class changes. The former stage-generation placeholder is removed. This closes camera allocation/class generation validation and the exact PlayerWatch distance-history ring; remaining per-class semantic maps and derived published-vector repair remain blockers.
3. Validate all relocation tokens while reading: every encoded pointer must resolve inside one of the two current fighter segments. Raw token/address bytes never enter the peer hash.
4. Require the reader cursor to equal the saved writer cursor. Then restore the supplements omitted by HgCpuDirect—FrameInput/pairs, LCG/LFSR/UCRT/MT as admitted, MoveCommand, VMPump/scheduler/SubVM, stage/wind, FP scope, and persistent presentation state—in native-consumer order.
5. Recapture the peer-canonical semantic regions. On exception, cursor mismatch, invalid token, identity drift, or semantic mismatch, apply the pre-captured undo image, verify undo, invalidate the generation, and disable native restore.

The opaque HgCpu bytes are never peer-hashed and never prove a complete fighter region. This primitive is valuable because it replaces a speculative whole-fighter field-by-field writer with SC6's paired pointer-relocating restore, while retaining hard blockers for everything it omits.

## Identity-generation table

| Native transition | Affected resolvers | Invalidation behavior | Evidence/status |
|---|---|---|---|
| Manager/world BeginPlay or resolver change | Manager, world mode, subactors, stage/provider, presentation managers | Increment `sessionGeneration`; bind after complete preflight | `0x1403E79C0`; **proven-static boundary** |
| FrameInput BeginPlay/replacement | Cache, clocks, dispatch arrays, transport/delegates | Invalidate old input generation; publish after registration | `0x1403E6EF0`, `0x1403E6FB0`; **proven-static** |
| Provider round reset/NewRound | Round cache, timers, relation/replay state, logical presentation IDs | Increment `roundGeneration`; discard older-round state; baseline after callbacks | `0x1403FB660`, `0x1403F4E00`, `0x1403F7D20`; caller order open |
| CPU-direct/SubVM replacement | Scheduler SubVM/derived allocation and every checkpoint whose HgCpu supplement references it | Increment per-slot allocation token; never restore old pointer; same-generation-only loads may proceed only after class/semantic preflight | `0x1402E26A0`, `0x1402E57D0`; class crossing remains open |
| Wind rebuild/topology mutation | Emitters, nodes, order, accumulated forces | Change wind token; invalidate any checkpoint containing wind | `0x1402D9F30`, `0x140334960`, `0x140333FD0`; **proven-static mutation** |
| Fighter registry destruction | Fighter/registry identities | Invalidate before freeing a node | `0x1402DCE70`; **proven-static** |
| Match teardown | All match state/resolvers/journal | Increment at entry; clear snapshots and pending commits | `0x1402DC380`; **proven-static** |
| FrameInput EndPlay/destruction | Input/cache/deque/callbacks/transport | Invalidate before removal/free; never reuse address | `0x1403EDF90`, `0x1403DE970`; **proven-static** |
| Replay start/stop/restart | Replay cursor/cache, round/camera/MoveVM replay state, live handler/callback identities | Invalidate before `LuxBattle_Replay_PostTick @ 0x1403829F0` queues the next mode. At entry, `dwExitGuard` at `+0x18` is tested before the first destructive camera-slot write at `0x140382A18`. The signature-gated centralized hook emits the invalidation before calling the original only when the guard is zero. Cross-round materialization is requested at `0x140435C20`, synchronously consumed as manager state 4 at `0x1403F7D70`, and becomes bindable only after the `+0x8E0` fencepost/common cleanup and fresh identity validation | **Replay-exit admission closed statically and implemented; runtime stop/re-entry identity proof required** |
| Scene/disconnect/lobby/process detach | Every native/hook identity | Stop admission at earliest signal; increment and teardown | Exact earliest signals **unknown/blocker** |

## Presentation journal table

| Event family | Value-only schema | Commit key | Discard/reconcile rule | Evidence/status |
|---|---|---|---|---|
| Audio create | `{cueSheetId, cueId, flags, logicalVoiceId}` | generation/round/coordinate/sequence/family/logical ID | Discard speculative create; confirm once and map logical/native; reconcile persistent lanes | `0x14054F8B0`; **blocked** |
| Audio stop/update | `{logicalVoiceId or signedKey, operation, value}` | Same envelope/logical ID | Update shadow speculatively; confirm via current-generation mapping | `0x140542490`, `0x140542730`, `0x140560D60`; **blocked** |
| Particle/VFX terminal | `{routeId, operation, ownerLogicalId, assetLogicalId, transform, scale, flags, eventLogicalId, effectLogicalId}` | Same envelope/event logical ID; the event ID is unique per operation while the effect ID remains stable across its lifecycle | Execute semantic hub; discard only a callsite-qualified speculative terminal; recreate/reconcile persistent effect and publish its current-generation component mapping | `ParticlePresentation.*` implements a pointer-free 68-byte C++ schema for only `BarrierHit`, `BarrierBreak`, and `WallBreak`, with `Create`, `Stop`, and `Finished` operations. The generated contract owns version/kind/size constants; dynamic route 4 fails closed, and non-create operations reject create-only values. Distinct event/effect IDs prevent same-coordinate stop/create operations from colliding in the exactly-once journal while retaining one logical-to-native effect mapping. `SpawnLuxParticleSystemComponent @ 0x1408A3920` is a complete creation/registration/activation terminal, but it cannot be suppressed globally. `HandleStageBreakableBarrierHit @ 0x140549F40` and `HandleStageBreakableWallBroken @ 0x14053D4B0` store its return and bind finished callbacks; Blueprint native-exec caller `DecodeAndInvokeSpawnLuxEmitterAtLocation @ 0x140CF4330` publishes the live component identity through `ppResult`, allowing later Blueprint code to branch, stop, or retain it. Use qualified static callsite routes only after their stop/update/destroy paths close; the dynamic Blueprint route remains unsupported unless proven unreachable for an allowlisted workload. **Value contract implemented; hooks blocked.** |
| Material/fade | `{targetId, parameterId, value, duration, mode}` | Same envelope/target ID | Reconcile display from canonical lane state; cancel obsolete fades | BeginPlay `0x14044F2C0`; terminal **unresolved** |
| Camera/view | `{actionId, kind, value parameters}` | Same envelope/action ID | Run semantics/RNG; compare the final pointer-free native camera publication bitwise; journal only independently verified UE render-side outputs | interface `+0xA0/+0xA8/+0xB0` resolved to read-only X/Y/Z provider accessors |
| UI/round/result | `{eventKind, player/round IDs, table IDs/scalars}` | Same envelope | Run manager callbacks; commit widget/announce terminal once | Concrete terminals **unresolved** |

The finished-callback ownership is now closed for the three static routes. `HandleStageBreakableBarrierParticleFinished @ 0x14054E890` compares the completed component against the barrier's hit-effect field and clears it on equality, otherwise compares and clears the break-effect field. `HandleStageBreakableWallParticleFinished @ 0x14054E8C0` unconditionally clears the wall's single particle-component field and ignores the callback component argument. Both callbacks mutate only presentation identity mappings; neither mutates canonical breakable gameplay state. Their value representation is therefore a `Finished` operation against a stable logical effect ID, never the component pointer.

This does not admit suppression. The static handlers also stop prior components, publish returned components into actor fields, bind delegates, update visibility/material/animation, and synchronously dispatch listeners. A return-address-qualified detour at `SpawnLuxParticleSystemComponent` is the narrow candidate boundary, but it remains disconnected until the listener read graph and matching stop/update/destroy paths prove that no semantic consumer observes those native component identities. Terminal-wide suppression remains forbidden, and the dynamic Blueprint caller remains unrepresentable in the production schema.

`DispatchLuxStageBreakEvent @ 0x14053D130` passes listeners only the stable barrier/wall ID and source-frame location through listener vtable slot `+0x68`; it never passes the actor or particle component directly. The dispatch mutates recursion depth and may compact dead weak entries, so it remains source-frame semantic work. `LuxActor_CollectActors_By8Classes_IntoTArrays @ 0x140417A70` first appends matching `PersistentLevel.Actors`, then appends matching actors from every `World->Levels[].Actors` list without pointer or semantic-ID deduplication. The corrected 600-frame normal-render probe observed wall ID 4 at orders 3 and 4 with no repeated-reference relation, proving two distinct native actors can share the semantic ID. Within a native generation, list order is therefore the stable identity; repeated exact references, when present, are represented as a pointer-free earlier-order relation. The sole observed vtable callback was `InvokeLuxStageBreakWeakDelegate @ 0x14041D870`. It resolves a weak UObject stored at listener `+0x08`, then invokes a per-instance semantic function at `+0x10`; it returns only whether the weak target was valid. Every observed bound function was `HandleLuxBreakableWallStageEvent @ 0x140428EE0`. That handler maps wall IDs 1..4 to payload 5, constructs a type-3/class-0x19 positional record, and calls `LuxBattleManager_DispatchBattleEventByClass @ 0x140519480` with route 2. The dispatcher computes current listener-relative Pan/Distance and performs terminal active-voice allocation. Therefore this observed listener chain is ephemeral audio presentation: rollback must preserve the source-frame listener dispatch/compaction semantics, suppress the terminal audio work during resimulation, journal only the value payload and location, and commit it exactly once. Native audio-player references and voice IDs are forbidden from canonical snapshots. This classification does not admit wall collision or break state, whose independent writers remain open. `StageBreakListenerDiagnostics.*` provides the inactive, trace-only topology probe: on the game-thread fencepost it reads at most 64 live wall/barrier list entries and 32 callback entries per actor, validates the native inline/heap collection layout, and records actor kind/ID/order, repeated-reference order, listener vtable and `+0x68` callback RVAs, plus the verified wrapper's bound-function RVA. It emits no native pointers and cannot mutate SC6. Production admission still requires normal-render observations for each content case and static review of every observed callback target.

`DestroyCallbackEntryCollection @ 0x1403AE520` is the shared terminal teardown for these emitters. It selects the inline entry or heap array, walks exactly `nCount` 0x40-byte entries, invokes each enabled type-erased callback object's virtual destructor at `+0x48`, frees per-entry heap callback storage, and finally frees the optional collection heap array. It does not reset the enclosing collection for reuse. The barrier destructor reaches it for `barrierEventEmitter` at actor `+0x390`; wall owner teardown `0x140537DA0` reaches the same routine for `breakEventEmitter` at actor `+0x3B0`. Journal mappings and checkpoint identities must be invalidated before this teardown begins; callback storage is never restorable state.

## Hook table

### Native bulk hybrid correction closure (schema 14, 2026-08-24)

The local reconstruction adapter now combines the bounded same-generation
`HgCpuDirect` image and the three-slot MotionBank image with pointer-free typed
exceptions. `CharaAnimationState` covers each fighter's clip/controller,
MoveVM-owned runtime section identity, cue scheduler, and at most 64 trigger
nodes. Packed-data references are section-index normalized; scheduler/list/node
allocations remain generation-bound identities. An inactive outer clip
canonicalizes the lagging nested runtime pointer away, matching
`LuxMoveVM_ResetCharaAnimSlotController @ 0x1402F78E0`.

The first complete depth-11 correction exposed a coupled stage-wind/LFSR
divergence. `IwWind_UpdateRingInOscillation @ 0x140333550` proved that RingIn
life `+0x30`, frame step `+0x130`, repeat state `+0x148`, and shared LFSR order
are semantic. The complete pointer-free 0x40-byte
`FLuxBattleVMFreezeRecord @ 0x1448462D0` is therefore now typed checkpoint
state. Restoring it alone did not converge: the first replayed batch prepended a
new `life=179/tick=1` RingIn while the expected graph merely advanced the two
existing RingIns.

`AdvanceLuxBattleStageWindEmitter @ 0x140334960` identified the missing
authority. Each fixed-list `IwWindEmitter` owns `nActive`, burst count, base and
reload timers, spawn parameters, and jitter through `+0xA7`; an expired reload
timer consumes shared LFSR and allocates a 0x1E0 RingIn. Schema 14 captures and
canonically hashes the verified pointer-free `0xA8` prefix for at most 16
generation-bound emitters. List sentinel, node, emitter, and ref-control
pointers are validation identities only. The unverified `+0xA8..+0xAF` tail is
neither restored nor hashed. Topology replacement invalidates the image.

With those emitter timers restored, normal-render DLL SHA-256
`4A8C20B7B172838C2B9A077845A354B058B79201FAF6CFACFAD17104FA997CD3`
passed the live depth-11 owned correction from coordinate 172 through 183: 11
batches, 11 coordinates, exact final canonical hash, and no undo path. The
generated schema SHA-256 was
`38F6A2EACCE604B4ED9602BD2FF53BC6EE50929CA19B375BB8DF1C4C8626BAF6`.
This closes correctness only for the measured replay/workload. Performance is
not admitted: undo capture was 0.633 ms, restore 7.345 ms, resimulation 21.481
ms, verification 0.647 ms, and total 32.021 ms, above the 16.67 ms depth-11
gate. Production rollback remains disabled and the allowlist remains empty.

The first performance correction removed diagnostic-only full checkpoint
captures from every successful inter-batch boundary and deferred the expensive
decoded-image difference scan unless the final canonical comparison actually
fails. These diagnostics remain available on failure and do not change the
restore, resimulation, or final recapture contract. On source commit `722d603d`
with the same schema and replay, normal-render dirty-build DLL SHA-256
`4ED3A091C6F140353239AD7F68A31D0940E108C5773AC64C0D1A384B009424C5`
again converged exactly at depth 11 from coordinate 172 through 183. Undo
capture was 0.630 ms, restore 6.812 ms, resimulation 1.698 ms, verification
0.673 ms, and total 10.464 ms. The adapter's restore instrumentation reported
local 2.250 ms, typed 0.100 ms, wind 0.030 ms, UCRT 0.010 ms, and total 4.060
ms maxima for this run. This single workload now passes the 16.67 ms depth-11
latency limit; p99 admission still requires the complete three-matchup sample.
Production rollback remains disabled and the allowlist remains empty.

Schema 16 closes the first normal-render performance gate for this replay
workload. The batch-entry scheduler now supplies its actual last retained
coordinate to `PlanResimulationBase`; the prior hard-coded empty value forced a
467-KiB reconstruction image every outer batch. The corrected cadence retains
one image every 18 coordinates, reducing frame-359 batch-entry storage from
166.6 MiB to 9.33 MiB while preserving the 29-coordinate resimulation bound.
Both local serializers reuse their capture buffers and use bounded hardware
CRC32C when available (with a portable word-wise fallback); HgCpuDirect is
format version 3 and MotionBankTriples is version 4.

Current-executable control flow at
`LuxMoveVM_InitializeCharaAnimClipPlayer @ 0x14037C230` and
`LuxMoveVM_AdvanceCharaAnimClipPlayer @ 0x14037C2F0` also proves an active
pre-bootstrap boundary is valid: `dwActive` may be nonzero while
`dwBootstrapState` and owner `+0x2B270` are zero, because the first native
advance publishes `pClipData` before consuming the runtime. The typed adapter
preserves and restores that null runtime exactly. Later normal-render replay
capture at coordinate 600 also observed a valid external clip rebinding while
the owner runtime still referenced its independently consumed packed section.
The adapter therefore normalizes and restores both verified packed-section
identities independently instead of imposing equality that native control flow
does not guarantee. The directly relevant Ghidra comments were updated and
saved; the advance function has zero effective fixable completeness deductions.

On dirty source commit `7cce740a`, normal-render DLL SHA-256
`A62F1B818D620C4C51F7B84C847F3CDAD49EB623B03369662292A134A4673C4B`
and generated-schema SHA-256
`40AD313E3D6DFFBC8BC424479A1983095E51C8D4A3662A957978BD5FEDCA17CB`
passed exact corrections at configured depths 1, 6, and 11 in one 360-frame
session. Total correction times were 7.290, 7.052, and 7.918 ms respectively.
Steady checkpoint capture p99 was 0.460 ms and maximum was 0.455 ms; component
p99 values were HgCpu 0.160 ms, MotionBank 0.240 ms, typed 0.040 ms, and encode
0.140 ms. This satisfies the capture and one-frame depth-11 limits for the
measured workload only. The three required content matchups and p99 population
remain pending; production rollback remains disabled and the allowlist empty.

Codec reset paths now destroy/reconstruct the large value image in place rather
than materializing another approximately 48-KiB temporary. The direct suite
caught the prior stack-overflow boundary. The three deterministic CTest targets
pass with schema 14, including exact emitter restore, excluded-tail
preservation, pointer exclusion, partial-write undo, and canonical round trip.

| Target | Phase | Owner | Install prerequisite | Teardown ordering | Recursion/thread rule | Failure behavior |
|---|---|---|---|---|---|---|
| `0x1403FE520` entry/exit | Transaction fence | BattleManager generation | All state/callback graphs admitted | Remove first after admission stops and in-flight depth is zero | Bound simulation thread; reject recursion/migration | Run stock; rollback disabled |
| `0x1402DBC60` tail `0x1402DC34C` | Coordinate observation | Match generation | Outer transaction nesting proved | Remove before owner destruction | Same thread/nesting token | Observe only; mismatch invalidates |
| Pair boundary before manager `+0x1210` | Input injection/filter observation | Manager + MoveDispatch | Complete callback/filter writers | Remove before Input/MoveDispatch EndPlay | Same thread; no reentrant injection | Preserve stock input; fail closed |
| BeginPlay completion and earliest teardown | Resolver generation | World/manager/input | Exact partial-init and earliest invalidation targets | Retain until simulation/presentation hooks removed | Game thread; ignore after invalidation | Increment generation; disable admission |
| UCRT IAT `rand`/`srand` | Scoped gameplay broker | Thread-local broker | Relocated return allowlist/thread/runtime proof | Disable before simulation hook removal; always restore forwarding | Exception-safe nesting; forward other calls | Forward and reject route on uncertainty |
| Route-specific presentation terminal | Journal | Current presentation owner | Terminal/schema proved | Remove before owner teardown after journal discard | Per-thread recursion guard | Stock outside speculation; reject unknown speculative route |

## Unsupported routes and qualification prerequisites

The ten static candidate regions above are supported as inactive implementation components. No complete native replay/rollback route is qualified yet because every end-to-end case also traverses unresolved enclosing state. The plan refers to three candidate cases without naming them elsewhere; this report defines the qualification split explicitly:

| Candidate case | Statically proven scope | Static prerequisites | Runtime qualification |
|---|---|---|---|
| Offline same-round replay correction | Frame commit, input-cache semantics, ten candidate regions, paired HgCpuDirect restore primitive, workload-scoped fighter coverage, Lux RNG layouts, teardown boundaries | Repeat the bounded fighter/HgCpu coverage proof for each admitted content case; close dynamic hit/list generations, remaining input callbacks, camera classes, presentation terminals, and bounded stage; reject unsupported/cross-generation SubVMs | Normal-render convergence on three representative workloads, hashes, resume timing, repeated corrections, stable generation |
| Cross-round replay seek/restart | Round reset functions, replay decoder, manager round-init | Full replay start/stop/NewRound order and rebuild contract for every region/journal | Normal-render bidirectional cross-round seek, restart/exit/re-entry, multi-round coverage |
| Online two-peer rollback | Packet enqueue/drain ownership and channel-4 lifecycle | Everything above plus UCRT/thread/FP proof, send/receive order, handshake/version/hash contract, disconnect signal | A normal Steam SC6 host and Sandboxie-isolated Steam SC6 client using distinct Steam identities and production Steam P2P, real corrections, latency/loss/reorder, multi-round, disconnect recovery, and one-hour soaks |

Explicitly unsupported despite the ten candidate components and one workload-scoped fighter coverage proof: any ordinary ActiveBattle restore until the remaining enclosing adapters and transactional repairs are complete; CPU-direct/SubVM generation crossing; VMPump restore across Begin/End, rebinding, or state-4 replacement; pending hit/list nodes; unmeasured content; every unqualified stage including wind/barriers/destructibles; unresolved camera actions; opcode `0x50006` without a broker; replay across an unobserved generation; and any presentation route without a proven terminal-specific handler.

## Contradictions with historical documents

| Prior claim/source | Current binary evidence | Resolution |
|---|---|---|
| Broad listener/event-hub suppression considered viable in older rollback notes | VFX collection 0 at `0x140400540` reaches gameplay trace/character latch state before visual terminals | Reject blanket suppression; journal only downstream terminals. |
| Earlier secondary-VFX notes used converted size `0x50` | Current conversion proves source `0x44` and converted `FLuxEnableVFxParam` `0x4D` | Correct extent is `0x44 -> 0x4D`; `0x50` is rejected. |
| Native round save/restore implied complete RNG coverage | Separate LCG, LFSR, xorshift, wind, MT and UCRT streams exist; native save/restore covers only xorshift | Every stream and shared draw order must be handled independently. |
| Offsets/completeness could imply production readiness | Bounded types still contain pointers, owners, containers, unknown writers and generation changes | Completeness is documentation quality, never an admission gate. |
| This report's first pass said “No native production region passed all six gates” and called the MoveCommand arena unresolved | The current Ghidra program contains exact `FLuxMoveCommandPlayerRollbackOverlay_Partial` (`0x3038`), the nine-bank/17-identity full partition, typed ordinary writers/readers, and the exact slot exclusions | The first-pass conclusion was wrong. Both MoveCommand semantic images are production candidates; identities/diagnostics/tail remain excluded and generation-gated. |
| This report's first pass treated HgCpuDirect only as incomplete field evidence | Current writer `0x1403841E0` and reader `0x140384540` are a paired inverse with pointer relocation and exact `0x28018` native capacity/stride | Use it as a bounded same-generation local reconstruction primitive, never as the peer hash or the complete adapter state. |
| Historical rollback notes are either wholly trusted or wholly discarded | Current binary re-decompilation confirms the MoveCommand partition, slot-param extent, HgCpu pair, and named writer/reader types; old runtime artifacts refer to retired source and older ABI/schema | Reuse historical files only as a search index and corroborating runtime record. Current static evidence defines this contract; new implementation needs new exact-artifact qualification. |

## Unresolved blockers and exact next actions

Schema-7 runtime closure: the generic callback executor at `0x141D38300` is now
signature-validated and observed only for the current battle manager's `+0x1210`
input-filter collection. The runtime captures the two-player pair before and
after the native collection, retains the pre-filter pair as authoritative input,
and retains the post-filter pair only for deterministic verification. A
normal-render 600-frame probe observed all 600 coordinates through 597 batches
(maximum width/ordinal three, one repeat) without a missing observation or
post-filter/fencepost mismatch. No value-changing filter fired in this workload;
changed-value coverage remains an admission requirement.

| Blocker | Exact next action | Closure evidence |
|---|---|---|
| Input and post-frame callbacks | **Closed for the measured replay generation.** Static registration/removal surfaces are closed for `+0x1210`, `+0x8E0`, `+0xA30`, `+0xB80`, and `+0xF70`; the binding now stores and verifies the pointer-free owner-generation/class/RVA/order topology before every candidate capture and invalidates atomically on drift. Repeat the proof at replacement/re-entry boundaries and for each content candidate. | Commit `8c351ca0`; normal-render 600-frame probe with 21 landing and 35 batch-entry captures through frame 600; zero topology/identity failures; collection headers, raw UObject pointers, and callback storage remain excluded |
| MoveDispatch adapter | Same-phase typed capture/transactional restore is implemented in `MoveDispatchState.*`. Wire it into the enclosing fighter adapter only after that adapter can atomically invalidate checkpoints when phase or allocation identity changes. | No pointer/header restoration; exact recapture and exact undo are covered by the native-candidate self-test |
| HgCpu supplement and fighter verification | Closed for the measured replay workload. The inactive bounded `0x28018` stream shim recorded 2,567 exact source spans per frame for 600 normal-render frames with no source-span truncation. The complete 4-KiB heat map accounts for all 13,836,916 changes outside those spans: 7,644,825 KMot working-buffer bytes, 6,187,659 primary matrix-bank bytes, 33,127 small matrix-bank bytes, 4,428 typed BoneDataBank/skeleton derived-cache bytes, and four relocated-identity-page bytes. No opaque stream byte or native pointer enters the canonical image. Repeat this proof per content case and implement exact restore/repair ordering before admission. | Report SHA-256 `684F6FC9A1A5D26C8B9030B02DB6CA74AF51005C0F38147BD6C00FCA780675EB`; 600 samples, zero span truncation, every observed delta classified; workload scoped |
| Unsupported SubVM classes/generation crossing | Keep every vtable absent from the 77-entry list fail-closed. If cross-generation restore is required, prove a prepare-then-publish factory transaction with rollback on allocation/init failure. | No stale pointer; exact class/extent and semantic recapture |
| Candidate-region implementation | Keep all ten candidate snapshots behind disabled capabilities; retain mutation, unknown-class, identity-drift, invalid-count, excluded-byte preservation, undo, and recapture tests | Implemented in `NativeCandidateRegions.*`; code matches exact banks/exclusions and performs no write before complete preflight |
| UCRT/thread/FP | UCRT callsite, owner-thread observation, schema-v3 checkpoint capture/hash, exact local restore, and adapter undo are implemented. The exact seed call bound the simulation thread, 734 admitted draws advanced the private stream, unrelated calls forwarded, and the broker reported no failure. FP observation is also closed for the measured replay: 597 samples, zero control drift, zero x87-status drift, and MXCSR sticky-status changes that the inactive adapter now restores narrowly. Acquire ownership only at a frozen baseline; failure-inject UCRT/FP exits during actual SC6 restore/resimulation. | Commit `a76b2189`; DLL `FAF55902...`; normal-render owned convergence and exact runtime undo remain required |
| Stage/camera | Enumerate proposed stage classes/topology and live camera interface/action vtables | Exact content allowlist, bounded counts, semantic repair |
| Presentation | Particle creation is closed, and proves that terminal-wide suppression is unsafe because the Blueprint native-exec route returns a live UObject identity. Trace the matching stop/update/destroy/callback paths for the two static barrier/wall routes; prove the Blueprint route unreachable per allowlisted workload or keep it fail-closed. Continue the same caller/return-use audit for audio, material/fade, camera, UI, and cinematics. | Callsite-qualified value schemas, logical-to-current-generation component mappings, exactly-once commit, persistent reconciliation, and no speculative UObject identity visible to Blueprint |

### Native batch stage-break presentation boundary (2026-08-24)

Current-executable Ghidra evidence closes the static wall/barrier particle stop and
spawn suppression boundary without suppressing the generic particle factory.
`HandleStageBreakableWallBroken @ 0x14053D4B0` owns authoritative break state
`+0x468` and fade values `+0x46C/+0x470`; its six mesh identities
`+0x420..+0x448`, particle template `+0x458`, and live component `+0x460` are
presentation-only for this transaction. `HandleStageBreakableBarrierHit @
0x140549F40` owns authoritative hit count `+0x468`; component identities
`+0x400..+0x418`, material-list counts `+0x438/+0x448`, hit-template-list count
`+0x458`, break template `+0x460`, and live hit/break components
`+0x470/+0x478` are presentation-only at this boundary. The material-list data
pointers and capacities are not modified.

`DeterministicHookSet` now signature-gates both handlers and
`DispatchLuxStageBreakEvent @ 0x14053D130`. An owned native batch may request
ephemeral suppression. The matching handler then captures and nulls only the
verified presentation fields, executes the stock handler so authoritative state
and control flow remain native, restores every masked field before the exact
synchronous listener dispatch, re-masks after the listener returns, and finally
restores the original presentation fields exactly. Any mask/restore failure marks
the owned batch `PresentationFailed`; the enclosing owned-seek transaction then
restores its full undo checkpoint. Normal execution is pass-through. The dynamic
Blueprint particle route remains untouched and unsupported during owned
resimulation.

`StageBreakPresentationIdentityMap` supplies the separate generation-scoped,
bounded pointer-to-value bridge needed by the future journal. Owner identity is
derived from actor kind, semantic ID, and canonical ordered-list position;
repeated native references canonicalize to the first order. Asset identity is a
route-qualified logical slot. Native addresses remain local binding metadata and
never enter journal values or canonical hashes. Generation drift, allocation
replacement, invalid repeated-reference relations, route aliases, and topology
mismatch fail closed. Focused tests cover these cases.

A normal-render 600-frame replay-entry pass installed all three hooks and reached
the requested watch limit with clean teardown. Dirty-build DLL SHA-256 was
`8EE00D1FB59100BB7042415466256D802E4C152059EDF0FD5FD43E28D23A52C6`;
replay SHA-256 remained
`95E12E394D35C13D5E0DD3DCE692F9E0A4022E2A84205A9EC75F2FA6726D7879`.
This proves installation and ordinary pass-through only. It does not certify an
actual speculative wall/barrier event, confirmed presentation replay, or the
other audio/camera/UI presentation families, so the owned seek still has no
production caller.

### Owned-batch battle-audio terminal (2026-08-24)

Current-executable Ghidra evidence closes the battle-audio side effect beneath
semantic listeners. `LuxBattleManager_DispatchBattleEventByClass @ 0x140519480`
is called by contact, move, phase, subsystem, and stage-break listeners. It
selects a shared audio player, appends current `Pan` and `Distance` parameters,
applies transient cue metadata, and ends at
`LuxAudio_RegisterActiveVoiceInstanceFromSharedPtr @ 0x14054F6D0`. The terminal
allocates a UCRT-rand-derived live voice ID and publishes a native active-voice
entry. The dispatcher's existing missing-route return is `0xFFFFFFFF`; callers
already treat that value as “no voice” and do not insert a tracking identity.

The formerly unnamed cue-metadata helper is now documented as
`LUXBATTLE_ApplyCueMetadataByKey @ 0x1405112B0`. It resolves a CRI Atom cue entry,
case-insensitively matches configured `0x28`-byte control rows and `0x30`-byte
extended rows, and updates only the battle manager's transient audio-routing
collections at `+0x390` and `+0x3A0`. Its current Ghidra audit has no plate or
label issues and a 71.35% effective score; remaining deductions are dominated by
register/SSA temporaries and the deliberately partial manager type. The program
was saved through native MCP tools.

`DeterministicHookSet` now signature-gates the dispatcher at RVA `0x519480`.
Outside an owned resimulation batch it is exact pass-through. While
`suppress_ephemeral_presentation` is active it returns `-1` without invoking the
dispatcher. This preserves the enclosing gameplay listener/event work while
preventing parameter-command allocation, transient audio-cache mutation, UCRT
voice-ID draws, live voice publication, and native audio identities from
escaping speculation. This is suppression, not journal replay: committing a
value-only audio event exactly once remains required before presentation can be
qualified.

A bounded normal-render 600-frame replay-entry probe passed with the dispatcher
hook installed and clean runner teardown. Diagnostic DLL SHA-256 was
`70877850469D18AB0CF06D568FF69BF3DFFFC8C018CB766AA56D7D4F31ED2531`;
the embedded committed source was `6229f246`, and the replay SHA-256 remained
`95E12E394D35C13D5E0DD3DCE692F9E0A4022E2A84205A9EC75F2FA6726D7879`.
The source was dirty only for this implementation under test. This proves
signature admission, ordinary pass-through, and teardown; it does not yet prove
suppression under an actual owned correction.

### Battle-audio selection contract and direct-caller inventory (2026-08-25)

Current-executable Ghidra MCP analysis closes the stateful contact remap at
`LuxMove_RemapAttackType_WithCounter @ 0x1403BA080`. Its corrected prototype is
`int __fastcall (ALuxBattleSoundEventHandler_Partial *, int)`. For contact types
6 through 14 it reads the handler-owned signed `int nContactTypeAlternation` at
`+0x3E0`, includes the pre-increment value in the returned action type, and
advances the field modulo two. Other contact types use the fixed jump-table
mapping without touching the counter. The handler vtable-helper constructor at
`0x1403ABF10` initializes the field to zero; no independent scalar reset or
destruction writer exists. Handler lifetime replacement is therefore the
invalidation boundary. The older `nSelectedPriority` field name was incorrect:
`LuxMove_SelectHighestPriorityActionType @ 0x1403D32F0` writes the separate
`playerModes` array at `+0x3C0`.

All direct code callers of `LuxBattleManager_DispatchBattleEventByClass @
0x140519480` were re-inventoried. Their authored payload inputs are:

| Direct caller | Payload inputs | Classification and ownership |
|---|---|---|
| `LuxMove_OnBattlePhaseChanged @ 0x1403C43D0` | Phase callback value and fixed cue `0x14` | Canonical phase input plus immutable content; handler lifetime |
| `HandleContactSoundEventForBattleSound @ 0x1403C63C0` | Source event class/type/position; immutable `voiceCueIds`; contact remap; stage-material lookup; two `playerModes`; player hit-effect preset; character style ID and alternate-contact flag | Source event and player/setup fields are canonical or immutable content. `+0x3E0` is the sole persistent presentation selector and is captured separately. Active voice IDs and tracking sets are terminal-only. |
| `LuxMove_SendSubsystemCmd_Type50 @ 0x1403C5AE0` | Source command side, position, authored subtype, fixed command `0x32` | Canonical source command plus immutable constants |
| `LuxMove_ComputeWeightedBodyPositionCmd @ 0x1403C7F00` | Source side, current weighted body-position rows, resolved player position, authored body-position cue `row + 0x8C` | Canonical source command and derived body-position result; temporary sets are derived scratch |
| `LuxMove_DispatchCmdsFromByteArray_Type3 @ 0x1403C60B0` | Character byte array, immutable global remap tables, source command byte, player position | Immutable content plus canonical source command/player transform |
| `LuxMove_SendAnimCmd_Type2_ByParams @ 0x1403C7A30` | Source player side and animation subtype mapped to fixed values `0x20..0x2C` | Canonical source command plus immutable mapping |
| `LuxStage_RegisterBarrierActor_BattleEvent0x19 @ 0x140427490` | Callback payload and fixed event fields | Canonical stage callback payload; actor lifetime |
| `HandleLuxBreakableWallStageEvent @ 0x140428EE0` | Wall ID lookup value and source position | Canonical stage state plus immutable wall mapping; temporary hash is derived scratch |
| `FUN_1405509B0` | Source opcode mapped to fixed cues, or helper result for opcode `0x48` | Canonical source command plus immutable mapping/helper content |

The checkpoint exception is a fixed 21-byte local image: session generation,
round generation, the signed two-state selector, and handler-observation metadata.
The native handler pointer is retained only as same-process validation identity;
it never enters canonical hashes, peer messages, or portable state. A
signature-gated detour observes the exact handler argument without changing the
native call. Capture validates the handler vtable and the `0..1` invariant.
Restore writes the selector transactionally before native typed images and before
resimulated semantic listeners, verifies the write, and restores the exact undo
value on failure. A checkpoint captured before the first handler observation
represents the constructor value zero and can be restored once the handler is
known. Round/session invalidation resets both the binding and observed identity.

Ghidra structural work, variables, plate/PRE/EOL comments, and the corrected
handler structure were saved through native MCP tools. The final completeness
score for `0x1403BA080` is 92.0%; the remaining eight fixable points are
register-only structural temporaries. Runtime qualification of this contract is
still required; the ordered count/route/payload/position identity gate remains
unchanged.

### Terminal audio ownership and stage-wind callback banks (2026-08-30)

Paired normal-render playback of the authored Silver Wolves' Haven replay
isolated two previously opaque hash failures into their exact native fields.
All 2,617 terminal presentation events and 846 battle dispatches otherwise
matched. The audio difference was confined to the process-local character-cue
CRI table slot at terminal source return RVA `0x519A6D`; the wind
difference was confined to callback-bank residue while the live pending count
was zero.

Ghidra re-verification of
`ALuxBattleSoundEventHandler_HandleCharaCueEvent @ 0x1403C6B80` proves that
source event mode 1 selects cue family 7, mode 2 selects family 8, the default
selects family 6, and modes 3/4 select option families 10/11. The selected owner
comes from the current live player-entry array at the source tick before
`LuxAudio_ResolveAndPlayCharaCue @ 0x140519970` returns through `0x140519A6D`.
That return address is diagnostic terminal provenance, not an independently
stable owner identity. The implementation now hooks the semantic resolver and
carries its exact event `bMode`, authored family byte, selected live owner, and
array provenance only across the synchronous terminal call. A terminal owner
mismatch fails closed. The broader graph is refreshed only after replay
generation preparation and preserves an epoch only when generation, managers,
containers, counts, and exact pointer-selector bindings all match.
`LuxAudio_RegisterCueSheetObjectAndReturnSlot @ 0x1405510B0` proves that the
numeric cue-sheet argument at the terminal is allocated by live-object arrival:
reuse a pointer-equal entry, otherwise fill the first expired/null hole, otherwise
append. In the paired failure, all 47 differences were this slot alone: family 7
and family 8 exchanged slots 6/7 between processes while owner, playback, cue,
value, operation, order, and source were exact. The journal therefore stores the
exact authored family key and resolves it back to the current process-local slot
only at presentation commit. Reports retain that raw slot separately so a future
failure identifies both the canonical field and local allocation symptom.
Ordered payload id, owner, authored family, cue id, and source-return identity
remain an exact gate; no audio identity was weakened.

`LuxBattle_TickStageWindAndAccumulateForces @ 0x140333FD0` proves that
`IwWindRoot+0x98` is a symmetric double-buffer label. Only `+0x9C` callbacks
from the selected bank are live and read in ascending order; unused selected
slots and the opposite bank are stale residue. Portable canonical bytes now
retain schedule state, the effect-pair latch, pending count, and the exact
ordered active pending span while normalizing the bank label. The complete raw
two-bank image remains in the same-process restore image and diagnostics. A
self-test moves the same live callback between banks while mutating stale slots,
then separately proves that changing the active callback still changes canonical
identity.

Ghidra effective completeness is 100 for
`LuxAudio_ResolveAndPlayCharaCue`, 100 for
`LuxAudio_RegisterActiveVoiceInstance`, 100 for
`LuxAudio_RegisterCueSheetObjectAndReturnSlot`, and 92 for the wind tick with eight
remaining fixable points. The verified program was saved through native MCP
tools.

| Replay/scene/disconnect | Close replay stop/order and earliest world/online invalidation signals | No identity survives transition/partial failure |
| Hook teardown | Audit Horse shutdown only after target set is final | Admission gate, reverse removal, zero in-flight callbacks |

## Recommended adapter sequence

1. Implement fail-closed session/round/allocation generations and exact undo storage without placing raw addresses in snapshots or peer data.
2. Keep the ten implemented candidate rows behind disabled capabilities: MoveDispatch headers and all identity pointers are validation-only; scheduler residue/reserved bytes, SubVM gaps/tails, MoveCommand exclusions, VMPump tails, and slot padding remain untouched; every load recaptures.
3. Use the implemented bounded HgCpuDirect stream shim (`0x28018` maximum) as same-generation undo/reconstruction state. Add exact writer/reader cursor, supplement repair, recapture, and failure-injection tests without activating replay seek; repeat the complete coverage measurement for every content candidate.
4. Keep unsupported SubVM classes and all generation crossing fail-closed unless a separate prepare-then-publish factory transaction is proved; never cross a replacement by restoring the old pointer.
5. Keep the closed manager callback topology bound to each native generation; repeat it at round/replay replacement boundaries, then implement semantic input capture behind a disabled capability.
6. Add independent RNG adapters, the qualified UCRT broker, and exact FP preservation on every exit.
7. Qualify a narrow exact stage/camera/content allowlist and add route-specific value-only journals while semantic callbacks remain live.
8. Add lifecycle invalidation/reverse teardown and run all three cases on one immutable artifact. Only independent review may populate `Schema::production_regions`.

## Ghidra saves and completeness audit

| Phase | Saved | Functions audited | Remaining fixable deductions |
|---:|---|---|---|
| 0 | Yes, through MCP | `0x1403FE520`, `0x1402DBC60`, `0x1403D2A20`; constructor identity cross-check at `0x1403DC7F0` | `0x1403FE520`: 7.29 effective fixable points. `0x1402DBC60`: scorer reports 23.23, dominated by overlapping typed-parent interior aliases and one rejected SSA duplicate documented in its plate comment; no unsafe overlapping globals were created merely to improve score. `0x1403D2A20`: 8.87. |
| 1 | Yes, through MCP | FrameInput producer/cache/decoder/drain/pair/filter functions above | `0x140427940`: 3.07 effective fixable; `0x1403F6770`: 8.0. Remaining issues are graph closure. |
| 2 | Yes, through MCP | VMPump, scheduler, SubVM create/swap/init, main/secondary/hit/animation paths; MoveCommand ordinary driver/predicate/personality/reaction writers; paired HgCpuDirect writer/reader and globals; `LuxBattleChara_InitBoneDataBank @ 0x1403034E0`; `LuxBattleChara_InitBoneDataBankSlots @ 0x140303230` | MoveCommand driver is 100% effective; predicate refresh has 9.80 fixable points; slot-param advance has 8.0 fixable points. BoneDataBank initializer effective score is 96.62 with 3.38 fixable points; slot initializer is 93.55 with 6.45 fixable points. Personality/HgCpu scorers retain >10 nominal points from overlapping array-interior aliases, raw virtual slots already documented by EOL comments, and register/stack projections; no overlapping data items or false stable types were forced for score. |
| 3 | Yes, through MCP | RNG initialization/helpers/xrefs and full-program FP scan | Runtime/thread and admitted-order proof remain. |
| 4 | Yes, through MCP | Wind lifecycle, barriers, stage edge and camera roots | Dynamic topology and concrete camera classes remain. |
| 5 | Yes, through MCP | Audio create/stop tracking, Sound/VFX/ColorFade BeginPlay, secondary-VFX hub dispatch, particle spawn terminal | Mixed semantic/terminal routes remain explicitly unsupported. |
| 6 | Yes, through MCP | Manager/Input BeginPlay and EndPlay, Input destruction, round reset/init, replay entry surface, match and fighter-registry teardown | Replay stop plus earliest scene/disconnect signals require closure. |
| 7 | Yes, through MCP | Synthesis audit of all required implementation tables; corrective re-audit of MoveDispatch masks, VMPump, scheduler, SubVM factory allowlist, MoveCommand, slot params, and paired HgCpuDirect primitive | Ten static production candidates admitted to this handoff only; no schema/source activation. Remaining enclosing blockers are listed by exact target above. |
