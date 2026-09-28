# G1 PhysX pre-wrapper recovery audit — 2026-09-24

**NO-GO for a production/test change.** The dispatcher offers a real pre-native boundary for both world physics ticks, but current source does not own a recoverable suspended physics transaction there. Callback-writer/receiver lifetime, callback-state undo, and disposition of the outstanding application/task graph remain unproved. No production/test code, hooks, Ghidra database, build, deployment or game process was changed. No tests ran. This is Astra's read-only assessment; no other agent was used.

[Raw Ghidra responses](rollback-g1-physx-pre-wrapper-recovery-audit-2026-09-24.native.json) retain decompilation, disassembly, xrefs and PE headers. Disassembly of undefined entries used `dry_run=true`; no functions/types/comments were created. [Source hashes and archive receipt](rollback-g1-physx-pre-wrapper-recovery-audit-2026-09-24.sources.json) retains complete reviewed source and pre-checkpoint instructions/status, including untracked files, in the existing build's content-addressed evidence store. Every call explicitly targeted the existing `SoulcaliburVI.exe` program (image base `140000000`). No fresh on-disk or mapped-binary identity qualification is claimed.

## Corrected type2 caller and owner

`1449E9E4C` is **not a vtable**. The program PE headers place it in `.pdata`; its three DWORDs are `02027C10, 02027E40, 03F53D20`, an x64 runtime-function record (begin RVA, end RVA, unwind-info RVA). A data xref from that address does not establish a virtual caller.

The actual caller is the tick entry `142018550`: load world from tick `+0x50`, load scene from world `+0x1C8`, and at `14201855E` tail-branch to `142027C10` if non-null. Table `1439EFAA8`, slot `+8` at `1439EFAB0`, contains `142018550`.

| Native world tick | Embedded owner | Table / virtual +8 | Registration and ordering |
| --- | --- | --- | --- |
| Start type0 | `UWorld+0x788` | `1439EFA78 / 142018570` | `14202742F` assigns group1; `142027441` registers |
| End physics | `UWorld+0x7E0` | `1439EFA90 / 142018350` | `142027472` assigns group3; `14202749A` adds start-tick prerequisite |
| Start type2 | `UWorld+0x838` | `1439EFAA8 / 142018550` | `1420274ED` assigns group3; `1420274FF` registers; `142027515` adds end-physics prerequisite |

Constructor stores at `1421B473E`, `1421B4758`, `1421B4770` establish those embedded table bindings. `1420273DC/3E6/3F4` bind each tick's `+0x50` to world. Type2 registration is conditional, including the returned settings object's byte `+0x50` at `1420274E7`; this audit does not infer that it is enabled in the retained combat window.

Ordinary execution is task table `1439CD9C0`, virtual `+8 = 14215ED20`, payload body `14215D250`, then tick virtual `+8`. The payload updates tick interval bookkeeping before the tick callback and clears tick `+0x18` after it. The outer wrapper clears constructed state, dispatches/releases its completion event and recycles task storage. Therefore the proposed boundary must precede the **whole task wrapper**, and saved task pointers are unusable after wrapper return.

Type2 is a distinct task after end-physics. Holding type0 does not provide admission or lifetime protection at type2's later callback consumption. `142027E40` explicitly incorporates an outstanding type2 `scene+0x158` dependency; a new type0 entry also must not assume previous type2 work is absent.

## Publication and substep obligations

`142027E40` first calls `142019030`, which consumes scene `+0x390/+0x398` work and resets/reallocates its storage, then calls `1420297F0(scene,0,scene+0x130)` at `142027E93`. A hold at the inner start routine would already be past those wrapper side effects. Type2 calls the same routine with `(scene,2,scene+0x140)` at `142027C5A`.

In `1420297F0`, native state changes before callbacks: smoothed delta at `14202990D`; active byte `scene+0xFE+type` at `142029916`; completion-event allocation at `14202991E` and slot publication at `142029935`; deferred scene updates via `14202B650` at `142029971`. Then scene `+0x10` dispatch occurs at `14202999F`, and non-substep `+0x80` at `1420299C2`. Thus callback-entry rejection and site11 cannot undo an untouched wrapper.

The completion chain is consistent for both scene selectors:

1. `142028440` retains the selected `+0x130/+0x140` event in a 0x40-byte completion task, and schedules initial delegate `1420555E0` with context `scene+0x370+8*type`. Its task-manager reference drop is not completion; see the [retained PhysX/APEX resolution](rollback-g1-physx-apex-completion-and-route-scope-2026-09-24.md).
2. `1420555E0` installs outer task `context+0xB0`. `142055C40` publishes a new `+0xC8` event and schedules `142055BC0` behind it through `141E77F10`, before substep callback dispatch/simulation. `141E77F10` retains its prerequisite and returns the child event. APEX completion eventually makes `+0xC8` dispatchable; the repeat releases that reference, fetches results and repeats, or drops outer `+0xB0` on the final branch.
3. Completion callback `14202E450` dispatches its retained graph event through `140D20A70` and retires its task. Scene readiness is followed by final fetch/collection: the bound delegate at `142025780` passes the stored scene selector to `142024D10`. Type2 schedules this behind `+0x140` and publishes child `+0x158`; type0's start wrapper constructs dependencies and aggregate `+0x160`.
4. `142024D10` fetches through the APEX scene resolved by `142042A10` (the older plate comment calling this interface PxScene is not used as proof), collects outputs and releases the slot/active flags. `142017820` clears `+0x160` **before** publishing body transforms and notifications. End-physics `142018350` chains unfinished aggregate work into its tick completion, or calls the finish routine directly. Event submission, a null slot, or start-task return alone does not establish completed publication.

The retained APEX trace supplies the DLL-side task retention/fetch edges; this audit does not repeat its native experiment. No cancellation/abandonment protocol for this graph follows from those normal completion edges.

## Source contracts that prevent an extension now

| Boundary | Current evidence and consequence |
| --- | --- |
| Physics admission | `Sc6ReplayTaskGroup.cpp:484` checks only table `base+39EFA78`, returning terminal site10 on failure. `ReplayPhysicsStepConsumer.hpp:72` recognizes only that start tick. Type2 is outside this check. `Sc6ReplaySchedulerState.cpp:449` also excludes `39EFAA8` from its tick allowlist; adding a dispatch case alone cannot establish type2 checkpoint support. |
| Hold domain | `Sc6ReplayTaskGroup.hpp:59` requires group5/cleanup-group5. `NativeReplayTraceTaskGuard.hpp:299` requires the selected trace component, its embedded component tick, group5 and no prerequisites. Its domain check at `:357` relies on already completed group3/4 physics waits and clear scene events/active flags. Neither group1 type0 nor group3 type2 satisfies that proof. |
| Host/transaction lifetime | `Sc6ReplayHost.Consumer.inl:20` rejects the standalone hold probe when `historical_restore_` exists. `:162` arms the historical prerequisite guard only without `consumer_hold_`. `:87/:107` require the diagnostic hold owner to suspend/resume. There is no combined physics hold/B-undo owner. |
| Resume | `Sc6ReplayTaskGroup.cpp:115` validates borrowed task/world/event state and forwards the original wrapper exactly once. It neither cancels the wrapper nor removes its obligations. `completed()` occurs after task storage has been recycled; it is not a PhysX scene lifetime lease. |
| Recovery | `Sc6ReplayHost.Restore.inl:1240` finishes the native application; `:1247` requires completed application, idle engine/executor, empty arena and complete interval. CPU settlement at `:1464` repeats those requirements. Cancellation at `:2045` explicitly forbids publication-only undo racing native C. Holding an unstarted physics task prevents this quiescence; blindly resuming it would consume the changed callback contract. |
| Actual prior recovery | `NativeReplayPrerequisiteGuard.hpp::Admit` returns `ForwardObserved` for its proved edge-removal case and drains ordinary execution. `Sc6ReplayHost.Interior.inl:323` then holds a completed application. This differs from undo while an unstarted physics task remains held; A210/B217/120 does not prove that transition. |
| B contents | `Sc6ReplayWorldState.hpp::PhysicsBoundary` retains scene/body/query/notification projections, not owned copies/generations of FPhysScene `+0x10/+0x80` delegate collections and raw vehicle owners. `NativeReplayCallbackAdmission.hpp:19` covers latent/streamable/resource admission, not these collections. No participant restores an arbitrary changed physics delegate or a freed receiver. |

Writer exclusion remains independently missing. `1420113E0` changes recursion and walks live callable storage; `141F605A0` destroys/frees a matching callable before compaction. `1430FBFF0` unregisters both handles, removes the map entry, destroys and frees their raw owner. The [retained lifecycle audit](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6d76c6e75c35db1995f103a2a20729a9b1988179e528424120ea35fadc656f12.md) identifies registration via `1430F5C20 -> 1421377C0` and scene replacement `1421C5950`. No common exclusion protects admission through pre-step, all substeps, type2 and final fetch. The destructor's task wait can pump unrelated work. Recursion count and the selected observer's zero overlaps do not retain receivers or exclude writers.

## Smallest falsifiable next check

Select **one real changed physics callback**, not another unchanged A210/B217 run. Through forwarding-only diagnostics coordinated with the existing hook owners, identify the first registration/removal affecting that scene's `+0x10/+0x80`: writer entry/return stack, scene and raw receiver identity, thread/epoch, and enclosing task/tick. Correlate it with the already identified type0 and type2 pre-wrapper boundaries, the selected scene events/active flags, and dispatch return. Registration `1421377C0` and removal `141F605A0` must be checked for lifecycle-bypassing callers as well as the known vehicle route. Do not add a competing hook.

The first hypothesis to falsify is: **the changed writer belongs to an identifiable not-yet-started task before every affected physics consumer, with all receiver/scene lifetime changes contained by that owner**. A writer inside a started callback, an unowned/foreign task, reentrant replacement, or receiver retirement falsifies the simple pre-world-start hold extension. A positive witness narrows the owner only; it still needs a source/native proof of callback-preserving application completion and B recovery (or a proved reversible writer transaction) before any RED/GREEN implementation. No timeout, event fabrication, callback suppression or site11 recovery is permitted.

Keep B and its input/history/window validity through application/render/GPU completion. Existing completion, retirement and poisoned-operation guards were left intact. Observer validity: no new observation. Simulation/recovery: prior selected prerequisite proof only; changed PhysX recovery unproved. Coherence/performance: unqualified. G1/G2 remain open; no408 or600 gate advanced.
