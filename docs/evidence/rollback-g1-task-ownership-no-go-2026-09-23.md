# G1 task ownership gate: NO-GO

The requested implementation stopped at the prerequisite lifetime gate. Production suspension and recovery are **not implemented or proven**. The new blocking regression remains RED; no xfail, skip, or relaxed assertion hides it. No deployment or live qualification is authorized by this result.

## Evidence and scope

- [Native task bodies and entry signatures](rollback-g1-task-dispatch-native-2026-09-23.json).
- [Native lifetime/destruction investigation](rollback-g1-task-lifetime-native-2026-09-23.json).
- [Earlier Niagara/physics consumer investigation](rollback-g1-consumer-boundaries-subagent-2026-09-23.md).
- [Immutable checkpoint index](rollback-g1-task-ownership-checkpoint-2026-09-23.json) retains raw logs and reconstructible source inputs.

Addresses refer only to the inspected SoulcaliburVI.exe image at 140000000; they are not portable owner identities. Findings are bounded native-code evidence, not an assertion that the demonstrated qualification match exercised all these routes.

## Ownership map

| Reference/resource | Acquisition or publication | Transfer/use | Release | Hold implication |
| --- | --- | --- | --- | --- |
| Graph task storage | 14215CA70 builder; 142156650 constructor | 1421679C0 prerequisite counter and graph dispatch; named queue 140D2B9F0 successfully removes one node by CAS | 14215ED20 after execution returns storage to TLS two-batch pool | Unique node removal is not a global anti-redispatch or payload lifetime proof |
| Completion event task+40 | Builder reference transferred by 142156650 | 142163BA0 retains prerequisite and sequencer event references; 1421679C0 registers dependencies | 14215ED20 dispatches completion via 140D20A70, then decrements event+48; last release 140D2EBE0 | Event retention does not retain task storage after native return or the component |
| Tick task+10 | Raw pointer copied by 142163BA0 | 14215D250 reads tick and prerequisites, updates interval timestamp, invokes tick virtual+8, clears tick+18 | Embedded tick destructor 142157BC0 can replace vtable and free prerequisites without waiting on tick+18 | No independent physical lifetime pin identified |
| Component tick+50 | Raw owning-component binding | 141D43230 dereferences it before 141D2CAC0 flag checks and component virtual+300 | 141D41970 logical destruction; 141D34350 -> 1403AF1B0 destroys embedded tick and may free component | Indexed validation must precede raw tick dereference; GC reachability alone cannot prevent logical destruction |
| Tick prerequisite backing +20/+28 | Native scheduler-managed array, weak object/tick entries | 142163BA0 validates/removes/queues prerequisites and retains their events | 142157BC0 frees backing; unregistration 141D58AC0 -> 142168580 -> 142164B00 removes scheduling membership | Hold needs lifetime/synchronization for backing, not just main owner |
| World task+28 | Raw scheduling-context copy by 142163BA0 | 14215D250 reads world clock +930/+934 for interval ticks | World teardown ownership not proven in this task audit | Missing hold lease; no blanket lifetime claim |
| Named pump depth/return flag | Production PumpDepth increments +3E0 and installs owned +3E4 | PumpOne dispatches at most one task | RAII restores outer flag and decrements depth | Existing manager continuation works; a new consumer hold also needs world/host propagation |

142167AD0 recursively traverses prerequisites; its inspected body does **not** execute them. Actual prerequisite scheduling occurs earlier through 142163BA0/1421679C0. Scheduling flags can select worker threads; the named-thread pump cannot enforce every eligible task route.

The first payload side effect is earlier than the component callback: 14215D250 may write tick+3C before tick virtual+8. Therefore the potential interception point is before forwarding task virtual+8 in DispatchTask, after dequeue. It is not yet a proven **suspendable** location because payload lifetime remains unowned.

## Concrete missing mechanism

A verified lease must protect physical retirement of the component, its embedded tick, prerequisite backing and world across the entire hold, synchronized with explicit destruction, GC and concurrent writers. Logical death, required callbacks and native ordering must still occur correctly. An event reference, indexed generation witness, or current Sc6ReplayObjectLease (GC reachability only, acquired/released at completed application boundaries) does not supply that mechanism.

DestroyActorComponent141D41970 performs lifecycle calls, tick unregistration, owner-list removal and PendingKill marking without a pending-task/event wait. Tick destruction likewise does not wait. Merely resuming a task after this sequence risks a stale tick pointer; retaining a logically dead component as active would violate the rollback contract. This is the exact blocker to enabling a retained-task record, not a reason to fake completion or use FinishManagerTask for component tasks.

## Regression result

`test_component_dispatch_revalidates_admitted_consumer` extracts the actual production PumpDepth, PumpOne and DispatchTask plus scheduler CaptureTick/ValidateBindings. It admits a generic component, verifies one unchanged dispatch, changes its component dispatch slot to Niagara, queues another task, and reaches the external native-entry probe: child exit68.

Controlled services are queue removal and native entry; the existing scheduler fixture controls indexed identity/storage. It does not simulate successful cancellation, event retirement, GC ownership, native callback results or B restoration. This is a production-dispatch RED after **scheduler admission**, not the full requested application-entry/native ownership proof. Full host-entry admission and a shipped-native task/event lifetime fixture remain required. The positive probe does not certify native lifecycle ordering.

## Route dispositions

| Route | First relevant work / boundary | Current disposition |
| --- | --- | --- |
| Generic component task | 14215D250 timestamp/prerequisite reads before component entry | RED: task forwarding lacks revalidation; hold lifetime not proven |
| Niagara initialization outside tick | 141BC65E0 allocates/publishes system and invokes callbacks | Uncaptured birth; exact C draining not permitted |
| Niagara reset/rebind | 141BCC8E0, 141BCE060 and 141BC4D90 mutate system/interface state before reads | No mid-application writer/consumer closure |
| Niagara source refresh / repeated transform reads | 141BC7F90 clears bindings; 141BC7760 refreshes transforms | Bool returns remain native validity/rebuild results, not cancellation |
| Physics start | 1420297F0 sets scene in-flight flag and event ownership before callbacks | Entry-only admission insufficient; no arbitrary return/abort |
| Physics substeps/listener mutation | 142055C40 queues dependent task before delegates; 1420113E0 callback dispatch | Per-substep ownership and all writer participation unproven |
| Collision/filter/query/constraint mutations | Remaining producer/consumer writer map required | Explicit unresolved route, no supported-cycle skip allowed |

No abandonment protocol has been established. Exact-branch draining cannot justify an uncaptured Niagara birth. Safe session termination with outstanding native/GPU work has not been tested here and is **not claimed as containment success**. Suspension: UNPROVEN. Termination: UNPROVEN. Complete-B recovery: UNPROVEN. The required RecoveryQuiescing -> actual completion -> Recovering -> validated B -> Recovered sequence remains unchanged.

## Next prerequisite and campaign restrictions

Resolve the physical-retirement lease and synchronized logical-death disposition for one concrete component family, then prove untouched task suspension/resumption using shipped task/event behavior and full host application admission. Only then add DispatchOutcome and the separate bounded retained-task record, propagate it through world/application continuations, and cover the remaining mutation routes. Do not introduce a generic engine cancellation system.

The baseline 628/integration, affected30 and bounded recovery receipts retain their historical scope; the current enlarged suite has a known failing G1 regression. State-policy v1 remains unchanged; debris motion remains exact/Unresolved. No production binary was changed, no fresh GREEN/integration/live receipt is claimed, no408 was retried, and no600 was launched. G2 remains blocked by G1.
