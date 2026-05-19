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
  native seek to refuse every drag tick and spam the log. Native seek now falls back to
  monotonic `seq/60` for old timelines and logs that warning only once. Seek-begin logs
  are verbose-only. Deferred native retries are throttled to one attempt every 5 cockpit
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

## Next test expectation

With `Use native DemoNetDriver seek on timeline seek` enabled, seek near round 2 should:

- avoid the crash,
- log `native DemoNetDriver::GotoTimeInSeconds submitted`,
- or log `native demo.GotoTimeInSeconds CVar submitted` if the direct pointer is still unresolved,
- show `POST_SEEK_TICK` ReplayPlayer/UDemoNetDriver state near the target when verbose diagnostics are enabled,
- resume character motion because UE4's replay driver reloads/replays packet state.

If it still stands still, inspect whether the submitted native task is actually
accepted and processed: `DemoCurrentTime`, `+0x791`, `+0x7B0`, and `+0x7B8` should
change as the task runs. A missing `demo_ms` means the timeline was generated by an
older build or the active driver was unresolved at capture time; HorseMod falls back
to monotonic `seq/60` for those older timelines, while regenerating with this build
gives exact native demo timestamps.

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
