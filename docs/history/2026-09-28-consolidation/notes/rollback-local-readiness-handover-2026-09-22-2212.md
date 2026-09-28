> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Rollback local-readiness handover — 2026-09-22, 22:12 CEST

## Start here

The user requested this handover and interrupted implementation. **Local readiness is not complete.** The last completed implementation batch passed its required build/native checks, motion diagnostic, full 625-test integration and affected 30-cycle comparison. **The checkout now contains a deliberately failing regression for the next change; that production fix has not been written.** Do not mistake the preceding green receipts for a pass on the current test sources.

The user's latest architectural concern was whether we had returned to treating debris as inherently important to rollback. The answer is **no: exact chunk trajectories remain candidates for exclusion; shared RNG, IDs, births/deaths, callbacks and safe ownership remain required.** Too much earlier work concentrated on exact restoration. Continue toward an enforced logical-lifecycle/presentation boundary, not another campaign of private constructors or exact poses.

Read in order:

1. [AGENTS.md](../../../../AGENTS.md).
2. [Primary local-readiness plan](../../../rollback-netcode-local-readiness-plan-2026-09-22.md).
3. This handover and the newest [status](../../../rollback-status.md).
4. [Original readiness review](rollback-netcode-readiness-review-2026-09-22.md) and [debris dependency review](../../../evidence/rollback-debris-combat-dependency-review-2026-09-22.md), treating their current-state claims as historical where superseded here.
5. [Motion implementation/live evidence](../../../evidence/rollback-ground-motion-probe-2026-09-22.json) and [handover checkpoint with immutable sources/logs](../../../evidence/rollback-handover-checkpoint-2026-09-22-2212.json).

The old scene-ID regression has been fixed and passed subsequent required checks. Do not redo it. Preserve that safety fix, the private body/shape work and its 120 native continuation ticks as useful fallback evidence. The unfinished boundary remains **A617 → C624 scene/render publication**.

## Exact interruption state

Only `tools/replay_ground_capture_selftest.cpp` was changed after the green 30-cycle checkpoint. It now supplies a controlled native-scene edge named `State::collision_update(...)`, with `collision_admitted` and `collision_calls`, then rejects a production update that ignores the edge.

Reproduction:

```powershell
python -m pytest -q -x tools/deterministic_qualification/tests/test_replay_run.py::test_ground_capture_uses_native_scene_transform
```

Retained result: **1 failed in 4.62s**, compiled selftest exit **46**, message **`ground update ignored the current collision domain`**. The shell wrapper returned zero because its last command printed the log; the test itself is unequivocally red. The [checkpoint](../../../evidence/rollback-handover-checkpoint-2026-09-22-2212.json) retains the raw log, changed fixture, corresponding production/test files and a full reconstructible source archive including untracked inputs.

This regression tests whether the actual `Sc6ReplayGroundDebrisState::ValidateUpdate` boundary consults current collision admission. Its controlled edge does **not** prove native collision isolation. Keep/add separate production-boundary negatives for actual free bodies, queries, notifications, constraints, filters and aggregates. The helper name is a proposed implementation shape, not an already existing production API.

No production collision change, new Niagara guard, listener, hook, world cancellation API or state-policy exclusion was added after the green batch. Do not assume an interrupted patch partially implemented them.

At 22:12:17 CEST, independent process inspection found **no Soulcalibur VI, qualification runner, relevant suite or build process**. Deployment journal is **clean**; its last run is `replay-5f4c1ccf51a04199b17602bf09600a7b`, and recorded game PID34052 is absent. Steam PID98544 remains running. E: had 18,310,303,744 bytes free. Recheck all of this before work; journal history is not a process inventory.

## Last verified batch

All rows below refer to the preceding production binaries and their retained source identities, before the new red fixture:

| Check | Result and scope |
| --- | --- |
| Required build/native | Pass, 100.969s; two CTests plus shipped native regressions |
| Named `changed-rolling407-ground-motion` | Pass, 930.657s; 407 depth-seven corrections, 2,849 resimulated ticks, 14,364 native callback comparisons, four fresh authored controls and 120 independent continuation ticks |
| Full integration | 625 passed in 398.81s; initial extracted-parser fixture omission fixed, production parser unchanged |
| Affected `rolling30-gate` | Pass, 313.75s; 30 corrections, 2,300 callback comparisons and 120 independent continuation ticks; cleanup complete |

Binary SHA256:

```text
runtime   4f8913b2f2b90bff2a3c24d1775e25ac86cc904bc1a43e9517fb25d78873c541
observer  fd65dd578c200a217ac3284cd0a9770644514d391217145e24aaff3f150cfb97
framework 978062983a0ecd42366922edccbf64bf35c5a2e7122c95cc75d29f50d836d9c6
```

[Central evidence](../../../evidence/rollback-ground-motion-probe-2026-09-22.json) links the source archives, raw logs and full receipts. Useful immutable identifiers:

- Required build manifest: `9ad7ba9758741e23a1769a0df84d649c71bec6509d11b0ecccc81c46cdaf63d4`.
- Motion407 manifest: `43d00075b6c5245181312180ba9b0b6439deb552bad3dc3c25ab2c03804ff6ad`.
- Full625 receipt: `f10c97305ad278866daed647f514da5549727ab0fc22772b524d681f57b60ee5`.
- Affected30 manifest: `e983c990bec7db4ae4f4511178a7adf19d678915aa00488e123913c1f9be6204`.

**The causal result matters:** at completed C623, before resume, native movement changed all five ground chunks. Immediate logical-root state and actual shared CRT state remained unchanged. The subsequent independent required-state comparison passed for 120 ticks. Receipt:

```text
run_id=replay-fb03999b811d401392e6e7b3f565ef83 tick=623
roots=1 meshes=5 changed=5 owned_bytes=1730488
native_move=true logical_unchanged=true rng_unchanged=true before_resume=true
```

This is positive evidence for excluding exact motion. It is not proof of all consumers, revised births/deaths, coherent presentation or a qualified replacement lifecycle. No historical expected values were installed.

## What is implemented and worth preserving

- `ReplayGroundPose::PerturbNative` performs bounded native movement, prereads all admitted transforms, rejects duplicate/null/nonfinite inputs, verifies meaningful transform lanes and detects no-op/partial movement. Padding is not semantic. This is qualification instrumentation.
- `Sc6ReplayGroundDebrisState::ProbeMotion` performs full existing owner/physics admission, retains leases, moves native components and verifies frozen logical consumers. It neither publishes an A replacement nor claims B recovery after an already committed diagnostic.
- `Sc6ReplayHost::ProbeGroundMotion` admits the one-use probe only at a completed idle transaction/application boundary, with no pending restore/retirement, and checks actual CRT state before/after.
- Observation schema **5** adds canonical ground logical observations: shared slot IDs, roots, root transform, controller/phase, recipe subset and clocks. Child motion and native addresses are excluded from this *observation*. Required fields missing on both sides reject. This does not remove child motion from production exact snapshots.
- Actual CRT state is observed independently at native boundaries/trajectory samples. The UCRT broker still calls original `rand`; it does not isolate cosmetic draws. Native tracing recorded shared-thread debris/audio RNG chains. Preserve actual corrected-history draws and ordering; never substitute a recorded fixed count.
- Particle event admission and the sole receiver observer preserve native completion/receiver execution. Dropwater Water_01 death → Water_02 spawn1 is a concrete required receiver route. Broader changed-lifecycle coverage remains open.
- Existing ground callback/transform admission checks root phases, reflected deactivation, manager listeners, child identity/ownership, navigation, dormant ticks, attachment/overlap/transform routes and verified virtual entries. Physics step callbacks admit only the verified empty vehicle-manager case and forward them unchanged.
- `Sc6ReplayHost::AdmitGroundUpdate` runs before native application entry. On failure it waits for required completion and uses the existing owning cancellation path when complete B is retained. Its local regression proves dispatch/ownership transitions, **not** 120-tick native B recovery.
- `ReplayStatePolicy.hpp` remains **version1**, and `debris_motion` remains **Unresolved**. No logical-lifecycle/presentation reconciliation replacement is implemented yet.

## Next implementation step and its limits

The earliest concrete gap is in `Sc6ReplayHost.GroundDebris.inl`:

- `ValidateUpdate` (around894) inventories current root/child callback and transform consumers but does not consult the full collision domain.
- `State::physics_domain` (around603) already inspects actual body/shape/query/constraint/filter consumers, including unrelated free bodies and native filter14204CF60. It is currently used by `PreparePhysics` (around1085).
- `PreparePhysics` also supplies native scene lookup/identity, scene locking, paired removal-operation admission and engine queue checks. Do not copy only the convenient predicate while dropping its preconditions.
- `physics_domain` currently skips aggregate-tagged volumes because **paired `ReplayPhysicsRemovalOperation::Prepare` proves empty aggregate ownership**. Reusing that method alone for updates would introduce a hole. Reuse/refactor the actual `ReplayPhysicsRemovalGuard::CaptureAggregates` / `EmptyAggregateVolume` conditions, including registry/free-list membership, member count, dirty/pending work, SAP endpoints and volume tags. Do not simply skip aggregates.

Proposed direction before interruption: add read-only current collision admission at the application boundary, with bounded accounted scratch, actual scene identities/locks and complete aggregate proof; route rejection through existing `AdmitGroundUpdate` recovery. Avoid running body removal or full historical capture to obtain a predicate. Add real native/production-boundary negatives alongside the new dispatch regression. `tools/replay_physics_markers_selftest.inl` already constructs real native scenes, empty aggregates and colliders and contains useful removal-guard negatives; extend existing fixtures narrowly rather than inventing another harness.

**This alone does not close G1.** A dependency can change after application entry. Enforce/prove safety at the relevant consumers during corrected execution and continuation. The explicit `Sc6ReplayWorld` / `Sc6ReplayTaskGroup` orchestration currently has no general Cancel/Abort API. Do not hold a half-completed world, skip required callbacks, resume partial state or call process termination successful B recovery. Any mid-update strategy must respect actual task/physics/application completion and complete B ownership.

### Remaining transform-reader route

[Native reader evidence](../../../evidence/rollback-ground-component-reader-native-2026-09-22.json) narrows direct generic component enumeration to:

- Reflected `GetComponentByClass`, `GetComponentsByClass`, `GetComponentsByTag` wrappers:1421F09E0/1421F0A70/1421F0B50.
- Native stage collector140418330 requests AtomComponent; reconstruction141ADCC20 requests ParticleSystemComponent;141FCEA90 requests SkeletalMeshComponent. Those class selections cannot select static-mesh chunks.
- **141BC7F90 `RefreshNiagaraStaticMeshSource`** can select a StaticMeshComponent from an explicit actor, parent/outer chain or owning actor, then read its transform+270. Its data-interface vtable is1437FCAA8; Niagara component vtable1437FA218, native class singleton144390358. This remains a potential chunk reader, not a demonstrated combat consumer.

The full external asset dump census searched **52,552 `.uasset`/`.umap` files** for ASCII `Niagara`. Only Engine/Content/EngineMaterials/WorldGridMaterial.uasset matched, with Niagara vertex-factory shader names. CookedIniVersion.txt records `Windows.Engine:Niagara:EnableNiagara:0=false`. These are supporting evidence, not package-completeness or active-runtime absence proofs. The earlier repository-only dump search covered merely143 assets.

[Supplemental native follow-up](../../../evidence/rollback-ground-reader-handover-followup-2026-09-22.json) retains141BC53D0/141BC8620: real Niagara initialization requires live system/component weak objects; code is not absent merely because the cooked setting is false. Existing scheduler capture checks tick-function families and indexed owner identity but does not explicitly ban Niagara component classes. No new listener/hook/admission implementation was added. Resolve the actual active route with the smallest bounded check; do not start a broad unrelated object/type census.

Root bounds/socket getter evidence already shows they do not aggregate moving chunks. Native movement reaches body transform publication with teleport1, but the positive live receipt verified component transforms, not an independent per-body pose readback. Preserve that distinction.

### A617 → C624 blocker

`Sc6ReplayHost::PrepareFreshParticleOwners`, `Sc6ReplayHost.ParticleOwners.inl` around213, rejects **`!operation.fresh_ground_roots.empty()` before `TransposeFreshParticleScheduler()`**. This is not a rejection inside transposition. Private root/graph/body reconstruction exists; real manager/scheduler/scene/render publication does not.

Do not remove the guard. Choose and implement the logical-lifecycle/nonexact visual strategy after the no-feedback boundary is justified. Preserve complete B and typed live-owner bindings. **No relevant publication change has occurred, so do not repeat the known failing408 diagnostic.** The407 success ends at C623 and does not reach this boundary.

## Qualification and remaining acceptance

Continue: failing-before/passing-after regression → required build/native checks → fresh preflight → shortest named diagnostic that exercises the change → compatible full integration → affected30 → applicable600. Reuse receipts only with verified source/binary/setup/profile/observer compatibility. The new red fixture changes integration's source fingerprint; the old625 pass is historical evidence, not a current cache grant.

Known existing commands/profile choices, after the fix:

```powershell
python tools/replay_test.py build
python tools/replay_test.py preflight --profile changed-rolling407-ground-repro
python tools/replay_test.py combat-restore --profile changed-rolling407-ground-repro
python -c "from pathlib import Path; from tools.deterministic_qualification.replay_local import run_integration; r=run_integration(Path.cwd(), Path('build_cmake_LessEqual421__Shipping__Win64/replay-tests')); print(r.get('result')); raise SystemExit(0 if r.get('result') == 'pass' else 1)"
python tools/replay_test.py preflight --profile rolling30-gate
python tools/replay_test.py combat-restore --profile rolling30-gate
```

Select the shortest representative profile after inspecting its actual setup. The ground window starts around610; an early seven-cycle check alone cannot exercise new ground collision admission. `changed-rolling407-ground-motion` already includes its perturbation flag. Do not rerun it without a relevant change/hypothesis. Preserve A210, arrivals217/218/219, A617 and depth7. The motion profile uses startup flush, serial particles, no async-loading thread and normal rendering; those diagnostic restrictions do not qualify every intended production configuration. Controls are keyed by profile SHA, capture code, diagnostics and identities; never relabel incompatible controls.

Report separately:

| Area | Current conclusion |
| --- | --- |
| Simulation | Independent compared scopes pass on the preceding binaries; A617→C624 and600 remain open |
| Observer validity | Schema5 positive/negative checks and independent comparisons pass; new collision regression is red |
| Lifecycle | Observed receiver/logical ground history matches; corrected inputs changing whether/when births/deaths occur remain open |
| Recovery | Existing B ownership and rejection routing preserved; full injected fault/cancel matrix with120 native B continuation remains open |
| Epoch/reset | Complete reset/re-entry/old-epoch rejection and retirement matrix remains open |
| Presentation | Normal renderer ran; coherent actors/effects/poses/HUD acceptance is not established by comparison or a held image |
| Ownership | Motion407 tracked peak906,369,192 bytes; complete simultaneous native/GPU/in-flight/deferred accounting is not qualified |
| Performance | Failed on prior measured correction costs, roughly250ms–1s against16.7ms; resumed TPS and viewport cadence are not full-update cost |

G1 boundary proof, G2 actual publication, G6 changed lifecycle, G7 recovery, G8 reset, G9 coherence/ownership/cost and G10 local netcode adapter/support matrix remain open. G3/G4 have scoped preceding-revision receipts only. **Do not run600 or start networking/GekkoNet while G1/G2 remain unresolved.** The future adapter must retain recorded inputs during resimulation, prediction/delayed corrections, epoch/revision/window validity, stable logical event identities and bounded history.

## Working and cleanup rules

- Reuse `E:/myMods`, `build_cmake_LessEqual421__Shipping__Win64` and `tools/replay_test.py`. No worktree/project copy, reset, checkout, commit or broad cleanup. This checkout contains substantial unrelated dirty/deleted/untracked work; preserve it, including work from “Continue rollback qualification”. A git diff alone cannot reconstruct these sources.
- Do not edit production/tests during a build, suite or live run. Do not restart old exec session IDs: all qualification sessions from this handover have completed. The evidence archival operation is not a game/suite.
- Stop each experiment at its first failure, preserve immutable raw logs and identify the earliest predicate. Retry only after a concrete change. Keep diagnostic output and disk use bounded.
- Journal: `E:/SteamLibrary/steamapps/common/SoulcaliburVI/SoulcaliburVI/Binaries/Win64/ue4ss/Mods/HorseMod/dlls/main.qualification-resources.json`.
- Game log: `E:/SteamLibrary/steamapps/common/SoulcaliburVI/SoulcaliburVI/Binaries/Win64/ue4ss/UE4SS.log`. Requests: `C:/Users/prest/AppData/Local/HorseMod/Qualification/`. Working logs are overwritten; cite retained evidence objects/manifests.
- Use the existing deployment/cleanup owner; verify mapped DLLs, restore files/configuration, terminate only owned test games and leave Steam running. Previous StreamDeck-launched games were closed by the user. There is **no blanket permission** to kill future user sessions.
- Preserve complete B until corrected execution and required render/application completion; preserve native/GPU lifetime guards, original eligibility and intermittent HUD instrumentation. Production owned memory ceiling remains1GiB;2GiB is only for labeled bounded diagnostics. Expected observations are comparison data only.
- No per-tick `UObject::IsReal`/whole-object-array scans and no competing hooks at owned native entries, including1403FCA60 and141D38300. Use indexed identities and existing owners.
- Ghidra: native MCP tools only, always pass `program="SoulcaliburVI.exe"`. The GUI may currently show EBOOT.elf for an unrelated Darth Vader task; do not switch/edit that program. `RefreshNiagaraStaticMeshSource` was renamed, conservatively prototyped `bool(void*, void*)`, commented, read back and saved in SoulcaliburVI.exe. Relevant skill: `C:/Users/prest/.codex/skills/ghidra-doc-function/SKILL.md`. No further annotations are implied by this handover.
- Use `rg` with directory plus `-g` patterns on Windows, not positional wildcard paths. Preserve existing status-file bytes when prepending; older sections may not be valid UTF8. Status belongs in its own document, never AGENTS.md.

Work stopped at the user's request to prepare this handover. The user's existing authorization was to review and implement autonomously. When resumed for implementation, carry that local-readiness mandate through its applicable gates rather than treating this checkpoint as completion.
