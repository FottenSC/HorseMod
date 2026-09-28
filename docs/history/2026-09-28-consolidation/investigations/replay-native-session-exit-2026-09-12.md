> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Retained replay session exit

The implementation defers the parameterless `ReplayBattleScene.OnRequestToStop` event until the replay host has released its native owners. Retained-index native exit now passes on runtime56723662 / observerb4d62966; historical-seek cancellation and re-entry remain unqualified.

The retained `build_cmake_LessEqual421__Shipping__Win64/replay-tests/native-session-exit-qualified-stage.json` links both raw reports/logs and reconstructible executed sources. Candidateb7125640bdf7424fa6d9a7236dbca22e matches600 observations/2567 callbacks against unchanged runtime-absent control6b1a4240693e42a3a02c48c8e0492d18. Stop deferral, checkpoint retirement and complete639-application detachment all occur at tick643, before exactly one native stop publication. Native termination and replay-list arrival follow. Both cleanup outcomes are complete with zero games. Exit2539494us includes the native fade; capture101740us remains a stutter. Neither is rollback-performance evidence.

The first run's parser admission failure and missing immediate-detachment application-count receipt are retained separately. Its matching observed prefix was not relabelled as a full executor pass. The candidate retry supplied the actual counter and reused the same verified control.

## Evidence and limits

The retained cooked script is `artifacts/replay-live-recovery-20260911/scene-exit-cooked-event-audit.json`, with complete parsed scripts and hashes of the actual `.uasset`/`.uexp` inputs beside it. Native decompilations are retained in `scene-exit-native-ownership-audit.json` in that directory.

* Replay `OnRequestToStop` has no parameters and calls its ubergraph at 2094. It clears the battle-name data value, then calls parent `BattleScene.OnRequestToStop` at 2254.
* Parent stop installs `OnFinishedBattleFadeout` and starts the native fade. The later callback enters the battle termination path and then `ReadyToStop`.
* Native `142F20110` registers the current scene's stop-completion delegate before starting transition work. `142F0AE70` handles that completion and reopens the manager's change-scene gate. The deferred event therefore has an asynchronous caller; it is not a suspended native stack.
* `1403FBBC0` destroys the battle context and clears GameMode's battle-manager pointer. Deferring only this callee would be insufficient: its callers can continue asset/scene mutations after it returns.
* The normal `RequestChangeScene` route first installs `OnPreUIChangeScene`. Its cooked entry 4345 resets battle win counts for the replay-list transition and disables pause-menu control before `OnRequestToStop`. These are real earlier mutations. The current live experiment does **not** qualify cancellation of a historical seek through this path. Local driver ordering tests are not evidence that all earlier native mutations can be undone.

The guard validates the actual Blueprint function, zero parameter extent, native weak identities for function/scene/manager, unique manager membership of the scene, and continued `CurrentScene` membership on every cleanup update. A failed binding blocks deferred publication. It does not revive a destroyed scene or ignore ownership failures.

## Ownership sequence

`Requested -> Recovering -> Retiring -> ReleasingHold -> Detaching -> Complete` is driven at the application's external entry, with existing seek/capture/retirement owners performing their own work. An uncommitted historical seek requests existing B cancellation; an irreversible commit must complete. Standalone caller-owned capture handles are not discarded. The index and all its pins retire before releasing the final hold. `Stop()` acceptance alone is insufficient: completed host detachment and executor disable precede the single native event publication.

The new fields are included in the host's existing `sizeof(*this)` owned-memory accounting. No new checkpoints, texture readbacks or presentation owners are introduced. Deferred GPU work retains its existing completion/retirement guards. Failure remains blocked with resources retained; it is not reported as successful native exit.

## Bounded live experiment

`python tools/replay_test.py session-exit --samples 600` uses the existing native import/Steam/deployment recovery path. The candidate arms 169->170 through the host, retains checkpoint170, resumes and observes a bounded combat prefix. It then requests the verified native replay-list transition. The control independently executes the same observed prefix and intro intervention.

Required receipts: retained checkpoint -> exit intent -> native stop deferred -> index/resource retirement and completed host detachment -> exactly one native stop publication -> observed TerminateBattle and replay-list arrival with executor disabled and host inactive. Prefix timing stops when observations stop; scene-exit wait is reported separately. No full-index, historical-cancellation, re-entry or rollback-performance claim follows from this experiment.

Local tests compile the production exit driver against controlled native-owner outcomes: B recovery before release, pending commit, capture ownership, GPU retirement, held-display release, pending application tail, stop rejection, incomplete detachment, stale bindings and exactly-once publication. The CLI regression also rejects accidentally treating this experiment as held-index cancellation or full index completion.

After a successful native exit, the next lifecycle work is fresh native load/reindexing and validation of the earlier transition callback's interaction with historical cancellation. Production automatic entry remains disabled until that ownership is qualified. General exact seeking, durable checkpoint coverage, initial coverage, controls, the strict sentinel and performance are still unfinished.
