# Replay Timeline Scrub Investigation - 2026-05-18

## User repro

- Generate Timeline, non-experimental button.
- After generation completed, click near the start of round 2.
- Initial crash: `EXCEPTION_ACCESS_VIOLATION reading address 0x00000018`.
- Stack included `main.dll!Horse::EBTracer::detour_mainsim()` and native SC6 replay/tick frames.
- After adding the round-boundary seek guard, the click no longer crashed.
- Follow-up result: characters stood still after seek/play.
- Enabling old bisection/speculative options then caused another crash.

## Current patch state

- `ReplayScrub` still nudges seeks off exact round-boundary frames, but it does
  not refuse earlier-round seeks. Cross-round support is now expected to come
  from the native UE4 demo checkpoint path rather than from HorseMod's raw
  snapshot restore.
- Risky broad restores are locked off:
  - chara+0x43F4..0x4428 restore
  - old UI-controlled one-shot PRA forward-bit force write
  - forced `bIsPlayingBack` toggle
  - broad speculative restore of WorldModePump/PRA/captured ReplayPlayer extras
- Normal seek now uses the native UE4 demo seek path first when the generated
  timeline has captured absolute demo time:
  - capture stores `UDemoNetDriver.DemoCurrentTime` per timeline tick
  - seek calls `UDemoNetDriver::GotoTimeInSeconds(driver, capturedTime, null)`
  - this is the engine path that rebuilds/replays packet state
- Optional ReplayPlayer cursor repair is default-off. When explicitly enabled,
  it writes:
  - `+0x39C int CurrentRound`
  - `+0x3A0 float CurrentTime`
  - `+0x3D0 bool bIsPlayingBack`
- The `ReplayPlayer` cursor is derived from the selected snapshot tags:
  - `CurrentRound = round_tag`
  - `CurrentTime = master_clock / 60.0f`
  - `bIsPlayingBack = 1`
- Native seek is async and latest-target-wins:
  - Replay tab checkbox: `Use native DemoNetDriver seek on timeline seek`
  - If `FGotoTimeInSecondsTask` is already active or `UDemoNetDriver+0x791` is
    busy, HorseMod keeps the newest requested timeline target and retries later.

## Important Ghidra findings

- `ALuxBattleReplayPlayer_RegisterProperties @ 0x14097BEB0`
  - Native registration says `CurrentTime` is a `FloatProperty` at `+0x3A0`.
  - The generated SDK header says `int32`; treat Ghidra as authoritative.
- `ALuxBattleReplayPlayer_Tick_AdvanceRoundResetSnapshot @ 0x140435C20`
  - Corrected Ghidra interpretation: this is `ALuxBattleReplayPlayer` tick, not
    `ALuxBattleFrameInputLog`.
  - `+0x39C` is `CurrentRound`, `+0x3A8` is `StateResetData`, and `+0x3B0` is
    `nTotalRounds`.
  - PRA bits 8/9 make this tick choose previous/next round reset data and copy a
    0xC0-byte round-reset snapshot into `BattleManager+0x1360`; it is not the
    per-frame character movement dispatcher.
  - Therefore the previous `force_pra_forward_hold()` idea was wrong and was removed.
- `UpdateFrameInputLogCacheLocalMode @ 0x1403F2AB0`
  - Local replay input cache writer.
  - In this context `pInputLog+0x39C` is used as an active-slot mask, not the snapshot playback cursor name used by `0x140435C20`.
- `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30`
  - Clears `pInputLog+0x3B8/+0x3BC` and refreshes per-slot current input from the `BM+0x450` frame-input actor.
- `GetCurrentInputForFrameInputLogSlot @ 0x1403F0680`
  - Reads `[BM+0x450]+0x3E0+slot*0x90` plus PRA-derived modifiers, then masks against `pInputLog+0x394`.
- `UDemoNetDriver_GotoTimeInSeconds @ 0x141E0ECA0`
  - UE4 native demo scrub primitive.
  - Queues `FGotoTimeInSecondsTask`, which should load/checkpoint and replay demo packets forward to the target time.
- `RegisterCVar_DemoGotoTimeInSeconds @ 0x140255B00`
  - Registers `demo.GotoTimeInSeconds`; setting it reaches the same native demo seek path without resolving a driver pointer directly.
- `Z_Construct_UClass_UWorld @ 0x1428A5B90`
  - Registers `UWorld.DemoNetDriver` at `UWorld+0xB8`.
  - HorseMod previously tried `FindFirstOf(L"DemoNetDriver")`, which can miss the
    active driver. `ReplayScrubDiag::read_demo_net_driver()` now prefers
    `ReplayPlayer->GetWorld()->DemoNetDriver`, then `GWorld->DemoNetDriver`,
    and only falls back to the object-array class search.
- `ProcessDemoGotoTimeTaskStart @ 0x141E1C980`
  - Saves old `DemoCurrentTime`, clamps target against demo total time, then asks the replay streamer for a checkpoint at target milliseconds.
- `HandleDemoGotoTimeCheckpointReady @ 0x141E027F0`
  - On failure restores old `DemoCurrentTime`.
  - On success stores checkpoint archive/time into the task for the poll/load phase.
- `ProcessDemoGotoTimeTaskPoll @ 0x141E1D010`
  - Waits for replay streamer readiness, then calls the heavy checkpoint loader.
- `LoadDemoCheckpointAndResumeAtScrubTime @ 0x141E13A00`
  - Tears down/rebuilds replay connection/actor state, clears GUID/cache state, loads checkpoint data, and advances demo packets toward the requested scrub time.

## Working theory

The round-2 crash was caused by seeking onto a low-master-clock later-round boundary while engine objects were in a transitional replay state. The boundary guard avoids that.

The standing-still symptom is different: HgCpuDirect/FrameInput restore can put chara state at the selected snapshot, and direct `ALuxBattleReplayPlayer.CurrentRound/CurrentTime` byte writes can make HorseMod's UI/timeline advance, but they do not rebuild the UE4 `UDemoNetDriver` packet stream that actually drives match-replay actors. The root fix is to queue `UDemoNetDriver::GotoTimeInSeconds` using the absolute `DemoCurrentTime` captured for that timeline frame. Direct ReplayPlayer byte writes remain as cursor repair, not as the motion source.

Fresh risk from the sub-agent review, now supported by Ghidra task tracing: direct `ALuxBattleReplayPlayer` byte writes may still be too shallow. `CurrentRound/CurrentTime/bIsPlayingBack` may be replicated outputs of the real `UDemoNetDriver` cursor, not the engine's source of truth. If so, they can update UI state while skipping the checkpoint/packet replay side effects that refill `RecordingData`, `StateResetData`, or `BM+0x460`.

Cross-round restore is a separate root problem. Ghidra shows `LuxBattle_HgCpuDirect_ExecFinalizeAndPost @ 0x140384540` rebuilds a pointer-fixup table from the *current live* P1/P2 chara globals, then decodes snapshot pointer references into that live graph before HorseMod's cursor/PRA repairs run. Restoring an earlier-round snapshot while the live viewer is parked in a later-round object/session context can fault inside this low-level restore. A proper cross-round fix likely needs an engine-level replay-driver round/checkpoint transition first, then same-round HgCpuDirect restore inside the rebuilt target-round context.

## Avoid

- Do not re-enable the old bisection toggles for normal testing.
- Do not use captured PRA/RP/chara extras as a broad seek restore.
- Do not assume `pInputLog+0x39C` has one global meaning; it is context-dependent across replay actor functions.
- Do not add blanket cross-round refusals as a "fix"; the user explicitly wants root-cause fixes.
  Cross-round attempts should either use a real engine round/checkpoint transition or log and
  fault-guard the low-level restore while investigation continues.

## 2026-05-18 follow-up patch

- Added an always-on concise seek line:
  `seek begin target_seq=... requested_tick=... tick=... round=... wall_tag=... master_tag=... paused=... rp_cursor=... native_demo=... demo_ms=... native_target_ms=...`
  This line is now verbose-only after the 23:55 performance fix.
- Guarded the experimental `demo.GotoTimeInSeconds` comparison path:
  if `ReplayScrubDiag` cannot resolve a readable `DemoNetDriver`, the CVar is not set.
  The previous log line `driver=0x0 rawCur -1` proved only that HorseMod set the CVar,
  not that native replay seeking accepted a task.
- Added a post-KO cross-round refusal:
  if `g_LuxBattle_LastRoundResultType != 0`, the live `CurrentRound` is readable, and
  the target timeline round differs, the manual restore is refused instead of crossing
  from round-result/post-match state into another round.
- Added generation safe parking:
  while the final round is live (`LastRoundResultType == 0`) the generator records
  safe final-round seqs. On successful completion it parks to `last_safe - 30`
  clamped to the first safe final-round seq, clears only `LastRoundResultType`, and
  clamps normal UI/seeks to that usable latest seq so captured post-KO tail frames are
  diagnostics-only.
- Defensive follow-up before testing:
  `collect_round_markers()` now also clamps to the usable latest seq. The timeline
  bar, drag target, step buttons, `request_seek()`, `find_slot_for_seq()`, and marker
  drawing now share the same view of the usable range.
- 22:38 test follow-up:
  same-round last-round seeks restored successfully but playback idled. A narrow
  same-round PRA forward nudge was tried, then removed after the later Ghidra recheck
  showed that PRA bit was a round-reset/navigation signal, not the movement source.
  The 22:38 log showed round-3 restores faulting repeatedly while the live context was
  parked in round 4. A temporary cross-round refusal was removed per user direction;
  cross-round attempts now log live/target round context and continue into the guarded
  restore path for root-cause diagnosis.
- 22:49 UI playhead fix:
  after Generate, capture is off, so the UI used to pin to `m_last_seek_target` while
  the SC6 timer kept advancing. `current_play_position()` now maps live
  `(CurrentRound, InputLog master clock)` back onto the generated tag timeline.
- 22:59 same-round playback false lead:
  Ghidra initially suggested `PlayerRecordArray+0x398` bit 9 gated a per-frame
  visual replay dispatcher. Rechecking against `ALuxBattleReplayPlayer` property
  registration showed that function is actually round-reset navigation
  (`CurrentRound`/`StateResetData`), not per-frame motion. The PRA hold was removed.
- 23:02 DemoNetDriver resolver fix:
  Ghidra identified the active UE4 demo driver as `UWorld->DemoNetDriver` at
  `UWorld+0xB8`. The diagnostic/native-comparison path now reads that pointer first,
  so native seek should no longer skip merely because `FindFirstOf(L"DemoNetDriver")`
  returned null.
- 23:18 native seek root fix:
  `m_use_demo_goto_time_seek` now defaults ON, capture stores absolute demo time, and
  seek calls `UDemoNetDriver::GotoTimeInSeconds` directly instead of writing the CVar
  with round-local `master/60`.
- 23:35 ordering/retry fix:
  Native seek now runs before the legacy HgCpuDirect snapshot restore and returns
  without applying that restore. This lets UE4 rebuild the replay context before any
  optional exact-state work, and avoids the cross-round pointer-fixup crash path.
  The busy-task check now reads `UDemoNetDriver+0x791`, `+0x7B0`, and `+0x7B8`; if
  the driver is busy, the newest requested demo timestamp stays pending and retries
  on later cockpit ticks.
- 23:45 actor-rebuild fix:
  The native checkpoint path can replace `LuxBattleManager` / `LuxBattleReplayPlayer`.
  New-replay detection now preserves the generated timeline during a native seek guard
  window or while a native seek target is pending, and only updates the cached actor
  identities. The UI seek target is recorded as the actual seek target only when
  `GotoTimeInSeconds` is submitted; deferred retries reapply the ReplayPlayer cursor
  immediately before submitting the newest target.
- 23:55 performance/playhead fix:
  A timeline generated before exact `DemoCurrentTime` capture had `demo_ms=-1`, causing
  native seek to refuse every drag tick and spam the log. The temporary
  monotonic `seq/60` fallback was later removed because it could make Play
  unlock on the wrong native replay time. Old timelines now show
  `no captured demo time` and must be regenerated. Seek-begin logs are
  verbose-only. Deferred native retries are throttled to one attempt every 5 cockpit
  ticks while the driver is busy/unresolved. The UI playhead no longer re-reads engine
  state while the timeline bar is actively held, so it stays under the mouse instead of
  snapping back to the live/end position between refused or deferred seek attempts.
- 00:15 lag follow-up:
  `EBTracer` was disabled by default because it was logging from hot sim hooks, but
  user retest showed this was not the capture FPS regression.
- 00:20 actual capture FPS root cause:
  Capture had started calling full `ReplayScrubDiag::read_demo_net_driver()` every
  captured tick to record `DemoCurrentTime`. In SC6 this often falls through to
  `FindFirstOf` probes for several demo-driver class names; each probe walks the full
  UObject array. During Generate/Capture that meant repeated object-array scans in the
  capture hot path. Capture now uses a raw `GWorld->DemoNetDriver(+0xB8)` fast read
  with SEH-guarded offset reads only, and the expensive fallback probe is throttled.
  The RegionStore self-test window was also reduced from 128 ticks to one tick so the
  initial Generate/Capture phase no longer gathers/memcmps every region each frame.
- 19:30 native seek resolver follow-up:
  The 00:31 log still showed `driver unresolved` after Generate. Rechecking Ghidra
  showed `UWorld::DemoNetDriver` at `+0xB8`, but `UWorld_Tick` also iterates
  `UWorld.LevelCollections` at `+0x120` with an observed `0x80` stride. The
  reflected `FLevelCollection` constructor confirms struct size `0x80`,
  `NetDriver +0x10`, and `DemoNetDriver +0x18`; HorseMod now checks both the
  direct `UWorld` field and each active level collection. The hot capture path first
  tries `ReplayPlayer->GetWorld()`, then `GWorld`, and caches the validated driver
  pointer, so exact `DemoCurrentTime` capture no longer depends on an object-array
  scan.
  If the direct pointer still cannot be resolved at seek time, HorseMod now falls
  back to the registered native CVar `demo.GotoTimeInSeconds`, which Ghidra showed
  reaches the same `UDemoNetDriver::GotoTimeInSeconds` task path. This fallback is
  less observable than direct pointer submission, but it is still an engine-native
  checkpoint/packet seek and avoids the shallow ReplayPlayer-only cursor path.
- 19:55 pre-test hardening:
  Ghidra confirmed `UDemoNetDriver::GotoTimeInSeconds` queues work immediately
  through the task array at `UDemoNetDriver+0x7A8/+0x7B0`, while the current task
  pointer lives at `+0x7B8` and the busy byte at `+0x791`. HorseMod now SEH-wraps
  the direct native call, then requires an observable task/busy-state transition
  before it records the seek as accepted. ReplayPlayer cursor byte writes are no
  longer part of the default path and are never done by the unresolved-driver CVar
  fallback; this prevents a failed native seek from looking successful only because
  shallow replicated cursor fields were changed.
- 2026-05-20 state-machine rewrite:
  The timeline scrubber now separates three states explicitly:
  UI requested playhead, visual preview snapshot, and native replay authority.
  The old replay tab kept local `static` playhead/drag flags and refreshed the
  playhead from `current_play_position()` after mouse release. When native seek
  was unresolved/deferred, `current_play_position()` fell through to the generated
  timeline edge, so releasing a drag could snap the UI to the end even though the
  user selected an older tick.
  `ReplayScrub` now owns `UiPlayheadState`, `PreviewState`, and `NativeSeekState`.
  The replay tab renders `timeline_view()` and posts intent APIs
  (`ui_begin_drag`, `ui_drag_to_seq`, `ui_end_drag`, `ui_step_to_seq`,
  `ui_pause_at_live`, `ui_request_play`) instead of holding UI-local authority.
  Dragging updates requested/displayed seq immediately and release no longer
  recomputes from `latest_seq()`.
  Preview restore is visual-only: it can restore the captured snapshot while
  paused, but it does not write `m_last_seek_target` and cannot mark native seek
  successful. `m_last_seek_target` is now only written when the native settle
  window observes a readable `DemoNetDriver` that is no longer busy and whose demo
  time has reached the current requested target.
  Native seeks are generation-tagged. Pending retries, direct submissions, CVar
  submissions, and settle windows carry the generation of the latest requested
  target; stale native results are logged and ignored instead of unlocking Play.
  The CVar fallback may submit a native request, but it does not unlock Play
  unless landing is later verified through a readable driver.
  The Play button now calls `ui_request_play()` and is blocked while native status
  is queued/submitted/settling/failed for the selected tick. Pausing at the live
  edge is treated as landed because no seek is required; seeking to older ticks
  requires native landing before playback is allowed.
  Subagent review found four follow-up issues and they were addressed before
  handoff:
  round-boundary adjustment now republishes the adjusted safe seq as the selected
  target so a real native landing can unlock Play; UI-facing drag/auto-play flags
  are atomic instead of plain fields mutated from both UI and cockpit threads;
  stale preview display writes are generation/request checked; and `drop_ring()`
  clears landed target/master authority with the rest of the scrub state.
- 2026-05-20 disabled-Play / driver-unresolved follow-up:
  Runtime logs showed the Play button was blocked for the correct reason:
  native replay authority never landed because `DemoNetDriver` was unresolved,
  and generated ticks still had `demo_ms=-1`. The fix did not weaken the Play
  gate. `ReplayScrubDiag` now exposes a report-based resolver that records the
  source tried, world pointer, container pointer, candidate driver pointer,
  task-field readability, time-field readability, and failure reason for each
  path. The resolver tries the cached driver, `ReplayPlayer->GetWorld()` outside
  the capture hot path, cached world, `GWorld`, `UWorld.DemoNetDriver`,
  `UWorld.LevelCollections[*].DemoNetDriver`, and finally non-hot object-array
  probing. Reports are logged once at Generate start and once on seek failure.
  The capture hot path uses `read_demo_net_driver_fast()`, which only uses cached
  pointers/GWorld and guarded raw reads; it does not run `FindFirstOf` or object
  array probes per captured frame. If the fast resolver cannot read
  `DemoCurrentTime`, Generate stores `demo_ms=-1` and logs that once per
  generation. Seeks against such a timeline are blocked with
  `no captured demo time`; the timeline must be regenerated after this build
  before exact native timestamps can exist.
  Candidate validation no longer rejects a driver solely because
  `DemoCurrentTime` or `DemoTotalTime` look odd. The raw time fields are
  diagnostic; the task/busy fields are the acceptance signal for native seek
  submission. Seek logs now include `+0x791`, `+0x794`, `+0x7A8`, `+0x7B0`,
  `+0x7B4`, and `+0x7B8` before/after `GotoTimeInSeconds`, so the next runtime
  test can distinguish unresolved driver, busy driver, missing task transition,
  and settle timeout. The replay UI now displays the specific block reason
  (`driver unresolved`, `driver busy`, `task not observed`, `settle timed out`,
  `no captured demo time`, etc.) instead of only `play blocked`.
  Ghidra MCP was checked during this pass, but no running instance was available,
  so no new labels/prototypes were applied. The offsets still requiring a live
  Ghidra verification pass are `GWorld`, `UWorld.DemoNetDriver`,
  `UWorld.LevelCollections`, `FLevelCollection.DemoNetDriver`,
  `UDemoNetDriver.DemoCurrentTime`, `UDemoNetDriver.DemoTotalTime`,
  `UDemoNetDriver::GotoTimeInSeconds`, and the task/busy fields around
  `FGotoTimeInSecondsTask`.
- Subagent review follow-up for this pass:
  The review found five strict-authority gaps and they were addressed before
  handoff. Missing `DemoCurrentTime` now blocks native seek instead of falling
  back to `seq/60`; the legacy snapshot path no longer publishes native
  `Landed`; unresolved direct driver seeks still try the native
  `demo.GotoTimeInSeconds` CVar path but remain gated on readable-driver settle;
  cached object-array demo-driver state is invalidated with the raw driver/world
  cache; and background native retry failures publish the current block reason so
  the UI does not stay on a stale status.
- 2026-05-20 Ghidra MCP verification pass:
  MCP direct TCP connect succeeded against `SoulcaliburVI.exe` at image base
  `0x140000000`. The load-bearing replay-seek offsets used by HorseMod were
  verified:
  - `GWorld @ 0x1443B4DB8`, matching runtime RVA `0x43B4DB8`.
  - `Z_Construct_UClass_UWorld @ 0x1428A5B90` registers
    `UWorld.LevelCollections` at `+0x120` and `UWorld.DemoNetDriver` at
    `+0xB8`.
  - `Z_Construct_UScriptStruct_FLevelCollection @ 0x1428B4BA0` confirms
    `FLevelCollection` size `0x80`, `GameState +0x08`, `NetDriver +0x10`,
    `DemoNetDriver +0x18`, `PersistentLevel +0x20`, and `Levels +0x28`.
  - `UDemoNetDriver_GotoTimeInSeconds @ 0x141E0ECA0` takes
    `(UDemoNetDriver*, float TimeInSeconds, FOnGotoTimeDelegate*)`, checks
    `UDemoNetDriverHasTaskNamed(..., "FGotoTimeInSecondsTask")`, also checks
    `driver+0x791`, allocates a `0x28`-byte task, stores target seconds at
    `task+0x14`, and queues it with `QueueDemoNetDriverTask`.
  - `UDemoNetDriverHasTaskNamed @ 0x141E13590` checks current task
    `driver+0x7B8`, then scans task array data `driver+0x7A8` using count
    `driver+0x7B0`.
  - `QueueDemoNetDriverTask @ 0x141E01CB0` confirms task-array layout:
    data pointer `+0x7A8`, count `+0x7B0`, max/capacity `+0x7B4`, current
    task pointer `+0x7B8`.
  - `ProcessDemoGotoTimeTaskStart @ 0x141E1C980` confirms task layout:
    `task+0x08` driver, `task+0x10` saved old `DemoCurrentTime`,
    `task+0x14` target seconds. It reads/writes driver `DemoCurrentTime +0x418`
    and clamps against `DemoTotalTime +0x414`.
  - `LoadDemoCheckpointAndResumeAtScrubTime @ 0x141E13A00` confirms
    `DemoCurrentTime +0x418`, `DemoTotalTime +0x414`, busy/task flag writes at
    `+0x791`, and checkpoint/loading flag writes at `+0x794`.
  Ghidra updates made: set the prototype for `QueueDemoNetDriverTask`, added
  plate comments to `QueueDemoNetDriverTask`, `UDemoNetDriverHasTaskNamed`,
  `ProcessDemoGotoTimeTaskStart`, and `LoadDemoCheckpointAndResumeAtScrubTime`,
  added a disassembly comment at `GWorld`, and saved the program.

## Next test expectation

With `Use native DemoNetDriver seek on timeline seek` enabled, seek near round 2 should:

- avoid the crash,
- log `native DemoNetDriver::GotoTimeInSeconds submitted`,
- or log `native demo.GotoTimeInSeconds CVar submitted` if the direct pointer is still unresolved,
- keep the UI playhead on the selected tick after drag release,
- keep Play blocked until `native seek settle landed` is logged for that selected tick,
- show `POST_SEEK_TICK` ReplayPlayer/UDemoNetDriver state near the target when verbose diagnostics are enabled,
- resume character motion because UE4's replay driver reloads/replays packet state.

If it still stands still, inspect whether the submitted native task is actually
accepted and processed: `DemoCurrentTime`, `+0x791`, `+0x7B0`, and `+0x7B8` should
change as the task runs. A missing `demo_ms` means the timeline was generated by an
older build or the active driver was unresolved at capture time; HorseMod blocks
Play with `no captured demo time` for those timelines. Regenerating with this
build is required so exact native demo timestamps are present.

Rapid dragging is supported by pending-target retry. `UDemoNetDriver_GotoTimeInSeconds`
silently drops requests while an `FGotoTimeInSecondsTask` is already queued or
`UDemoNetDriver+0x791` is set, so HorseMod now avoids calling during that state and
keeps retrying the newest target instead.

## 2026-05-19 native seek ownership pass

- Ghidra recheck of `ProcessDemoGotoTimeTaskStart @ 0x141E1C980` and
  `LoadDemoCheckpointAndResumeAtScrubTime @ 0x141E13A00` confirmed the native
  seek is asynchronous: `GotoTimeInSeconds` only queues `FGotoTimeInSecondsTask`;
  later UE4 demo-driver ticks load the checkpoint and replay packets to the target.
- HorseMod was able to queue or attempt native work while its pause gates remained
  engaged, which can prevent the queued task from landing until playback resumes.
  Added a native-seek settle window: the UI may remain logically paused, but
  `is_scrub_active()` temporarily releases the WorldTick/ReplayClock/Actor gates
  for up to 120 frames after a native seek is submitted. When the task is no
  longer busy and the driver time is near the requested target, the settle window
  ends and pause re-freezes on the landed state.
- The direct driver resolver no longer rejects a `UWorld` / `FLevelCollection`
  driver pointer solely because `DemoCurrentTime` / `DemoTotalTime` look odd.
  Those fields are needed for exact timestamp capture, but Ghidra shows the
  call safety decision depends on the driver pointer and task/busy state
  (`+0x791`, `+0x7B0`, `+0x7B8`). A strict time sanity check was causing a real
  pointer with unusual timing fields to look unresolved.
- Removed the default visual-only legacy fallback when native seek is enabled.
  If native seek cannot queue, HorseMod now keeps the native request pending
  instead of applying an HgCpuDirect preview that can make the characters appear
  moved while the engine's real replay cursor is still at the wrong time.
