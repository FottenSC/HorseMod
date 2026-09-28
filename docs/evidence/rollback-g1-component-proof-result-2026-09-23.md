# G1 component consumer proof: implementation and current no-go

The tested post-admission Niagara change is now rejected by production dispatch before native task entry. G1 does not close: an untouched component-task hold, group-2 native-work ownership, and complete-B recovery for this route are not established. No recovery injection was launched. The stock census is observation only.

## Retained results

| Check | Result and limit |
| --- | --- |
| Production PumpOne/DispatchTask RED then GREEN | Prior RED child68 reached changed consumer. Current targeted suite2 PASS/415 deselected; real Windows fail-fast child exitsC0000409. Controlled queue/native-entry services do not prove host lifetime. |
| Shipped constructor/payload/graph wrapper | Required build/native PASS. Actual linked production dispatcher completes unchanged native callback/event/refcount/TLS retirement. Rejected task/tick/event/pool bytes unchanged. No dependent-task/last-reference/held-owner proof. |
| Stock census240 | FAIL at native_presentation_completion_failed:113 tagged frames below existing120 minimum. Task identity rows are partial evidence. Cleanup complete. |
| Stock census360 | PASS;77 component owners,7 classes observed in205..224; required observer completion and cleanup succeed. No simulation equivalence, coherent-B, recovery or performance qualification. |
| Integration628/affected30/bounded B recovery baseline | Retained previous identities/scopes only. Runtime/observer changed; not re-certified by census or local checks. |

RED240 manifest:530c22e7a8310c1bbcf8aa2c70173095ad44271b53600212325679b8e706e7ea. GREEN360 manifest:7be0a187867bc7acce818c8dd5bf79704f281870b4b08b2f576e1200a8b587ac. Both are retained in replay-tests/evidence/manifests and linked by the checkpoint JSON. Native production proof:[receipt](rollback-g1-production-dispatch-native-2026-09-23.json).

## Concrete task and ownership map

The successful stock run observes three Engine.LineBatchComponent owners at native205, group2, flags4e, original game thread/task-thread2. Native class/vtable identity is concrete; stock owner/class serials are0, not generation leases. Existing scheduler BindObject can acquire a real serial before admission. Census names/indices are diagnostic, not portable logical identity.

| Resource or route | Acquisition / first side effect | Transfer / release / missing enforcement |
| --- | --- | --- |
| Graph task | Scheduler142163BA0 builds; constructor142156650 transfers event reference; queue140D2B9F0 removes node | Wrapper14215ED20 completes/references/releases/recycles. Task/event retention does not pin raw payload. |
| Tick/payload | task+10 ->embedded owner+110; payload14215D250 traverses prerequisites and may write tick+3C before callback | Common tick destructor142157BC0 is too late for owner state; separate owner/world protection required. |
| Completion | task+40 constructor-transferred reference; prerequisite/sequencer references separate |140D20A70 closes; wrapper decrements+48 then returns task to pool. Never signal solely to unblock hold. |
| Concrete component | table1438774C8; consumer141D88EC0; query1402D9BF0 pure | deleting141D6E9F0 ->earliest body141D6D520 ->optional free. Monitor must precede body writes; not implemented. |
| Lines/points/meshes | component+808/+818/+830; tick decrements lifetimes, removes entries, reallocates/frees nested mesh data | Flush141D774C0 and HorseMod LineBatcherBackend also mutate. C-drain/B-undo needs these data and render state captured or an enforced empty-state contract. |
| World and prerequisites | wrapper world+28 supplies time; tick prerequisite backing may reallocate | world destructor1421B4E60; prerequisite writers142159B90/142164920. Before-entry retirement/mutation exclusion absent. |
| Host callbacks | Overlay writer is EngineTickPost | Existing framework Deferred disposition can retain callbacks and complete once after continuation. Current manager-only interior hold cannot substitute for strict component hold. |
| Shutdown | GuardedMain can exit loop without another TickApplication | FEngineLoop_Exit14039B420 can tear down owners. Held-task shutdown guard absent. |
| Concurrent/native work | Actual component executes in DuringPhysics group2; CRI worker exists independently of -nothreading | No group2 in-flight exclusion proof or bounded CRI callback closure. No destructive CRI->LineBatch route demonstrated; absence is not proven either. |

Native maps: [LineBatch lifecycle](rollback-g1-linebatch-family-native-2026-09-23.json), [engine-post exclusion](rollback-g1-linebatch-engine-post-exclusion-2026-09-23.json), [strict hold](rollback-g1-strict-task-hold-native-2026-09-23.json), [physical lifetime](rollback-g1-physical-lifetime-native-2026-09-23.json). LineBatcherBackend's old comment denying a native lifetime sweep is contradicted by141D88EC0; this does not independently prove that all instances double-decrement.

## Exact missing mechanism and disposition

A sufficient bounded design may use strict exclusion rather than a universal engine lifetime lease. It still needs: a separate retained task with validated generation/epoch/contract; an application entry branch before services, retirement and arbitrary pumps; preserved deferred EngineTickPost; earliest selected-owner/world retirement and shutdown guards; prerequisite/owner-actor lifetime checks; actual group2 native-work and independent-callback exclusion. Existing manager pending_task predicates are insufficient.

Until these are proved, production uses terminal rejection for the tested unsupported consumer and never exposes ConsumerHeld as resumable or recoverable. Local Windows process termination is proven; whole-game/native-GPU safe termination is not separately qualified. Exact C draining is not authorized for uncaptured Niagara birth or uncaptured LineBatch render/data mutations. Complete-B recovery and120 independent continuation remain OPEN.

No policy relaxation: exact/Unresolved debris, policyv1, production1GiB. No408 retry, no600, no G2 handoff. Six journaled prior states restored, no game, existing Steam98544 retained. Dirty/untracked work preserved.
