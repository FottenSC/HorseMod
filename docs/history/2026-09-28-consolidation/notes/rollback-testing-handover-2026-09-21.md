> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Handover to the rollback/replay implementation agent

The testing workflow has changed. Use the existing runner and its new profiles, preflight, focused tests, control catalog, and evidence manifests. Do not resume the old pattern of rebuilding, running the full suite, and reconstructing a long live command for each diagnostic adjustment.

This handover describes the last verified checkpoint, not a fresh inspection of the game installation. Run preflight before your next live experiment. Work in `E:/myMods` and `build_cmake_LessEqual421__Shipping__Win64`; preserve unrelated dirty work. No new checkout, harness, task, or production rollback rewrite is needed.

## Read this much first

1. [AGENTS.md](../../../../AGENTS.md) for durable rules.
2. [Active depth-seven plan](../../../rollback-depth-seven-plan.md) for acceptance criteria.
3. [Current status](../../../rollback-status.md), now approximately 4 KB, for the blocker and next experiment.
4. [Testing commands](replay-testing.md) when selecting a run.

Do not load the full historical transcript or archived status into working context. The former 206,040-byte status is preserved intact at [archived status](rollback-status-history-2026-09-20.md), with its original relative evidence links. Search a specific signature or read a bounded section only when the current investigation requires it. Treat historical notes as evidence, not current instructions.

## Verified starting point

- `root-transform-417` independently passed 417→424 and 120 continuation ticks. Candidate `replay-9affe1fc7cf043dbb1dc6547187efa64` reused compatible stock `replay-d403b758ab5b42528d21d54250a1ecbd`; both cleanups completed. [Immutable pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f114b901b9b15173b31d62fd228f78bbcb4b61140f564a8c438f9f844541e3c3.json).
- Runtime identity for that pass: `fa15ba3c69966ee87839977889a8a8975c1e7c25ac8e2ae2771e69a52d4f99bf`. Verify provenance against the current checkout rather than assuming it remains current.
- The integration checkpoint passed **519 Python tests** and native boundary checks. After the final reporting change, **123 runner tests** passed. These are separate verification points, not a claim that the full suite was rerun after every final edit. [Verification index](testing-improvements-2026-09-20.md).
- Rolling30 failed at its **first transaction**, native tick 217, before A publication. Complete-B transient capture required **89,821,158 bytes**, with **69,233,874 available** under the 1 GiB ceiling; shortfall **20,587,284 bytes**. Already-owned bytes were **1,004,507,950**. Recovery completed at B=217; cleanup reported zero games. [First-failure bundle](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/5f9bdcf570444cbc1f0f76f6808af50fce2365fd3c913c55a8a0528eaae3512f.json).
- Rolling600 was not attempted after that failure. The short transaction does not prove the preceding 208 rolling cycles, rolling qualification, or the remaining changed-input/recovery/coherence/performance gates.
- The stale Steam launch and missing loader were resolved. The stale launch was retired under explicit user authorization, **not** observed Steam cancellation. Last preflight was ready. Do not treat that one-time override as a general recovery rule.

## Replace the old operating loop

Use one hypothesis card per defect. Keep it small and update its outcome rather than accumulating competing “Current” sections:

```text
Failure signature:
Earliest failing cycle/tick and traversal:
Suspected producer or missing state:
One proposed change:
Expected distinguishing observation:
Production-boundary local regression:
Shortest named live reproduction:
Outcome and next decision:
```

Follow this sequence:

1. Read current evidence before rerunning anything. Run observational preflight before a live build or launch. Resolve journal problems through the existing resource owner; offline analysis/tests can continue while live admission is blocked.
2. For a locally reproducible defect, add a regression at the production boundary that fails before the fix. Select the affected local group(s), not the full suite by habit.
3. Make one justified change. Investigate native consumers and ownership first where behavior depends on them. Keep Ghidra work bounded to the dependency blocking this hypothesis.
4. Build only affected native work when necessary. Observer/native instrumentation changes require rebuilding; Python reporting changes can reuse binaries only when source/build provenance verifies them. Mandatory native boundary checks still run before live execution.
5. Run the shortest representative named profile. Stop the experiment at its first failure. Investigate before retrying; a specific fix, instrumentation change, or falsifiable hypothesis must justify the next attempt.
6. Inspect the generated first-failure bundle and cleanup. Keep recovery work in the same investigation; a failed run is not finished until its deployment/process state is accounted for.
7. At an integration/qualification checkpoint, run the full suite, then rolling30, then rolling600. Qualification profiles already invoke integration tests; do not reflexively run a duplicate full suite immediately before them. Rolling600 requires a retained rolling30 pass on matching binaries and the current rolling30 profile.
8. Replace the compact status with the outcome and exact next decision. Continue routine authorized work; do not stop simply because one tool failed. Conversely, do not expand into a new defect or production change without checking task scope.

The review's recommendation to stop a turn or start a fresh task is a context-management option, not a mandatory interruption after every experiment. Finish cleanup and leave a coherent handoff. A fresh task should receive these compact documents, not the entire old transcript; create one only if the user asks.

## Commands to use

Run from `E:/myMods`:

```powershell
python tools/replay_test.py preflight --profile root-transform-417
python tools/replay_test.py local --group ownership
python tools/replay_test.py local --group root-transforms
python tools/replay_test.py local --group contact-latches
python tools/replay_test.py local --group runner
python tools/replay_test.py local --group auto --changed-path HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp --list-tests
python tools/replay_test.py local --group all
```

The checked-in [source/test map](../../../../tools/deterministic_qualification/replay_test_groups.json) uses exact test identifiers and dependent groups. Unmapped production changes or an unknown change set fall back to the full suite. Supply the actual changed paths for this investigation; do not disguise shared ownership changes as a narrow parser change. Do not edit source while tests/builds are retaining and checking a frozen snapshot.

| Profile | Purpose | Use |
|---|---|---|
| `root-transform-417` | Diagnostic | One 417→424 transaction plus 120 continuation ticks. |
| `rolling-cycle42-repro` | Diagnostic | Existing 48-cycle reproduction; focused tracing around the cycle-42 window. |
| `rolling30-gate` | Qualification checkpoint | 30 consecutive corrections plus independent continuation. |
| `rolling600-qualification` | Qualification checkpoint | 600 corrections, after a compatible rolling30 pass. |

Launch with `python tools/replay_test.py combat-restore --profile <name>`. Profile JSON records setup interventions, anchors, cycle count, diagnostics, budget, timeout, and comparison purpose. Raw profile bytes, hash, and resolved settings are retained. Conflicting behavioral CLI overrides reject. Create an explicit reviewed profile variant for a different experiment instead of silently weakening one of these.

There is **no named cycle-208 rolling profile**. The root-transform profile isolates the retained late transaction; it cannot replace the preceding rolling history when that history matters. Do not claim arbitrary cycle counts are supported merely because profiles exist.

After an investigated control interruption, the existing candidate-resume route can be used as:

```powershell
python tools/replay_test.py combat-restore --profile root-transform-417 --resume-candidate
```

This validates the retained profile/diagnostics, binary identities, protocol, raw-log integrity, and cleanup. It is not blanket resume support for every failed campaign. Inspect which candidate the working report references: later runs overwrite working aliases, while immutable manifests preserve prior evidence.

## Diagnose once, retain enough evidence

Optional tracing is runtime-selectable through the existing owners: stack depth `0`, `8`, or `16`; subsystem mask audio=1, RNG=2, particles=4, presentation=8; tick window and byte limit. Changing those selections does not require another native compile. Profiles own their selections, so use a profile variant when changing them.

Diagnostic storage is bounded and accounted for. Overflow invalidates completeness. Required correctness observations remain enabled independently. The tooling extracts bounded context from already-recorded logs, preserving available traversal identities; it does **not** promise every subsystem field or a universal two-tick post-failure trace. Missing evidence is explicitly unavailable. Never advance a failed simulation just to obtain following context.

Native rejection and offline comparison are different detection events. The new capacity extractor records the first native rejection, cycle/tick, required/available bytes and phase. An offline mismatch does not imply the game stopped at that tick. Generated evidence does not automatically prove recovery, coherence or performance; keep the separate result dimensions and “not measured” values.

## Independent controls: reuse compatibility, never outcomes

Use the existing catalog. Compatibility covers game/framework/observer/native dependencies/replay, authored-input settings, setup interventions, capture protocol/settings, coverage and relevant capture code. Candidate runtime identity can be excluded only when stock evidence proves that runtime was absent. Raw logs and complete cleanup are mandatory.

Startup variation is real: independent native loading may consume an extra stage-particle update before simulation. Matching callbacks alone did not detect it; original-forward particle time and shared RNG did. Reject incompatible startup before classifying later differences as rollback failures. Never reset state, filter differing lifecycle events, or select a control because regenerated results happen to agree.

The passing short test reused a control cataloged **before** its candidate and selected by original-forward compatibility. Prior incompatible attempts remain failures. See [startup hypothesis](testing-startup-hypothesis.md). Automatic retry-until-match is not implemented or authorized. Incompatible controls are retained as evidence; do not assume every failed pairing is automatically indexed for future reuse.

## Keep context and evidence compact

- Keep `rollback-status.md` at roughly 2–5 KB: one blocker, last independent pass and identity, exact next experiment/hypothesis, deployment state, acceptance matrix, history links. Replace it; never prepend another “Current” section. Do not move progress back into AGENTS.md.
- The runner generates factual summaries and immutable manifests under `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence`. It does **not** automatically write the hypothesis or replace the current-status document; that editorial step remains yours.
- Link immutable manifests and their content-addressed logs. Working `combat-restore-*.json` and raw-log paths are aliases, not durable evidence links.
- Retain distinct first failures, failing-before/passing-after regressions, final native passes, qualification and unresolved recovery evidence. Deduplication is prospective; no automatic historical deletion was introduced.
- Source archives preserve reconstructible dirty/untracked source contents. Do not create redundant project copies or repeatedly dump whole raw logs into context. Search signatures, then read the smallest useful interval.

## First assignment for the rollback implementation agent

Investigate the cycle-1 ownership rejection at tick 217 from retained evidence before another launch. Inspect the simultaneous rolling window, complete B, checkpoint scratch, native/GPU and deferred ownership around `Sc6ReplayHost.Checkpoint.inl`'s `transient_capture` admission and `Sc6ReplayHost.Restore.inl`'s complete-B preparation. The logs show retained checkpoint ownership reaching 926,383,071 bytes at tick 217 before B capture grows the total.

First distinguish a real storage requirement from duplication, excessive reservation, or incorrect accounting; none is established yet. Do not merely subtract the shortfall, free in-flight resources, release B early, suppress required callbacks, or raise the qualification cap. A diagnostic 2 GiB allowance cannot establish the production 1 GiB gate.

Production fixes were outside this testing-improvement task. If your existing rollback assignment already authorizes ownership fixes, proceed under that authorization; do not ask again solely because this tooling task had a narrower scope. Otherwise resolve the scope question before editing production behavior. After a regression and fix, use the shortest representative rolling reproduction, then the integration/30/600 sequence. Preserve the separate subsequent gates for changed inputs, B recovery/cancellation, epoch reset, normal-renderer coherence, complete ownership and full-update performance.
