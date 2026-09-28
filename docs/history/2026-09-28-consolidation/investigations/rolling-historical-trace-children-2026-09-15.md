> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Historical trace children in the rolling window

The first unresolved native boundary is A338/B345 after128 committed rolling
cycles. The retained partition identifies expired mesh27c7f1a8080 as an A child;
A and B each contain two different child identities and109 common trace states.
See [report](../../../../artifacts/rollback-owner-integration-20260915/rolling-600-trace-owner-partition.json).
Retaining a UObject lease does not prevent explicit actor/component destruction.

## Native construction boundary

Rechecked through the existing SoulcaliburVI.exe Ghidra program. The retained
[decompilations](../../../../artifacts/rollback-owner-integration-20260915/historical-trace-child-native.json)
contain the complete factory and caller, including earlier native annotations.

- `1408D1C00` returns a strong `{state, controller}` pair. It allocates108 bytes,
  initializes the F8-byte state, selects a parts/kind asset, optionally constructs
  an attachment, spawns the actor, configures mesh/material/animation ownership,
  adds the mesh tick prerequisite, and allocates the three trace history arrays.
- `1408D907A` calls this factory with parts-setting index1 for child traces.
  `1408CE180` then appends strong child membership and can accelerate the oldest
  child's fade. Reconstruction must not call that append/start route merely to
  obtain physical storage: the historical fade values and logical birth already
  belong to A.
- `141CC8120` only initializes base actor/default-subobject storage. It is not a
  complete UObject spawn or trace factory.
- The component selects its asset at488 and charge table at49C. The parts list is
  asset30/list30; the parts setting array is50 with18-byte stride; child setting
  index1 selects normal/Saint/Evil kind tables30/40/50, with empty special tables
  falling back to normal. Kind rows have10-byte stride.
- `1408D5910` configures mesh, actor-owned dynamic materials and animation class.
  Fresh animation/proxy/material owners must remain native allocations. The
  captured animation class binding now allows source selection to be checked
  without dereferencing an expired historical animation object.

## Integration obligations

The new source inventory resolves constructor inputs from retained A values and
bindings plus validated live root assets. It is read-only and does not prove
construction, publication, execution or retirement. The local fixture uses
PAGE_NOACCESS for historical controller/state, mesh, animation and array storage.

A children require independent physical owners even when their historical
objects are still live in B: corrected execution can destroy them. B retains its
existing strong storage and callback/scheduler/render ownership until recovery
or commit. The projected A image needs typed translations for state/controller,
actor/mesh/animation, nested arrays, cached parent weak refs and scheduler
prerequisites. Primitive IDs and native allocation headers come from the fresh
owners; they are not historical values to restore.

The existing private-B child transaction initially admits only empty A child
arrays. Extending it requires journaled strong ownership transfer for fresh A,
separate private B retirement, and C-created/A-destroyed child settlement.
Simply removing `historical_child_publication`, skipping expired scheduler rows,
or retaining logically dead objects is not sufficient. Partial factory failure
must either recover through its acquisition journal or poison and contain the
session; no partially restored state may resume.

Next native source check should use an ordinary A338/B345 seven-tick case to
avoid paying for128 earlier rollback cycles while examining this dependency.
The original rolling failure remains the later integration regression.

## Shared mesh writes and animation destinations

The factory's actor virtual call at1408D2023 resolves through143361660+480 to141C26DE0, now named and documented `RegisterAllActorComponents` in the existing Ghidra program. It calls141C20DA0 with an unlimited component count and clears actor86 bit0. The helper reaches141D58BA0, which performs native component initialization/registration and tick registration. [Factory/configuration/vtable/assembly evidence](../../../../artifacts/rollback-owner-integration-20260915/trace-factory-render-native.json), [verified wrapper annotation and registration consumer](../../../../artifacts/rollback-owner-integration-20260915/trace-factory-registration-verified.json). A full factory result is a registered native owner; render preparation must preserve this lifecycle and explicitly account for its proxy/work, rather than assuming detached storage. Native return does not establish GPU completion.

The native factory also writes mesh-asset imported bounds at50..6B through1420BC8A0. That function recomputes extended bounds at6C..87 using positive/negative extensions88..9F via1420A3700. [Retained native readers/writers](../../../../artifacts/rollback-owner-integration-20260915/trace-shared-bounds-native.json). Fresh-child acquisition now admits only an idempotent write: origin zero, extents1600, imported radius bits452D3480, zero extensions and derived radius1600, with the factory's source constants checked. An unsupported mesh rejects before native allocation; shared B asset data is never repaired to fit admission.

1408D5450 publishes source rows into every reflected matching node and the primary animation row array using native deep copies. It can allocate fresh destinations and calls HandleExistingParallelAnimationEvaluationTask on the owning fresh mesh. Retained historical array addresses cannot be used as fresh bindings. The pending projected-A handoff must include these destinations and their application completion, not just the state scalar rows.

## Completed native death and the remaining strong handoff

The [rechecked native functions](../../../../artifacts/rollback-owner-integration-20260915/trace-native-child-death.json) distinguish controller payload death from actor destruction:

- TickActiveTraceLifetimes1408D9C20 waits for queued material work, removes the fading weak entry, finds the matching child strong entry, calls HandleActorDestructionRequest, removes matching child strong entries, then releases temporary strong references. It stops an attached VFX slot through its normal native path. This route must continue unchanged during the seven resimulated ticks.
- TearDownAllActiveTraces1408D8130 likewise requests actor destruction before removing the matching child strong entry and balancing temporary references. It then clears fading weak entries, geometry/hash state and deactivates the remaining tag domain.
- DestroyLuxActiveTraceRuntimeState1408CED30 only releases the cached-parent weak reference and the three heap arrays. It does not destroy the actor or attachment. A strong count of zero alone is not a complete actor/render settlement receipt.

The operation already retains one weak controller identity without an additional strong reference. Initial publication must transfer the factory's single strong owner into the privately allocated A child array; it must not leave an extra private strong reference delaying native death. Undo before execution restores B headers before returning this ownership to the private child journal. After execution, independently captured C must either identify the surviving fresh child or prove native state/actor destruction and completed application/GPU work. PendingKill owners and destroyed state payloads must never be inspected or revived.

The current Prepared trace participant assumes every historical baseline state survives. Its execution proof must separate unchanged retained owners from reconstructed A children. The same distinction applies to cached-parent weak accounting: a fresh A child's parent reference was acquired before preparation, whereas a C-created child's reference is acquired during execution. Counting both as C-only additions would double-count surviving fresh-A parents and miss their native release on death. This remaining integration is not granted by the new scheduler projection or factory tests.

The scheduler now preserves the constructor's one common prerequisite through its normal private backing journal. An allocation marker distinguishes detached constructor backing from complete B. Before execution, undo returns that backing to the fresh tick; after execution recovery, the already retired fresh owner cannot reclaim it, so completed transaction cleanup releases it explicitly. Unfinished publication and poisoned ownership retain all buffers. This local cleanup proof does not replace native strong/actor/GPU settlement.

## Dormant primitive post-physics binding

Run replay-7acee43220a341098834e73200e1ac68 rejected the mesh+7B0 binding even though all88 retained/native bytes matched. The typed Target check was too strict for a never-enabled primitive post-physics tick. Native registration141DA9000 assigns tick+50 only after141D5FB60 admits CanEverTick (+0C bit02). Executor141D9BAB0 returns immediately for a null Target. Vtable143882140 and diagnostic141D99F80 identify UPrimitiveComponent::PostPhysicsTick. [Native consumers](../../../../artifacts/rollback-owner-integration-20260915/trace-null-postphysics-native.json).

Projection now admits this exact null pair only with matching prefix, the verified vtable, CanEverTick clear, registered bit40 clear and no pending task at18. It preserves the native null Target; no owner or scheduler event is synthesized. Other secondary tick kinds retain their existing typed ownership requirement. Registration and execution annotations were read back and saved in the existing Ghidra program. The registration pointer remains opaque; the unused second execution formal is not a recovered full virtual ABI.
