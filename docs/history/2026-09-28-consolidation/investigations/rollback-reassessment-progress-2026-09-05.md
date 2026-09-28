> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Rollback reassessment: implementation and evidence ledger

This is a development ledger, not a beta certificate. The revised plan is not complete.

## Verified changes

- Python and the actual C++ release loader share `ProductionReleaseCertificate.json`; the Python-produced certificate interop fixture passes acceptance and rehashed mutation rejection.
- Existing fast native/Python checks remain. Latest full checks: 180 Python checks and 10 CTest executables/integration entries passed. Counts do not establish live correctness.
- Native correction observations now report achieved intervals, replay work, input identity, local canonical tail, and correction-local presentation replacement/reuse/publication/payload/drain. Bilateral agreement uses a later common confirmed hash; local predicted tails are not required to match.
- Snapshot admission freezes prepared capacities. Partial prewarm failure and retained history preservation have native fixture coverage.
- Prefix takeover uses a persistent frontier lease at EngineTickPost with producer/downstream actor gates and the existing manager detour. Preparation time and engine frames actually held are reported separately. Deliberately delayed acknowledgments and timeout cleanup remain unproven live.
- Online scenario inventory, preflight argument validation, resource predecessor restoration, sealed reports, interrupted-stage recovery, and unchanged-failure retry rejection are implemented in development tooling. Further gaps are listed below.

## Invalidated claims

The user's observation that the previous online test remained in character intros exposed a false lifecycle claim. `m_online_rounds` counted input-generation replacements. The old generation-2 correction captures do not prove round-two combat. They remain useful development captures for the boundaries actually recorded.

`online-round2.json` (DLL `5b32164a411c44367f96ecd8d1c8dfed05bdd1dcbab0f947754ad850af012995`) failed its first cycle with a 97 ms correction. Its achieved depths were 7, 12, 1, but they occurred during intros. Depth 12 published 13 changed presentation events bilaterally and later reached common confirmed coordinate 480. The last correction had no subsequent bilateral confirmed boundary. Re-entry and natural match completion were not covered.

The earlier 177 ms prefix-hold report included gate preparation. Log timestamps show approximately 1 ms between hold/release in that initial case. It is not evidence of a 177 ms delayed acknowledgment.

## User-observed menu delay and primary sources

`ReplayQualificationMod::TickGameThread` polls automation every 15 engine ticks. Old active-character delay of 62 polls and stage focus delay of 30 polls produced roughly 17 s and 8.5 s waits in the captured run.

Raw Kismet bytecode confirms:

- `StageSelect.HandleInput(InputType=1, Key=16, ControllerId=GetActiveInputIndex())` sends `DecideStage`, sets `bDecidedLeftPlayer`, and calls `DecideCursor(true)`. The old automation sent only the first command.
- `BattleSetupScene.OnDecidedChara` is the complete local character decision entry. `DecidedCharaCode_L/R` differs from `DecideCharaCode_L/R`.
- `CharaSelectInitState` clears both flags. `PlayerMatchValueInitState` pre-decides the remote side before advancing. `PlayerMatchCharaSelectExecState` requires both flags, verifies the local choice, binds its receive delegate, then the passive peer sends GuestCharacter. The active peer responds to GuestCharacter with CharacterSet; the passive acknowledges with CharacterComplete. Stock code supplies retries.
- Native `LuxOnlineBattleSync_OnRecvBattleSync_Dispatcher` at `140511CF0` consumes the first CharacterSet and suppresses repeated CharacterSet broadcasts once `+1BCE` is set. Manually invoking TrySendCharacter before the stock subscription boundary is unsafe.

Relevant asset pairs are retained under `dump/SoulcaliburVI/Content/UI/GameFlow/GameScenes/BattleSetup/`: `layout/StageSelect`, `layout/CharaSelect`, `BattleSetupScene`, `State/CharaSelectInitState`, `State/Online/PlayerMatchValueInitState`, and `State/Online/PlayerMatchCharaSelectExecState`. Generated pseudocode is supporting analysis only; bytecode was checked independently.

## Native combat boundary

Ghidra primary observations:

- `LuxBattle_AdvanceWorldModePump` (`1402D9CD0`) publishes current mode vtable +18 into master mode `144846364`.
- ActiveBattle vtable `143E873D0`: Tick is `ProcessActiveBattleModeFrame` (`140384EE0`); type getter `140301450` returns 2.
- Applied round index `1448463A4` initializes to 1. `EvaluateLuxBattleRoundResult` (`140386530`) increments it after a nonzero ushort result at `144846408`, including result 2.
- A completed forward observation of active combat, a played terminal result, and next active round are required. Input epochs alone cannot count rounds. Current mode is rechecked when arming and correcting, and correction replay intervals must fall after the recorded combat start.

These facts were appended to the existing Ghidra plates for the world-mode pump and setup dispatcher and saved through native MCP tools.

## Latest failures and next discriminating check

`evidence/rollback-reassessment/combat-boundary/`: both local character confirmations were accepted at timer 30 but both peers remained in CharaSelectExec for over 30 s. The run was manually stopped using graceful closure of its owned processes. `operator-stop.json` records the actual first failure; the harness subsequently reports process identity change. Deployment and requests were restored. The capture lacked callback-subscription measurements, so the early-send race is a code finding, not a proven cause of that particular stall.

`evidence/rollback-reassessment/combat-boundary-stock-sync/`: both processes fail-fast at character selection. Windows event 1000 and the PDB-resolved dump `C:/Users/prest/AppData/Local/CrashDumps/SoulcaliburVI.exe.41924.dmp` place the diagnostic boundary in `CharacterSyncObservation+0x1C7` from `SelectLocalCharacter+0x11E`. UE4SS's `GetMulticastDelegate` virtual exists from UE4.23, not UE4.17; the unavailable wrapper throws into the diagnostic's noexcept boundary. The checked inline delegate read replaces this call. No gameplay failure is established by this attempt.

`evidence/rollback-reassessment/combat-boundary-inline-delegate/` passed those menu boundaries: both characters confirmed with timer 30; stage confirmed with timer 15; stock callback subscription and CharacterSet receipt were observed. Native roster/handshake was 012/015 on both peers. Three input generations occurred before first active combat at generation 3, frame 963. Common confirmed SHA agreement reached frame 1620. Host then failed `state_hash_mismatch` just after both peers sent frame 1650, with zero corrections injected. The report's frame 630 is misleading: failure reporting unconditionally substituted the baseline for any hash mismatch. It is not an established divergent baseline. Cleanup was graceful, deployment restored, requests disarmed, zero game processes remaining.

The user's further observation that fighters appeared absent remains unresolved. Accepted codes and native ActiveBattle do not prove rendered actors. Raw Kismet shows focus publishes battle CreationProfile synchronously, but preview creation is deferred at least one tick and signals `bReadyDemoHuman` later. The revised driver focuses once, waits for current preview readiness and profile identity/style/weapon agreement, then confirms. It has a ten-second deadline. A combat-entry diagnostic records actual battle actor count, player/character/weapon IDs, mesh asset and visibility getters. These new observations have compiled but have not yet run live.

The next probe adds a local confirmed SHA/component record before sending, and the exact disputed coordinate, local/remote SHA and local frontier at comparison failure. Received hashes remain in the bounded coordinator queue until locally confirmed and completed. This ordering guard addresses a concrete general risk; both peers had sent 1650 in the failed capture, so confirmation lag is not its diagnosed cause. No Gekko health-check failure is implicated: that feature is disabled for the configured save policy.

Current evaluator changes reconstruct combat rounds from native active/result/next-active observations and reject a bare round-two counter. Functional development/release aggregation reparses current-scenario sealed correction logs; it no longer accepts old summary-only intro coverage. The scenario contract is version 2. Full release obligations and failure/cleanup raw re-evaluation still need work.

`evidence/rollback-reassessment/combat-boundary-visible-profiles/` (runtime `2d8700e090d742144129269ab6be4f99c72d822d4bdeae4ad47853717399a4c1`, bridge `1ce89babc982a8f5bce8544256dc5c52c0dc2e85776ee8244751378ceaa6de07`) proves the new selection prerequisite: both previews first reported ready=0/profile_matches=1, then ready=1/profile_matches=1 about 1.4 seconds later. Style 12/14 and default weapon 0 matched; both confirmations retained timer 28 and stage timer 15. It did not reach combat. Host stock TerminateBattle occurred at 00:57:35.497 after frame 5; sandbox at 00:57:39.464 after frame 4. The custom coordinator's later timeout at 00:57:42 is secondary. Cleanup required emergency process termination after the graceful deadline, then restored deployment/disarmed requests with zero remaining game processes. This is not a graceful cleanup pass.

Next boundary: identify the stock caller/reason for that early TerminateBattle. The existing pre-hook now records the FFrame caller chain and classifies pre-ownership battle termination immediately as NativeLifecycleEnded before clearing identity. Actor/mesh observations also run at first intro mode 6 so a pre-combat exit cannot hide the render-readiness measurement. No further run is justified without these new observations and validation of the lifecycle change.

## Remaining implementation and qualification

Latest `combat-boundary-stock-error/` capture (runtime `a6242945...`) reached genuine active round two, then failed correction-one presentation agreement: host replacement/published/changed counts were 19/19/19; sandbox 0/0/0. Both drained locally. This is a real difference, not just the legacy cumulative-total evaluator complaint. Both actors existed, but raw and combined character visibility stayed zero at intro, combat round one, and combat round two. Main mesh assets were absent; separate creation components were not measured. The user's observation of missing fighters is consistent with the visibility evidence. Stock error did not recur. Cleanup was graceful, deployment restored, requests disarmed, zero remaining game processes.

Primary-source reassessment found a concrete scope defect in `CallbackExecutorDetour`: after authoritative input publication latched `authoritative_input_requested`, every subsequent non-input collection had `before_valid=false` and skipped the original executor. Native `141D38300` is the generic callback executor; verified callers include visibility collection30 (`140400236`), weapon setup, bone transforms, damage, and trace updates. Explicit replay does not latch the same flag, creating forward/replay asymmetry. Hermes independently confirmed the call graph and defect. The repair scopes rejection to the input collection, including the abort check. An existing native fixture now covers successful publication followed by nested and subsequent semantic dispatch in forward and replay contexts, while preserving invalid-input rejection. The build and all ten CTest entries passed; 170 Python tests passed. No live claim exists for this repair yet.

Ghidra getter bodies `14090F260` and `14090F280` were recovered, typed, named and documented: raw visibility reads actor+533; effective visibility is +533 && !+532 && +535. Dynamic component inventory and pending setup callback count (+550, drained by TickActor `1403D0590`) are added to the next observation. Next test stops at the first native failure; otherwise it must demonstrate visible initialized fighters and the same genuine round-two correction/re-entry boundary. Strict normal-render seek and Tira remain required after this shared semantic-callback change.

### Callback scope repair: new evidence and user-observed replay failure

`combat-boundary-callback-scope/`, run `paired-efd2596c4eae4f80ba386813691461c1`, used DLL `fac4209fffe71de1d7e7358287593f7c054d097f83afc12bc4f49a084947eef5`. It stopped before combat at host round-transition `missing_input`: observed_end=1:361, canonical_end=1:359, current=1:362, pending=1:361, next_gekko_frame=239, confirmed=238. Baseline was 1:122. Peer disconnect is secondary. Cleanup was graceful and restored deployment/disarmed requests, with no remaining game processes. Dynamic creation components contain the expected character assets; the absent main mesh asset is not evidence of an unloaded fighter. Battle actors are hidden at the first intro frame, but cinematic DemoHuman actors were not measured, so intro visibility remains unresolved.

Native simulation loop `1403FE520` calls the input collection once per admitted input item and can then repeat inner simulation while manager+1462 is set. The bridge's affine Gekko-frame/native-coordinate mapping and replay's one-input-invocation-per-coordinate requirement therefore need an input-epoch boundary review. Do not expand round-seal adjacency to mask this failure. A fixed-size boundary trace has been added to source to distinguish the exact pending-input/repeat/rebaseline sequence; it has not yet been run.

`strict-seek-callback-scope.json` passed internal normal-render seek consistency on the same DLL: target1204, source_end1565, 361 historical coordinates, then600 fresh frames. The user observed wrong positions and moves whiffing near the end, inconsistent with the original replay. Treat this as an unresolved source-fidelity failure. Its recorded `resolved_hit_calls=0` alone neither proves nor disproves the cause. Historical comparison uses a timeline captured in the same run and does not establish faithful original playback. No further games were launched after the observation. The raw log is preserved beside the report, with its hash and the user observation in `strict-seek-callback-scope.observation.json`; the original report is unchanged. Inspect stock replay production, the historical-input handoff, and native/rendered positions before another live test. Tira and actual round-two correction/re-entry remain unproven on this DLL.

- Finish live menu and actual combat-round admission; diagnose the first genuine native/canonical failure rather than resume the old blanket ladder.
- Complete current-scenario sealed-log re-evaluation. Aggregation must reparse raw captures rather than trust saved evaluator summaries.
- Bind stage reuse to executable/content dependencies; close launcher/impairment creation ownership gaps and persist operator stop reasons in normal reports.
- Implement controlled-traffic and authenticated-gameplay impairment measurements. Current clumsy proof checks have no producer; jitter/burst-loss capability assumptions need resolution. No impairment pass exists.
- Finish unified offline/event coverage, retained highest-slot fixture, explicit seven-frame performance reporting, shared one-hour endurance/cycling workload, and revised release aggregation. Old matrix counts and duplicated soak gates remain in release publishing.
- Test actual enabled production activation with qualification requests disabled, then fresh installation, mismatch rejection, teardown and re-entry. No live production activation pass exists.
- Qualify one unchanged final release binary against all revised obligations. No beta-readiness claim is warranted.
# Rewrite follow-up: visible fidelity remains unresolved

The user explicitly reported the same visible failure. Do not treat earlier internal seek passes as a resolution.

### Latest consumer-boundary result

`rewrite-consumer-steam-seek.json` passes on actual mapped DLL `68beae97db844719f41070fe94cb6f4dd3b8328ac293751c0154421eeb8dd532`. Raw log SHA-256 is `3c6ac25e8207dd28ff92ec029b4a4e8142ca3e17806131e484d14196b36698cc`. The 50% seek reconstructed 18 coordinates in 6,272 microseconds, verified 361 historical frames from target1204 through source1565, then completed 600 fresh frames at a measured 60.021 native ticks/second. Deployment restored; zero game processes remain. This repairs the measured frame1198 mask mismatch by reconstructing the observed inactive TutorialManager consumer interval. The live observations satisfy the parent inert guards. It does not establish original recording fidelity or online correction readiness.

The latest fast checks pass 178 Python cases and 10 CTest entries (including real publisher/loader interoperability). The new cleanup fixture covers delayed Steam delivery during cleanup and after timeout/restart. Steam direct-launch failures were traced to SteamAPI_Init client discovery; launch through Steam succeeds. A signed-in client did exist: the earlier logout-based diagnosis was too strong. See the runtime rewrite document for the native consumer and durable launcher contracts.

* The old replay runner did not deploy its requested DLL. Earlier offline binary claims without mapped-module proof are unverified; `replay-source-handoff` is specifically invalidated. The paired runner has separate deployment evidence.
* Actual `21e472c3...` seek/uninterrupted logs agree at 21 sampled boundaries, including ten fresh samples. That is a narrow consistency result within one modded path.
* New passive EngineTickPost controls compare 1,200 consecutive observations from source round 0/cursor 0. HorseMod-absent and HorseMod-loaded `c7e23652...` runs match source inputs, native frame progression and both fighters' simulation/root-step/render triples. See `evidence/rollback-reassessment/rewrite-passive-control-comparison.json`. Both share the importer and other UE4SS mods; continuous capture health and original recording fidelity are not established by this comparison.
* Primary code/Ghidra proves a seek ownership bug: seek requests run after native outer `1403FBF30` returned, while old landing code restored an inner fencepost and assumed native tail execution remained. The replacement retains completed-outer state, removes the old landing mask/cache patch, and tracks every recorded native outer call through a fixed frontier.
* The first completed-outer probe fails seeking target 1204 from source 1565 with `state_hash_mismatch`. Its original masks do not distinguish native replay-stage failure from completed-state comparison. Raw log sealed as `rewrite-completed-outer-seek.UE4SS.log`, SHA-256 `965d279029555764ab8628034c66a9f092ceec0c423f04f0c6e35b1808e45412`. A subsequent localization probe adds stage and completed component/native fingerprints. No identical uninstrumented rerun is justified.
* Current limitations: interior seek targets fail before restoration; empty reconstruction calls need retained input-cache handoffs; corrected offline batches invalidate their old completed identity. Input epoch ledger/Gekko token migration and the rest of the approved beta obligations remain incomplete. Online campaigns remain paused.

## Producer-entry check: still failing

Actual loaded binary `3440f3bf...` corrected the recorder phase mismatch: expected and observed GameTime/UpdateTime/RecorderTime all equal 360 at target 1204. The completed state still differs in player two's move-event mask (expected 0, observed 1). See `evidence/rollback-reassessment/rewrite-producer-entry-seek.json` and the runtime rewrite document for sealed identities and native source predicates. The user's visible fidelity report remains unresolved. No online campaign or beta claim follows from this partial repair.

## First divergent call and snapshot omission

The first-boundary probe (`eb70cf3a...`) matched eleven reconstructed calls before call 594 first differed in P2 event mask. Native TutorialManager Tick140437F50 clears that mask after BattleManager and also advances provider state/ticks +4B8/+4BC. Owned BattleManager-only replay omits the consumer interval; previous snapshots omitted those provider fields. The fields are now captured/restored/hashed under snapshot schema54, with existing fixture coverage extended for identity and partial-write undo. The actor interval itself is not reconstructed yet. See the runtime rewrite document for exact primary sources and artifact hashes.

## Latest measured failure: native frame1198

Schema54 probe `rewrite-provider-state-seek.json` measures first failure at frame1198 / native call594, with requested target1204. InputLog clocks and added provider state agree; P2 event mask does not. Runtime `c3fb6603...` and raw logs are sealed in that report; cleanup completed. Correction verification subsequently became read-only, removing expected-input/mask overwrites and their unused helper APIs while retaining full failure undo. The runtime rewrite, visible fidelity fix and beta qualification are incomplete.

## Latest status after runtime admission review

Current runtime `833a280e24f772122f4e662623d41382166af4b0a0fc7a80494d6b262f150d32` passed the strict normal-render seek in `rewrite-admission-final-strict-seek.json`, with 361 historical coordinates verified and 600 fresh frames resumed; cleanup restored deployment and left zero game processes. Latest fast checks are 180 Python cases and ten native/integration entries. The runtime now excludes correction from native input publication and rejects failed outer preparation before native maintenance. Presentation arms only after successful preparation, failed seeks terminalize explicitly, and repeated failures cannot restart preownership cleanup delay. Hermes reviewed the failure paths.

The last exact online attempt (`online-forward-boundary/`) failed startup before gameplay on earlier DLL `893d96ed...`. Host and sandbox launch paths were asymmetric; both now use Steam `-applaunch` while retaining sandbox port isolation. A standalone Steam API initialization failure is not accepted as proof of logout. The sandbox Steam updater independently reported file-rename error 5/update reversion, followed by a UI startup exception. A user check of the sandbox library remains pending; no newer online gameplay claim is made.

The full interval migration remains necessary: Gekko state keys and SnapshotStore still assume native coordinate identity, which cannot represent distinct maintenance-only intervals. InputLog task receivers, owned pause effects, transport-cache preservation and full consumer completion must be resolved before that runtime boundary can be rewritten safely. The detailed current analysis and exact evidence are in `rollback-runtime-rewrite-2026-09-05.md`. Beta remains incomplete.

## Sandbox startup resolved; first online native boundary retained

Sandbox Steam update failed while renaming inherited movie files. Materializing only the missing movie files in sc67 allowed verification and successful login; direct sandbox game launch then succeeded. The sandbox service installer in the user screenshot was a separate failed child, stopped without altering the host Steam service. Steam is no longer a user-action blocker.

`online-ready-steam/report.json` retains paired-8aa9e8c8a0b6495e8cdad2fd0e0bdf0e on runtime833a280e24f772122f4e662623d41382166af4b0a0fc7a80494d6b262f150d32. Sandbox first fails at native round transition: observed end1:361, canonical end1:359, current1:362, pending coordinate1:361. Native input publication at360 produces repeated coordinate361. Cleanup completed gracefully with deployment restored, requests disarmed and zero game processes. This is an intro-boundary failure, not round-two combat evidence.

The next diagnostic change retains the first identity replacement before binding release; later cleared capture diagnostics cannot erase it. It also observes recorder receiver state without invoking native callbacks. Root and backing camera topology differences are distinguished. Python180 and CTest10 pass; live evidence must name the actual new binary. Integrated native scheduling interval ownership and remaining beta coverage are still unfinished.

## Latest proven boundary and remaining runtime work

The retained diagnostic identified a false scheduler replacement: inactive scheduler+8 contains constructor residue, and native activation assigns its own fighter. The schema55 repair preserves exact dormant bits and validates both active ownership and a local binding serial. Runtime `a8ebeb0e...` passed strict seek and the Tira canary. Its exact online run passed that scheduler boundary, then failed because native coordinate1:362 did not match affine Gekko expectation1:361. No scheduler replacement was recorded. Both clients cleaned up gracefully; no Steam sign-in action is pending. The raw logs and first boundary are sealed under `online-scheduler-binding/`.

Runtime `b26fe044...` adds bounded interval-keyed snapshots and transactional complete-suffix coordinate rebuilding. Focused checks exercise distinct zero-coordinate states, changed geometry, stale identities, capacity failures, retained-prefix compaction and buffer reuse. Native/integration checks and `interval-storage-strict.json` pass. These storage APIs are not yet connected to the native Gekko Save/Load/Advance path; online rollback is still incomplete.

Primary RE also disproved the old audio-only description of provider+0x394. Its setter reaches Unreal world pause and photography CVars. Ghidra comments/naming were corrected and saved. The executable migration must separate those external scheduling effects and transport work from deterministic producer admission, include recorder receiver effects, and complete saves only at the full interval boundary. Broader online, impairment, endurance and production activation coverage remain unproven.
## Interrupted-run cleanup and native ingress reassessment

The host and Sandboxie Steam clients remain running (PIDs46880 and62164). `owned-launch-probe.json` proves both newly launched game processes mapped runtime `b26fe044632ddcdf553386d09d1de9c556882c8b490a49717fe63205fda6acfb`; the resource journal returned to clean, predecessor deployments were restored, and zero games remained. This was a disarmed launch/cleanup probe, not a gameplay or rollback pass. The Steam service screenshot is no longer the current blocker.

Paired and replay runs now share the installation lease, including recovery of legacy output-directory journals and interrupted stage journals. Deployment publishes verified temporary bytes atomically. Deferred Steam/Sandboxie launches carry pre-journaled command markers; impairment helpers carry a pre-journaled environment marker without changing Clumsy's arguments. Cleanup adopts and stops only matching identities, tolerates an independently exiting helper, and refuses PID reuse. Request temporary ownership is saved before a writer can leave partial bytes. Prior native room/observer reports are backed up, parked, and restored after validating this run's generated report identity. Hermes reviewed these call chains. The Python suite passes184 tests, including actual lease contention, partial publication/request interruption, retained report restoration, and helper creation before PID save. No impairment-effect pass is claimed.

The runtime remains unfinished. The last online failure is still `owned_frame_coordinate_mismatch` in `online-scheduler-binding/first-boundary.json`; the later launch probe does not supersede it. Interval-keyed storage and suffix rebuilding are implemented but not wired into an independently derived online producer transaction.

Primary ingress evidence now identifies the upstream logical sample (`1403FC640`, FrameInput+3E0+slot*90), CommonInput normal held/pressed (`1403FBC70`, +394/+398, strideA8), and reflected SetLocalInputEnabled (`1403F93A0`, InputLog+394). Final source function1403F0680 synthesizes0x4000 from CommonInput bits8/9 and applies LocalInputMask. That final value is state-derived; it cannot stand in for immutable upstream ingress during correction. LocalInputMask is not reset by native round reset and must not be assumed permanently zero.

The native interval must own input projection, cache/clock evolution, required receiver effects, and the complete BattleManager return. Stock transport, world-pause changes, and persistent FrameStream/recorder publication require separate once-only ownership. Current historical clock/cache handoff writes cannot be retained as a divergence repair. Production activation, online corrected combat/re-entry, impairment measurements, endurance and final release qualification remain unproven.
