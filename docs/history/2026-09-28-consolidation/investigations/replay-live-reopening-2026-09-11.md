> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Live reopening qualification after testing resumed

## Harness review and current attachment witness (2026-09-12)

The corrected external harness review is confirmed. Indexed-seek comparison now fails its aggregate and CLI exit status when independently calculated resumed pacing is below58TPS; gameplay comparison remains separately reported. Seek latency stays independent, and historical reports without rate measurements explicitly report `not_measured`. Strict-seek, normal-render and synthetic-stress parsers now independently validate count/time/rate arithmetic, including viewport and native clocks separately. Live rate readers wait for complete log lines before validating arithmetic. No inconsistent current-producer live output was demonstrated. Displayed FPS remains unmeasured; viewport-update scope was already documented and is unchanged.

151 focused Python tests pass, including forged arithmetic, partial live log writes, latency-versus-pacing aggregation and CLI failure propagation. Both C++ suites and shipped-native physics fixture pass. [Retained build](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/resume-pacing-validation-build-stage.json) points to a reconstructible source archive; exact runner/parser/test bytes were checked against that archive. This is local/build evidence, not a new live qualification or seeker-completion claim.

The latest bounded C11000 witness is runtimef04f8fb9 / observered845fde, candidate9b1d789f5728468bb542085c69c97b10. [Retained report](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/trace-attachment-domains.json) links its retained raw log and executed sources. It still rejects traces_C/image_shape before commit; B retained, complete process cleanup, zero games, no native control and no late B recovery. Bootstrap passed after the user confirmed closing the previous attempt.

Both C-only actors have zero lifespan and no demo driver. Their exact mesh and optional LuxTraceAttachComponent have empty ReceiveEndPlay scripts and activation/deactivation delegates, no parent/children. Root1 has a live VFX slot1604 and an attachment whose owner differs from the trace actor. SkeletalMeshActor has one reflected interface; its identity and actual navigation branch were not recorded. These are scheduling/lifecycle dependencies, not justification for copying renderer caches.

The bounded remaining retirement contract must establish actor EndPlay and delegate emptiness, the complete world network context (not only DemoNetDriver), navigation admission, exact attachment owner membership and VFX birth ownership, plus the texture-streaming removal branch. Native world destruction also notifies streaming managers, clears the actor's level entry, removes network-object membership, and unregisters owned ticks/components. Native attachment destruction removes owner component membership even for this currently unregistered attachment. No native destruction or guard relaxation has been added. Required proof remains an assembled native C-only retirement, fresh ordered completion, complete B recovery and120 independent continuation ticks; standalone witness/build success cannot satisfy it.

## Current follow-up: live retirement witness and timing correction

Candidate `replay-2583a4e88e0e404b8144efe6044ae068`, runtime `24e5a397b64a6ce773e083af53a34ca37e7c65267a2dd23ededbf08c471613d8`, observer `413774d55885d378c2e61cff50c080689a7e35b5e96613d3df2b452aa8f5bfbc`, repeated the bounded A170/B2504 -> C11000 cancellation request with read-only failure instrumentation. [Retained report](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/trace-late-retirement-domains.json) points to its retained raw log and reconstructible executed sources. It rejects `traces_C/image_shape` before commit, retains B, and cleans up completely with zero games. This is a diagnostic result, not B recovery or completed seeking.

Both C-only actors resolve `ReceiveDestroyed` to `/Script/Engine.Actor:ReceiveDestroyed`, flags `08020800`, empty script; their actor destruction multicast counts are zero. Each actor owns exactly one component, its known skeletal mesh. Both meshes have zero overlaps and null animation completion events; flags `2e4e0e03` do not select the physics-state callback branch in native141D43340. Strong counts are1, weak counts2/3. No actor/mesh/attachment/animation identity aliases any retained A/B trace identity. Root1 still has a separate attachment outside this owned-component set. These observations narrow the destruction contract; they do not prove safe native destruction or general absence of callbacks. Actor1B0=`0B` selects the additional141C12290 branch, and world/network/level cleanup plus attachment/VFX lifecycle and fresh CPU/render/GPU completion remain to be accounted for. No admission guard was changed and no actor was destroyed by this probe. Checkpoint-memory/GPU-readback impact is zero; diagnostic logging time is not production latency evidence.

The older strict-seek rate observer discarded elapsed time whenever a poll saw zero tick progress. `ReplayResumeRateWindow` now measures contiguous monotonic wall time through the first observed boundary reaching the requested minimum. It reports the actual observed tick count, including an overshooting multi-tick poll, without interpolating a timestamp. Further observation time cannot change the completed rate window; tick/clock regressions fail. The existing strict-seek entry point is preserved. [Build evidence](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/strict-resume-wall-clock-build-stage.json) includes reconstructible sources; both C++ suites/native physics fixture pass, with focused stalled-poll, normal60TPS, overshoot and regression cases.146 Python tests pass. The changed observer has **local proof only**, not a new strict-sentinel live qualification.

[Offline cadence analysis](../../../../artifacts/replay-live-recovery-20260911/capture-cadence.json), reproducible from the retained earlier recovery log, confirms a179709us gap170->171 and38.5405TPS over169->199. Capture A reports139373us internally; checkpoint capture and replay-output setup occur in this region. Correlation does not apportion all179709us between them. Independent post-recovery timing is120ticks/1999772us with18333us maximum gap. The43565 milli-TPS trajectory field is whole-session **net tick progress** across rewinds/seeking/pauses; it is not steady playback speed. Preserve these separate categories in qualification: capture/setup interruption; full request-to-completed seek with preparation/resimulation/tails; resumed cadence; rollback capture/restore/resimulation cost.60TPS after recovery does not qualify rollback performance, and the capture stall remains unresolved.

The user re-enabled live testing. [Retained audit](../../../../artifacts/replay-live-recovery-20260911/audit.json) links reports, their retained raw logs, reconstructible source archives and local test output.

## Launch prerequisite

First bootstrap failed because this installation's shim was renamed `dwmapi.dll.DISABLED`; the marked game loaded the Windows system DLL and no observer. Cleanup completed. The runner now temporarily copies the verified disabled shim through the existing deployment journal and verifies its mapped path/hash. Both retained local shim copies hash to `aa8eeee6a86537febdb4f6e3ba6aba7f825534e3f50092f7cbb745365a52a3dd`. Unknown disabled copies reject before launch. Existing enabled loaders are preserved. The temporary DLL was removed after each run; the disabled copy and existing Steam clients remain.

## C5750 recovery: pass after investigating first failure

The first candidate reached C5750 but reopening rejected `execution_image_weak_retirement`. A diagnostic-only retry identified an appended bookkeeping row with strong=1, baseline weak=1, added=1, removed=0, no C expired lease, and a retained B live owner. The mistake was equating newly tracked weak references with newly created controllers.

Reopening now drops newly tracked metadata for either a proven C-expired lease or an exact live state/controller present in retained A and B. It never drops a displaced B reference. Original controls retain their existing lease subtraction checks. Unknown live controllers reject. Focused tests cover the combined live/expired case and negative B-reference/ownership cases. The completed row diagnostic was removed; both failures remain retained.

Runtime `a25545e265faf65bc56ad66714d694c64b60e4a1d682378aebd078d4d4611a5b`, observer `413774d55885d378c2e61cff50c080689a7e35b5e96613d3df2b452aa8f5bfbc`:

- Candidate `replay-64ef1eafe50d49968dc3d468e31899c9`: A170/B2504 -> C5750, deliberate failure after all11 CPU participants settle, successful reopening,13 C-only particle retirements, fresh native completion, complete B2504 recovery.
- Native control `replay-db875cf735644e4586ca95213e2b1422`:120 continuation ticks and480 callbacks match; complete Lux animation-proxy outputs match;10 inactive HUD pool observations match. Both cleanups complete, zero games.
- Capture140223us; A publication including B undo160560us; conservative owned reservation339811682 bytes. Request-to-ready15420770us; engine9346632us, tails5815271us, prefix74545us. No diagnostic GPU maps. This cancelled transaction does not establish completed-seek/rollback cost and exceeds500ms readiness.
- Held-output ownership passes, but full actor/effect presence, HUD freshness and persistent-render-corruption review remain incomplete. No pixel-equality claim. Changed inputs and earlier-participant/reopening-retry live faults were not exercised by this all11-settled test.

## C11000: exact ownership blocker identified

Same binaries, candidate `replay-8e971bc0ebf84e4ea414c66b5867780c`, reaches11000 then safely rejects `traces_C/image_shape` before commit. B remains retained; cleanup completes. No independent control is launched and B recovery is not proven for this target.

The snapshot-only partition establishes A/B each retain109 live states with no missing owners in C. Root0 base pool62 and root1 base pool47 remain. Both A/B child arrays and weak arrays are empty. C adds two live children:

| Root | Controller | Actor | Mesh | Attachment | C memberships |
|---|---|---|---|---|---|
| 0 | 17897f68560 | 179b513d260 | 1786e327090 | absent | child[0], collection2[0] |
| 1 | 17898c449c0 | 1788bddc4b0 | 1789b1950b0 | 17899686a60 | child[0], collection2[1], collection3[1] |

C also retains one expired weak lease; collection3[0] on root1 references controller17895d7bb60. All collection rows used copied-complete storage. Addresses are run-local evidence, not restore bindings to reuse in another process. This proves the set partition, not destruction safety or absence of aliases to other owned resources.

Next required ownership work is the actual two-child actor/component/attachment teardown closure: ReceiveDestroyed function and delegates, overlaps/physics callbacks, tick prerequisites, animation work and reference multiplicity. Native memos01/13 establish relevant consumers; their emptiness or ownership on these live instances remains unproven. Do not relax shape/membership/lifetime checks or invoke whole-component teardown. Initial partial render capture recovery remains a separate open path. No new lighting-history capture is justified.

Build/both C++ suites/native physics fixture and107 Python tests pass. Full arbitrary seeker, complete execution gate, general lifecycle recovery, visual coherence and performance remain unfinished.
