# Changed-free-body design decision — 2026-09-28

**Leave the production-boundary regression RED.** The current admitted domain permits a kinematic actor without proving that it cannot become free. Handling that change after task production requires a verified abort-to-quiescence contract, which the inspected native and host interfaces do not provide. This is a bounded no-go decision, not proof that cancellation is impossible or that the change occurs at A617.

No production or test code changed in the delegated pass. Root reproduced the RED on the current checkout immediately before it: [fixture exit 136](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/50f76bfae511e4fd9e28a250afb3937cb964abf9d2b57b4e5c9fc6560a2248bb.log), with its [runner manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/99622bfa5aaf01f8a57bfd6f1a16040a100298604ffc943cb723c82a3a2aeaa9.json). No build, deployment or live experiment followed.

## Why structural exclusion is not established

| Boundary | Exact contract and consequence |
| --- | --- |
| Application entry | [ValidateUpdate](../../HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl), line 750, uses a local `State` and explicitly excludes mid-update coverage. [physics_domain](../../HorseMod/horselib/deterministic/ReplayGroundCollisionDomain.inl), lines 73–80, accepts an unrelated dynamic actor solely when its copied body flag has bit 0 set, then continues without establishing a stable owner or writer exclusion. This includes zero current debris because a first birth remains possible. |
| Lock lifetime | [collision_update_domain](../../HorseMod/horselib/deterministic/ReplayGroundCollisionUpdate.inl) releases each [SceneLock](../../HorseMod/horselib/deterministic/ReplayGroundSceneLock.inl) before returning. It supplies a point-in-time observation, not protection through subsequent native ticks, births, substeps or resume. |
| Initial start dispatch | [DispatchTask](../../HorseMod/horselib/deterministic/Sc6ReplayTaskGroup.cpp), line 410, checks `ReplayPhysicsStepConsumer::StartTask`: callback/vehicle admission, not the collision inventory. Adding collision rejection here would encounter an already produced/popped graph task; the existing rejection disposition is terminal. |
| Substep and resume | Non-tick substep tasks bypass `ConsumerAdmissionHooks`. `ResumeConsumerTask`, line 116, validates the retained binding and hook lease, then calls the wrapper without ordinary dispatcher predicates. Neither route provides collision writer exclusion or native cancellation. |
| Host recovery | [ConsumerAdmissionHooks](../../HorseMod/horselib/deterministic/Sc6ReplayTaskGroup.hpp) exposes no cancel operation. The [world hold boundary](../../HorseMod/horselib/deterministic/Sc6ReplayWorld.hpp), line 30, is group 5. [CaptureHistoricalExecution](../../HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl), line 1131, and execution settlement, line 1333, require completed application, idle engine/executor, empty arena and completed interval. `Cancel`, line 2142, requests recovery; it does not discharge a held physics task. |

The entry collision predicate is also not automatically valid during substeps: it requires zero native scene phase, no engine aggregate event and empty deferred/substep input queues. A separate consumption-time contract would need native validation; removing those checks would weaken existing protection.

## Native obligations rechecked

[Raw native responses](rollback-changed-free-body-design-decision-2026-09-28.native.json) explicitly target the existing `SoulcaliburVI.exe` program. No database edits occurred. Initial delegate `1420555E0` has no defined function; its body was checked with **dry-run** disassembly. No fresh mapped-binary qualification is claimed.

`142163BA0` binds tick `+0x18`, retains prerequisite events and publishes sequencer references. `14215D250` clears the binding after the callback; `14215ED20` then completes the event, releases task `+0x40` and recycles storage. Destructor `142158AF0` only releases its event reference and optionally frees storage. Destruction cannot replace completion.

World start `142018570` reaches `1420297F0`, which publishes active/event state before callbacks. Initial substep `1420555E0` installs `+0xB0`; `142055C40` publishes `+0xC8` and schedules the dependent repeat before callbacks/simulation. Repeat `142055BC0` releases `+0xC8`, then fetches/repeats or releases `+0xB0`. These normal completion branches provide no verified rejected-work abort. See also the retained [pre-wrapper ownership audit](rollback-g1-physx-pre-wrapper-recovery-audit-2026-09-24.md).

Consequently, holding alone cannot reach the host recovery boundary; dropping the task leaves native obligations; forwarding consumes rejected physics; disabling the tick or signaling completion changes required execution. Broadly rejecting all entry kinematics would remove the regression's admitted control and still prove nothing about later births. None is a supported fix. The fixture's possible held-task success message itself says B recovery is unproven.

## Smallest falsifiable next experiment

Root's follow-up in the same `SoulcaliburVI.exe` program found one concrete native writer route, without establishing its A617 execution. Registration data at `143AFF848` pairs the `SetSimulatePhysics` name with reflected thunk `141A95740`. That thunk calls component virtual `+0x520`; the admitted static-mesh child vtable `1436CEFB0+0x520` points to `142053F50`, which calls `142001C40` on body instance `component+0x430`. That routine changes body-instance flag `+0x74` and calls `142007730` → `141FE6730` → `141FE97F0`. The last routine reads the native rigid-body flags through virtual `+0x178` and writes them through `+0x170`; when simulation is enabled, its branch can clear kinematic bit 0. Direct callers of `142001C40`, including `14210D1A0`, also exist. This closes one **possible writer mechanism**, not its task phase, dynamic receiver binding, actor-membership effects or reachability in the admitted match. The registered function name alone is not an A617 invocation witness.

Perform a **read-only writer audit**, without deployment: start from the shipped public body-value accessor used by the predicate (`PhysX3_x64+0x7810`), resolve the source of copied flag `+0x9C`, and trace one legitimate kinematic-bit-clearing writer to its enclosing UE operation **before its first effect**. Include actor insertion/replacement when evaluating that owner; entry membership alone is insufficient.

Test the narrow hypothesis that the proposed supported owner can perform that mutation only before application admission. A reachable writer inside an admitted pre-physics task, substep, resumed continuation, reentrant callback or independent thread falsifies that exclusion. A positive result closes only that writer; all reachable flag/membership writers and owner lifetimes must be closed before a supported-window proof is claimed. If exclusion fails, identify a native operation that settles the exact tick binding, prerequisite/sequencer events, scene/substep owners and application tails without executing rejected physics before implementing an abort. No such operation is identified here.

The existing RED remains unchanged. Observer validity, simulation equality, coherent rendering, complete-B recovery and performance gain no new qualification; no A617 observation exists from this pass.

## Retained changes

The delegated pass added only this decision, the linked native JSON and the [source archive](rollback-changed-free-body-design-decision-2026-09-28.sources.zip). Root subsequently updated this decision and `rollback-status.md` with the current RED receipt and writer trace. The archive contains 19 exact reviewed files, including dirty/untracked production and regression sources, with a SHA-256 manifest. Archive SHA-256: `663b6cca11e54d0f2e88fda7b7a6b39e7f3c45ddadf232dbfe3fdf4779e73f53`. Production source, tests, build artifacts and unrelated work were left intact.
