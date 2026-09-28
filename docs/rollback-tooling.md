# Rollback tooling

Run commands from the repository root (`E:/myMods`), using `python tools/replay_test.py` and the existing `build_cmake_LessEqual421__Shipping__Win64` directory. [Status](rollback-status.md) identifies the first blocker; [the active plan](rollback-netcode-local-readiness-plan-2026-09-22.md) defines G0-G10. This page explains how to select checks, inspect evidence and operate the runner. Tooling success is not rollback qualification.

## Normal work sequence

Read status and retained evidence first. For a locally reproducible defect, add a failing production-boundary regression, fix it, then run affected local checks. Choose one of the selections below for the change; they are alternatives, not a suite to run in sequence. Preview a changed-path selection before executing it.

```powershell
python tools/replay_test.py status
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_replay_reporting.py
python tools/replay_test.py local --changed-path tools/deterministic_qualification/replay_outcomes.py --list-tests --output full
python tools/replay_test.py local --changed-path tools/deterministic_qualification/replay_outcomes.py --layer unit
python tools/replay_test.py local --group fixture-sharing
```

At the live-admission checkpoint, complete the required native build/checks and full local suite, certify G0, and run fresh preflight for the chosen profile. Qualification profiles also require the integration receipt described below. Advance through the shortest named native experiment, affected rolling30 and rolling600 only as the active plan permits. **Stop after any failed command; do not paste the whole sequence through a failure.** Investigate before retrying, with a specific fix, instrumentation change or falsifiable hypothesis. Complete cleanup and retain evidence each time.

These commands illustrate the admission steps for `rolling30-gate`; they are not authorization to bypass a RED prerequisite or skip the earlier gates. Inspect `python tools/replay_test.py --help` and the profile before choosing an experiment. Use the same profile for preflight and launch.

```powershell
python tools/replay_test.py build
python tools/replay_test.py local --group all
python tools/replay_test.py certify-g0
python tools/replay_test.py preflight --profile rolling30-gate
python tools/replay_test.py combat-restore --profile rolling30-gate
```

## Validation scopes

Repeat `--test` for exact files/pytest nodes or `--changed-path` for affected source groups; do not combine them. `--list-tests --output full` previews the command, files/nodes and native commands without executing tests. Layer filtering happens later during pytest collection, so the preview is not a final count of tests in that layer. Unknown dependencies conservatively select all. Bare `local` examines dirty/untracked paths and may select the whole suite in this busy checkout.

| Layer | Use |
| --- | --- |
| `--layer unit` | Python predicates, parsers and data checks |
| `--layer workflow` | CLI, filesystem and orchestration |
| `--layer native-contract` | Compiled production fragments with controlled native dependencies |
| `--layer all` | All selected layers, the default |

Groups come from [replay_test_groups.json](../tools/deterministic_qualification/replay_test_groups.json). Exact selections, partial groups and filtered layers never produce full-suite G3 evidence. The admission checks have distinct responsibilities:

| Check | What it establishes |
| --- | --- |
| `build` | Retains source-qualified binaries and runs the two ordinary CTests plus the shipped game/PhysX marker check. |
| `local --group all` | Runs every local layer, stops at the first failure and includes the mapped shipped native commands. Required for live admission. |
| `certify-g0` | Verifies retained build/full-local/native evidence and observes cleanup/process ownership; writes a G0 receipt. It does not build, rerun tests or recover resources. |
| `run_integration` | Runs or reuses the verified full pytest suite for qualification orchestration; shipped native executable checks remain separate. Qualification profiles invoke it automatically. |
| `preflight --profile ...` | Checks the selected profile's prerequisites and current launch resources. Both blocked and unknown prohibit launch. |

`local --group all` does not populate the orchestration integration cache, and that cache alone cannot replace the native-inclusive local receipt required by G0. Use the existing [integration helper in the active plan](rollback-netcode-local-readiness-plan-2026-09-22.md#10-runner-evidence-and-handoff-to-networking) when preparing that cache offline. Existing validators must accept each receipt for the current inputs. Two ordinary CTests do not replace mandatory shipped native checks. A historical `tooling-integration` exclusion cannot qualify G3; consult current readiness rather than treating its old blocker wording as latest status.

The fixture cache reuses compilation only: every requested fixture executes in its own process/directory. Keys include generated/preprocessed dependencies, compiler/linker recipes, search order, relevant environment and verified binary hashes. RED overrides/unclassified recipes bypass reuse. Trace/storage and scheduler/dispatcher fixtures share compilation where compatible; Windows fail-fast is a separate variant. Full-suite reuse requires verified source, command, interpreter, pytest, raw log and native build provenance. Python-only reporting changes can reuse native binaries only when native dependencies remain compatible.

## Profiles, controls and observers

Profiles in [replay_profiles](../tools/deterministic_qualification/replay_profiles/) freeze hypothesis, setup, window, identities, decision, injections, required coverage and validator. Behavioral overrides cannot silently alter a profile. Legacy profiles cannot claim newly required coverage. Zero-hit capture may be observationally complete but inconclusive about ownership; task correlation is not a lease.

Use the shortest relevant window and compatible independent controls. The short A617->C624 profile authors the frozen final input prefix but omits previous rolling corrections; it cannot replace the accumulated 408-cycle regression. Rolling600 requires a compatible retained rolling30 pass and complete cleanup. Keep original anchors, setup interventions, input-arrival coordinates and control alignment.

Control reuse checks identities, raw integrity, runtime absence where required, setup/diagnostics and cleanup. Unchanged-input selection uses original-forward startup observations, never regenerated outcomes. Changed inputs need independent final-history controls for the relevant revision. The approved CRT compatibility split requires corresponding modified controls. Startup mismatch is separate from rollback divergence; do not select successive controls until one agrees.

Optional diagnostic depth `0|8|16`, masks, tick windows and per-DLL byte limits are bounded. The first overflow invalidates capture completeness. Diagnostics must not change callbacks/RNG; required comparisons remain enabled. Qualification disables optional tracing.

For supported `combat-restore` cases, `--resume-candidate` revalidates and reuses the completed candidate capture, then completes the independent-control comparison, reusing a compatible control or capturing one as needed. Profile, diagnostics, identities, raw integrity and cleanup must still match. Investigate the earlier failure before using it. The separate `--candidate-only` option skips the independent control and cannot qualify continuation.

## Read evidence instead of rerunning

```powershell
python tools/replay_test.py status --report PATH_TO_REPORT --output json
python tools/replay_test.py inspect --report PATH_TO_MANIFEST --participant ground --tick 617 --output json
python tools/replay_test.py inspect --report PATH_TO_MANIFEST --offset 123456 --output json
python tools/replay_test.py status --output json > previous-status.json
python tools/replay_test.py status --since previous-status.json --output json
python tools/replay_test.py evidence --rebuild-index
```

`PATH_TO_REPORT` / `PATH_TO_MANIFEST` are placeholders for retained files. `status` and `inspect` are read-only; rebuilding the evidence index is an explicit offline write. Status separates build/local/live compatibility, scoped attempts, latest failures and recorded deployment ownership. Missing identities cannot qualify a gate. Game, replay, runtime, framework, observer, PhysX, UCRT, policy and configuration matter, not merely a mod DLL hash.

Output modes `compact|json|full` affect presentation only. Current compact/JSON schema 2 uses structured outcome schema 1 (check ID, category, scope, outcome, coordinates and evidence); missing observations remain null/unknown. Compact success/failure budgets are 2/4 KiB with linked full transcripts. The legacy adapter recognizes known report fields, not arbitrary recursive failure keys.

Indexed inspection verifies raw/index hashes and reports bounded context with continuation offsets; repeated ticks in different traversals remain distinct. Legacy scans use bounded windows and say when the search is partial. `inspect` success means a valid read, not experiment success. `status --since` compares semantic changes, ignoring elapsed time alone. No extra game ticks are run to gather post-failure context.

Immutable manifests/logs/source receipts live under `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence`. Source format `objects-v2` stores compressed content by uncompressed SHA-256 in SQLite; snapshot manifests include dirty/untracked files, deletions, dependencies and identities. Publication re-verifies content/membership. Older ZIPs remain supported and retained.

```powershell
python tools/replay_test.py evidence --seed-source SOURCE_RECEIPT
python tools/replay_test.py evidence --export-source SOURCE_RECEIPT --destination E:/myMods/build_cmake_LessEqual421__Shipping__Win64/replay-tests/portable-source.zip
```

`SOURCE_RECEIPT` is a placeholder for a verified receipt. Export is portable and deterministic. Broad source retention and native compatibility are separate; documentation edits may change a workspace fingerprint without changing native behavior. Never relabel old receipts as validation of new sources.

## Timing and ownership

Orchestration uses monotonic parent/child spans. Do not sum nested phases as wall time; cache hits, failures and interruptions stay visible. Native rolling protocol 1 spans B admission through the ordinary forward update, next checkpoint and required retirement. It separates preparation, publication, resimulation, application, capture, forward and retirement; records median/p95/max and every interval above 16.7 ms; and does not subtract assumed observer overhead.

The current final transaction has no next checkpoint and is marked `terminal_no_next_checkpoint`, outside the complete-sample distribution. Aborted/missing-end intervals and warm-up/teardown remain separate, visible coverage. Backlog is an owner-category count, not tasks or bytes. Resumed TPS, viewport cadence and displayed FPS are distinct; none substitutes for full rollback-update cost.

Observer-overhead comparison requires compatible identities/setup/workload, complete intervals, independent comparison and cleanup. It measures the optional witness increment, not common observer cost. Production qualification requires normal rendering, diagnostic GPU readbacks off, 600 consecutive cycles per required configuration, no growing backlog and complete simultaneous ownership at or below 1 GiB. Bounded diagnostics may use 2 GiB. Unmeasured categories stay open.

## Deployment and cleanup

Preflight reads required files, journal/backups, processes and disk space; it never recovers resources. Both blocked and unknown prohibit launch. A pending Steam launch remains pending until explicitly cancelled or delivered to its marked process; absence of a game process is not cancellation.

Recovery belongs to `RunResources` under the deployment lock; launch rechecks journal and deployed identity under that lock. Verify actual mapped DLLs before relying on an experiment. Restore prior files/configuration/settings, terminate only owned test processes and leave Steam running. A clean recorded journal is not a fresh process inventory. Never edit source during builds/suites/live runs.

## Detailed sources

Current implementation: [CLI and live orchestration](../tools/replay_test.py), [local selection and integration cache](../tools/deterministic_qualification/replay_local.py), [G0 certification](../tools/deterministic_qualification/replay_baseline.py), [preflight resource checks](../tools/deterministic_qualification/replay_preflight.py), and [profile resolution and reuse checks](../tools/deterministic_qualification/replay_profiles.py). Use these to verify command behavior; keep changing run results in status/evidence.

The archive retains [testing workflow](history/2026-09-28-consolidation/notes/replay-testing.md), [output/inspection semantics](history/2026-09-28-consolidation/notes/replay-tool-output.md), [full tooling contract and additional commands](history/2026-09-28-consolidation/notes/rollback-tooling.md), [tooling validation](history/2026-09-28-consolidation/notes/rollback-tooling-validation-2026-09-26.md), [test-selection measurements](history/2026-09-28-consolidation/notes/rollback-test-workflow-2026-09-27.md), and [startup compatibility investigation](history/2026-09-28-consolidation/notes/testing-startup-hypothesis.md).
