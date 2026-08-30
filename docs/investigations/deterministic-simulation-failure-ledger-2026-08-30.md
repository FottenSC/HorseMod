# Deterministic simulation qualification failure ledger

This ledger preserves authoritative failure causes and diagnostics separately
from candidate hashes. Every listed artifact is regression evidence only unless
an immutable release certificate explicitly promotes it.

## 2026-08-30 — confirmed-hit trigger was structurally impossible

- Source commit: `0278d46eee6cb57a37f6a3242194b08ea1b0aa60`
- HorseMod DLL SHA-256:
  `2DAADE7685D27B59EFFBCE19C9F6154257D012141DAF92813ABCC0E0F7DC1DB1`
- Authored replay map: **Silver Wolves’ Haven**
- Matrix row: `confirmed_hit`, depth 1, primary pass
- Raw log:
  `docs/investigations/evidence/release-0278d46e/offline/raw/astaroth-raphael-silver-wolves-haven__confirmed_hit__depth_1-primary.log`
- Terminal harness error:
  `forced depth-7 qualification did not reach a terminal result`
- Observed cause: the location-3 request predicate required both a battle-audio
  dispatch and a particle spawn. The replay exercised valid damaging hits, but
  its particle-spawn count remained zero, so the forced request could never be
  armed. This was a trigger-definition failure, not a canonical hash mismatch.
- Authoritative repair: observe
  `LuxBattle_ApplyDamageFromPendingHit @ 0x1402FF620` before it consumes and
  clears `g_pLuxBattlePendingHitAttacker`. Record ordered attacker slot,
  reaction move, and transition flags; reject unknown fighter roots or failed
  reads. Presentation audio and particles remain independently exact but do not
  define whether a resolved hit occurred.
- Required regression: rebuild an immutable schema-v47 candidate, rerun the
  earlier **Silver Wolves’ Haven** baseline and correction rows, then rerun the
  entire 51-row normal-render offline matrix.

## 2026-08-30 — resolved-hit evidence omitted from JSON serialization

- Source commit: `642e2ef266a6686d5ff5b2865d6407e51ca8d465`
- HorseMod DLL SHA-256:
  `9D2AD9084EFCBF6F38FAA08C3AD21FD72CDEB15EE1557F9FF1CB8D57F99BC45D`
- Authored replay map: **Silver Wolves’ Haven**
- Last completed matrix row:
  `near_round_start`, depth 1, primary pass
- Raw report:
  `docs/investigations/evidence/release-642e2ef2/offline/raw/astaroth-raphael-silver-wolves-haven__near_round_start__depth_1-primary.json`
- Simulation result before campaign interruption: 600 corrections, exact
  canonical convergence, zero capacity failures or growth events, 59.548
  active-replay FPS/TPS, correction p99 2450 microseconds and maximum 2759
  microseconds.
- Reporting failure: `GameplayRngCoverageEvidence` parsed
  `resolved_hit_calls` and `resolved_hit_sequence`, and the live confirmed-hit
  gate used the count, but `runner.py` manually serialized the older field set
  and omitted both values from the JSON report. This was an evidence-schema
  defect, not a simulation or canonical divergence.
- Cleanup: the campaign was interrupted before accepting further rows; SC6 was
  absent and `trace`, `correction_probe`, and
  `forced_depth7_qualification` were explicitly restored to `false`.
- Required regression: serialize both fields, freeze a new source identity,
  rebuild the unchanged simulation DLL with that identity, and restart the
  51-row matrix from its **Silver Wolves’ Haven** stock control.

## 2026-08-30 — stale particle requirement in presentation coverage

- Source commit: `0ffffa725af8c7a6c8ffa95f5affdafffdea6f37`
- HorseMod DLL SHA-256:
  `B6C80C67710FF679315218D3D1ADB6C36C1C2D30ECDFB4B37E907551FB8D812F`
- Authored replay map: **Silver Wolves’ Haven**
- Matrix row: `confirmed_hit`, depth 1, primary pass attempt
- Preserved bounded log:
  `docs/investigations/evidence/release-0ffffa72/offline/raw/astaroth-raphael-silver-wolves-haven__confirmed_hit__depth_1-primary.log`
- Terminal harness error:
  `forced correction native/presentation coverage is incomplete`
- Native correction result: the resolved-hit request armed at frame 1583 and
  completed 600 corrections across generations 6 through 10 with one round
  transition. Canonical convergence was exact; correction p99 was 2500
  microseconds and maximum was 2988 microseconds.
- Presentation evidence: 419 audio dispatches, 104 discarded speculative audio
  calls, 1276 audio terminal calls, 113 Blueprint audio calls, 600 verified
  audio batches, 600 verified camera batches, zero audio-sequence mismatches,
  zero camera-publication mismatches, zero presentation failures, and zero
  particle spawns.
- Authoritative cause: the native request trigger and Python gate used the new
  resolved-hit boundary, but the C++ aggregate presentation-coverage label
  retained the retired condition that location 3 must suppress at least one
  particle spawn. The authored match has no particle activity, so the label
  was structurally forced to `incomplete` despite exact presentation.
- Repair constraint: remove only the particle-*presence* requirement for
  confirmed-hit location. Ordered audio payload IDs, audio/camera batch
  identity, journal completion, zero pending payload bytes, zero capacity and
  publication failures, and exact handling of every particle that is actually
  authored remain mandatory.
- Cleanup: SC6 was absent, the temporary replay bridge was removed, and all
  diagnostic flags were restored to `false`.

## 2026-08-30 — round-end armed from pre-round lifetime activity

- Source commit: `c14fbefb3398233c27c663e5df12de272c1b65c9`
- HorseMod DLL SHA-256:
  `65A81D52C9671C00C523987BF1D616AB2B1048F3ADD483114CF57810774CE490`
- Authored replay map: **Silver Wolves’ Haven**
- Matrix row: `round_end`, depth 1, primary pass attempt
- Preserved bounded log:
  `docs/investigations/evidence/release-c14fbefb/offline/raw/astaroth-raphael-silver-wolves-haven__round_end__depth_1-primary.log`
- Terminal harness error:
  `forced correction native/presentation coverage is incomplete`
- Correction result: the request armed at generation 6/frame 980 and completed
  600 corrections through frame 1580 without a generation transition.
  Canonical convergence was exact; correction p99 was 2200 microseconds and
  maximum was 2241 microseconds. There were 600 verified audio batches, 600
  verified camera batches, zero presentation failures, zero capacity failures,
  zero capacity growth events, and no journal residue.
- Missing terminal evidence inside the correction window: zero suppressed stage
  wall calls, zero suppressed stage barrier calls, zero semantic stage
  dispatches, and zero suppressed battle-audio stop-all calls. The lifetime
  stop-all counter was already nonzero before the first stable authored replay
  frame, so location 4 incorrectly treated pre-round setup as a new round-end
  barrier.
- Native contract: `LuxBattleManager_Tick_MainStateMachine_At1461 @
  0x1403FBF30` executes the complete simulation worker first and only then calls
  the native round-over predicate. Lifetime presentation counters are not that
  native decision. The Ghidra plate contract now records this ordering and the
  need for a post-gameplay counter baseline; completeness is 85.54% raw / 98.07%
  effective with 1.93 fixable points, and the program was saved.
- Authoritative repair: capture wall, barrier, and stop-all counter baselines
  after the first stable replay frame and arm location 4 only on a subsequent
  authored terminal-event delta. Require a suppressed stop-all inside every
  round-end correction window. Wall/barrier events remain exact when authored,
  but the matrix must not require a map with no break actor to invent one.
- Cleanup: SC6 was absent and all diagnostic flags were restored to `false`.
- Required regression: build a new immutable candidate, rerun unit tests, rerun
  the **Silver Wolves’ Haven** round-end depth-1 primary/re-entry pair, then
  restart the complete 51-row normal-render matrix from its stock control.
