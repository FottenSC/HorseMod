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

