> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Rollback/replay continuation handover — 2026-09-22

## Read first and scope

Read [AGENTS.md](../../../../AGENTS.md), the [active depth-seven plan](../../../rollback-depth-seven-plan.md), this handover, then only the latest checkpoint in [rollback-status.md](../../../rollback-status.md). This is the primary continuation context and supersedes the September 21 handover's starting point and old status paragraphs saying a build/run is active. Older documents remain evidence, not current instructions.

The user's goal is to finish the rollback netcode / replay system. The executable acceptance contract currently covers **local rolling rollback by seven native simulation ticks in active combat in one retained match**, with changed inputs, lifecycle, recovery, reset, normal-renderer coherence, ownership and performance. Finish that prerequisite first. Networking/GekkoNet integration and historical replay seeking are deferred by the active plan; neither is established by current evidence. Do not pivot to networking to avoid an unresolved local gate. After local acceptance, carry the broader networking/replay goal forward with explicit transport/session/replay requirements and an acceptance plan grounded in the existing implementation; this handover does not invent a network protocol or claim one is qualified.

Use the existing `E:/myMods` checkout, `build_cmake_LessEqual421__Shipping__Win64` and `tools/replay_test.py`. Preserve unrelated dirty/deleted/untracked work. Do not create another harness, checkout or rewrite. No production/test source edits while a build, test suite or live run is active.

## Current checkpoint: what passed and what did not

- Latest native run: `replay-814437dbe50e4fcab7af42b538f0a3f5`, [immutable manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e30de8dd338012c0bd467da366304a4ce13d16a31515237001636bbd8cc81fdc.json). It completed 407 corrections and then rejected **A617 -> C624**, with complete B624 preparation recovery before A publication.
- The ground auxiliary material-parent capture fix passed live. The empty cold ground root constructed and detached successfully (`code=0`, `cold_root_ready`, flags `80600`, tick bytes `e/c`). Native destruction and a fresh GPU completion permitted retirement. **This proves an empty root's construction/retirement, not reconstructed ground execution.**
- The original expired companion still fails admission. The earliest recorded predicate is `particle reconstruction inventory rejected target=617 B=624 code=7 ... before_factory=true`, raw-log line **279772**. Next come `restore preparation failed participant=fresh_particle_owners`, the rolling failure and the final native run failure. The [reporting extension evidence](../../../evidence/replay-reporting-extension-2026-09-22.json) links an immutable structured index of these four events, produced offline without modifying the original manifest.
- Earlier [independent 407 pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/7c2692c908b9378c44f29e32b294de06d56b1b4762697805f4abdb090d05c680.json): A210 through C623, 2,849 regenerated ticks/intervals, zero missed cycles, 14,364 native callbacks compared, independent authored revision controls and 120 continuation ticks. Simulation/observer evidence only, on its recorded earlier binaries; it never reaches A617 -> C624.
- The last full integration receipt was [550 tests](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4604d4d77e8a8b9884d15104021e6d392b21ed0c3089bca0c49e2b207c92f4c9.json). It predates the latest ground work and reporting changes. Do not call it current integration.
- Most recent work was reporting, not rollback production behavior: **273 selected regressions passed, including 60 reporting cases**. These cover runner/fidelity, profile compatibility, evidence retention, deployment/recovery and source retention. They do not replace full integration or a native campaign. See [retained tests and source contents](../../../evidence/replay-reporting-extension-2026-09-22.json).

At handover, read-only process inspection found no replay runner, build or game active. The deployment journal records `clean` for the latest native run, whose cleanup receipt says `complete=true`, `games_remaining=0`. No new deployment or live campaign ran during reporting or handover work. These are point-in-time observations: verify fresh preflight before the next launch. Leave Steam running.

## Immediate engineering blocker and source map

The expired historical companion was independently identified as a physics mesh: object index **268798**, serial **17418**, captured vtable ending **36CEFB0**, physics scene 0 / actor 48 / kind 2 / two shapes, with no particle/material/manager-slot role. [Immutable companion witness](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/5830853145b51fd80005515e3690b092afbf9e308e74f54bd6ead6a2da43e530.json).

The next implementation is **typed reconstruction of the ground child mesh/material/controller/physics/render graph and its bindings**, using owned historical images. Preparation must remain private while complete B is recoverable; publication must install the correct A graph and callback phase without replaying births, RNG draws, activation or impulses. A cold root alone must never grant expired-companion admission.

Relevant existing implementation under `HorseMod/horselib/deterministic/`:

| Source | What to preserve / extend |
| --- | --- |
| `Sc6ReplayGroundDebrisState.hpp`, `Sc6ReplayHost.GroundDebris.inl` | Ground inventory, owned configuration/material/transform images, private B lifecycle and empty `ColdRoot` acquisition/destruction/release. Extend the existing owner rather than adding parallel lifetime management. |
| `ReplayGroundDebrisConfiguration.hpp` | Owns native 0x78 configuration, 0x50 placement rows and nested arrays. Auxiliary parent arrays at +0x50/+0x60 are valid even when mesh/class +0x38/+0x40 are null; preserve null entries and lease nonnull assets. |
| `Sc6ReplayHost.ParticleOwners.inl` | `PrepareFreshParticleOwners()` currently constructs cold roots when captured ground owners expire, then deliberately retains the original guard. Existing retirement queues require fresh completion before lease release. |
| `Sc6ReplayHost.Checkpoint.inl`, `Sc6ReplayHost.Restore.inl`, `Sc6ReplayHost.cpp` | Historical capture, preparation/publication/recovery integration, operation ownership and byte accounting. Ground checkpoint capture itself must not remove live physics. |
| `Sc6ReplayHost.Rolling.inl`, `Sc6ReplayHost.Seek.inl` | Revision/generation validity, eight private replacement boundaries, complete-B recovery and commit/retirement. Preserve these contracts. |
| `Sc6ReplayParticleCopy.*`, `ReplayGpuCommandState.hpp`, packed GPU image helpers | Existing native/GPU completion, private B images, lossless independent packed checkpoints, predication preservation and pending-command guards. |

Existing ground regressions in `tools/deterministic_qualification/tests/test_replay_run.py`:

- `test_ground_configuration_owns_native_nested_arrays`
- `test_ground_capture_uses_native_scene_transform`
- `test_ground_cold_root_rejects_publication_and_incomplete_retirement`
- `test_ground_body_initial_kinematic_admission`

The first three cover configuration lifetime, native transform offset **+0x270**, and cold-root rejection of invalid publication/retirement, including pending/cancelled/timed-out/stale completion. Fixtures are `tools/replay_ground_configuration_selftest.cpp` and `tools/replay_ground_cold_root_selftest.cpp`. [Configuration evidence](../../../evidence/rollback-ground-configuration-2026-09-22.json), [auxiliary-parent RED/GREEN/build evidence](../../../evidence/rollback-ground-auxiliary-parents-2026-09-22.json).

### Native facts that constrain reconstruction

Use the existing SoulcaliburVI.exe Ghidra program through native MCP. Apply the function/type skill when documenting; save/read back changes. Do not import another program, edit the database via scripts or add competing hooks.

Read the compact [ground controller/streaming map](../../../evidence/rollback-ground-controller-native-2026-09-22.md) before touching native construction:

- `1408A3860` is a high-level world ground spawn: registration, configuration, placement RNG and activation. It is not a private historical factory.
- `140863B30` / `1408635E0` deep-copy configuration arrays; `1408A2B60` consumes material overrides.
- Controller `140895CC0` operates on ground component +0x820 and consumes RNG; do not confuse it with the VFX manager.
- Mode 2 alone is insufficient: it covers pending and completed physics impulse phases. Preserve the bound callback, context and controller clocks. Fade/deactivation callbacks and normal autodestruction must execute correctly.
- Bound delegate constructor `143078D00` creates a 48-byte object with context +8, callback +0x10, nonzero sequence +0x20. Reconstructed bindings need a fresh context and explicit ownership.
- `SetStaticMesh` (`141DD8790`) queues even unregistered components in texture streaming through `14214B4C0` / `142141460`. Manager +0x70 (`14214B520`) removes queued/built dynamic membership. Private child setters therefore have native ownership side effects; prove cleanup/completion before releasing them. No such setter was added to the current cold factory.

Already solved contact/marker/SAP and trace-child paths are not a reason to restart the implementation. Read only the dependencies blocking the ground graph. Expected observations are comparison data, never reconstruction inputs.

## Exact continuation sequence

1. Inspect fresh process/journal state and the earliest retained predicate. Do **not** immediately rerun 408: the latest failure is explained and unchanged.
2. Choose one bounded producer change needed for the graph, verify its native consumers/ownership, add a failing-before production-boundary regression where locally reproducible, implement it, and pass targeted tests. Keep the expired-owner admission guard until the actual replacement graph satisfies its contract.
3. Run required build/native checks with the existing runner. After a specific executable fix or necessary instrumentation change, use the shortest **same-anchor** named diagnostic reaching this failure.
4. After that passes, obtain compatible full integration and affected 30 coverage, then affected 600. A failed experiment stops at its first failure; engineering continues from its earliest native predicate. Retry only after a specific change/hypothesis, never by moving anchors or weakening eligibility.

Existing commands (PowerShell, from `E:/myMods`; conditional steps, not instructions to launch all immediately):

```powershell
python tools/replay_test.py status
python -m pytest -q -x tools/deterministic_qualification/tests/test_replay_run.py::test_ground_configuration_owns_native_nested_arrays tools/deterministic_qualification/tests/test_replay_run.py::test_ground_capture_uses_native_scene_transform tools/deterministic_qualification/tests/test_replay_run.py::test_ground_cold_root_rejects_publication_and_incomplete_retirement
python tools/replay_test.py build
# Only after the next concrete change and passing prerequisites:
python tools/replay_test.py preflight --profile changed-rolling408-ground-repro
python tools/replay_test.py combat-restore --profile changed-rolling408-ground-repro
```

408 is the shortest existing diagnostic that reaches A617 -> C624 while retaining A210 and the authored arrival schedule. It is not a final qualification profile. It uses normal rendering but does not capture coherence images. Do not replace it with another 407 run to avoid the unsupported boundary.

For a full integration receipt in the cache actually consumed by orchestration, use the existing helper when needed:

```powershell
python -c "from pathlib import Path; from tools.deterministic_qualification.replay_local import run_integration; r=run_integration(Path.cwd(), Path('build_cmake_LessEqual421__Shipping__Win64/replay-tests')); print(r.get('result')); raise SystemExit(0 if r.get('result') == 'pass' else 1)"
```

`local --group all` does not populate that separate integration cache. Never fabricate compatibility/cache receipts. Reuse only receipts the existing validators accept; source changes invalidate relevant evidence.

After diagnostic/integration prerequisites, the existing affected commands are:

```powershell
python tools/replay_test.py preflight --profile changed-rolling30-proposed
python tools/replay_test.py combat-restore --profile changed-rolling30-proposed
# Only after a valid compatible 30 pass:
python tools/replay_test.py preflight --profile changed-rolling600-proposed
python tools/replay_test.py combat-restore --profile changed-rolling600-proposed
```

**Compatibility detail:** 408 currently sets both `flush_startup_loading=true` and `no_async_loading_thread=true`; the checked-in proposed 30/600 profiles set the former but not the latter. Do not silently import 408 controls or assume setup equivalence. Use each profile's matching independent controls and existing validators; any deliberate profile/settings change requires its own compatible evidence. Preserve intro skipping, seeds, serial particles and setup interventions equally in candidate/control.

The three overlapping corrections arrive at ticks 217/218/219 for samples 172/170/168, first consumed at 217/215/213 (inclusive delays 1/4/7). Native sample coordinates are not simulation ticks. Compare each traversed revision to its own authored control and retain 120 independent final continuation ticks. This schedule does not establish 600 new arrivals or deliberate creation/suppression of every required lifecycle event.

## Remaining gates — report separately

| Gate | Current evidence and required finish |
| --- | --- |
| Simulation / observer validity | Earlier independent 407 passes; current 408 stops before active-A ground restoration. Finish the graph, then affected compatible 30/600 and complete regenerated/revision/continuation comparisons. No missed cycles or stale old-history checkpoints. |
| Lifecycle | Full ground graph is open. Complete explicit CPU/GPU birth/death, callback, scheduler, RNG and physics coverage, including corrections that create/suppress events. Constructor-only and protected-death aborts are not passes. |
| Recovery | Latest B624 preparation recovery is a narrow receipt, not independent recovery qualification. Inject before publication, during resimulation and after GPU drain before C/render settlement; require complete B history/window recovery plus 120 matching continuation ticks. Include partial acquisitions, invalid bindings, failed publication/completion, capacity, repeated cleanup and poisoned operations. |
| Epoch/reset/reload | Stop old-epoch admission, settle/recover pending work, drain retirement, discard the window, warm up seven ticks in the new epoch. Prove round change, exit/re-entry and replay/scene replacement. Safe termination of an unrecoverable case must be followed by native reload proof. |
| Presentation | Normal renderer alone is insufficient. Inspect bounded actor/weapon/effect/pose/HUD evidence during correction, changed lifecycle, recovery and continuation. Held pixels and hashes do not prove coherence. |
| Ownership | Production ceiling **1 GiB**. Earlier 407 reports 905,432,624 owned bytes, but the full transient/native/GPU/in-flight/deferred allocation audit is open. Process working set is a different metric. Diagnostic 2 GiB is never production qualification. |
| Performance | Fails. Prior corrected runs cost hundreds of milliseconds or worse; no current full-update performance pass exists. Measure capture, B/preparation, publication, seven ticks, completed application/render tails, ordinary forward work, retirement, queue/backlog and end-to-end time. Require 600 unchanged and 600 changed cycles with normal rendering, median/p95/max and every overrun against **16.7 ms**. Viewport cadence, resumed TPS and submission timing cannot substitute. |

Preserve the intermittent HUD failure instrumentation: [original immutable rejection](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f500ac51559bfff90a9e8343e6990832cd1ac213e4ff9a347df267440972cd29.json), checkpoint210, damage slot0, active1, scalar valid but binding/sources false, time0.066666670143604279. On recurrence, stop, inspect the earliest native predicate and fix the producer. Never move anchors or relax eligibility to bypass it.

Keep complete B and its revision/window until corrected execution and required application/render/GPU completion succeed. Submission is not completion; timeout is not cancellation. No PendingKill revival, logically dead active owners, suppressed required callbacks, guessed pointer substitution or freed in-flight resources. Do not use UObject::IsReal in per-tick loops or add competing hooks at 1403FCA60 / 141D38300.

## Binary identities and deployment

At handover, current on-disk runtime, observer, framework and both native test executables matched the latest native manifest's recorded hashes. These identify tested files, not permission to bypass fresh validation:

| Binary | SHA-256 |
| --- | --- |
| Runtime | `8e8f7689b400a6d9c347070239470047d6c4026560a226e8f3893511c2ad89e9` |
| Observer | `65037f92c47d410d29a047c7401bc2550aa68ef5044ebfaae32aac78ee5076e9` |
| Framework | `978062983a0ecd42366922edccbf64bf35c5a2e7122c95cc75d29f50d836d9c6` |

Game, PhysX, UCRT, replay, config, source contents and build logs are linked by the native manifest. Reporting-source changes do not change those binaries but can invalidate runner/source compatibility; let preflight/build provenance decide.

Deployment journal: `E:/SteamLibrary/steamapps/common/SoulcaliburVI/SoulcaliburVI/Binaries/Win64/ue4ss/Mods/HorseMod/dlls/main.qualification-resources.json`. Use the existing `ReplayRun` and cleanup owner. Verify mapped DLLs, restore previous files/configuration, terminate only owned test processes and leave Steam running. A journal state is recorded evidence, not live process proof.

## Use compact tools rather than rerunning for output

[Tool usage](replay-tool-output.md), [initial reporting evidence](../../../evidence/replay-reporting-2026-09-22.json), [latest extension evidence](../../../evidence/replay-reporting-extension-2026-09-22.json).

- Compact is default; `--output json` emits one versioned bounded summary; `full` is available but not needed to recover already retained details. Success <=2 KiB, failure/unknown <=4 KiB. Follow immutable console/report/log links.
- `status [--report PATH] [--since PREVIOUS_JSON]` is read-only. `--since` compares semantic fields, ignores elapsed-time-only differences and distinguishes new run identities.
- `inspect --report PATH [--participant NAME] [--tick N] [--offset N]` is read-only. It returns bounded context and source coordinates, with pagination. New retained indexes verify index/log hashes. Legacy scans read at most 1 MiB per request, exclude unverified identities and may require continuation; a partial no-match is not proof of absence.
- For this older native manifest, the separately retained index in the extension's native-parser audit gets directly to line279772 without walking 71 MB of legacy pages. Existing manifests were not rewritten. Broad diagnostic extraction does **not** promote acceptance gates; the established capacity-failure classification remains isolated.
- Standalone runner/trajectory commands share compact output and retain original transcripts. Output mode cannot change profiles, test selection, exits or qualification decisions. `replay_run.py` ownership logic is byte-identical to the previous reporting checkpoint.
- Search narrowly with `rg --no-ignore` on directories plus `-g` filters; PowerShell positional globs may fail. Read bounded source/log sections, not full status history or embedded profile/source JSON. Retain reconstructible source contents, including untracked production/tests, and checkpoint concise status with immutable evidence links.

Completion means all applicable gates in the active plan pass. A diagnostic pass, safe cleanup, reporting improvement or network prototype does not complete local rollback acceptance.
