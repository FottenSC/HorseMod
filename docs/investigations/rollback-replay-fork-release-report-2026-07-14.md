# Two-client replay-fork GekkoNet validation (2026-07-14)

## Outcome

The deterministic replay-fork lane now passes with one normal SC6 process and
one Sandboxie SC6 process. It preserves GekkoNet and exercises the shared
Save/Load/Advance runtime through authenticated Horse UDP. The deployed DLL is:

```text
SHA-256 AFFCB33FACFEEB3582B4B13C38173E8FD76B75A96344B8FE93378A06094CB67F
```

This is automated `rollback-core-integration` evidence. It is deliberately
reported as `production_certified=false` and `proof_non_skip=false`.

## Final evidence

- `E:\myMods\artifacts\rollback_replay_fork_paired_120s_final.json`
  passes a 120-second clean oracle and an independent 120-second
  `wifi_50ms_jitter` run. The profiles use fresh process pairs and converge to
  canonical hash `0x191C3326AD15CB1B`, terminal evidence hash
  `0xDB17AE02C7E5E264`, host-owned input hash `0x4BA264782BB7744C`,
  and remote-owned input hash `0xCE380FBBE420D031`. The impaired pair records
  1,062 Loads and 4,326 rollback Advances, snapshot peak 128, zero summary
  overwrites, 14,410 presentation synchronizations, and zero presentation
  failures.
- `E:\myMods\artifacts\rollback_replay_fork_hold_feasibility_3fresh_final.json`
  passes three fresh launches (six distinct SC6 PIDs). Every client freezes for
  120 uncredited outer ticks at replay sequence 2751, round 1, master 414,
  agrees on baseline hash `0x3A17F716EFB7D666`, passes direct advances of
  1/2/8/15/60 frames, restores the anchor, releases all gates, and resumes the
  replay.
- `E:\myMods\artifacts\rollback_replay_fork_drain_boundary_smoke.json`
  passes a short clean/impaired terminal smoke on the final build. Its traces
  do not contain an advance at or beyond `run_frames`, so it does not by itself
  prove the drain-only boundary branch described below.
- `E:\myMods\reports\replay_tests\replay_seek_e2e_20260714-013958-seek.json`
  passes the mandated strict replay test: 4/4 600-frame watch cases,
  2,400/2,400 state comparisons, zero mismatches, 59.8--60.4 ticks/second
  resume rates, and maximum seek-validation time 0.478 seconds.
- All 31 standalone rollback C++ self-tests pass on the final source/build.

## Defects found and fixed during acceptance

1. Gekko bootstrap consumed a new replay input every service tick before both
   peers reached `GekkoSessionStarted`. Clean and impaired profiles therefore
   began with different input cursors. Bootstrap now resubmits the same anchor
   input and advances the replay cursor only after session start.
2. The runner reused a consumed replay session for the second network profile.
   Every profile now receives a fresh host/Sandboxie pair, and every launch
   index receives its own clean-versus-impaired oracle comparison.
3. Clean runs were incorrectly required to contain forced correction. Loads,
   rollback Advances, and prediction divergence are now mandatory only for an
   impaired profile.
4. Terminal proof used the first predicted terminal `final_hash` even after a
   rollback replaced the mutually accepted terminal summary. The accepted
   corrected summary now promotes the authenticated terminal hash.
5. A delayed correction of drain-only frame 7200 attempted to write beyond the
   bounded 0--7199 evidence table. Gekko still executes drain frames, but only
   in-window corrected frames publish replay-fork evidence.
6. Native simulation objects were being confused with visible UE actor
   wrappers. Presentation publication now resolves `BattleCharaArray` actors
   by `PlayerIndex`, calls the stock character transform getter and exact
   `AActor::SetActorTransform` path, and verifies the result through
   `K2_GetActorLocation` without mutating canonical gameplay state.

## Presentation evidence

The two-client traces prove that presentation objects are process-local and
distinct from native simulation objects. Each credited publication requires:

- both stock transform getters and setters to run successfully;
- non-null actor vtables and root components;
- finite native transform targets;
- exact independent `K2_GetActorLocation` readback;
- unchanged canonical hash, replay observation, and native simulation/render
  position fields; and
- motion across credited publications.

The long paired run records 14,410 successful publication events with zero
failures per profile (7,205 per client). In the clean profile each client
observes 6,602 distinct P0 actor transforms and 6,546 distinct P1 actor
transforms; P0 world X spans -287.03 through 938.77 and P1 spans -510.85
through 966.97. This closes the reported frozen-model defect at the
actor-transform boundary. It is runtime transform/readback evidence, not a
pixel-diff or GPU render capture.

Ghidra MCP was used to document `SetActorTransform @ 0x141C2A1D0`, apply the
exact prototype, recover the partial actor/root and hit-result layouts, and
save the program before the production binding was implemented.

## Release-lane distinction

| Lane | Evidence scope | Current result |
|---|---|---|
| Replay-fork lab | Automated rollback-core integration | Pass |
| Local VS attach | `MirroredVersus` production lifecycle | Not certified by replay-fork |
| Player Match/Casual attach | `StockOnlinePvp` production lifecycle | Not certified by replay-fork |
| Blueprint/UI automation | Smoke and gameflow diagnostics only | Not a rollback gate |

Incomplete production snapshot coverage and the two attach-based production
certification lanes remain explicit release blockers. Replay-fork does not
waive them and cannot emit or satisfy `proof_non_skip`.

## Evidence limitations

- The successful final soak did not reproduce a rollback Advance at frame 7200
  or a corrected terminal frame. The out-of-window drain exclusion and
  corrected-terminal promotion are code-reviewed fixes derived from the two
  preceding failed 7,200-frame traces, but still need a deterministic targeted
  self-test to exercise those exact branches.
- Actor publication is not a clean-versus-impaired presentation oracle. Stock
  interpolation legitimately follows predicted state before correction, and
  clean/Wi-Fi actor targets are therefore not identical on every presentation
  frame. Current evidence proves that visible actor transforms update through
  the stock path and read back exactly without gameplay mutation; it does not
  prove pixel-identical rendering or ideal interpolation quality.
- The JSON reports record executable/schema build identity but not the deployed
  module SHA-256. Artifact timestamps postdate the DLL deployment and the DLL
  hash was independently rechecked. The runner now hashes the deployed module
  before and after execution and requires it to remain unchanged; the final
  artifacts above predate that reporting-only improvement.
