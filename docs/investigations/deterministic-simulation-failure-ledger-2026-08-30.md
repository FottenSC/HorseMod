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
