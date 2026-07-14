# Rollback two-process harness handover (2026-07-10)

## Purpose and current outcome

This is a Soulcalibur VI game-modding and reverse-engineering task. The immediate objective is to make the live two-process harness launch two clients into a match quickly and reliably, then validate Horse-owned UDP/Gekko rollback with actual inputs. Stock SC6 Steam/Luxor networking is diagnostic infrastructure, not the intended production rollback transport.

The work is **not finished**. The semantic UI and public Player Match discovery/join path now work reliably, but the stock Luxor transport handshake stalls before battle. The latest experiment that forcibly marked the transport ready crashed or hung the sandbox client. Do not repeat that experiment as an acceptance path.

At handover time:

- All `SoulcaliburVI` and `CrashReportClient` processes have been stopped.
- The last build/deploy completed successfully.
- Menu navigation, public room creation, room discovery, ping, semantic room selection, and the native join-complete callback are proven.
- `player-match-battle` still fails.
- The strict replay seek test has **not** been rerun after the latest harness changes.

## Implementation update later on 2026-07-10

The requested continuation was implemented without resetting the dirty tree or touching the unrelated DotVanisher changes. The implementation is materially farther along, but the complete two-account definition of done is still not satisfied: no second authenticated Sandboxie client was available for the final live runs, and production activation remains correctly gated by nine unrelated pending gameplay-coverage entries.

### Online-session state model and unsafe-option removal

- `HorseMod/horselib/RollbackOnlineStageState.hpp` now owns callback synchronization, provisional false-to-true create handling, bounded false-only timeout, 5.2-second ping success, 22-second ping retry, host/guest adoption, and the membership-versus-transport distinction.
- Callback atomics are synchronized before polling and again after native calls that can invoke a callback in the same game-thread tick.
- `membership_ready` is sufficient only for the lobby gate. Battle readiness requires the native connect-complete callback or independently sampled active state `3`, a non-null `LuxorSessionConnection`, and a valid named session.
- Host adoption checks named-session state, lobby ID, public connections, `HostingPlayerNum`, and local Steam lobby ownership when identity is available. Guest adoption requires the expected lobby/owner without promoting membership into transport readiness.
- Manual join-complete and transport-ready compatibility calls are hard-disabled in configuration and runtime. Their report fields remain false/unsupported for compatibility. Route-tag replacement remains diagnostic-only.
- `RollbackOnlineStageStateSelfTest` covers provisional creation, timeout, host/guest adoption, ping timing/retry, callback ordering, membership-not-transport, and exactly-once semantic room selection.

### Private Player Match room creation fix

- The normal host path no longer bypasses the GUI merely because a target Steam owner is configured. Direct native creation is restricted to explicitly named diagnostic rooms/sessions.
- The host creation state machine follows the cooked creation-window contract: resolve `RefPlayerMatchMenu`, call `GetWindowByName("PlayerMatchRoomCreationWindow")`, send exactly nine `Down` commands, one `Right` command to increment `privateSlot` from `0 = Off` to `1 = On`, nine `Up` commands, then one `Decide` command on `Create Room`. The earlier Left command was invalid at value 0 and visibly left the room public.
- The resolved window is cached for that creation lifetime. The obsolete global `FindFirstOf` creation-window fallback was removed.
- The lobby report gate requires the exact `9 Down / 1 Right / 9 Up` navigation counts, private-room command evidence, the final Decide, the post-Decide state poll, and successful Steam `SetLobbyType(k_ELobbyTypePrivate)` enforcement before an invite; native-only session appearance cannot satisfy it.
- Live report `E:\myMods\reports\rollback_two_client_acceptance\rollback_two_client_acceptance_continue-stock-private-abi_lobby-20260710.json` proves the host sequence, `OnCreateSession(true)`, and native lobby `0x1860000661F6093`. The host gate passed; the sandbox still failed with `target-search-result-not-found`, so guest discovery/transport remains the next stock blocker.
- Final focused build/deploy succeeded with DLL SHA-256 `5E8F53EC3A4EC8BEE4CD2D89CB9AA03E757C31854868ADBDAC1950FE7765FD9F`; all 14 two-client report self-tests passed. Both game clients and request files were stopped/removed afterward.

### Authenticated mirrored Local VS implementation

- `RollbackLifecycleMode::{StockOnlinePvp, MirroredVersus}`, `RollbackBattleLaunchDescriptor`, authenticated UDP profile v2, descriptor hashing, and `LaunchBarrier::{SetupApplied, BattleBaseline}` are implemented in `RollbackLaunchContract.hpp`, `RollbackUdpRuntime.hpp`, and `RollbackProductionRuntime.hpp`.
- The handshake authenticates lifecycle mode, opposite Gekko slots, common native-input source, seed, rollback settings, descriptor hash, build ID, and schema ID.
- Default mirrored ownership is native P1 on both clients, host Gekko slot `0`, and sandbox Gekko slot `1`.
- The focused state machine starts Horse UDP in the main menu, calls `MainMenuScene_C.OnTransitionVersus()` once, falls back only to the reflected `EBATTLE -> EVERSUS` semantic path, waits for `VersusBattleSetupScene_C`, applies and reads back the shared descriptor, exchanges `SetupApplied`, calls `OnSetupBattleLauncher()` and `OnRequestToStart()` once, arms/freezes the production tick boundary, and exchanges an exact frame/epoch/canonical `BattleBaseline` before Gekko may start.
- Any descriptor, presence, epoch, peer-generation, baseline, or peer-readiness loss after ownership is fail-closed and cannot resume stock simulation.
- Production reporting now includes lifecycle, slots, desired/observed/peer descriptor hashes, both barrier stages, baseline frame/epochs/hashes, local/remote confirmed input hashes, canonical consensus, restore verification, and presentation-ledger counters.
- Live `Load` now recaptures canonical state after restore and fails closed on a mismatch. Reports distinguish baseline restore, predicted-branch restore, and final restore verification.

The operator runner now accepts `--mode mirrored-versus` with the exact ladder:

```text
inventory -> role-manifest -> horse-udp-ready -> mirrored-versus-setup -> mirrored-versus-battle -> rollback-proof -> soak
```

The production phases retain one authenticated request generation instead of reconfiguring between phases. Intermediate success preserves the active session; any failed phase issues explicit cleanup. The current pinned identifiers for rollback window `12` are:

```text
SC6 executable ID: 0x1135D62F163558E1
Horse schema ID:   0x6A848479E5A8E91
```

Example command once two authenticated clients are already running in their main menus:

```powershell
python E:\myMods\tools\rollback_two_client_acceptance_run.py --mode mirrored-versus --expected-build-id 0x1135D62F163558E1 --expected-schema-id 0x6A848479E5A8E91 --native-input-source-slot 0 --seed 0x5C6B0001 --strict
```

`direct-connect` remains the default for now. The plan required switching the development default only after a passing live mirrored ladder, and that live proof has not occurred.

### Stock opcode `0x15` receive/dispatch diagnosis

Ghidra MCP now identifies and documents the missing guest-side boundary:

- `HandleLuxorSessionRouteOpcodeMessage @ 0x142E6B100` deserializes the outer opcode `0x15` session transport, reads channel and inner opcode, resolves the route through transport vtable `+0x58` with `+0xA8` fallback, and calls the ready-keyed broadcaster with group `9` and code `innerOpcode + 0x10`. Effective completeness is `93.87`; the program was saved after verification.
- `BroadcastLuxorReadyKeyedDispatch @ 0x141E46A40` walks the callback pool in reverse using listener count `+0x50`, entry stride `0x40`, enabled word `+0x30`, callback storage `+0x20`, and callback vtable dispatch `+0x68`. `LuxorReadyKeyedCallbackPool_Partial` and opaque route typing were added. Effective completeness is `96.13`, with only three documented raw callback-entry offsets and `3.87` fixable points; the program was saved.

The outer opcode handler does not allocate `LuxorSessionConnection` itself. It requires a resolved route and a registered ready-keyed listener/delegate. New observe-only hooks/events record both the handler and broadcaster, including QPC, thread, caller RVA, route/key, dispatch group/code/inner opcode, listener count, local-user slot, active state/substate, transport state/readiness, and connection/channel evidence. The packet timeline now distinguishes:

```text
no sandbox ingress
  -> active dispatch but no opcode-0x15 handler
  -> handler stops before route/delegate broadcast
  -> ready-keyed broadcaster reached (including listener count)
```

No completion/readiness handler is invoked by these probes. A new live stock run is still required to choose the evidenced branch and make the native fix at the route/identity/delegate source.

### Validation completed

- Full build/deploy/validation passed: `E:\myMods\reports\rollback_validation\rollback_validation_20260710-161930-721427.json`.
- The bundle passed all 25 standalone C++ self-tests, both Python self-test scripts, all ten request-file game labs, live-online readiness analysis, and strict replay. The report script now contains ten focused cases after adding the serialized production-request check.
- `RollbackProductionActiveGuardSelfTest` explicitly rejects presence loss, lifecycle-generation change, UDP-handshake-generation change, peer loss, and schema/coverage change.
- The separately mandated strict replay passed: `E:\myMods\reports\replay_tests\replay_seek_e2e_20260710-162354-seek.json`, 4/4 cases, `2400/2400` frames, zero state mismatches, maximum seek validation `0.485s <= 0.5s`.
- Timeline-generation code was not changed, so the conditional normal-versus-lux-no-render oracle comparison was not required.

One stale validation mismatch was found and fixed during the bundle: the durable end-to-end event was already passing, but `rollback_lab_test_run.py` still expected an obsolete two-packet human-log schema. The validator now checks the current Save/Load/Advance/rollback counts and nonzero/equal final hashes. The targeted lab and the complete bundle both passed after redeploy.

### Remaining hard blockers

The authenticated baseline contract is now covered, reducing pending production-manifest entries from ten to nine. The remaining entries are deliberately not waived:

1. HgCpu canonical field map.
2. KHit allocator/node lifetime.
3. Historical camera argument policy.
4. Round-start canonical field map.
5. CCpu command-array pointer/padding semantics.
6. Stage wind-emitter mutable graph.
7. Canonical stage identity.
8. Breakable presentation reconciliation.
9. Presentation object lifetime/thread affinity.

`rollback-proof` therefore fails honestly with `pending_gameplay_entries=9` until those evidence items are resolved. This is why the mirrored lane has not been made the default and why real Save/Load/Advance cannot yet be claimed from a live two-process production session. Stock Casual also still needs one new two-account run with the new handler/broadcaster probes. These are the remaining definition-of-done items, not hidden test omissions.

## Repository state and precautions

Primary implementation file:

- `E:\myMods\HorseMod\horselib\RollbackP2PHarness.hpp`

Important working-tree fact: `RollbackP2PHarness.hpp` is currently untracked, so a normal `git diff` does not display its contents. Do not delete it, reset the worktree, or assume an empty diff means no work exists. There are other user changes in the worktree, including unrelated DotVanisher work. Preserve them.

The evidence table also has local changes:

- `E:\myMods\docs\investigations\rollback-netcode-evidence-table.md`

Use `apply_patch` for edits. Follow the repository Ghidra MCP policy: native MCP tools only, structural changes before comments, and completeness verification after documentation/type work.

## What was implemented

### Semantic main-menu navigation

The harness no longer drives menus by repeatedly pulsing XInput or globally scanning every UObject each tick.

- Title entry uses one XInput Start pulse.
- Main menu navigation uses reflected Blueprint calls (`GetItem`, `FocusItem`, `OnDecide`).
- The exact route is Network -> Player Match.
- The semantic path dispatches seven actions and reaches `PlayerMatchLobbyScene_C` in roughly one second after the stable main menu.
- This removed the observed menu flicker, long stalls, and much of the artificial low-FPS behavior.

The old creation-window lookup was wrong. `PlayerMatchRoomCreationWindow` is a window/config key owned by `LuxPlayerMatcthMenu_C`, not a `PlayerMatchRoomCreationWindow_C` UObject class. The previous repeated global scan waited for a class that does not exist and was observed around 24.5 FPS. The correct UI lookup, if ever needed, is through `RefPlayerMatchMenu->GetWindowByName("PlayerMatchRoomCreationWindow")`; the current host path bypasses it with native creation.

### Host public Player Match creation

Relevant functions are near these locations in `RollbackP2PHarness.hpp`:

- `use_targeted_native_player_session_create` near line 23206
- `has_reusable_host_named_session` near line 23218
- `service_host_native_create_pipeline` near line 23233

Implemented behavior:

- Targeted native public Player Match creation is the default host path for the target owner/room.
- A false `OnCreateSession` callback is provisional, because the successful 2026-07-08 trace emitted false and then true for the same request about 373 ms later.
- True callback success is latched; a false-only request waits for a bounded timeout instead of issuing duplicate creation requests.
- A reusable native named session is validated from its native session pointer, session info, public connections, and state.
- A valid existing host session can be adopted at a new phase boundary, avoiding a second create request. This emits `native-create-adopted-existing-session`.

Live evidence confirmed one host creation in the lobby phase and adoption of that same session in the battle phase.

### Guest discovery and semantic room join

Relevant functions:

- `stage_selected_result_with_gameflow_decide` near line 20185
- `poll_search_results_and_join` near line 24128
- `query_player_match_session_fname` near line 35611

Implemented behavior:

- The ping wait is wall-clock based with a 22-second limit. The game repeatedly takes about 5.13-5.22 seconds to return `OnPingSearchResults`; the former 240-tick/~4-second limit was invalid.
- Ping failure/timeout uses the standard bounded find retry ladder.
- `SearchConnecting` preserves the full `FindResult` array on the lobby scene.
- The harness resolves the active `RoomSelectMenu` through `PlayerMatchSelectRoomState.RefRoomSelect`; global `FindFirstOf` is diagnostic fallback only.
- It sends one semantic `RoomSelectMenu.OnHandleInput(KeyDown, Decide)` with controller ID `-1`.
- The default path is `stage_selected_result_with_gameflow_decide`; the former `stage_selected_result_on_player_match_scene` compatibility path targeted the wrong state.
- The guest does not require a Steam invite or controller-driven Steam overlay.

The exact live sequence was:

1. `OnFindSession`
2. `OnPingSearchResults` about 5.218 seconds later
3. `RoomSelectMenu` ready
4. one semantic `OnHandleInput`
5. `OnJoinSession(true)`
6. `OnSessionMemberJoin`

`query_player_match_session_fname` was added as a diagnostic/reflection query. Ghidra independently proved the native name is exactly `FName("PlayerMatch", FNAME_Add)`, so this is not the cause of the remaining failure.

### Diagnostic native join and compatibility experiments

The direct native join diagnostic was changed to stage the selected lobby result and enter `SearchConnecting` before `JoinSession`. It still did not create a transport connection and is not the default.

`service_join_complete_compat_if_needed` (near line 34610) was relaxed so a member-join event alone did not suppress it. Live tracing later proved the real join-complete handler already executes, while manually calling it after disconnect can throw/SEH. Revert or permanently disable this compatibility call before production acceptance.

The `online_stage_transport_ready_compat` experiment is unsafe. When combined with route fixup in the final run, the sandbox process (PID 55584) produced a crash dialog/hung. Keep the flag off and do not convert it into a success gate.

## Ghidra-validated native findings

The `ghidra-doc-function` V5 workflow was used on the join-complete handler. Ghidra's database already contained the correct name and prototype, so no unnecessary mutation was made.

### `HandleLuxorJoinSessionComplete`

- Address: `0x142E5A7A0`
- Prototype: `void __fastcall HandleLuxorJoinSessionComplete(LuxorConnectManagerPartial*, ulonglong qwSessionName, int nJoinResult)`
- Completeness: effective 100%, zero fixable deductions; the lower raw score was structural-only.
- It only takes the success path when `nJoinResult == 0` and an active connection exists.
- It resolves the named session and creates a `LuxorSessionConnection` when the named session state is byte `1`.
- It always notifies the active connection through vtable offset `+0xC8`.
- `StartLuxorConnectManager` at `0x142E594F0` registers it as the OnlineSession join-complete delegate. Registration xrefs are at `0x142E59A7C` and `0x142E59A91`; the delegate slot is around manager `+0x1B0`, with the handle in the delegate array at `+0x28`.

### `PlayerMatchName`

- Native registration table entry at `0x143D131C0` maps the reflected name string to thunk `0x142E4B1C0`.
- The thunk constructs the literal at `0x1432B4B38`, `"PlayerMatch"`, with mode `1` (`FNAME_Add`).
- The adjacent Rank Match implementation is symmetric.
- Generated SDK C++ showing `NAME_None` is a placeholder and is not runtime evidence.

## Cooked-asset evidence

Relevant copied assets are under:

- `E:\myMods\dump\SoulcaliburVI\Content\UI\MenuElement\MenuWindow\PlayerMatchWindow\LuxPlayerMatcthMenu.{uasset,uexp}`
- `E:\myMods\dump\SoulcaliburVI\Content\UI\Data\GameData\PlayerMatch\PlayerMatchRoomCreationConfigData_ui.{uasset,uexp}`
- `E:\myMods\dump\SoulcaliburVI\Content\UI\Data\GameData\PlayerMatch\PlayerMatchRoomCreationWindowConfig_ui.{uasset,uexp}`
- `E:\myMods\dump\SoulcaliburVI\Content\UI\Data\GameData\PlayerMatch\PlayerMatchRoomSearchConfigData_ui.{uasset,uexp}`
- `E:\myMods\dump\SoulcaliburVI\Content\UI\Data\GameData\PlayerMatch\PlayerMatchRoomSearchWindowConfig_ui.{uasset,uexp}`
- `E:\myMods\dump\SoulcaliburVI\Content\UI\GameFlow\GameScenes\Online\layout\PlayerMatch\RoomSelectMenu.{uasset,uexp}`
- `E:\myMods\dump\SoulcaliburVI\Content\UI\GameFlow\GameScenes\MainMenu\DB_MainMenuList.{uasset,uexp}`

The stock public-room flow does not require invites:

- Make Room -> `Decide(StartCreateRoom)` -> `MakeConnecting` -> `RequestCreateSession`
- The stock config sets the public flag true.
- Search copies all results to `RefLobbyScene.FindResult`.
- `SelectRoom.OnEntry` creates `RoomSelectMenu` and registers its receiver.
- `RoomSelectMenu.OnHandleInput` creates an integer UI data object for `SelectIndex` and sends `RequestInputCommand("OnDecide", Param, -1)`.
- The Select Room state indexes `FindResult` and calls `RequestJoinSession`, which invokes `SessionHub.JoinSession(mainUser, PlayerMatchName(), result)`.

## Runtime evidence and reports

### Semantic room-selection success

Report:

- `E:\myMods\reports\rollback_two_client_acceptance\rollback_two_client_acceptance_semantic-room-select-casual-20260710_player-match-lobby.json`

Full run (battle still failed):

- `E:\myMods\reports\rollback_two_client_acceptance\rollback_two_client_acceptance_semantic-room-select-casual-20260710.json`

Sandbox trace:

- `C:\Sandbox\prest\sc67\drive\E\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260710_135338_pid77524.jsonl`

### Stock join-complete callback proof

Sandbox trace:

- `C:\Sandbox\prest\sc67\drive\E\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260710_140210_pid85340.jsonl`

Observed timeline:

- `OnJoinSession(true)` fired twice at QPC `5581039369588` and `5581039403828`.
- The named lobby was `0x186000063418FC1` with named-session state byte `0` while active connect entered state `1`, then state `2`.
- No guest `LuxorSessionConnection` was created.
- Roughly 18.6 seconds later active connect reached state `5`, substate `9`, and disconnected.
- `OnDestroySession(true)` arrived roughly 20.056 seconds after join; after teardown the named session disappeared/reset.

This proves the native join-complete handler ran. The missing step is later in the transport handshake.

### Route-fix diagnostic

Host trace:

- `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260710_141404_pid42816.jsonl`

Sandbox trace:

- `C:\Sandbox\prest\sc67\drive\E\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260710_141420_pid67000.jsonl`

The host reached active state `3`, had a non-null session connection, applied route fixup once, and sent opcode `21` twice through the active endpoint. The guest emitted only opcode `0` heartbeat traffic, stayed in active states `1 -> 2`, never created a session connection, and returned to state `0`. Route fixup alone is not the solution.

### Unsafe transport-ready experiment

Reports:

- `E:\myMods\reports\rollback_release_gate\rollback_release_gate_semantic-transportready-routefix-casual-20260710.json`
- `E:\myMods\reports\rollback_two_client_acceptance\rollback_two_client_acceptance_semantic-transportready-routefix-casual-20260710.json`

Host/sandbox traces:

- `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260710_141855_pid33628.jsonl`
- `C:\Sandbox\prest\sc67\drive\E\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\Saved\ReplayTrace\replay_trace_20260710_141916_pid55584.jsonl`

Launch, menu, navigation, and lobby gates passed; battle failed, and the sandbox crashed/hung. Do not force the ready flag.

### Older comparison run

- `E:\myMods\reports\rollback_two_client_acceptance\rollback_two_client_acceptance_diagnostic-native-queryport27012-uijoin-rtfix-20260708-1553_direct-release.json`

This was useful proof of public host creation and owner-specific guest selection, including the false-then-true create callback pattern. It never reached battle and is not a passing rollback result.

## Root cause as currently narrowed

The UI is no longer the blocker. The remaining stock Casual failure is the guest-side Luxor transport handshake after successful public-room membership:

```text
host creates public room
  -> guest finds and semantically selects exact owner
  -> native JoinSession succeeds
  -> member join occurs
  -> native join-complete handler runs
  -> guest active connection enters state 1 then 2
  -> host sends opcode 21
  -> guest never proves receipt/dispatch and never builds LuxorSessionConnection
  -> timeout/disconnect
```

The next investigation should trace the **guest receive/dispatch path for opcode 21**, not add more menu pulses or force later readiness state.

## Known code issues to fix before trusting gates

An independent code review identified these concrete issues:

1. Callback synchronization occurs after `poll_search_results_and_join` in the service loop. The poller can therefore see stale success/failure state. Synchronize callbacks before polling, or explicitly advance the pipeline again after synchronization.
2. `sandbox_member_join_confirmed` currently treats room membership as transport readiness in several gates near lines 37657-37821. Membership is not a connection. Require `OnSessionConnectComplete` or independently proven active/session-connection state.
3. Host phase adoption validates a reusable named session, but readiness still depends on a current-phase callback. Add a native-evidence adoption branch. Where possible, also verify `HostingPlayerNum`/owner identity to reject a stale guest session.
4. Add analogous guest phase-start adoption if later phases intentionally reuse a previously joined room.
5. Remove or hard-disable manual join-complete and transport-ready compatibility calls. Neither is a legitimate acceptance mechanism.
6. Add self-tests for false->true create, valid host adoption, 5.2-second ping latency, ping retry, and exactly-once semantic room selection.

## Recommended continuation: two parallel routes

### Route A: finish stock Casual diagnostics

Use this for final public Player Match validation, because it proves the real two-account online lifecycle without invites.

1. Fix the callback ordering and readiness/adoption gate defects above.
2. In Ghidra, document the receive/dispatch counterpart for the host's opcode `21` send path. Follow the function V5 order and update names/types/comments only when evidence supports them.
3. Add read-only runtime hooks/traces at guest packet ingress, channel lookup, opcode dispatch, and the state transition that should allocate `LuxorSessionConnection`.
4. Correlate host opcode `21` send timestamps with guest ingress. Include route key/tag, endpoint identity, Steam user IDs, local user slot, and ports. Current intended ports are host UDP `27015` and sandbox UDP `27012`.
5. Compare against the known host state-3/session-connection path. Determine whether the packet is absent, rejected by route/channel lookup, rejected by identity, or dispatched without the expected state transition.
6. Keep route fixup diagnostic-only until guest receipt is proven. Never force the ready flag.
7. Only count the lobby phase as ready after native connection evidence, then proceed to stock battle setup.

### Route B: add a fast mirrored Local VS rollback harness

This is the preferred development loop for Horse UDP/Gekko because it avoids waiting on SC6's Luxor transport. It is not a substitute for the final stock Casual diagnostic, but it should make rollback iteration dramatically faster.

The exact invite-free menu route is proven by `DB_MainMenuList`:

```text
Down, Down, Down, Decide, Down, Decide
root index 3: EBATTLE
battle submenu index 1: EVERSUS
```

A faster reflected candidate exists: `MainMenuScene_C.OnTransitionVersus()` has zero parameters. Call it once from a stable main menu and require `VersusBattleSetupScene_C` to become current/queued. This direct transition still needs one live validation run.

Cooked exports on `VersusBattleSetupScene_C` include:

- `InitializeBattleSetup()`
- `OnSetupBattleLauncher()`
- `OnRequestToStart()`
- `OnStartCharaSelect()`
- `OnStartVersusInfo()`

Recommended synchronized sequence:

1. Complete an authenticated Horse UDP setup-ready barrier.
2. Call `OnTransitionVersus()` once per process.
3. Wait for stable `VersusBattleSetupScene_C`.
4. Reuse `patch_direct_battle_setup_selection()` for identical characters, colors, stage, finite rules, `bAutoStart=true`, and `bLocalBattleProvider=true`.
5. Set `ChangeBattleVersusType(PvP)` and an identical random seed.
6. Call `OnSetupBattleLauncher()` and `OnRequestToStart()` once.
7. Require stable `VersusBattleScene_C`, matching launch/epoch descriptors, and matching canonical baseline hashes before unfreezing simulation.

Ghidra also validates `execMakeVersusBattleSetting` at `0x140C86500`. It marshals both character codes/colors and stage code into core builder `FUN_1405D7FB0`, then commits the returned Lux data table. After a focused live test, `MakeVersusBattleSetting(...) -> LuxUIBattleLauncher.Setup(...)` may be cleaner than raw setup patching.

Do not simply classify all couch Versus as production PVP. Current lifecycle code accepts only presences `7/8`; Local Versus uses presence `5`. Add an explicit authenticated `MirroredVersus` mode that requires:

- successful Horse handshake;
- matching launch descriptor and random seed;
- `VersusBattleScene_C`;
- PvP launcher type;
- local-provider setup;
- matching epoch/canonical baseline;
- synchronized launch barrier.

Also separate `native_input_source_slot` from the Gekko network player slot. Both processes will normally read their physical controller from native P1, while host maps that input to Gekko slot 0 and guest maps it to Gekko slot 1.

Prior direct-stage report `direct-full-rerun-20260706-1` ran more than 11,000 frames of UDP/correction tests but remained in `TrainingBattleSetupScene_C`, and corrected hashes differed between clients. It proves useful local battle machinery exists, not that mirrored Local VS is canonically synchronized.

## Build and test status

Completed successfully during this work:

- `cmd /c build_and_deploy.bat`
- `python tools\rollback_two_client_report_selftest.py` (6/6)
- Existing UI navigation self-test, including the semantic-final-only case

Not passed:

- live two-process `player-match-battle`
- end-to-end canonical two-client rollback acceptance

Not rerun after the latest UI/harness changes:

- mandated strict replay seek test

Run it after the next relevant build/deploy:

```powershell
E:\myMods\tools\replay_seek_test_run.py --kill-game --launch-game --allow-unknown-presence --start-replay E:\myMods\ReplayExample\REPLAY_12744704008398858106.bin --timeline-generation-mode lux-no-render --case-preset watch --watch-frames 600 --wait --analyze --strict --min-resume-tick-rate 58 --resume-tick-window 120 --max-seek-validation-seconds 0.5
```

If shared timeline generation changes, compare `normal` and `lux-no-render` first and require zero oracle mismatches.

## Suggested first work session for the next agent

1. Read this handover, `RollbackP2PHarness.hpp`, the newest acceptance reports, and the rollback evidence table. Do not reset the dirty tree.
2. Build once to establish the baseline.
3. Patch callback ordering, membership-vs-connection gates, and adoption idempotency; add focused self-tests.
4. Keep all unsafe compatibility flags off.
5. Validate the zero-parameter `OnTransitionVersus()` route in one short two-process run. If it works, implement the explicit `MirroredVersus` launch mode and synchronized setup barrier as the fast development path.
6. In parallel, use Ghidra MCP to identify/document guest opcode-21 receive/dispatch and add read-only traces. Resume stock Casual only after the new observability exists.
7. Require a real visible battle, matching frame/epoch/canonical hashes on both clients, cross-matched actual inputs, and exactly-once presentation commits before declaring success.

## 2026-07-11 invite fallback correction

- A live attempt exposed a real privacy defect: the creation-window sequence sent Left while the cooked `privateSlot` value was already at its minimum `0 = Off`, and an unrelated player entered. Both clients were stopped immediately.
- Repository inputs `dump\SoulcaliburVI\Content\UI\Data\GameData\PlayerMatch\PlayerMatchRoomCreationWindowConfig_ui.{uasset,uexp}` decode the authoritative item contract. The corrected route is `9 Down / 1 Right / 9 Up / Decide`, which increments `privateSlot` to `1 = On` before lobby creation.
- Invite fallback now independently calls `SteamAPI_ISteamMatchmaking_SetLobbyType(lobby, k_ELobbyTypePrivate)` after native lobby ownership is sampled. The invite path is hard-gated on that successful return as well as the corrected GUI counts. This is defense in depth; no invite may be sent for an unconfirmed privacy setup.
- The prior sandbox attempt proved authenticated lobby offer, exactly-once `InviteUserToLobby`, successful `LobbyEnter`, and metadata request. It did **not** prove SC6 lobby membership or transport: the direct native invite-event call threw and was caught.
- Ghidra correction: `HandleSteamLobbyInviteAcceptedEvent @ 0x1429BDB90` expects `FOnlineSubsystemSteam*` at event `+0x10`. It invokes subsystem virtual slot `+0x18` to obtain the shared `FOnlineSessionSteam`, then queues through subsystem `+0x280`. `FOnlineSessionSteam` construction at `0x1429AA8A0` stores the owning subsystem at session `+0xA20`. The harness now resolves that owner instead of passing the online-session object itself.
- Do not claim the bridge complete until a fresh private live run proves the visible GUI value is On, `rollback_steam_lobby_private ok=true`, native conversion/delegate dispatch, equal named-session lobby IDs, member callbacks, and native transport on both peers.

## Definition of done

The handoff is complete only when all of the following are true:

- No repeated XInput/menu automation or global per-tick UObject scans are used for normal navigation.
- Both clients enter a stable visible match without Steam invites.
- The selected route has explicit, proven lifecycle and input ownership.
- Horse UDP/Gekko exchanges actual local inputs and produces real Save/Load/Advance events.
- Both clients agree on corrected frame, epoch, canonical gameplay hash, and cross-matched input streams.
- Snapshot restore gates include post-baseline, post-prediction, and final-restore equality.
- Failure remains fail-closed; no forced readiness and no stock-simulation fallback mid-round.
- Unit/self-tests, live two-process acceptance, build/deploy, and the mandated strict replay test all pass.
- Touched native functions have been rechecked in Ghidra with no fixable completeness deduction above ten points.

## 2026-07-11 configurable controlled-route selections

Character and stage selection is now an explicit operator contract for
`direct-connect` and `mirrored-versus`. The operator runner accepts
`--left-character`, `--right-character`, and `--stage`; tokens may be decimal,
hexadecimal, or normalized aliases. `steam-online` rejects the options rather
than changing stock Player Match selection.

Primary asset evidence copied from the full dump into the repository:

- `dump/SoulcaliburVI/Content/UI/GameFlow/GameScenes/BattleSetup/BattleSetupScene.{uasset,uexp}`
- `VersusBattleSetupScene`, `State/CharaSelectExecState`,
  `State/StageSelectExecState`, and their Chara/Stage phase pairs
- `DB_CharaSelectSlot`, `DB_StageSelectSlot`, `DB_BattleCharaSetup`,
  `DB_BattleStageSetup`, `FBattleCharaSetup`, and `FBattleStageSetup` pairs

`BlueprintToCpp` was configured for
`Content/UI/GameFlow/GameScenes/BattleSetup` and regenerated pseudocode for 13
assets. The decisive Kismet control-flow labels in generated
`BattleSetupScene.cpp` show:

- `OnStartVersusInfo` reads `DecidedCharaCode_L/R`, calls `ChangeChara` for
  both sides, calls `ChangeStage(DecidedStageCode)`, then consumes the values
  through `OnSetupBattleLauncher`.
- `OnSetupBattleLauncher` passes both character codes/colors and
  `DecidedStageCode` to `MakeVersusBattleSetting`.
- the stage-decision branch accepts a playable code, resolves random only for
  the explicit random entry, stores `DecidedStageCode`, and calls the stage
  decision UI path.
- `GetStageSelectedIndex` maps the selected stage code back through the
  stage-select object; no menu automation is needed by the native harness.

The data-table parser independently recovered packed stage IDs and map paths.
For example packed `0x009` maps to code `009` and
`/Game/Stage/STG009/Maps/STG009`, while packed `0x010` maps to code `010`.
Character inputs remain raw setup indices (`0 = Mitsurugi`, `5 = Sophitia`),
which the harness converts with the existing native character-to-setup-index
function before creating profile-backed player setup objects.

Implementation/reporting changes:

- `tools/sc6_launch_catalog.py` is the shared conservative resolver/catalog.
- Request files contain only resolved numeric `launch_left_character`,
  `launch_right_character`, and `launch_stage` values; `-1` means omitted.
- Direct mode applies each explicit field after replay-wrapper/default
  metadata. Mirrored mode overlays the authenticated launch descriptor.
- An explicit packed stage ID is tried exactly once. Registry failure no
  longer falls through to another/default stage.
- Setup writes are immediately read back, selection hashes are emitted, and
  two-client gates compare final selections and observed hashes.
- The mirrored launch trace now includes raw requested IDs, resolved character
  setup indices, observed stage, and descriptor hashes.

Local verification completed after these edits:

- `HorseMod` compiled successfully.
- `RollbackLaunchContractSelfTest` passed, including distinct character and
  stage hashes.
- `RollbackUdpRuntimeSelfTest` passed, including authenticated character and
  stage descriptor mismatch rejection.
- `sc6_launch_catalog_selftest.py` and
  `rollback_two_client_report_selftest.py` passed.

Still required before declaring live acceptance: deploy the current DLL, run
two non-default selections in each controlled route, visually verify both
fighters/arena, capture matching runtime readbacks on both peers, run the full
validation bundle and mandated strict replay test, then stop both clients and
crash reporters.

### Live selection evidence and remaining launch blocker

Artifact
`reports/rollback_two_client_acceptance/rollback_two_client_selection_direct_taki_sieg_stg009.json`
proves the selection layer on both real clients:

- requested raw characters `2` / `6` resolved natively to setup indices `3`
  / `7`;
- both profile-backed setup objects were created and assigned;
- packed stage `9` resolved exactly once, its active-map request succeeded,
  and the stage read back as `9`;
- both clients reported desired/observed selection hash
  `0xE148C7482B189034` with all readback gates true.

The run did not satisfy battle acceptance. After the successful selection
patch, the existing direct gameflow transition remained at
`CurrentSceneStopComplete` waiting for the battle scene. A second selection on
those already-failed clients produced UE4 crash dialogs during reconfiguration;
do not reuse a failed direct-stage process for the second-selection proof.
Restart both clients first.

Fresh-process repetition subsequently proved the contract is configurable,
not hard-coded. Artifact
`reports/rollback_two_client_acceptance/rollback_two_client_selection_direct_mitsu_voldo_stg010_fresh.json`
records both clients changing to:

- requested raw characters `0` / `4` (Mitsurugi / Voldo);
- native setup indices `1` / `5`;
- packed stage `0x10` (`STG010`), read back as decimal `16`;
- matching desired/observed selection hash `0xE991EAF495DF407D`.

This differs from the first live contract (`2/6`, setup `3/7`, stage `9`, hash
`0xE148C7482B189034`) on every selected field. Profiles, exact stage registry
lookup, active-map request, and all readback gates passed on both peers in both
runs. The fresh second run still reached the unrelated direct battle-scene
transition crash after selection proof; character/stage mutation itself had
already completed successfully.

Artifact
`reports/rollback_two_client_acceptance/rollback_two_client_acceptance_selection-mirrored-taki-sieg-stg009-r6.json`
proves the same non-default descriptor was authenticated and Horse UDP ready.
Both clients reached MainMenu and called `OnTransitionVersus()` exactly once,
but `VersusBattleSetupScene` never appeared. The host failed closed with
`VersusBattleSetupScene-timeout`; the peer then reported authenticated peer
readiness loss. No selection write was attempted in this mirrored run.

Runner corrections discovered by live testing:

- mirrored role-manifest now expects the `production` case;
- controlled-run default input delay is `1`, matching production config
  validity (the prior default `0` always produced
  `invalid-production-config`);
- fresh title launches may dispatch one semantic title decide, with no XInput
  pulses or per-tick retries. In the recorded run both clients already reached
  MainMenu, so this fallback remained unused.

Validation artifacts:

- full bundle: `reports/rollback_validation/rollback_validation_20260711-030527-955796.json`;
  all self-tests passed and strict replay passed. Its first stock-observe/live
  failures were caused by an orphaned process locking the request file.
- clean isolated reruns passed:
  `live_online_capture_readiness_selection-rerun.json` and
  `live_online_capture_trace_readiness_selection-rerun.json`.
- mandated strict replay passed in
  `reports/replay_tests/replay_seek_e2e_20260711-031216-seek.json` with 4/4
  cases, 2400/2400 watch frames, and zero state mismatches.
