> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Supporting audit: rolling epoch transition and failure recovery

2026-09-22. Source investigation only, alongside the active ground-debris physics work. No production/test edits, build, deployment, game launch, or Ghidra mutation. This is a handoff, not a new acceptance receipt. Reconstructible inspected sources are indexed in [the evidence manifest](../../../evidence/rollback-support-epoch-recovery-2026-09-22.json). References below use the retained versions; the shared checkout can move.

## 1. Input-observation failure can bypass corrected-execution recovery

**Source-confirmed control-flow gap; native triggering/recovery consequences remain untested.** `Sc6ReplayHost.Rolling.inl:158–178` calls `FailRolling` for source/epoch, consumption order and binding failures. `FailRolling:3–11` only sets the rolling witness to Failed and logs; it does not set `seek_.cancel` or request recovery. During a scheduled correction this observer is enabled once `correction_installed` is true, while the seek still owns B.

The callback route is `Sc6ReplayExecutor.cpp:241` -> `Sc6ReplayHost.Index.inl:38` -> `ObserveRollingInput`. The executor callback returns void; that invocation does not itself turn the rolling failure into executor failure. `DriveRollingReplacement:181` also has no Failed-phase admission check, so it may continue replacement work after this notification. `ObserveRollingSeek:328` then immediately ignores a Failed rolling operation. In contrast, the existing replacement failure lambda at lines 186–194 sets `seek_.cancel` and deliberately postpones the terminal rolling receipt until recovery. `AdvanceSeek` at `Sc6ReplayHost.Seek.inl:332–354` uses that cancellation flag to retire provisional captures before undo.

The qualification observer at `ReplayRolling.inl:36` treats Failed as terminal. Its `ReplayQualificationMod.cpp:7453` failure handler attempts executor detach and publishes a failed result, rather than requesting seek cancellation. Process cleanup is containment, not proof of original-B recovery. This audit does **not** establish premature B destruction or partial-state resumption; existing ownership guards still apply.

Smallest follow-up: extend the existing production-extraction regression route in `test_replay_run.py` to inject an input-observation error after correction installation, first with no provisional checkpoint and then with provisional retirement pending. Assert that the first failure is retained, cancellation is requested, the rolling observer stays nonterminal while GPU/capture retirement and B undo are pending, B revision/window remain unchanged, and only recovered completion becomes terminal. Include an error before A publication to distinguish safe immediate rejection. Do not implement by blindly cancelling an already irreversible commit.

## 2. Round rejection exists; rolling reset/drain/re-warm is missing

**Confirmed implementation gap, already consistent with the open epoch/reset gate.** `ReplaySourceState::SameRecording` compares round and tracker-active state in addition to replay identity. `StoreRollingCheckpoint:14` therefore rejects a new-round checkpoint against the initial `rolling_.source`; scheduled input observation may reject it earlier. Neither path drains the old rolling window and establishes a new seven-tick baseline.

`Sc6ReplayHost.hpp:143–144` exposes Idle/Advancing/Capturing/Ready/Seeking/Retiring/Complete/Failed and Begin/Read/Release only. `RollingOperation:112–116` accepts Release only from Complete. A failure with populated checkpoint slots cannot use that release action. Begin requires Idle. Do not describe a new-round rejection as successful epoch reset.

The separate session-exit service recovers/releases a seek and retires the index (`Sc6ReplayHost.SessionExit.inl:211–242`), but contains no rolling-window drain. `Sc6ReplayHost::Stop:351–359` rejects live retained checkpoint references. Thus the existing exit implementation alone is not evidence that a failed or interrupted rolling campaign can drain its own ring and exit. Exact native manifestation is untested; do not bypass Stop's guard or erase borrowed/in-flight references to make it pass.

Smallest follow-up: use the existing rolling-window and session-exit production-boundary fixtures. Cover epoch change during warm-up, ordinary forward execution, correction execution and post-commit retirement. Retain a separate operation pin across ring drain; verify GPU completion before native retirement, no further old-epoch admission, and seven fresh native ticks before the first new-epoch rollback. For a destroyed session, require safe termination/reload rather than restoring old owners. A native round-transition continuation is still required after local ownership tests.

## 3. Changed-cycle receipt reports retirement complete before its new retirement request is drained

**Confirmed reporting-order mismatch; no demonstrated resource-lifetime violation.** `CommitRollingReplacement:88–91` drops the old window and sets `seek_retirement_pending_=true`. In the same `ObserveRollingSeek` invocation, lines 350–355 call that function, increment the cycle count, and print `retirement_complete=true` without waiting for the newly requested retirement. The application owner may correctly join retirement later; this line is not proof that it has already done so. Its `correction_us` also ends before that later work.

Smallest follow-up: extend the existing rolling receipt/commit tests with a successful changed replacement whose retirement remains pending. Assert that the receipt distinguishes transaction release from old-window retirement, and record completion/cost at the actual retirement boundary. Preserve end-to-end full-update accounting; do not sum overlapping intervals or reinterpret correction time as full rollback-update time.

## Priority and qualification

Address item 1 before deliberate changed-input failure/recovery qualification. Item 2 is a prerequisite for the plan's epoch-reset and scene-exit gate, not a reason to divert the active physics reconstruction experiment. Item 3 matters for honest ownership/performance evidence. The earlier cinematic-recorder coverage question in [the round-end audit](round-end-replay-rollback-coverage-2026-09-15.md) remains separate and was not re-investigated here.

No simulation, observer-validity, lifecycle, recovery, epoch/reset, presentation, memory or performance gate was promoted. No tests were run: all findings above are source observations or explicitly labelled untested consequences. Proposed regressions should be added to the existing runner/fixtures, not a parallel harness.
