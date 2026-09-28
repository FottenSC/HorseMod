> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Continuation prompt: finish local rollback, then networking

Continue implementing and qualifying the replay/rollback system in `E:/myMods`. Carry the work through the applicable acceptance gates; do not stop at diagnostics, a proposal, a constructor pass or a helper pass. Networking/GekkoNet follows local acceptance, not the other way around.

Read first, in this order:

1. `AGENTS.md`
2. `docs/rollback-depth-seven-plan.md`
3. This handover
4. The latest checkpoints in `docs/rollback-status.md`
5. `docs/history/2026-09-28-consolidation/notes/rollback-next-phase-handover-2026-09-22.md` for the earlier full campaign context

This handover supersedes that earlier September22 handover for current implementation and experiments. Older status entries describing active builds/runs are historical. Inspect current files before editing: much of this implementation is untracked, so `git diff` alone is incomplete.

## Objective and durable rules

Finish local rolling rollback by seven native simulation ticks at every eligible boundary in one retained active-combat match. Preserve original A210 anchors, correction arrival/revision/window eligibility, input alignment, independent controls and intermittent HUD instrumentation. The independent407-cycle pass does not cross the current A617→C624 ground-debris boundary.

Use the existing checkout, `build_cmake_LessEqual421__Shipping__Win64` and `python tools/replay_test.py`. Preserve unrelated dirty/untracked work. No production/test edits during a build, test suite or live run. Stop failed experiments at the first failure; retain evidence, identify the earliest predicate and retry only after a concrete implementation/instrumentation change. Read retained evidence instead of rerunning for verbose output. Search narrowly with bounded output; PowerShell does not expand positional path globs for `rg`—use a directory and `-g`.

Keep complete B until corrected execution and application/render/GPU completion succeed. Preserve revision/window, native lifetime, membership, poisoned-operation and deferred-retirement guards. Submission is not completion; timeout is not cancellation. Production owned-memory limit is1 GiB. Expected observations are comparison data, never restore payloads. Do not suppress required callbacks, replay placement RNG, revive PendingKill objects, or indiscriminately replace pointers.

Use the deployment/recovery journal and existing cleanup owner. Restore files/configuration; terminate only owned test processes; leave Steam running. Freshly verify process/journal state and preflight before deployment. At handover, a fresh process inventory found no game, runner, pytest, compiler/build or native selftest process. Recheck rather than assuming this persists.

## Current implementation and proven boundary

Ground private reconstruction now constructs the root, five child meshes, ten MIDs, controller and native PhysX actors/shapes. It owns displaced response allocations, early child leases, complete world/relative/cache/bounds pose packets, and native factory temporary arrays. Private construction is separate from historical state restoration and active scene/render publication.

Last LIVE experiment: `changed-rolling408-ground-repro`, run `replay-0c7cc381c83649ae96ff14673b5e3da2`.

- A617/B624: `ground_physics_private_ready`, all five children, graph ready, native physics created, published=false.
- Native body termination, graph destruction, fresh GPU-complete retirement and preparation recovery to B624 passed before A publication.
- First remaining rejection: expired physics companion, code7; rolling stopped after407 cycles.
- Cleanup complete, games_remaining0.
- Immutable manifest: `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0014962073139838c675aeb4e5bf47e26333de33bc0bf1efac97fc14df1befc4.json`.
- Sources/checks/live receipt: `docs/evidence/rollback-ground-private-physics-2026-09-22.json`.

That run predates the NEW owned initial-body-state implementation below. Do not claim it qualified the newer code.

## Newest work: locally passing, integrated, NOT yet live-qualified

`ReplayGroundInitialBodyState.hpp` captures semantic body values from the actual native source while alive. It does not import expected observations or copy an owning actor image. Its current supported case is an initial sleeping kinematic: rigid flags3, zero velocity/wake countdown, no pending target or buffered work. Other retained-owner checkpoints remain capturable, but this reconstruction path explicitly rejects unsupported phases.

The packet owns public scalar values plus three coupled physical poses: BodyCore at actor+90, BodyToActor at+B0, buffered BodyToWorld at+140. Native scalar setters restore mass/inertia and other scalar settings. Exact comparison includes inverse mass/inertia, all selected public values and all three physical poses. Writes require a private actor with no scene, BodySim, query handles or buffered writes. No placement initialization/RNG is replayed.

An actual shipped-SDK regression with a nontrivial centre-of-mass transform failed with ordinary pose setters at generated property0x34. Installing all three captured physical poses under private guards passes. A separate earlier failure was an overly strict final guard requiring live-body kinematic storage before scene insertion; native E0180 reports no target when that private storage is null. This was fixed without weakening the live-body removal guard. Earlier field3A reports were stale baseline diagnostics, NOT established pose failures.

Integration now:

- `Sc6ReplayHost.GroundDebris.inl::State::read_initial_physics` captures owned state per child from the actual actor, validates typed BodyInstance user data and the shipped metadata getter, and captures engine initial-velocity bytes/flag.
- `Sc6ReplayGroundColdGraph.inl` copies those packets into the private child owner.
- `Sc6ReplayGroundColdPhysics.inl` requires a supported captured packet before acquisition, installs it into the fresh private body, and restores BodyInstance+12C velocity and flag18 so the helper's sample of the current B actor cannot supply A's state.
- Logs now distinguish `initial_body_values_restored=true` from `typed_scene_bindings=false`. `physics_state_offset` identifies a property or guard rejection. The expired-companion guard and explicit fresh-ground unsupported-publication guard remain intact.

Latest validation:

- Three focused production-boundary tests pass: `ground_cold_root_rejects`, `ground_capture_uses`, `ground_configuration` in `tools/deterministic_qualification/tests/test_replay_run.py`.
- Required build passed, including two ordinary CTests AND the shipped-SDK native checks. The build runner calls `run_local_regressions`; do not assume the compact two-CTest count is the whole native suite or rerun it needlessly.
- Latest build console: `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3c7999ac70fb7dffe363a6146d7c86324aedd212032e20f2d6dcd0d69ad4fb8a.log`.
- Immutable RED/GREEN/source receipts: `docs/evidence/rollback-ground-initial-body-state-2026-09-22.json`.
- Existing120-tick independent initial-kinematic removal/recovery continuation passes. The NEW reconstructed body was verified privately and released; it has NOT yet received its own independent120-tick continuation test.
- No live run has been launched for this newest capture/installation integration.

## Immediate next work

1. Review the newest packet/capture/installation against native consumers. Consider extending the existing SDK fixture to add the reconstructed body to a separate scene and compare120 ticks against independently authored control, including kinematic targets followed by simulation enablement and mass/inertia-sensitive impulses. This was the intended next local validation, not completed work. Do not create a new harness.
2. With compatible required checks passed, fresh preflight then the shortest named408 diagnostic can test this concrete new integration. Stop at the first failure; use the precise property/guard witness. A private-body-state pass still will not remove the known expired-companion rejection.
3. Finish typed historical physics and render/scene publication. Required relationships include root/children/controller, component/body/actor/shapes, scene/solver/island/query/filter identities, render resources, scheduler/manager references and leases. The current world physics projection is identity-bound. Fresh allocation does not make an old snapshot's owning pointers installable. Preserve complete B and transactional undo through publication and completion.
4. Extend reconstruction beyond the initial sleeping-kinematic phase when applicable to subsequent rolling boundaries. Do not skip eligible cycles or shrink the window to avoid active bodies.
5. Close the separate particle-event admission gap below before broader lifecycle qualification.
6. Proceed through shortest representative diagnostic → compatible full integration → affected30 → affected600, with independent corrected controls and required120-tick continuation. Finish recovery/cancellation, epoch/reset, presentation, complete ownership accounting and performance gates. Only then move into networking/GekkoNet.

Commands (retain compact output in uniquely named logs):

```powershell
python -m pytest tools/deterministic_qualification/tests/test_replay_run.py -q -x -k 'ground_cold_root_rejects or ground_capture_uses or ground_configuration'
python tools/replay_test.py build
python tools/replay_test.py preflight --profile changed-rolling408-ground-repro
python tools/replay_test.py combat-restore --profile changed-rolling408-ground-repro
```

408 enables both startup loading flush and no-async-loading-thread. Proposed30/600 profile settings differ; use matching controls and validator receipts. The273 reporting/tooling tests are not current full rollback integration. Consult the earlier September22 handover for integration/profile commands and complete acceptance coverage.

## Native ownership facts to preserve

- Helper ctor141FE7680 is extent98. Private factory141FF1790 with its fifth argument false creates actors/shapes and output arrays without scene insertion. Full wrapper141FF9100 additionally adds to scenes and initializes dynamic properties; it is NOT private.
- Factory input/output arrays use native heap backing because native code may shrink/reallocate. Preserve real scene IDs for retirement. TermBody142005C40 →142005D80 with zero scene ID can clear an actor pointer without releasing it.
- `ReplayGroundPrivateBody.hpp` validates actual shipped dynamic vtable/module, typed user data, exact shapes and scene/BodySim/query absence. `ReplayGroundPrivateQueues.hpp` additionally excludes new owners from FPhysScene queues/maps and transform outputs. Scene absence alone is insufficient.
- BodySetup readiness390 must hold before construction. The static-mesh getter141DC4E30 is verified from raw bytes/vtable618 but is not a defined Ghidra function; do not claim it was decompiled.
- Pose evidence: `docs/evidence/rollback-ground-pose-native-2026-09-22.md`; body/factory dependencies: `docs/evidence/rollback-ground-body-native-2026-09-22.md`.
- Streaming removal14214B520 joins two CPU tasks only on the built-record branch; queue-only removal skips it. This is not a GPU drain. Preserve `(flags &3)==0`, allowing bit4. Generic removal swaps with last and may reallocate. No deadlock is established. Reject unsupported private static membership.
- Material cluster mutation remains conditional on actual FUObjectItem association: root flag01000000 / metadata+C. Outer alone does not establish it; no cluster leak is demonstrated. Keep this secondary to the concrete blocker.
-1403C8F30, misleadingly named UObject_PostEditChange, is a duplication callback forwarding mode==2 through virtual+C8; the relevant child path141DA72A0 resets41C to-1 for false.

## Separate particle-event admission gap (still unimplemented)

Read `docs/evidence/rollback-particle-event-feedback-2026-09-22.json`. The retained report is `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/605b259bfea4c92f9a96419d73fc7b26788449f5dd955aad8739387e79cfb186.md`.

Native141F73990 completion invokes receivers141FA0B80 before world dispatch. Spawn receiver141FD3E30 can allocate/spawn particles. Shipped STG009_Dropwater connects Water_01 death End to one Water_02 spawn; its game-callback list is empty. Native completion is already invoked: no skipped receiver or live admission of unsupported listeners has been demonstrated.

Common VFX admission must census component delegates850/860/870/880, relevant AEmitter delegates398/3A8/3B8/3C8, world+C0 particle-event-manager indexed identity/vtable394B368, and authored per-event callback objects. Persistent stage components must be covered; the detached visitor skips them. Empty component delegates do not prove other callback domains are empty. Nonempty routes require traced targets and mutable-owner-state coverage, not retained pointers alone.

EventGenerator vtable3957D58 has an authored array at+30/count38, stride28; each record's callback pointer-array header is+18. Native141FD03B0 supplies it to transient rows. World dispatch slots5F8/600/608/610 handle spawn/death/collision/burst queues918/928/938/948 (strides40/50/78/48); script958/stride40 is internal receiver input. Receivers consume LOD+A0/A8, reached from emitter+28. Preserve internal receivers and verify death-to-spawn processing exactly once before transient rows are discarded. Add narrow production-boundary regressions and use indexed validation/owned hooks; no competing hooks at1403FCA60 or141D38300.

## Reporting and evidence

Report separately: simulation/observer validity, lifecycle, recovery, epoch/reset, presentation, ownership and performance. Current live proof covers private graph/physics construction and retirement plus preparation-only B624 recovery. Active A publication, complete boundary equivalence and all broader applicable gates remain open. Performance is unqualified; networking is deferred. Never label the overall system finished while these gates remain open.

Update concise status and immutable evidence at coherent checkpoints. `docs/rollback-status.md` contains older invalid UTF8: edit it as bytes and preserve existing bytes. Its prefix is `b'# Rollback current status\r\n\r\n'`. Keep AGENTS.md durable; do not add run logs there. Existing source-retention archives include untracked production/test inputs. At handover E: had about24 GB free.

Do not spawn subagents unless the user or applicable instructions explicitly authorize delegation. Use the existing SoulcaliburVI.exe Ghidra program through native MCP, applicable function/type skills, no second import or database-edit scripts. Read-only PE/Capstone inspection of the shipped PhysX DLL was used for SDK consumers; do not confuse this with saved Ghidra annotations.
