> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Next-phase handover: local depth-seven rollback

Checkpoint: 2026-09-21, after the latest single-correction native pass and 541-test integration pass. The user requested this handover before the next live experiment. **The project is not complete. Resume changed-input rolling30, then continue the active acceptance plan without weakening its gates.**

## Read first and preserve the workspace

1. [AGENTS.md](../../../../AGENTS.md): durable ownership, native investigation, testing and cleanup rules.
2. [Active plan](../../../rollback-depth-seven-plan.md): the acceptance contract.
3. This handover and [current status](../../../rollback-status.md).
4. [Runner workflow](replay-testing.md) and [retained rolling600 coverage table](rollback-rolling600-coverage.md).

Work in **E:/myMods**, using **build_cmake_LessEqual421__Shipping__Win64**. Preserve the extensive dirty/untracked implementation and unrelated work. Do not create another checkout, driver or harness. The older [testing handover](rollback-testing-handover-2026-09-21.md) describes an earlier state; its starting-point results are superseded here.

No owned replay test, build or game process was running when this handover was prepared. Integration session 97906 finished successfully. The interrupted command only read status/plan and requested CLI help; it did not launch rolling30. A fresh process inventory found no replay runner, CMake/Ninja or Soulcalibur process. Unrelated Python services were left alone. Prior live receipts report complete deployment cleanup; still run preflight before the next launch. Leave existing Steam clients running.

## Verified checkpoint and exact evidence

The links below point to immutable manifests containing raw-log references, binary/settings identities, source retention and cleanup results. Do not substitute overwritten working stage JSON or UE4SS.log.

| Evidence | Result and limits |
| --- | --- |
| [Latest changed-input one-cycle pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e267e8442a177210cea20ff75b3e4235819cf7ff39c31634efcae45b84739d47.json) | A210 to C217, exactly seven regenerated ticks, revision 1/generation 2, all eight private replacement boundaries committed; independent histories and 120 continuation ticks passed. Latest runtime, including predication and pending-accounting fixes. Three runs cleaned up. |
| [Current full integration](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f2ea12d69428f3d9da027fe429a746b4e56031dcc76afa07c9785f13994383a6.json) | **541 Python tests passed**, plus DeterministicCoreSelfTest --diagnostic-trace and NativeCandidateRegionsSelfTest --contact-latches. Pytest 228 seconds; overall receipt 251.875 seconds. No game launched. |
| [First changed-input one-cycle pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/751ba8ee1ec409093ecd0c06e2f2dd47c0ad50ca808eaae6d262e9e992c1c3f2.json) | Earlier runtime: 1,372 callback comparisons, 127 pose traversals, 127 ordered lifecycle/RNG events, 127 HUD traversals, 122 active-player updates and 120 continuation ticks matched. Preserve as independent evidence, not current-binary qualification. |
| [Retained unchanged rolling600](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/124bed6cff7c7c23b67d0099978a031559c1cb01eeaf66662b3a499838bbcb4e.json) | Earlier baseline: T217 through816, 600 cycles, 4,200 regenerated ticks, 120 continuation ticks, zero missed cycles; 529-test integration at that point. |
| [Retained unchanged rolling30 prerequisite](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/6c8c1a47da8e9ba43e76b15fce4f28ac8c468d91da006150fe6bcdae8949b009.json) | Compatible with the retained baseline600, not the new binaries. |

Latest native candidate: **replay-9feeae9b0c2043c290bbdcb237e056dd**. Its correction cost was **1,263,331 microseconds**, capture64,887us, preceding forward23,355us; reported completed peak **837,023,449 bytes**, final owned495,357,991 bytes. Performance fails. Pending-command accounting changes moved observation points; this peak is **not a complete transient/native/driver allocation audit**. The previous pass reported887,563,589 bytes and1,000,863us correction.

Current tested binary hashes:

- Runtime: **74f2641b13ac79c9bf8e4a250380d9a8add2b42f04b42f422183f85f632ed37d**
- Observer: **e6f28b783404636fe56a6f865c72393575e31a8fdf0060cdccc65edc196ddd39**
- Framework: **978062983a0ecd42366922edccbf64bf35c5a2e7122c95cc75d29f50d836d9c6**
- [Last preflight receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0fe737890ddcdfdc7dd136a4067aaf06da183efc9c9d7dc7c4da13864ae65a5a.json)

Integration source archive manifest: 2488ad51889f5387b1801a21d1e3bf449b87da7640ccc0145af84721fa704994; workspace fingerprint e2e50af95569202e3f0fc61a7036b40fdc32fd2a99d2e228540e37009a03109c. Revalidate compatibility rather than hardcoding these as approval.

The last test-only edit updated a source-extraction locator in test_replay_run.py to accept the new VFX argument. An integration attempt stopped after277 passing tests on that locator; the complete rerun above passed. No production behavior changed after the latest native pass.

## Immediate next experiment

Use the existing runner:

~~~powershell
python tools/replay_test.py preflight --profile changed-rolling30-proposed
python tools/replay_test.py combat-restore --profile changed-rolling30-proposed
~~~

The required build/native checks and shortest one-correction diagnostic passed before the current integration. Reuse compatible evidence; let the runner verify build provenance, native signatures, mapped modules and profile identities. If inputs have changed, follow the normal invalidation rules. Do not launch600 after a failing30. Stop at the first failure, inspect its first native predicate, retain evidence and cleanup, add a production-boundary regression where reproducible, and retry only after a concrete fix or instrumentation change.

After an independently passing30 on compatible inputs:

~~~powershell
python tools/replay_test.py preflight --profile changed-rolling600-proposed
python tools/replay_test.py combat-restore --profile changed-rolling600-proposed
~~~

These are **proposed mechanism diagnostics**, not final performance qualification. Their schedule is frozen in the checked-in profiles:

| Arrival tick | Expected revision | Authored sample | First consumption in retained evidence | Inclusive delay |
| --- | --- | --- | --- | --- |
| 217 | 0 | 172 | 217 | 1 |
| 218 | 1 | 170 | 215 | 4 |
| 219 | 2 | 168 | 213 | 7 |

All rows use round0, two players, raw overrides0/0. The successful one-cycle profile instead edits sample169 arriving217, first consumption214, delay4. Simulation ticks and sample coordinates are distinct. Admission must use actual consumption records. The30/600 profiles issue three overlapping corrections, then roll unchanged inputs in the corrected history; they do **not** demonstrate600 new correction arrivals or chosen create/suppress lifecycle events.

Independent controls must exist for every history actually compared. Never compare an early revision's regenerated traversal against a later final revision. Keep120 independent continuation ticks after the last correction.

Operational subtlety: local --group all retains a full receipt but does not populate the separate run_integration cache used by qualification orchestration. Do not fabricate a cache entry. If the runner legitimately requires another suite, follow its checks or fix cache plumbing narrowly with a regression. Proposed diagnostic profiles are not qualification receipts.

## Implementation already present

### Transaction and protocol

The existing rolling host, real input-source API and runner now support immutable bounded correction schedules, actual native sample-consumption tracking, revision/generation admission and private replacement windows. Protocol14 adds the explicit scheduled entry point; legacy unchanged-input behavior/protocol13 remains supported. Complete B, its input revision and original history remain owned while corrected execution uses a separate capture owner. Eight replacement boundaries are privately prepared and adopted after required completion.

Primary files are under HorseMod/horselib/deterministic:

- Sc6ReplayHost.Rolling.inl: admission, replacement driving, revision/window commit and rolling receipts.
- Sc6ReplayHost.Checkpoint.inl: corrected capture routing, completion and accounting.
- Sc6ReplayHost.Seek.inl: transaction, cancellation and B recovery ownership.
- Sc6ReplayParticleCopy.hpp/.cpp/.Capture.inl: native/GPU capture, reopen and retirement.
- Existing runner, observer and correction tests under tools/; locate with rg --no-ignore rather than creating replacements.

### Lossless independent GPU checkpoint storage

Full new-window textures exceeded1GiB. Whole-texture sharing saved zero in the measured native segment. The implemented alternative stores each of four position/velocity textures independently as a baseline16x16 tile plus every bitwise differing tile; native census found13–21 different tiles out of4096. Dense images retain full storage. Two color textures remain full. There is no previous-checkpoint chain and no assumption that unused pixels may be discarded.

ReplayGpuImageEquality.hpp/.hlsl, ReplayGpuPackedImage.hpp, ReplayGpuImageExpand.hlsl and ReplayGpuImageMaterializer reconstruct full operation-private images bit-for-bit. Complete B remains full. Packing receives an additional query through the existing completion owner before sealing; source/full references stay alive until completion. ReplayCaptureAccounting.hpp accounts shared identities and reservation/retirement transfers. GPU scratch and simultaneous full/packed preparation are charged by current accounting, but the broader allocation audit is still open.

Real D3D tests run on WARP and hardware: all-byte reconstruction, source overwrite after packing, sparse NaN/negative-zero data, dense fallback, capacity rejection, pending completion and exact query ownership. Build generation is integrated in HorseMod/CMakeLists.txt.

### Most recent defects fixed, with regressions

1. **Corrected VFX capture included quarantined B owners.** CaptureReplayVfx now receives the existing historical birth quarantine only for corrected capture. Complete-B/ordinary capture remains unchanged. Excluded inventory is validated; manager slots referencing excluded owners still fail partition checks. [RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/274da2c13be5581f3bf4bf40f5e718338d9a79ad15a06ce496aad5fbebcd5f5e.json).
2. **Replacement advance treated asynchronous Releasing as failure.** DriveRollingReplacement waits through existing Releasing/Arming/Advancing/Stepping/Settling phases. Failures request seek cancellation and B recovery before terminal rolling failure. No extra callbacks, native ticks or application updates were added. [RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/98f58b90688cea427ac731df3c7b0451e801c01e9cb235392678323de526dcc3.json).
3. **Renderer predication could suppress checkpoint GPU work.** ReplayGpuCommandState.hpp temporarily disables and exactly restores the current predicate/value around comparison, packing, reconstruction and resource copies. Copy helper scope avoids C++ unwinding objects inside SEH callers. False-occlusion-predicate tests failed before the fix on actual D3D. Submission still does not imply completion. [RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/beacc8a0e09b5c6c93b0a4dc67bf5ca4fb0df75da3eb7590037a5fc2b7e531d7.json).
4. **Ownership reads preceded pending RHI command checks.** Capture, seek and rolling now wait before AdmissionBytes reads. Production-entry tests assert no accounting read while the command is pending. This closes specific read-order defects, not every possible native allocation race. [RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/6abeaec81437c14b639022ac2e51efef6c54b4e64e28a99b7a956b822fb4dbd4.json).

Focused group: python tools/replay_test.py local --group gpu-storage. It currently contains seven tests, including production source-extraction fixtures replay_checkpoint_vfx_route_selftest.cpp and replay_rolling_transition_selftest.cpp. [Focused pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3bfcb3878a6ebbee3acc8e29c25e7c36563caf36bb4fc11fce91f38aa178536e.json).

## Native lifecycle investigation ready for follow-up

Existing SoulcaliburVI.exe Ghidra program only; edits below were verified and saved through native MCP. Apply the ghidra-doc-function skill when continuing documentation. Do not import another program or edit the database through scripts.

- **ConstructCpuSpriteParticleEmitter @141F8E0B0**: receives caller-owned emitter storage, calls common constructor141F8DC30, installs vtable143949B60, returns the same pointer. It does not allocate or prove component/template binding. Partial common-state return/parameter type documented; score84%, remaining constants/partial layout acknowledged.
- **CreateCpuMeshParticleEmitter @141F942F0**: factory virtual+278, allocation0x200, common constructor, vtable143949D88; clears tail fields, calls virtual+8 initializer141F9C890 with template/component, then helper141F94140 and returns. Initializer stores template+10/component+18 and initializes durations. Factory/complete mesh type remains void* to avoid false array-of-partial-struct decompilation. Null allocation flows to a native dereference; do not describe a safe nullable contract. Score67.98%, unresolved helper and tail semantics documented.
- Observer ReplayPresentationObserver.hpp already records GPU factory return at141F941E0 and emitter retirement through141F78870. Runtime NativeReplayCpuEmitterLifetime.hpp owns destructor/lifetime hooks. CPU constructor receipts are **not implemented yet**. Before adding observation, recheck hook ownership and ABI. A sprite constructor return alone is not a bound/published emitter receipt.
- Worker1403FE520 consumes input-time delta and can traverse repeatedly while manager+1462 is set without publishing a new input pair. HandleBattleManagerD40Event @1403F5420 sets that repeat byte; ManagerBeginPlay registers it with an async listener hub. DispatchLuxBattlePoseCategoryArmedEvent @1404009D0 and LuxBattle_PlayerIntro_ActivateNextChara @140385F10 establish an intro producer. This is not proof of the needed active-combat repeat case.
- [Retained native producer audit](../../../evidence/rollback-native-lifecycle-producers-2026-09-21.json) contains pre-documentation decompilation. Its pending-documentation wording predates the saved Ghidra edits above; use the saved program for final names/types/comments. No CPU hook or new combat profile was implemented from this research.

## Open gates and next implementation work

| Gate | Current limit / required work |
| --- | --- |
| Every-tick corrected rolling | One cycle passes; current30/600 still unrun. Prove overlapping revisions, stale-history rejection and unchanged cycles afterward. |
| CPU/GPU lifecycle | Baseline has GPU construction/retirement and CPU retirement, but no explicit CPU construction proof. Choose create/suppress birth/death samples from retained lifecycle evidence, freeze them and compare each revision independently. |
| Pending work and sample reuse | Startup multiple/repeated ticks do not establish rolling combat coverage. Current replacement capture accepts completed application boundaries; pending-boundary/repeated-consumption behavior remains incomplete. Preserve recorded execution boundaries rather than adding convenient updates. |
| B recovery | Earlier capacity/VFX failures exercised portions of B recovery, not120-tick independent rolling recovery. Rolling fault orchestration remains unfinished; corrections_recovered is unused. Cover cancellation before A, during resimulation, after GPU drain before C/render, partial acquisition/publication/completion, invalid bindings, capacity, repeated cleanup and poison. |
| Epoch/reset/reload | Epoch change currently fails consumption_epoch. Implement stop-admission, settle/recover, retirement drain, old-window discard and seven-tick warm-up; test round transition, scene exit/re-entry and replay replacement. Unrecoverable native failure requires safe termination plus native reload, not partial resume. |
| Normal-renderer coherence | These proposed profiles use normal rendering but do not capture coherence images. Inspect bounded actor, weapon, effect, pose and HUD evidence during changed events, recovery and resumed continuation. Held images and hashes are insufficient. |
| Ownership <=1GiB | Packed storage passes measured admission. Audit transient B/old/new/scratch/native/GPU/in-flight/deferred ownership, including allocations not represented today. Never use2GiB as production qualification. |
| Full update <=16.7ms | Fails decisively: latest correction1.263s. Old baseline median248.333ms, p95339.005ms, max388.014ms; all600 corrections exceeded16.7ms. Neither receipt is complete end-to-end update qualification. |

Keep the intermittent HUD rejection open. [Original failure](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f500ac51559bfff90a9e8343e6990832cd1ac213e4ff9a347df267440972cd29.json): checkpoint210, damage slot0 active1, scalar true, binding/sources false, time0.066666670143604279; active_damage_sequence/checkpoint_domain/rolling_begin_rejected. It has not recurred. Preserve failure-only instrumentation. On recurrence stop immediately, inspect the first failing native predicate, add a production-boundary regression and fix the producer; do not move anchors or relax eligibility.

Performance work must first separate capture, complete-B acquisition, preparation/publication, seven-tick execution, application/render completion, forward work, retirement and end-to-end latency, including queue/backlog and overlap. Old measured capture medians: HUD16.188ms, traces11.356ms, VFX requests7.133ms, VFX handlers6.864ms, world6.093ms. Investigate repeated discovery/validation/allocation/copying one measured change at a time. Cache only identity-validated immutable metadata; use indexed object lookup and bounded scratch. Do not cache mutable membership or suppress callbacks. Final performance requires600 unchanged and600 changed cycles, normal rendering and ordinary forward work, hardware/settings, median/p95/max, every overrun, peak ownership and backlog. Optional readbacks/tracing must be disabled with observer overhead measured compatibly; correctness observations remain.

## Practical guardrails for the next agent

- Follow targeted regression -> required build/native checks -> shortest named diagnostic -> valid integration -> affected30 -> affected600. Reuse compatible independent controls and unrelated evidence.
- Do not edit production/test sources while a build, local suite or live run is active: source fingerprint checks intentionally reject that.
- On Windows use rg --no-ignore with directories and -g filters; positional filename wildcards may fail.
- Preserve reconstructible source archives, including untracked production/test files. Evidence manifests live under build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests.
- Deployment journal: E:/SteamLibrary/steamapps/common/SoulcaliburVI/SoulcaliburVI/Binaries/Win64/ue4ss/Mods/HorseMod/dlls/main.qualification-resources.json. Use existing journal/cleanup owner, never blanket-kill game/Steam/services.
- Do not add competing hooks at1403FCA60 or141D38300, use UObject::IsReal per-tick, revive PendingKill objects, release in-flight GPU resources, treat timeouts as cancellation or use expected observations as simulation inputs.
- Update compact status at coherent checkpoints. Report simulation, observer validity, visual coherence, recovery, ownership and performance separately. Declare completion only when every applicable gate passes.

