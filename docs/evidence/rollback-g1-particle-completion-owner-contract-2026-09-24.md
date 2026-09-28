# G1 positive particle completion: owner and complete-B contract audit

Date: 2026-09-24. Work performed by GPT-6 Astra in the existing E:/myMods checkout. No production or test code changed; no project build, local regression run, deployment, or live run. Existing dirty work and build were preserved. Parent owns the build/native checks, deployment/recovery journal, live testing, cleanup, and status update.

Decision: pursue the **particle completion route without color fade**. The retained positive route has a concrete native task and manager listener. Its complete-B admission contract remains open. This audit closes two narrower questions: the exact producer of all 16 observed manager registrations, and the immediate material side effects that prevent a lane-only color-fade participant. It does not claim changed-consumer recovery.

## Retained evidence and identities

- [Audit index and exact selected observations](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/88fffbceb0df8079d310f45d746fd935bf21b967312ebc5a9555397ae17544dc.json): SHA-256 matches filename; 68,901 bytes. Includes retained-run identities, source archive identity, native raw reference, selected pair/producer, all registration/prune entries, and working-tree status.
- [Native MCP raw transcript](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/d9ff1bd427036203594e7158608ec45bc643cd84fa7f79673b993f57fe0e9214.json): 819,947 bytes; includes source decompilation/disassembly, partial type inspection, annotation mutations, save receipt and post-save verification. Every program-scoped operation selected the existing SoulcaliburVI.exe. No second import or database-edit script.
- [Original positive candidate report](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/8e4653618b1b4bf54e77a50f4e9f9165d1a7b79a676d4c4c46226337e2373085.json) and [raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a113b43343a7e3c2aa5658440cc98250eb5af8f93bd999dc910bfab9204e3b2e.json), run replay-bda836b3a520426fbcb3101085464fcb, PID 3888.
- Current reviewed workspace fingerprint: 9c4e4361aa5917ffeae19e3315a541e47689f4224f453ef00647539f700dcd57. It matches the previously retained [source archive](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-a205582866f1c91465a73147757951791769d2eddf5d270ebe28ec1f60aa03f1.zip), archive SHA-256 02574518c1b25eb3dcc775b53017f4ed81e8c18c12136488b9d4101bd59fbe50. Verified archive hash, manifest, membership and every payload. This preserves dirty/untracked source bytes; no redundant archive was created.

The original candidate's runtime/framework/observer/source identities differ from later Niagara work. Its exact game/runtime/framework/observer/replay/config identities and original build provenance are in the audit index. They identify historical observations; they are not a receipt for currently mapped DLLs. Parent must establish current identities before any deployment.

## Smallest positive retained route

Offline assertions verified 45 manager dispatch entries: 39 return callers at RVA 89F91D, under particle producer RVA 1F73990; six at RVA 8A1CB7, under primary-disable producer RVA 3C5250. The sidecar labels caller_route as other, so classifications here use exact caller RVAs and native code, not those labels.

Select dispatch **pair 14**, producer **746**, as the smallest concrete starting point:

| Item | Recorded value |
|---|---|
| Particle | UObject index 279166, serial 8859, address 1830612005824; table RVA 335DB28 |
| Producer | RVA 1F73990; observed application epoch 2003; async_fence=0, update_active=1; returned |
| Task scope | kind 1, scope 94056; task=context=scheduled_task=1830549954640 |
| Function | 1830612006096 = particle+0x110; table RVA 3865F98 |
| Completion/world | 1829015878848 / 1832250067808 |
| Threads | native 2; OS 47428 |
| Manager | index 310119, serial 8362; collection manager+0x388 |
| Listener | one row [311114,8104,17559]; receiver table RVA 3360CA8 |
| Collection | count 1, capacity 4; same rows/header on dispatch entry and return |

The callback payload is recorded as a pointer, not its slot scalar. There is no before/after trace-state image proving that this callback matched a live trace slot, changed +0xD8, or deactivated an attachment. Epoch 2003 is not a proven simulation-tick coordinate or seven-tick correction window. The 39 particle records refer to distinct particle owners. These limitations preclude promoting pair 14 into a changed-consumer recovery test without further evidence.

## Native route and existing machinery

The existing mechanisms were inspected before considering a new participant or task layer.

- [Sc6ReplayTaskGroup.cpp](../../HorseMod/horselib/deterministic/Sc6ReplayTaskGroup.cpp), DispatchNativeTask around lines 447-558, already calls consumer acquire before the task payload and supports Forward/Hold/Reject/ForwardObserved. Ordinary native tasks forward their entire wrapper; custom manager tasks use their existing pending-task protocol. The latter rejects ForwardObserved. Hold requires a separately proven ownership contract.
- [NativeReplayPrerequisiteGuard.hpp](../../HorseMod/horselib/deterministic/NativeReplayPrerequisiteGuard.hpp), Admit around line 306, covers selected trace roots and mesh prerequisites. An unmatched task forwards. A kind-1 scope, matching task pointer, or GC reference does not extend that contract to particle completion or arbitrary registration writers.
- [Sc6ReplayHost.cpp](../../HorseMod/horselib/deterministic/Sc6ReplayHost.cpp), ValidateParticleCompletionOwnership around line 491, validates indexed UObject serials, audited trace receivers, both FName words, native/empty-script dispatch, exact manager thunk/final vslot. [Sc6ReplayVfxState.cpp](../../HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp), ValidateCompletionOwnership and RetainOwners, provide captured membership restrictions and GC retention. They do not continuously exclude other writers or prevent explicit logical destruction.
- [Sc6ReplayTraceState.hpp](../../HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp) already captures trace state +0xCC..+0xEF including +0xD8, weak collections +0x3E0/+0x3F0/+0x400, private manager map storage, and relevant attachment transform/active/registration bits. Attachment admission checks exact table and Deactivate/ShouldActivate/SetComponentTickEnabled targets, empty lifecycle delegates and immutable bindings. A claim that these callback fields have no B representation would be wrong.
- [Sc6ReplayTraceStorage.inl](../../HorseMod/horselib/deterministic/Sc6ReplayTraceStorage.inl), Undo around line 455, still validates bindings before restoration. VFX PreparedManager::SettleExecution/Undo retain purpose, settled-execution, B fingerprint, binding and payload checks. None were weakened.
- [Sc6ReplayHost.Restore.inl](../../HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl), SettleHistoricalCpuExecution around line 1464, requires engine/executor idle, empty task arena, idle application, completed interval and settled rendering before participant settlement. A held unfinished task cannot be relabeled settled.

Native execution establishes why admission must precede the whole task:

1. 14215D250 updates tick state before invoking the tick function. The outer 14215ED20 wrapper finishes completion processing, releases its event and recycles the task. Its pointer cannot become a post-return lease.
2. 141F73990 releases asynchronous bookkeeping, evaluates completion, and can destroy/null emitter instances before calling 141F6FF20. The latter broadcasts component+0x970, finalizes emitters, clears active state, disables ticking, and may enter auto-destroy or cleanup paths.
3. 14089F870 finds the first matching manager primary slot (manager+0x3E8, stride 0xC0), broadcasts its ID through manager+0x388, then removes matching owner slots.
4. 1408D3F20 copies and temporarily promotes fading weak refs. Matching trace state +0xD8 becomes -1; attachment virtual +0x238 may deactivate; exact weak membership is removed; temporary references are balanced.
5. Native attachment 141D41870 calls ShouldActivate and SetComponentTickEnabled, clears the active flag and broadcasts deactivation delegates. Empty delegates must remain an enforced condition for the admitted subset.

A zero particle fence at producer entry is not proof of later GPU completion. Native return is not application/render/GPU settlement. The selected particle's auto-destroy, emitter, attachment and callback conditions still need to be bound to its captured state and enforced throughout corrected execution.

## Positive writer resolved from existing evidence

All **16** registration entry callers are RVA **8CDC8C**, the return address after CALL 1408C9120 at **1408CDC87**, inside **BindLuxTraceVfxFinishedAndResetFadingTraces (1408CDBB0)**. All 16 nested prune callers are RVA **8C9232**. Their producer_id=0 records lack an enclosing observed producer; they do not imply the native producer is unknown.

The binder's sole direct caller is **StartLuxTraceComponentTrace (1408D8C40)**. Before invoking the binder, Start has already changed fields, configured/created trace state, affected materials and registered component ticking. The binder additionally stops fading manager records, changes trace/attachment state and weak membership, can queue deferred VFX registration, changes actor/mesh state, and clears component+0x438.

Start has callers **ActivateLuxTraceManagerRequest (1408CD940)** and **BeginTrace (1408D5FF0)**. Activate invokes Start before its duplicate-active-slot test and manager-map insertion; BeginTrace prepares topology/owners before optional Start. Admission at AddUnique, prune, or the binder is therefore too late for the outer transaction. The actual enclosing execution owner of these selected registrations remains unproved.

## Color-fade participant: exact no-go

Native partial layout is useful but insufficient for a safe new B participant:

| Region | Native-proven fields |
|---|---|
| Manager | +0x388 settings UObject; +0x390/+0x398/+0x39C player array; +0x3A0 texture; +0x3A8 byte mask |
| Player, stride 0x28 | lane array at +0; queued array at +0x10; reset dword at +0x20 |
| Lane, stride 0x20 | entry array at +0; current index +0x10; value +0x14; rate +0x18; mode +0x1C |
| Entry, stride 0x18 | compaction/removal and byte predicates +0xC/+0xD observed; remaining interpretation partial |
| Queued entry | existing partial stride 0x34; no proven allocation generation |

Disable 1404704F0 can compact/shrink entries through 1419C0A00 and FMemory_Realloc. Entry indices are not generations. The existing entry annotation describing a +0x10 qword token conflicts with updater float accesses at +0x10/+0x14; it cannot be used as a generation contract.

More decisively, **ID=-1 resets materials synchronously**:
1404704F0 -> 1404507B0 -> call at 1404508EF to 14047A1D0 -> 1404790B0/140479530. The latter resolves player/attachment/weapon material receivers, writes scalar, texture and vector parameters through 141F45660/141F457B0/141F45940, and commits through 141F32CE0 at 1404797E1. 14047A1D0 also updates the manager mask. These effects occur before the disable callback returns, in addition to later manager tick/material updates.

A whole owned-array image could avoid reliance on per-entry generations, but still needs exclusive native allocation ownership, receiver identities/generations, complete material parameter restoration and settled render/GPU work. Manager UObject retention alone supplies none of that. Existing host participants do not include a complete color-fade/material participant. This is the specific reason to defer fade and continue with particle completion.

## Safe changes and limitations

Applied the function/type skills. Renamed 1404507B0 to ResetBattleColorFadePlayerState, validated/applied its manager-pointer and signed-player-index prototype, then wrote the multiline plate comment. Corrected 1408CDBB0's plate to place it near the end of trace start, distinguish stored weak/FName identity from the unused stack descriptor, and record the observed producer and admission limits. Saved SoulcaliburVI.exe and re-read both names/prototypes/plates successfully. No shared struct definition was changed.

Register-only decompiler locals were not exposed as database variables; no forced local types were fabricated. The reset function's completeness result was raw 73.484, effective 90.418, fixable 9.581; partial types remain. This is a documented conservative annotation, not full layout recovery.

One possible source issue was investigated but not promoted to a defect: native Start clears component+0x450 while capture currently treats the surrounding +0x44C range as a binding. The observed constructor also clears it, and this audit did not establish a nonzero reachable producer/consumer contract. Inventing a nonzero fixture would not demonstrate a production bug. No RED/GREEN patch was justified; offline retained-data assertions are not production regressions.

## Next falsifiable experiment and parent handoff

The next experiment is bounded to the selected **particle/trace-start owner contract**, reusing the existing task/actor owner and observer machinery:

1. Resolve the enclosing native owner/task of the two Start caller families, and its first side effect, before coding admission. Check current hook ownership first. At the existing registration hook, retain the enclosing task/actor identity and the relevant outer-call ancestry for the known RVA 8CDC8C writer. Associate it with the pre-payload task observation already available in Sc6ReplayTaskGroup. Test the hypothesis that every selected manager+0x388 mutation lies inside a serial, identifiable task whose admission point precedes all trace-start effects. An absent/different owner, unmatched generation, reentrant writer, or earlier unowned effect falsifies it.
2. For the first selected particle dispatch with an owned listener, establish its simulation tick, manager slot payload, matching live trace +0xD8, weak membership, attachment identity/delegates, particle auto-destroy/emitter conditions, and relevant scheduled task state before effects. A stable listener row alone does not prove callback mutation. Pair 14 is a concrete lookup target, not an assumed changed callback.
3. Stop at the first unsupported owner/effect. Preserve the raw prefix and identify that producer; do not rerun an unchanged experiment. Narrow instrumentation must answer these contract questions and remain within the existing runner. No new observer-only endpoint is proposed as completion of G1.
4. Only after the entire selected subset can be admitted and settled: add the production-boundary failing-before RED using the actual owner/admission/B path, make the narrow fix, and run GREEN with existing local groups. Exercise a real changed callback state followed by cancellation and complete B recovery. Check at least task/application completion, trace/manager state and ownership, render/GPU/deferred retirement, and independent continuation; keep complete B until every required settlement and recovery check succeeds.

Parent then owns python tools/replay_test.py build, native checks, exact mapped binary/replay identities, the shortest justified same-setup live window, journal/recovery cleanup and status. No build or live command is justified by this audit alone until the missing contract is implemented. No unchanged Niagara A210/B217 retry, no 408 retry, no 600-cycle run. No site11 termination counted as recovery, required callback suppression, PendingKill revival, fake task lease or imported expected state.

## Gate accounting

| Dimension | Result of this work |
|---|---|
| Observer validity | Original 154 events, 45 producers; no drops, pending pairs, unstable or truncated records. Sidecar explicitly lacks synchronized snapshot, writer coverage and execution-owner lease proof. Exact writer callers and selected owner relationships verified offline. |
| Simulation | No new simulation execution or determinism claim. |
| Recovery | Existing B machinery inspected; positive changed-particle complete-B recovery remains unproved. Fade deferred for the precise synchronous material/ownership gap above. |
| Coherence | No new normal-rendering or actor/effect/HUD evidence. |
| Performance/memory | No new runtime measurements. Existing retained sidecar owned 3 MiB; this is not total production ownership accounting. |
| Status | G1 remains open. No status-file modification by this worker. |

