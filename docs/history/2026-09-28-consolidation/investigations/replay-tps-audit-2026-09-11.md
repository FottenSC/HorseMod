> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Replay TPS harness audit — 2026-09-11

The recent logs contain a real, brief cadence interruption around initial checkpoint A170 capture and private replay-output preparation. Recovery playback subsequently runs near 60 TPS. Some harness rates are correctly calculated but cover only that later window; the aggregate trajectory rate is misleading across rewinds. The older strict-seek accumulator can actually overreport TPS by dropping stalled polling time.

This was an offline audit. No game was launched and no production/harness behavior was changed. Reproducible analysis and source-formula reproduction: `artifacts/replay-tps-audit-20260911/analyze.py`; machine-readable results: `artifacts/replay-tps-audit-20260911/cadence.json`. All three analyzed log hashes match the retained live-recovery audit. The current observer source exactly matches its retained executed source archive (SHA-256 `913ba517868d77c96390f7d1b4e34cc74fe698204a7b74f0febe240c19f5f177`).

## Findings

1. **P1 — strict-seek resume rate drops same-tick waiting time.** `tools/replay_qualification_mod/ReplayQualificationMod.cpp:6262–6282` only adds elapsed time when `frame > seek_resume_last_observed_frame_`, but updates the previous timestamp on every poll. Active-phase polls that see an unchanged frame discard their elapsed time. Reproduction of those exact accumulator operations: 100 unchanged polls over one second, followed by 120 ticks over two seconds, reports 60 TPS instead of the wall-clock 40 TPS. This affects the older `replay-entry` strict-seek route; the recent `combat-restore` world-resume timer does not use this accumulator. An additional approximation prorates a multi-tick observation's elapsed time when it crosses the requested window endpoint. Fix direction: use a fixed monotonic start and actual observed tick delta, retaining every no-progress interval; explicitly describe any overshot endpoint rather than inventing a partial interval's timing.

2. **P2 — aggregate trajectory TPS is not valid playback throughput after a rewind.** `ReplayQualificationMod.cpp:4823–4832` subtracts the initial logical tick from the final logical tick, while elapsed time and viewport counters continue through repeated execution, capture, held UI updates and recovery. The latest candidate reports `native_ticks=2703`, `elapsed_us=62043895`, `tick_rate_milli=43565`, and `viewport_fps_milli=133502`. These are arithmetically consistent but neither 43.565 TPS nor 133.502 viewport updates/s describes ordinary resumed gameplay. `tools/deterministic_qualification/replay_fidelity.py:83–98` checks arithmetic, not whether the counter/time scopes are meaningful across seeks. The dedicated recovery gate correctly uses its separate window. Fix direction: label this aggregate as mixed-phase diagnostic data, and measure normal playback, fast-forward work, deliberate holds, capture and recovery separately. Physical executed ticks alone would still produce a mixed-phase average unless the phases are separated.

3. **P2 — a passing resume average misses the observed startup hitch.** `ReplayQualificationMod.cpp:3998–4005` deliberately starts the recovery clock after seek release, and `:1623–1642` stops after 120 ticks. This is appropriate for resumed playback, but says nothing about A170 capture or earlier UI setup. The indexed-seek equivalent (`:4713–4737`, `tools/replay_test.py:218–243`) also measures only the first 120 resumed ticks, even when the continuation covers 600. An average alone cannot establish absence of transient hitches. World-resume logs additionally record maximum gap and gaps over 20 ms, but the pass expression at `tools/replay_test.py:1289–1292` only checks average tick/viewport rates. Keep correctness and pacing outcomes separate; report short-window rates and gap statistics over each normal-playback phase, and report intentional capture/hold interruption duration explicitly.

## Retained log evidence

Log paths below are under `build_cmake_LessEqual421__Shipping__Win64/replay-tests/`.

| Run/phase | Measurement | Interpretation |
| --- | --- | --- |
| `trace-reopen-recovery-candidate.UE4SS.log`, initial traversal | Tick 169→170: 62.000 ms; 170→171: 179.709 ms; 172→173: 78.085 ms | Brief interruption at 23:29:57, around A170 capture/output preparation |
| Same initial traversal | Worst 30-tick window 169→199: 38.5405 TPS; worst 120-tick window 89→209: 52.5504 TPS | The hitch is real in observation wall time; these are window averages, not sustained rates |
| `trace-late-owner-partition.UE4SS.log`, initial traversal | 169→170: 59.152 ms; 170→171: 162.739 ms; 172→173: 64.109 ms; worst 30-tick window: 40.7304 TPS | Similar interruption in the next retained candidate at 23:34:24 |
| `trace-reopen-recovery-native.UE4SS.log` | Overall 59.990 TPS; worst 30-tick window 58.9218 TPS; 170→171: 25.042 ms | Independent native control has a smaller disturbance near the same tick |
| Recovery candidate, first 120 resumed ticks | 120 / 1.999772 s = 60.0068 TPS; maximum gap 18.333 ms; zero gaps over 20 ms | Dedicated monotonic-clock recovery result is correct |
| Recovery candidate, all logged post-recovery samples | Ticks 2505→2747; worst 30-tick window 59.7631 TPS; worst 120-tick window 59.9735 TPS; maximum adjacent gap 18.333 ms | No comparable slowdown in the roughly four seconds recorded after recovery |

Candidate lines 2519–2521 prepare/arm the private replay output. Lines 2591–2594 show capture readiness and `historical capture cost ... elapsed_us=140223`. Line 2597 retires the private output; it is prepared again near line 2694 before tick 173. These operations overlap the observed hitches. The logged capture timer starts after surface readiness (`ReplayQualificationMod.cpp:2840–2857`), so its 140.223 ms does not cover the entire visible interruption. The second candidate similarly logs 124.058 ms capture. This strongly supports capture/window setup as the immediate cause; these logs do not profile every millisecond or prove which operation accounts for all overhead.

The ~1.009-second gap just before the target's engine-post observation is the explicitly deliberate one-second held-target test. It must not be classified as a spontaneous gameplay stall. C5750 request-to-ready is 15.421 seconds; the C11000 attempt is 29.197 seconds and fails settlement, leaving no resumed-playback timing evidence for that attempt.

## Checks and limits

`python -m pytest tools/deterministic_qualification/tests/test_replay_run.py tools/deterministic_qualification/tests/test_trace_parser.py -q`: **117 passed**. These existing tests validate parsing and selected rate cases; they do not disprove the accumulator defect or certify transient smoothness.

The audit script calculates tick deltas over timestamp deltas, splits traversals on counter repeats/rewinds, retains capture/hold time rather than silently deleting it, and searches 30/60/120-tick windows. It uses log wall timestamps (diagnostic, subject to logging latency/clock adjustment), while the dedicated C++ timers use `steady_clock`. The close agreement of post-recovery cadence and the monotonic measurement supports the conclusion. Engine-post observation cadence is not a per-present GPU frame-time trace, so this audit cannot establish complete visual smoothness or identify the exact moment the user noticed.
