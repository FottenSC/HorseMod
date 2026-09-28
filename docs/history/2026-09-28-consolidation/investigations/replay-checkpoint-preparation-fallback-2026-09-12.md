> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Checkpoint preparation fallback

## Retry-boundary cancellation and fresh request

Runtime `5a6799cf` / observer `b22e4641`, candidate `da6b19ffbb1a4a96a97aea7de31d6e12`, now passes cancellation at `RetryingPreparation`: native B11503 observations, callback count and engine epoch remain unchanged, no earlier checkpoint starts, and native release completes. The cancelled request took141778us. A fresh request then repeats the deliberate2504 preparation rejection, falls back170 and completes2510 plus120 independent ticks/480callbacks/Lux poses against native `b1464e54370345c4afce490949dec1e3`. Both cleanups completed with zero games and no diagnostic GPU readbacks.

The first harness attempt rejected the candidate because its parser expected `LogTrajectorySample`'s record name in the phase field. Actual records are `index_preparation_B_before/after ordinal=1 phase=engine_post`. Their independently read values are identical. The corrected parser and140 focused regressions passed; the same candidate was revalidated without rerunning it. Only the missing native control was launched, after checking unchanged runtime/observer/framework/game/replay hashes and exact-build bootstrap. Both C++ suites and the native physics fixture pass. `checkpoint-retry-cancel-audit.json` links retained reports, logs, original parser failure, executed and corrected parser source archives, and the exact control-only invocation.

Cancellation evidence is unchanged B before publication, not120 post-cancellation B ticks. The independent120-tick comparison belongs to the fresh completed seek. Fresh request-to-completed5594728us includes510240us deliberate hold;5084488us excluding hold **fails500ms**. The separate cancelled request is not hidden inside that measurement. Preparation281567us, engine3062500us, application tail1566298us, completion132289us;464495976 owned bytes. Native Lux poses match, but no new downstream rendering/image review was performed. Full checkpoint coverage, initial HUD lifetime restoration, remaining execution/lifecycle/corpus gates and rollback performance remain unfinished.

## Bounded live result

Runtime `5a6799cf3cd1a75795dd977ec392e56f9c5eb3b3215ca55bdb97c9d8209fa2d5`, observer `79f1694691e23fd32a1fdcfd9c5fe8b6b27371b593f113ff06e8a5c485049625`: candidate `replay-72453a35a6114eee8cd995c3e7d15664` passes against native control `replay-303f090148ea4260a94bbd4a43f19b45`. Full11502-entry indexing retained170 and2504. Qualification rejected2504 after complete B11509 capture and target preparation, before A publication. The native cancellation/release guards acknowledged unchanged B, retired preparation, and the host selected170. It completed exact2510 and120 independent gameplay ticks/480callbacks/Lux poses. Both cleanup reports show complete/zero games. Diagnostic GPU readbacks were disabled.

`checkpoint-preparation-fallback-{candidate,native,stage,bootstrap,bootstrap-stage}.json` under the build replay-tests directory point to retained raw logs; `checkpoint-preparation-fallback-live-build-stage.json` retains the reconstructible executed source archive and local-test/build logs. Their log/archive hashes were verified.139 focused Python tests, both C++ suites, the native physics fixture and exact-build bootstrap pass.

Completed request cost5726323us includes a deliberate513985us hold; remaining completed cost5212338us **fails500ms**. Preparation270654us includes the rejected attempt and retry; advance prefix29396us, engine3112719us, application tail1656704us (frame-sync subset226045us), completion/retirement131882us. Owned bytes464495976 remain below512MiB. Resume60.126TPS is a separate passing pacing result, not rollback-performance evidence.

This proves automatic fallback after one deliberate never-published preparation failure, not arbitrary rejection recovery or full checkpoint coverage. User cancellation overriding `RetryingPreparation` remains locally tested only. There was no new image review; Lux poses do not prove downstream rendering. The full execution gate explicitly remains incomplete on these identities (zero-tick and move-state3 paths, external-pause and interactive coverage). Initial HUD lifetime integration, broader exact-target/lifecycle/corpus coverage and performance still prevent full seeker completion.

Command: `python tools/replay_test.py equivalence --full-match --index-seek --index-extra-checkpoint 2504 --index-seek-target 2510 --index-seek-continuation 120 --index-preparation-fallback --timeout 420`.

Runtime `a3073abfe1829fdd7d660bf5affe0fe0dbc49630c2a45f2a66cad38206be1f97`, observer `20042dd06ca4a9a7a91fdb6e670e88702ad4d19f91bf824bb1bf49033dd6c5b4`. Build, both C++ suites and the shipped-native physics fixture pass;138 focused Python harness tests pass. `build_cmake_LessEqual421__Shipping__Win64/replay-tests/checkpoint-preparation-fallback-build-stage.json` retains the exact source archive, build log and local-test log. No live fallback proof on these identities.

## Implemented ownership

Automatic selection previously skipped expired VFX owners, but stopped on every later preparation rejection. It now retries a strictly earlier eligible checkpoint only when the original preparation has retired, never published A, has no execution owner and no outstanding restore command. Explicitly supplied checkpoints keep their original failure behavior.

The new `RetryingPreparation` orchestration phase calls the existing restore cancellation API once. That API, not the selector, verifies unchanged B and all release/lifetime guards. Retry waits for `Recovered`, no pending work, `original_recovered`, the exact original tick, and successful native release. It then transfers the rejected checkpoint pin to existing deferred retirement. The following update must drain that owner before requesting the next preparation. No captured values or expected observations are changed.

User cancellation changes the retry to ordinary recovery and suppresses replacement. Missing earlier coverage, failed cancellation, lost bindings, failed B verification, post-publication failure or a partially constructed execution owner preserve failure/recovery semantics. Strictly decreasing checkpoint ticks bound the number of attempts. The target, request observer and original undo coordinate remain unchanged. `preparation_fallbacks`, `last_preparation_failure` and the retained native fallback log expose the rejected attempt; a successful later seek must not conceal that evidence.

The code adds no checkpoint or GPU capture participant. Existing checkpoint pin ownership and512MiB accounting remain in force. An actual fallback repeats B/preparation work after releasing the failed attempt; its request-to-completed cost is not measured by local tests. Resumed60TPS does not establish that cost.

## Focused local proof

The selftest compiles production `Sc6ReplayHost.Seek.inl` against controlled asynchronous restore owners. It covers automatic207 rejection followed by205 selection, a release wait, displaced-pin retirement wait, cancellation overriding retry, rejected cancellation, missing B-recovery evidence, explicit-checkpoint rejection, published-A rejection, partial execution-owner rejection, and exhaustion of earlier coverage. These tests prove orchestration, not native recovery internals. Existing native recovery evidence is scoped to its original binaries.

## Live protocol design (now executed above)

Add a qualification-only fault at a selected extra checkpoint after complete B capture and target preparation, before A publication. Do not corrupt captured/native state or bypass lifetime guards to cause the failure. Use an already supported two-checkpoint indexed domain, require the failed attempt's GPU/prepared-owner retirement and B acknowledgement, then require earlier-checkpoint selection, exact target completion and120 independent unchanged-input ticks. A separate cancellation at the retry boundary must retain B and prevent the second request. Keep diagnostic GPU readbacks off and report all preparation attempts in full request-to-completed timing.

The explicit `InjectPreparationFailure` qualification action and `--index-preparation-fallback` runner option now arm this boundary. The independent parser requires unique, ordered complete-B/fault/recovered-release/new-B/selection evidence before starting a native control. The old expired-VFX fallback test remains a separate path. Cancellation overriding the retry still needs its bounded live proof; no full-seeker completion follows.
