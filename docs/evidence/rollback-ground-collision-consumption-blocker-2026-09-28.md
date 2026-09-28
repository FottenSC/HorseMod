# G1 prerequisite: changed free body at PhysX consumption

The production-boundary regression is RED. No production implementation was
changed: current task/host APIs do not establish recoverable cancellation of an
untouched PhysX start or substep task. A new terminal veto would not satisfy B
recovery. G1 remains open.

Changed files are `tools/replay_component_dispatch_selftest.cpp`,
`tools/deterministic_qualification/tests/test_replay_run.py`, and this report.
The existing component-dispatch fixture/compiler and existing runner were reused.

Run exactly once:

```powershell
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_replay_run.py::test_physics_dispatch_rejects_free_body_changed_after_entry --layer native-contract
```

The fixture compiled successfully. The test failed on behavior, not setup or
compilation; runner exit 1, fixture child exit 136, one failed test:

```text
entry collision admission=accepted; same unrelated body changed kinematic->free; current collision predicate=body_unrelated_dynamic
G1 RED: changed unrelated free body reached PhysX start wrapper; native_entries=2 completed_tasks=2 collision_checks=2
```

Retained [raw failure log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a89e8182c6e5cf8f1331d39989e0aed44e28b45b11be77588b92422fe7cc3acc.log),
[runner manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/db8822e12ca0b128efcfbce0fb817cdcbfd90a546834ba3dc68372c5ed85f69f.json),
and [reconstructible source snapshot](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-7fe1e840f771574ef05e946b0b29bdaa4fbe27236f8b3957272f72668069fabb.json).
The snapshot predates this report and retains the exact regression source tested.
Recorded build identities in the runner manifest are existing artifacts, not a
build or validation of new production code.

The regression executes the unmodified production `collision_update_domain` and
`physics_domain` through the existing `GroundCollisionFixture`, followed by the
actual extracted `Sc6ReplayTaskGroup::PumpOne` / `DispatchTask`. It checks the
zero-debris case, because the application may produce its first debris birth.
One unrelated actor initially reports kinematic state and passes collision entry
admission. A control dispatch reaches the native-entry probe. Changing only that
actor's public body flags to free/dynamic makes the same collision predicate
reject with `body_unrelated_dynamic` and the correct actor address. A second
task still reaches the PhysX start wrapper probe. Thus neither the missing
predicate nor its result is supplied by a stub.

This is a compiled local contract test. Scene/actor storage, native scene lookup,
locking, public body-value copying, empty engine queues, weak lookup, and native
queue/wrapper services are controlled fixture edges. The shader bytes come from
the installed executable and pass the production hash predicate. No solver runs;
the full host `ValidateUpdate` root census, native concurrency/lifetimes, actual
completion, and B recovery are not demonstrated. The fixture grants no lease or
host cancellation callback. It also fails if rejection becomes terminal; retaining
an untouched task would establish only the before-effects prerequisite, not B
recovery. Start is the reproduced case; substep collision rejection remains
unproven.

Code-level blockers and the smallest prospective integration:

- [Ground update admission](../../HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl)
  (`ValidateUpdate`, line 747) checks callbacks and collision at application
  entry and explicitly excludes mid-update coverage. Reuse the collision
  predicate at the existing owned consumption path rather than adding a hook.
  Its current scene-phase and engine-quiescence requirements also need validation
  for that consumption boundary; the entry predicate cannot simply be declared
  valid during in-flight physics.
- [DispatchTask](../../HorseMod/horselib/deterministic/Sc6ReplayTaskGroup.cpp)
  (lines 410–497) checks only `ReplayPhysicsStepConsumer` callback/vehicle
  admission for start/substep. Rejection returns `TerminalFailure`; `PumpOne`
  line 382 fast-fails. Substep tasks bypass `ConsumerAdmissionHooks` entirely.
  The initial/repeat wrappers own B0/C8/completion obligations; suppressing or
  completing their wrappers is not a cancellation contract.
- [ConsumerAdmissionHooks](../../HorseMod/horselib/deterministic/Sc6ReplayTaskGroup.hpp)
  (line 51) exposes acquire/validate/begin/completed, with no abandon/cancel
  operation. Hold requires a separately verified family lease. The
  [world suspension boundary](../../HorseMod/horselib/deterministic/Sc6ReplayWorld.hpp)
  (line 30) is restricted to group 5. Existing host hold wiring protects trace
  consumers, not PhysX tasks. `ResumeConsumerTask` calls the retained wrapper
  after hook validation; it does not rerun ordinary dispatcher predicates.
  Any eventual guard must cover resume as well as initial dispatch.
- [Restore cancellation](../../HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl)
  (line 2142) requests recovery quiescence; it does not cancel native task/event
  obligations. Execution recovery waits for completed application, idle engine
  and executor, empty arena, and completed simulation interval (line 1333;
  also `CaptureHistoricalExecution`, line 1131). A retained unexecuted physics
  task cannot reach that boundary by the current API. Running it to settle would
  consume the rejected domain; faking completion would violate ownership.
- [HistoricalRestoreSupported](../../HorseMod/horselib/deterministic/NativeReplayMaterialTaskGuard.hpp)
  remains false. No positive material Start lease is introduced or inferred from
  the supplied native terminal-route findings.

The missing prerequisite is a verified PhysX task-family lifetime/event owner
with a host abort-to-quiescence operation that preserves complete B until native
and application/render settlement succeeds. Only then can the shared collision
check reject before the wrapper's first effect and route the transaction into
existing B recovery. No callback suppression, fake task completion, terminal
fast-fail, or additional native hook was implemented.

No production build, additional test run, deployment, live experiment, Ghidra
operation, or status-file edit was performed. Simulation equality, coherence,
native ownership/recovery, and performance remain unmeasured by this regression.
