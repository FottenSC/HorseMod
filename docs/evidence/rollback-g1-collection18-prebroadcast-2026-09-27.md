# G1 collection18 pre-broadcast boundary — 2026-09-27

**Decision: NO-GO for recovery admission.** The existing callback hook is early enough to observe this trace-start broadcast, but it does not own a stable receiver/state transaction. A bounded diagnostic value copy is a possible next implementation; it is not a lease, undo image, or resolving live profile. No production/test code, public API, state policy, build, suite, deployment, or live experiment changed. GPT-6 Astra performed this work without delegation. The completed kind2/kind3 correlation was reused.

## Retained evidence and current baseline

- [Audit index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a61d9dd8fb4af19adaea2c55bb9ef13c0b0be53a73eea1a542f39160162c1370.json): read-only runner output, full checkout inventory, source/binary checks, journal and six prior deployed/configuration states, process inventory, prior status bytes, and original live sidecar scope.
- [Native MCP transcript](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/d3defa14e21ef2afdc319116f8f9f9105dac35e42342161d3f15a86f9ac1113e.json), including assembly, decompilation, comment changes, save and post-save verification; [hub constructor](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/248153cf93609d0e98b77dbd845f0bfcf990f550a7985bdb77a6401f9f7c6b1e.json). All program-scoped calls explicitly selected the existing SoulcaliburVI.exe.
- [Current build/native receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ad66ef08f96a692afeb65a3f4fe77474823ee56a9f06e161021df4c31064a76b.json) and its reconstructible source snapshot `a96a0ae414a66b5b36f94e0595034c358b78e6a0d9bbd3e254eb39d3221dcc39` remain applicable. All 22 audited production/test files match retained payload bytes; the complete workspace fingerprint remains `f441246ddeb1f8f4cb696a0f61f6e5163438dab379b4b2a5b7fd78480ee3b971`. Runtime, observer, framework and both test executable hashes match that build.
- September 27's [858-case local selection](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/639efff35afd037abd74f81730728bad2f494affdfa70db595a4db516a3f92ee.json) is current but excludes the known G1 RED and does not qualify G3. Its full-suite failure was not rerun unchanged.

The journal is clean and all six files/absences match their recorded prior states. Independent process inspection found no game, build or test process; Steam PID 9240 remains running. No mapped candidate DLL identity can be claimed without a game. The [pre-audit September 27 status](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/376288e84c5b11c3e384040df948af82d0d385b6a5b32d84afff855e78bbc6af.md) is preserved byte-for-byte.

## Exact boundary and receiver selection

`140400A95` calls `141D38300` with RCX = dispatcher+`0x7E8`, RDX = stack descriptor; return PC is `140400A9A`. Constructor `1409128E0` places the dispatcher at hub UObject+`0x28`, so this is hub+`0x810`, collection18 in the array starting at hub+`0x30` with stride `0x70`. Character BeginPlay registers `1403C5360` there through `14043D210` at `1403B54E4`. This resolves the two different offset bases without assuming the dispatcher is itself a UObject.

[CallbackExecutorDetour](../../HorseMod/horselib/deterministic/DeterministicHookSet.FrameAndStage.inl) already owns `141D38300`; installation is in `DeterministicHookSet.LifecycleAndJournal.inl`. It currently observes/publishes only the input-filter collection and otherwise forwards the original executor. Its global in-flight count protects hook lifecycle, not collection backing, receiver state, or B. No competing detour is needed or permitted.

The exact listener is **world-context receiver -> descriptor-selected target**, not simply receiver -> its own trace component:

1. At `1403C5370`, RCX still holds the incoming listener character. `1403EF7A4` moves it to RDX for `GetWorldFromContextObject`; the per-world registry supplies the battle manager.
2. `1403C5386..1403C5391` selects battle-manager+`0x390`[descriptor.SourcePlayerIndex], bounded by +`0x398`, then that selected character's trace manager at +`0x458`.
3. `1408CE950` builds the request from the descriptor **and current trace assets/profile/palette/override fields**; `1408CD940` calls Start before duplicate-slot validation/insertion.

Thus listener index/serial alone does not bind the affected manager/component. A witness needs the hub/world identities, the receiver's world resolution, battle-manager generation, player-array selection, selected character, trace manager and component, plus source assets. Multiple native listeners can select the same target; their actual count/order must be observed and preserved, not deduplicated. The old disable-hub count of two is not a collection18 census.

The refreshed decompiler still prints `GetBattleManagerFromWorldContext()` without its argument. Assembly proves the receiver argument; the plate now records this limitation. The descriptor is transient start configuration. Copy its defined semantic fields, not uninitialized stack padding or its pointer. It contains no retained spline/history, B state or allocation ownership.

## Collection lifetime and recursive writers

| Native fact | Consequence for the proposed witness |
| --- | --- |
| Executor increments +`0x64`, snapshots count once, iterates descending indices and reloads backing at +`0x40` each iteration | An entry snapshot does not bind every later native invocation |
| `14043D210` calls compaction, then appends/grows even when compaction was deferred by positive recursion | Recursion depth zero at entry does not exclude a later recursive registration |
| Positive-recursion branch of `1403CA7A0` destroys callback storage, reallocates it to zero and clears entry+`0x30` | A copied heap pointer can become invalid before outer return even on one thread |
| `140399DF0` refuses compaction while recursion is positive | This protects compaction only; it is not a general writer lock |
| One-argument callable has weak index/serial, callable pointer and delegate handle | UObject generation and delegate identity do not establish collection/heap allocation generation or lifetime |

Current [CallbackTopologyProbe](../../HorseMod/horselib/deterministic/CallbackTopology.cpp) captures five battle-manager collections at `0x1210/0x8E0/0xA30/0xB80/0xF70`; it does not cover hub collection18. Its records also do not retain a delegate handle or collection-allocation generation. The existing bounded disable-hub observer explicitly says that guarded matching copies are not exclusion/ABA proof and marks writer coverage false. Its lock serializes observer metadata, not all native writers.

The [retained live sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7840e51724420a6ce1584d28b8e56f2c74057f7e7b7f16c1d5cdda6e07776910.json) contains a before-`140400590` snapshot at dispatcher+`0x78`, plus registration ancestry through `400A9A`. It has **no collection18 descriptor/receiver/state snapshot**. Its zero drops/pending/unstable/truncated records do not fill that missing coverage. A before/after byte comparison at the existing hook could detect some changes; it cannot reject before an intervening listener has already written state, prove ABA absence, or supply recovery.

## Complete affected B participants and completion

Existing trace recovery is substantial and must be preserved. [Sc6ReplayTraceState](../../HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp) retains roots, strong controllers, spline/history arrays, trace timing including +`0xD8`, component flags, attachments, actor/mesh/animation state, private weak collections and manager-map backing. [Storage undo](../../HorseMod/horselib/deterministic/Sc6ReplayTraceStorage.inl) checks B bindings, private ownership and settlement before restoring. It is incorrect to claim that none of the trace callback state has B coverage.

The additional closure required for this broadcast is concrete:

| Effect in the verified Start chain | Required participant/remaining gap |
| --- | --- |
| Start resets component fields, collects or creates states, initializes geometry, changes lifetime/fade/origin and weak membership | Bind the actual descriptor-selected root and every existing/new controller; preserve existing capture and fresh-child admission restrictions |
| `1408D5840` walks the selected trace actor's mesh-provider material slots and calls vector setter `141F45940 -> 141F1E420` | Bind every affected MID and its parameter backing/resources; the native vector setter can append/grow 40-byte rows at MID+`0xB8`, write color and publish/invalidate material resources |
| Final `1408D5670` copies **both** component+`0x418` and +`0x428` strong-reference arrays, then calls `1408D55B0` for every live mesh actor | The scalar reset affects the union of retained base/child trace actors, not only the newly selected trace |
| Tick registration, then `1408CDBB0` | Scheduler state; VFX manager+`0x388` listener backing; primary slot state; trace+`0xD8`; attachment deactivation; weak membership; deferred VFX work; actor/mesh state and final +`0x438` latch |
| Manager active-slot insertion follows Start | Include the trace-manager map and validation dependencies, even when the slot already existed and Start still ran |

No complete trace-MID value/backing/publication participant was found in the audited trace path. World material scalars belong to a separately bound scalar-only collection; particle material recipes cover particle MID lists. Neither establishes trace-mesh MID coverage. A native annotation calling the scalar leaf presentation-only is not an implemented reversible reconciliation contract and does not cover the vector write, aliasing, resource retirement or complete B. No comparison policy was relaxed.

Manager-task ownership remains independently open. `Sc6ReplayTaskGroup` retains `pending_task_` across kind2/kind3 execution and finishes/recycles it only in `FinishManagerTask`; `ReplayVfxExecutionScope` is explicitly an execution witness. The prior task correlation is valid within its recorded scope, but has no original allocation-generation/handoff receipt. Returning from this nested broadcast neither completes that task nor yields a new host-owned paused boundary. Keeping the stack descriptor alive would not change that.

`SettleHistoricalCpuExecution` still requires idle engine/executor, an empty arena, idle application, completed interval and settled rendering. The existing consumer-hold probe selects a different pre-task boundary and rejects an active historical restore. There is no demonstrated transition from a suspended nested broadcast to complete-B recovery. Native return, render submission and elapsed time cannot replace application/render/GPU completion. All existing undo and deferred-retirement guards remain intact.

## Smallest falsifiable next step

The next proposed implementation is **one bounded, observation-only occurrence at the existing CallbackExecutorDetour**, not a new recovery veto or another kind2/kind3 correlation campaign. Hypothesis: the first collection18 broadcast enclosing the selected registration has a fully readable finite listener graph and descriptor-selected target, and its entire affected trace/material set fits existing bounded ownership representations.

Before coding that observer, use the current production hook body as the regression boundary. A controlled native callee must remain responsible for forwarding/recursion, not a fixture-side guard. Required cases are unrelated/input-filter forwarding, two listeners selecting one target, receiver/world/selected-target generation mismatch, same-address row replacement, recursive append/removal, unknown layout, unreadable/over-capacity rows and an affected material outside the selected new trace. Observation must fail closed **as evidence** while native callbacks remain unsuppressed; all ownership/undo/completion claims stay false. Copy only validated values at entry; return-side bookkeeping must not dereference borrowed entry/receiver pointers. Integrate into the existing bounded observer/runner and charge its fixed capacity.

If this produces a justified diagnostic candidate, require its affected `local --test ... --layer native-contract` RED/GREEN and build/native checks first. Any future shortest native observation must have explicit diagnostic admission and exact identities/setup; today's full-suite G1 blocker and undefined live profile remain unchanged. Capture only the first relevant occurrence with descriptor fields, each listener generation/handle/order, selected target/root, trace-controller memberships and both-array material identities. Unknown targets, overflow, reentry, mutation or missing material coverage falsify the hypothesis at that first occurrence; do not rerun unchanged. This observation can resolve the concrete coverage question, **not** writer exclusion or recovery by itself.

A recovery implementation is still contingent on an enforceable writer/lifetime contract, complete affected B and a legal manager-task-to-settlement transition. Only then is a real changed-state cancellation with complete B and **120 independent continuation ticks** meaningful. The current known RED at `test_consumer_host.py::test_vfx_finish_dispatch_revalidates_live_completion_rows` remains required; simply adding a dispatcher rejection/fast-fail would not supply that recovery contract.

## Checkpoint results

| Dimension | Result |
| --- | --- |
| Observer validity | Existing retained witness remains bounded but has no collection18 state snapshot or ownership proof; native/source audit adds no live observer qualification |
| Simulation | No new execution; historical scoped 120-tick comparison remains within the incompatible failed paired stage |
| Recovery | No changed-consumer complete-B/application/render/GPU recovery; site11 termination remains containment only |
| Coherence | No new normal-renderer qualification |
| Performance | Historical 55.202 TPS failure unchanged; no new runtime overhead or rollback-update measurement |
| Ownership/memory | No new runtime allocation; 1 GiB production ceiling and existing guards unchanged; material/task ownership remains open |
| Gates/cleanup | G1/G2 open, full suite known G1 RED, debris exact/Unresolved; six prior deployment states verified, no game, Steam9240 retained |

Native edits were limited to two stale executor EOL comments that incorrectly claimed mutation exclusion, and the listener plate describing world-context versus selected-target identity. They were read back, saved through MCP and verified after save. No prototype/type/name changes, database scripts, imports, snapshots or new hooks were used. No regression was added because no production/test change was justified; existing current checks were reused rather than relabeled as new tests.
