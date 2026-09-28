> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Reproducible rollback testing

Use the existing checkout and build directory. The depth-seven plan remains the acceptance contract; these commands improve iteration, not its thresholds.

## Commands

```powershell
python tools/replay_test.py preflight --profile root-transform-417
python tools/replay_test.py local --group root-transforms
python tools/replay_test.py local --group runner
python tools/replay_test.py local --group auto --changed-path HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp --list-tests
python tools/replay_test.py combat-restore --profile root-transform-417
python tools/replay_test.py combat-restore --profile rolling-cycle42-repro
python tools/replay_test.py local --group all
python tools/replay_test.py combat-restore --profile rolling30-gate
python tools/replay_test.py combat-restore --profile rolling600-qualification
```

Preflight reads journals, deployments, backups, required files, processes, and free space. It never recovers resources. `blocked` and `unknown` both prohibit a live launch. A pending Steam launch remains pending without explicit cancellation or marked-process delivery; no process is not proof. Recovery stays with `RunResources` under the deployment lock. The launch boundary rechecks the active journal and deployed identity under that same lock.

Profiles are versioned JSON in `tools/deterministic_qualification/replay_profiles`. Behavioral overrides that conflict with a profile reject; create an explicit profile variant for a different experiment. Profile bytes and resolved settings are retained. The 30/600 profiles test unchanged-input correctness, not full lifecycle, visual, or performance qualification. Their native continuation is 120 ticks. Short profiles use 180 seconds; the slow 600-cycle diagnostic-cost campaign permits 900 seconds.

`local --group auto` selects from explicit source-area mappings. Unknown paths or an unknown change set select all tests. Use `--changed-path` to declare the actual change under test when the checkout contains unrelated work. The complete selection, including native commands, is reported. Contact-latch and ownership groups include the native contact regression; all also includes the native diagnostic-budget regression. These require verified built binaries: run the `build` stage if their provenance is stale. Native production fixtures remain mandatory before live runs. Qualification profiles run the integration suite, and rolling600 requires a retained independent rolling30 pass on the same binaries and current rolling30 profile, with complete cleanup.

Local runs retain a reconstructible source snapshot and reject changes during execution. An unchanged source snapshot is reused by fingerprint. Source retention and test execution are reported separately from native rollback timing.

Qualification runs reuse a retained passing full Python integration receipt only when the workspace fingerprint, test command, interpreter identity and pytest version match. The source archive, receipt and raw log are verified before reuse; corrupted evidence blocks admission. Source changes require a fresh integration run. Mandatory native boundary checks still run before every live launch.

Python-only orchestration changes may reuse verified native binaries. Consumed generators, native files, generated inputs, build commands and recipes still invalidate provenance. Current capture sources are retained separately from the sources that produced those binaries.

## Diagnostic tracing and retained evidence

The existing hook owners accept `--diagnostic-stack-depth 0|8|16`, `--diagnostic-mask` (audio=1, RNG=2, particles=4, presentation=8), first/last tick, and a byte limit per DLL. The profile diagnostic windows target the retained failure locations. Qualification profiles disable optional tracing. The journal owns the temporary diagnostic settings file and restores its predecessor.

Tracing never changes callbacks or RNG outcomes. Diagnostic events have a conservative output-byte charge; the first exhaustion produces an overflow receipt and invalidates capture completeness. Both DLL instances and stack scratch have an additional 8192-byte observer reservation. Required comparison observations remain enabled independently of optional stack tracing.

The runner writes content-addressed manifests, raw logs and concise summaries under `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence`. Manifests preserve comparison failures, bounded already-recorded context, identities, cleanup, and separate acceptance results. Missing categories are explicitly unavailable. No game ticks are executed to collect post-failure context. Offline detection is not described as a live stop.

Use `python tools/replay_test.py evidence --evidence-report <stage.json> --evidence-report <candidate.json> --evidence-report <control.json>` to package retained reports without launching or rebuilding. Existing evidence is never deleted. Source archives remain reconstructible, including dirty/untracked inputs and deletions.

The control catalog retains independent stock captures with exact control-side identities, settings, coverage, and capture-code identity. Reuse requires runtime-absence proof, raw integrity, and completed cleanup. Unchanged-input controls are also keyed by original-forward callback and particle/RNG observations before correction. Selection never consults regenerated outcomes or tests multiple controls for rollback agreement. Startup incompatibility is reported separately from rollback divergence. Changed-input legacy cases retain their authored-history comparison rather than assuming the original and corrected startup histories are identical.

After investigating an interrupted or startup-incompatible comparison, `combat-restore --profile root-transform-417 --resume-candidate` revalidates the retained candidate and runs only the control side. The profile hash and diagnostic settings must match; existing binary, protocol, raw-log, and cleanup checks still apply. This does not relabel the earlier failed stage or authorize automatic retries until agreement.

## One hypothesis per defect

Record: failure signature; earliest cycle/tick; suspected producer or missing state; one change; distinguishing observation; local regression; short live profile; outcome and next decision.

Run targeted regression → required build → shortest native reproduction → integration suite → rolling30 → rolling600 at qualification checkpoints. Stop at the first failure; retry only after a fix, instrumentation change, or falsifiable hypothesis. Do not automatically stop work at an arbitrary task boundary: finish cleanup, retain evidence, and replace the compact current status at a coherent checkpoint.
