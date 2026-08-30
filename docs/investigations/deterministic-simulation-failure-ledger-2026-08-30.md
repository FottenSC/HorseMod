# Deterministic simulation qualification failure ledger

This ledger preserves authoritative failure causes and diagnostics separately
from candidate hashes. Every listed artifact is regression evidence only unless
an immutable release certificate explicitly promotes it.

## 2026-08-30 — general battle-event terminal lacked source-scoped ownership

- Source commit: `b3b79de0a1c46d5ad6e007c3d606230487e5f9ac`.
- HorseMod DLL SHA-256:
  `F130B4F0788C77B2DD7D320C37FB73C7F607766C98992F3124B13CD53B04279E`.
- Authored replay map: **Silver Wolves’ Haven**.
- Stock four-round control passed at 60.082 normal-render FPS/TPS, but paired
  baseline A failed before presentation identity publication with
  `horsemod_presentation_coverage_api_unavailable`.
- First native failure: batch 3, frame 0→3, one terminal call from return RVA
  `0x519789`, unresolved owner `0x22714D89580`, graph stage 1, epoch/bindings
  zero; the frame fencepost then failed with `presentation_failed`.
- Authoritative cause: `0x519789` is the call return inside
  `LuxBattleManager_DispatchBattleEventByClass @ 0x140519480`, not a generic
  terminal. The dispatcher can author its first voice before the process-wide
  CRI/BGM owner roots are published, while the implementation recognized only
  the distinct character-cue source return `0x519A6D`.
- Repair: carry the dispatcher's exact live class/shared-player selection to
  its synchronous terminal, admit the complete battle-manager subgraph even
  while optional CRI/BGM roots are unavailable, and preserve a playback mapping
  across the later expanded provenance epoch only when the selector still maps
  to the same native owner. The failed DLL and stock control are non-certifying.
- Required regression: schema-v49 unit tests and a new immutable candidate,
  followed by a fresh **Silver Wolves’ Haven** stock control and paired exact
  normal-render baselines before any correction request.

## 2026-08-30 — audio generation was bound before initial admission

- Source commit: `3097bdaafc64ab5161f4bafe435db3bb4ae20940`.
- HorseMod DLL SHA-256:
  `44F49F6A99713AFFAB15EA1D56398922722DEE45E8722948C0BC78802C040A30`.
- Authored replay map: **Silver Wolves’ Haven**.
- Paired baseline A failed before identity publication with
  `horsemod_presentation_coverage_api_unavailable`.
- First native failure: batch 3, frame 0→3, audio owner graph stage 1,
  epoch/bindings zero, unresolved terminal caller `0x519789`; the frame
  fencepost then failed with `presentation_failed`.
- Authoritative cause: the new graph gate required a nonzero replay generation,
  but graph preparation ran before `ObserveOuterTickBegin` admitted the initial
  generation from captured pre-tick state.
- Repair: admit the generation in the begin callback, publish it to the audio
  owner, and only then construct the exact-generation graph before native tick
  execution. The failed DLL and its stock control are non-certifying.

## 2026-08-30 — pre-tick audio graph assigned an exact cue to the wrong owner

- Audited ancestor commit: `dc3cc3d9`; the defect remained present in candidate
  commit `ef3884cc350a2f85fda16de98c094bfdc73d76a1`.
- Invalidated candidate DLL SHA-256:
  `3E125C248AF27753CA1E31783871E7F2080C07871F1DC4E70108412C60B005A5`.
- Authored replay map: **Silver Wolves’ Haven**.
- Paired normal-render baseline: 3,463 terminal events on each run. The first
  mismatch was global terminal 39; operation, order, cue, and route matched,
  but the character-cue family/slot identity was 7 versus 6. Forty-seven
  terminal records differed.
- Authoritative cause: the outer-tick graph sampled the character-player array
  before `LuxAudio_ResolveAndPlayCharaCue @ 0x140519970` selected its live entry.
  The terminal pointer was known, so the miss-only refresh path never ran and
  silently associated an exact cue payload with the wrong logical owner.
- Additional accepted defects: a local duplicate-pointer shortcut ignored a
  conflicting selector; epoch reuse compared bindings without manager,
  container, generation, or lifetime provenance; stage identity invalidation
  also cleared unrelated audio state; wind canonicalization could return a
  partial prefix for an invalid bank/count; and local wind-root restore omitted
  `+0xA8..+0xAF`.
- Authoritative repair: hook the synchronous semantic resolver, capture exact
  `bMode`, authored cue family, selected owner, and live array only for that
  call, and fail closed on terminal mismatch. Reuse an audio epoch only when
  generation, managers, containers, counts, and exact bindings all match.
  Validate wind images before emitting bytes and restore the complete local
  root image.
- Required regression: schema-v48 unit tests, then a paired exact-audio
  normal-render baseline on **Silver Wolves’ Haven** before any correction
  matrix row. All evidence from the invalidated candidate remains
  non-certifying regression evidence.

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

## 2026-08-30 — real round-end delta exposed missing new-generation history

- Source commit: `49407bc20671d62f5db1b6820623f456d714e563`
- HorseMod DLL SHA-256:
  `8D02EEE4A93A9F803FCDB5942ED551C4AF1B0529C49D642F71A125595D27A9D9`
- Replay bridge SHA-256:
  `4D123DB9E7EAC882DDEFFBB5F1A550B44FCFB5D30053EDE784687ED21F42E84F`
- Authored replay map: **Silver Wolves’ Haven**
- Targeted row: `round_end`, depth 1
- Preserved log:
  `docs/investigations/evidence/release-49407bc2/targeted-round-end-depth-1-generation-mismatch.log`
- Terminal harness error:
  `forced depth-7 qualification failed: result=failed completed=0 status=generation_mismatch`
- Boundary evidence: the repaired counter baseline did not arm at the stale
  setup value or frame 980. It armed only after round 1's real terminal
  stop-all, at generation 11/frame 2372. The first correction preflight then
  rejected because the new generation had not yet accumulated a same-generation
  checkpoint. No restore or resimulation operation ran; all restore masks and
  replayed-coordinate counts remained zero.
- Authoritative cause: the qualification already waited for insufficient
  history after a generation changed while an active run was in progress, but
  did not enter that waiting state when the run was first armed on the exact
  generation-transition fencepost. Treating this as terminal would either fail
  a correct barrier or tempt an illegal cross-generation restore.
- Authoritative repair: latch the post-baseline source stop-all, mark a newly
  armed run as awaiting generation history, and remain fail-closed until the
  first same-generation correction preflight succeeds. Report the source
  terminal latch explicitly and require it in the Python round-end gate. The
  terminal event remains part of exact source presentation identity; the
  correction window begins after the barrier and never restores the prior
  generation.
- Cleanup: SC6 was absent and all diagnostic flags were restored to `false`.
- Required regression: rebuild HorseMod and the configure-time-bound replay
  bridge from the new commit, pass all local tests, rerun the focused
  **Silver Wolves’ Haven** round-end depth-1 primary/re-entry pair, and restart
  the complete 51-row matrix from the stock control.

## 2026-08-30 — replay teardown erased terminal presentation evidence

- Source commits: `38c16b25` and repaired candidate `41d77da2`.
- Repaired HorseMod DLL SHA-256:
  `FBEB9B4E7E99FF89D293C475E0EA0C2F0B6095415FDD5CBFBFBD47C29D91DA61`.
- Authored replay map: **Silver Wolves’ Haven**.
- Symptom: a complete normal-render replay could finish with exact canonical
  coordinate `17:8284`, yet the bridge subsequently reported
  `horsemod_presentation_coverage_api_unavailable` after replay exit.
- Authoritative cause: replay-exit identity deduplication ran before the
  service captured its terminal value-only health snapshot. Teardown therefore
  removed the native owner needed to answer the final presentation query.
- Repair: capture terminal qualification evidence before exit identity
  deduplication. Three paired full replays and a three-cycle same-process
  re-entry campaign then preserved exact canonical and presentation evidence.
- A second occurrence in the `41d77da2` offline campaign had a different cause:
  both repeated baselines shared one `armed_baseline` scope, but the first run
  correctly disarmed diagnostics. The second automatic smoke restored that
  already-disarmed config and launched the full replay without deterministic
  hooks. Candidate `fc8ff245` rearms each process independently.
- No canonical divergence was observed. All listed runs remain non-certifying.

## 2026-08-30 — offline evaluator used synthetic replay metadata and legacy config

- Source commit: `fc8ff245b174e961dd3a8144d85c6549c1390454`.
- HorseMod DLL SHA-256:
  `2A00104E1D169D898BDF6C6C8541A294097008C152358B54500AC533346A4EC2`.
- Authored replay map: **Silver Wolves’ Haven**.
- Completed live rows included the stock baseline, every near-round-start and
  active-combat depth, confirmed-hit depth 1, and confirmed-hit depth 6 primary.
  Their subprocess gates passed, including exact canonical convergence and
  presentation coverage, but the aggregate evaluator marked every completed
  row failed.
- Authoritative metadata cause: the manifest recorded map 111 for stage 273
  and derived fighter indices by subtracting one from launch codes. The native
  importer reports stage 273/map 17 and fighters 12/14; launch code `012` is
  already native Astaroth index 12, while other launch codes are one-based.
  Launch-selection codes therefore cannot serve as replay metadata.
- Authoritative config cause: the installed config retained ignored legacy
  fields `rollback_depth` and `local_player_location`. Native `LoadConfig`
  ignores them, while certification deliberately requires the exact nine-field
  native contract.
- Repair: bind explicit stage/map/fighter replay metadata in the candidate
  manifest and canonicalize certifying configs to the nine fields understood
  by the native loader. The evaluator remains strict; no gate was weakened.
- The interrupted confirmed-hit depth-6 re-entry was cleaned up without
  accepting evidence: SC6 was absent, the request and temporary bridge were
  removed, and all diagnostic flags were restored to `false`.

## 2026-08-30 — JSON key ordering could falsely reject the native config contract

- Source commit: `945ded3d65d13e8a0df58ef45a075dfa71adf012`.
- HorseMod DLL SHA-256:
  `A01342449533188182A0089B2551DF5D317824EA556E5BA0A3ED0F9562420D3E`.
- Authored replay map: **Silver Wolves’ Haven**.
- The fresh stock control passed at 60.08 normal-render FPS/TPS with native
  stage 273/map 17 and Astaroth 12/Raphael 14, proving the preceding manifest
  correction. Before a combined matrix row was accepted, source inspection
  found that `write_report` sorts every JSON object key while
  `is_exact_contract` required Python dictionary iteration order to match INI
  file order. Every deserialized certifying report was therefore guaranteed to
  fail despite exact native fields and values.
- Authoritative repair: compare JSON config projections as unordered exact
  field/value mappings, and separately require the SHA-256 of the canonical
  LF-terminated nine-line native config. This preserves and strengthens the
  byte-order contract without assigning semantics to JSON object order.
  Offline, strict-seek, Tira, and paired-online release gates all bind the
  canonical config hash; paired deployment canonicalizes both peer configs.
- The campaign was stopped before accepting a row. SC6 exited through WM_CLOSE,
  the temporary bridge and request were removed, and all diagnostic flags were
  restored to `false`. The stock artifact is non-certifying regression evidence.

## 2026-08-30 — deterministic replay missed the unchanged 58 Hz gate

- Source commit: `496798b983ed70a501569d1082a13587af3d8a5d`.
- HorseMod DLL SHA-256:
  `3EE1D9064D244C06BF2F4F891A014C6162EDD41ED79A4C7D934698445ABCA6EA`.
- Authored replay map: **Silver Wolves’ Haven**.
- Two automatic deterministic smoke attempts failed only the unchanged 58 Hz
  performance threshold: 120 active frames measured 55.851 and 57.178 Hz.
  Both had valid native ownership/checkpoint activity and no canonical,
  presentation, RNG, or lifecycle failure. Their same-process stock controls
  measured 60.08 Hz.
- A diagnostic build then excluded the first 30 active frames while leaving
  the threshold unchanged. The next 90 frames still measured only 56.366 Hz,
  disproving a round-entry-only timing bias; that workaround was removed before
  freeze.
- The native log reports roughly 109 MB per candidate image and a 5.02 ms
  checkpoint-store p99 during initialization, while capture itself remains
  about 0.15–0.23 ms. The authoritative storage/copy path must be reconstructed
  and optimized before another candidate can pass. The release threshold and
  untrimmed timing intervals remain unchanged.
- Both failed attempts cleaned up completely: SC6 absent, temporary bridge and
  requests absent, and every diagnostic flag restored to `false`.

## 2026-08-31 — baseline retained correction history it never consumed

- Source commit: `496798b983ed70a501569d1082a13587af3d8a5d` plus the recorded
  deterministic dirty patch; all evidence remains non-certifying.
- Authored replay map: **Silver Wolves' Haven**.
- A 600-frame diagnostic measured 59.337 Hz and showed that the expensive path
  was retained rollback history and startup storage, not canonical capture.
- Repair: ordinary smoke/baseline requests bind the complete native candidate
  surface and continue canonical, RNG, camera, audio, ownership, and lifecycle
  observation without retaining restorable checkpoint history. Seek requests,
  forced corrections, and online predicted-player ownership override that
  policy and retain the full bounded history.
- Regression evidence: a three-cycle persistent-process campaign passed at
  60.383–60.387 Hz; a complete **Silver Wolves' Haven** authored-outcome
  baseline passed at 60.083 Hz with exact final canonical coordinate `21:8284`,
  8,280 presentation batches, 17,476 audio events, 20,881 ordered events, zero
  capacity/presentation failures, and clean teardown. A separate active-combat
  depth-1 run retained 528,461,218 owned bytes, completed 600 corrections, and
  converged exactly at 59.621 Hz, proving correction history was not weakened.
- Harness lifecycle defect found during this repair: direct
  `replay-entry --deterministic-baseline` armed only its automatic smoke and
  restored `trace=false` before the full process. One outer reversible baseline
  scope now owns both processes; a regression test verifies both see
  `trace=true` and the caller's exact config bytes are restored afterward.

## 2026-08-31 — first strict seek allocated transactional verification scratch

- Source commit: `496798b983ed70a501569d1082a13587af3d8a5d` plus the recorded
  deterministic dirty patch; all evidence remains non-certifying.
- Authored replay map: **Snow-Capped Showdown**.
- First attempt reached active replay frame 600 and restored the 10% target at
  generation 6/frame 1024, then fail-fast rejected `growth_events=1`. Canonical,
  timeline, ownership, presentation, and RNG failure fields were all zero.
- Authoritative cause: native-region, motion-bank, and move-dispatch
  transactional undo/verification buffers were prepared lazily by the first
  restore, after the active qualification window had established its capacity
  baseline.
- Repair: reserve every bounded transactional scratch owner during native bind.
  The identical normal-render 10/25/50/75% run then passed with zero capacity
  growth, exact canonical convergence, and clean teardown. The four seeks used
  7, 7, 13, and 1 resimulation coordinates; validation took 4.877, 4.312,
  5.727, and 2.907 milliseconds; all 120-frame rate windows were approximately
  60 Hz.

## 2026-08-31 — matrix orchestration discarded reusable captures

- The previous campaign keyed every row to one aggregate Python-package hash
  and had no resume check. An evaluator-only metadata repair therefore made
  mechanically valid raw captures unusable, while an interruption restarted
  already completed clean-process rows.
- Repair: raw capture identity is now separate from evaluator identity. Reuse
  requires exact HorseMod DLL, schema, capture harness, qualification bridge,
  config bytes, authored replay, and game executable hashes plus exact native
  map/fighter metadata and cleanup proof. The evaluator has its own hash and may
  regrade immutable captures without claiming to have produced them.
- The offline campaign now reuses complete rows and individually reusable raw
  stock/primary/re-entry/seek captures only after those checks pass. Its order
  front-loads the canary ladder: baseline (including automatic 120-frame smoke),
  active-combat depth 1, strict seeks, then the remaining matrix. Final
  certification still requires the independent clean-process re-entry evidence;
  persistent-process cycles remain development acceleration only.
