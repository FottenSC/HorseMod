# Rollback Netcode Methods Investigation - 2026-05-19

## Scope update

Transport is out of scope. Assume GekkoNet or another layer supplies:

- local input for frame `F`,
- confirmed remote input for frame `F`,
- notification that an older predicted frame was wrong,
- max rollback frame count.

HorseMod's job is only the local engine:

- save battle state,
- restore an older state,
- inject corrected inputs,
- run the SC6 simulation forward quickly until it reaches the present frame.

## Executive conclusion

The replay timeline tech is useful for rollback, but not as-is.

The usable primitive is `LuxBattle_HgCpuDirect_*`: SC6 already has native
state-save/state-restore for battle/chara/global/timer/physics/VFX state.
The unsafe assumption would be that restoring that blob alone is equivalent to
rollback. It is not. Correct rollback also needs the input cache, BM catch-up
cursors, round-state driver, and a deterministic one-frame resimulation path.

The stock online path is delay-based:

- sends compact per-frame input packets,
- keeps a 512-frame per-player input cache,
- resends historical input windows,
- stalls when remote input is absent,
- has no content hash/checksum exchange,
- has no save/restore/resim loop.

So the most viable plan is:

1. Reuse SC6/HorseMod snapshot primitives for same-round save/restore.
2. Add a deterministic one-frame simulation harness.
3. Inject local/remote input directly into the same frame boundary SC6 normally
   consumes.
4. On correction, restore the right snapshot and run frames forward in a tight
   catch-up loop with rendering suppressed.
5. Keep Unreal/actor lifecycle stable; rollback only within the active round.

## Ghidra anchors

Rollback/replay transport bookmarks were added in category `RollbackNetcode`.
Rollback/resimulation bookmarks were added in category `RollbackResim`.

- `LuxBattle_HgCpuDirect_ExecMoveChangeAndPost @ 0x1403841E0`
  - state-save candidate, current snapshot stride `0x28018`.
- `LuxBattle_HgCpuDirect_ExecFinalizeAndPost @ 0x140384540`
  - state-restore candidate; restores into the current live object graph.
- `LuxBattle_PerFrameTick @ 0x1402DBC60`
  - primary one-frame resim boundary candidate. Runs input prep, chara
    simulation, hit resolution, camera/timer/VFX, and increments
    `g_LuxBattle_FrameCounter`.
- `LuxBattle_TickCharaInput @ 0x140312510`
  - authoritative input commit inside `PerFrameTick`. Reads
    `g_LuxBattle_LatestEngineInput_PerPlayer` and writes chara input fields
    `+0x2150/+0x2158` plus held-frame derivatives.
- `LuxBattle_TickCharaMainSimulation @ 0x14034DA70`
  - main per-chara sim tick. Necessary but not sufficient by itself.
- `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`
  - fixed-step/catch-up driver that consumes cached inputs and advances round state.
- `LuxBattleManager_TickInputPipelineDispatcher_VTableC80 @ 0x1403FDB80`
  - InputLog per-tick dispatcher. Needed only if the rollback harness uses the
    normal InputLog cache path.
- `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30`
  - refreshes `pInputLog+0x3B8` from BM+0x450 frame input records.
- `UpdateFrameInputLogCacheLocalMode @ 0x1403F2AB0`
  - fills `pInputLog+0x3C0` from `+0x3B8`, indexed by master clock.
- `LuxOnline_DrainRingBuffer_DecodeInputPackets_AndUpdateCache @ 0x1403F6770`
  - receive-side stock online input packet drain. Useful as a reference only if
    GekkoNet is bypassing stock transport.
- `LuxOnline_SendInputPacket_PerFrame_Opcode0 @ 0x1403F84E0`
  - stock online input packet sender. Not needed for GekkoNet transport.
- `LuxOnline_SendInputPacket_BatchedRange_Opcode1 @ 0x1403F8710`
  - stock input-history resend. Not needed for GekkoNet transport.

## Existing state/data primitives

Current ReplayScrub per tick captures:

| Region | Bytes | Rollback relevance |
|---|---:|---|
| HgCpuDirect sim blob | `0x28018` = 163,864 | Main battle state candidate |
| InputLog window | `0x4084` = 16,516 | input cache/cursors |
| ReplayDataBlock | 1,021 | replay-viewing decoder only; probably not live online |
| Extras | `0x208` = 520 | BM cursors, FrameInput records, ReplayPlayer diagnostics |

For rollback in live online, the likely minimum state is smaller than replay
scrub but still larger than HgCpuDirect alone:

- HgCpuDirect state blob.
- `ALuxBattleFrameInputLog` cache/cursors needed by online mode:
  - `+0x398` active slot count,
  - `+0x39C` active/player mask,
  - `+0x3A0` last frame id,
  - `+0x3A4` master clock,
  - `+0x3C0..+0x43BF` 512-entry input cache per player,
  - `+0x4400` online-active role flag,
  - `+0x4404` sync/guard byte,
  - `+0x4410` drain cursor,
  - `+0x4414` min store frame watermark.
- BM simulation/catch-up cursors:
  - `BM+0x1488` last frame id,
  - `BM+0x148C` last applied/master cursor,
  - `BM+0x1490` frame advance counter,
  - input arrays around `BM+0x1498/+0x14A8/+0x14C8`.
- RNG/desync-sensitive globals if not already covered by HgCpuDirect.

The ReplayDataBlock is important for match-replay viewing, but live online
uses `ALuxBattleFrameInputSync` / `ALuxBattleFrameInputLog` and the online
drain path instead. It should not be assumed necessary for live rollback until
runtime tracing proves it.

## Stock online model

`ALuxBattleFrameInputSync` defaults:

- `InputLagMin = 2`
- `InputLagMax = 15`
- `InputLagOffset = 2`
- `InputLag = 2`
- `RTTInterval = 60`
- `SyncTimeOut = 120`

Important detail: `InputDelay` at `ALuxBattleFrameInputLog+0x390` is not the
real online delay knob in observed builds. It is constructor-zeroed and the
known “fake frame” NOP patch is effectively a placebo. The live delay window is
encoded by the 4-bit frame-low packet field and the cache/watermark logic.

`LuxOnline_SendInputPacket_PerFrame_Opcode0 @ 0x1403F84E0` sends one compact
packet per frame per active slot:

- header low nibble: `frameId & 0xF`
- header high bits: `slot`, opcode, sender/munge key
- payload: one input byte

`LuxOnline_SendInputPacket_BatchedRange_Opcode1 @ 0x1403F8710` resends a range
of historical inputs from the `+0x3C0` cache.

`LuxOnline_DrainRingBuffer_DecodeInputPackets_AndUpdateCache @ 0x1403F6770`
drains packets on the game thread and writes cache entries. This is the right
place to observe or augment remote input arrival.

`LuxBattleManager_UpdateOnlineFrameSyncCounter_At1638 @ 0x1403FDEC0` increments
stall telemetry when the sim cannot advance because no remote input is present.
That is delay-netcode behavior, not rollback.

## Method A - Direct PerFrameTick rollback harness

This is the fastest first experiment with GekkoNet, but it has an important
caveat: `LuxBattle_PerFrameTick` is not the whole UE frame. The BM actor tick
`LuxBattleManager_Tick_MainStateMachine_At1461 @ 0x1403FBF30` runs in parallel
later in the frame and calls `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState`.
That path advances BM round-state, frame counters, and some online/sync state.

So Method A is a hypothesis to test, not a proven final loop.

Do not drive the stock online InputLog path initially. Treat SC6 like an offline
deterministic simulator and feed both players' inputs into the same boundary that
normal local play uses.

Per local frame:

1. GekkoNet/rollback controller chooses the input pair for frame `F`.
2. Build a 24-byte `FLuxBattlePerFrameTickArgs`:
   - `+0x00 = &inputP1Qword`,
   - `+0x08 = &inputP2Qword`,
   - `+0x10 = camera/axis args`, likely copied from the live tick's args or zeroed
     if camera input is irrelevant.
3. Call or let SC6 call `LuxBattle_PerFrameTick(args)` exactly once.
4. Capture post-frame state with `HgCpuDirect_ExecMoveChangeAndPost`.
5. Store `{frame, inputs, predicted flags, snapshot, hash}`.

On late remote input arrival:

1. If it matches prediction, mark confirmed and do nothing.
2. If it differs:
   - restore the snapshot before the wrong frame,
   - overwrite the real remote input in the history,
   - run `LuxBattle_PerFrameTick(args)` forward to present using real inputs where known and
     predictions where still unknown.

Requirements:

- A one-frame deterministic advance harness.
- Ability to call or re-enter the `PerFrameTick` body without letting UE render or
  duplicate actor ticks.
- Round-trip snapshot determinism test.
- State hash to detect silent divergence.
- A way to keep normal UE ticking/rendering paused while a rollback catch-up loop
  runs several sim frames.

Pros:

- Bypasses stock delay/stall online behavior.
- Uses the same local input boundary as offline versus.
- Keeps the rollback controller simple: input pair in, one SC6 frame out.

Cons:

- Need to identify the cleanest call point and avoid duplicate side effects.
- Need to provide a valid camera/axis arg or prove it can be stable/zero.
- Need to pause the normal actor tick pipeline during manual catch-up.
- May miss BM actor-tick side effects; determinism tests must catch this.

### Generate Timeline EXP2 probe

Implemented in HorseMod on 2026-05-19 as a separate `Experimental 2` button in
the Replay Scrub Generate Timeline UI.

This is intentionally a proof probe before a full direct-step generator:

1. Save the live replay state through the same regions used by ReplayScrub:
   HgCpuDirect sim blob, InputLog window, ReplayDataBlock, and extras.
2. Call `LuxBattle_PerFrameTick` once through a CodeCave bypass trampoline.
   The bypass reproduces the first seven original prologue bytes and jumps to
   `LuxBattle_PerFrameTick+7`, so it still runs if WorldTickGate has patched
   the public entry to a freeze RET.
3. Use the current `g_LuxBattle_LatestEngineInput_PerPlayer` qwords as the
   frame inputs and pass null camera/axis args.
4. Capture the post-step sim hash and timing.
5. Restore the saved state immediately.

The probe logs:

- whether the direct call faulted,
- whether restore succeeded,
- wall-frame and master-clock before/after,
- P1/P2 input qwords,
- pre/post HgCpuDirect sim hashes,
- direct step microseconds and total probe microseconds.

This does not yet generate a full timeline. It answers the first load-bearing
question: can HorseMod safely execute the authoritative chara-side battle frame
boundary manually from the replay UI without letting the normal UE frame own the
call? If this probe is stable, the next step is to add a loop that supplies a
known input stream and captures each post-step state, then compare hashes
against the normal render-skip generator.

## Method A2 - Full battle-frame rollback harness

This is the more defensible long-term target if Method A fails determinism.

A single rollback resim frame should reproduce the relevant work of one normal
battle frame:

1. InputLog/FrameInput tick updates input mirrors/caches if using that path.
2. `LuxBattle_PerFrameTick(args)` advances chara simulation.
3. `LuxBattleManager_Tick_MainStateMachine_At1461(pBM, delta)` advances BM
   SimulationLoop, round-state, frame counters, and timer/online-sync siblings.
4. Any load-bearing actor ticks discovered by hash mismatch are added, but
   rendering, Slate, UMG, and scene redraw stay skipped.

This is slower than Method A but still much faster than full UE rendering. It
matches the existing freeze work: HorseMod already had to gate both
`PerFrameTick` and BM Actor::Tick because either side advancing alone causes
drift.

The likely implementation shape is a detour around a stable per-frame hook:

- At the beginning of a normal frame, if no correction is pending, inject the
  predicted input pair and let the original frame run.
- If correction is pending, suppress visible presentation, restore the snapshot,
  call the battle-frame step sequence repeatedly with historical inputs until
  caught up, then let the current normal frame continue.

The open question is the exact minimal actor-tick subset. Start with
`PerFrameTick + BM MainStateMachine`; add more only when the hash test proves a
missing side effect.

## Method B - InputLog/BM SimulationLoop rollback harness

This drives the stock `ALuxBattleFrameInputLog` / BM catch-up route instead of
calling `PerFrameTick` directly.

Per resim frame:

1. Write desired inputs into BM+0x450 frame-input records or directly into
   `pInputLog+0x3B8`.
2. Ensure `UpdateFrameInputLogCacheLocalMode` stores the inputs into
   `pInputLog+0x3C0` at the target master-clock index.
3. Set BM/InputLog cursors so
   `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState` sees
   `delta == 1`.
4. Let the SimulationLoop advance round state and input processor state.
5. Let `PerFrameTick` run the chara simulation once.

Pros:

- Closer to SC6's replay/online architecture.
- Preserves BM round-state sequencing naturally.

Cons:

- More moving pieces and cursors to restore.
- Easier to produce off-by-one input/cache mistakes.
- Likely slower than direct PerFrameTick because more actor/InputLog machinery
  must remain active.

This is a good fallback if direct `PerFrameTick(args)` fails determinism because
the BM/InputLog side has hidden required side effects.

## Method C - Hybrid direct input + BM cursor repair

Use direct `PerFrameTick(args)` as the simulation step, but still restore/repair
BM/InputLog cursor fields around rollback so UI, round-state, and engine
bookkeeping do not drift.

This may be the practical version:

1. Snapshot HgCpuDirect state.
2. Snapshot a compact cursor block:
   - InputLog `+0x394..+0x4414` initially,
   - BM `+0x1488/+0x148C/+0x1490`,
   - maybe BM input arrays `+0x1498/+0x14A8/+0x14C8`.
3. On restore, restore both.
4. Run direct `PerFrameTick(args)` for resim.
5. After each manual step, update BM/InputLog frame counters to match the new
   frame if the direct call did not naturally advance them.

Pros:

- Likely fastest viable route.
- Keeps round-state/cursor drift visible and fixable.

Cons:

- Needs runtime hash tests to learn which cursor repairs are required.
- Risk of masking a missing real side effect with manual counter writes.

## Method D - Replay-style timeline checkpoint rollback

This is mostly a trap for live netcode.

ReplayScrub can store a long, deduped history and seek around it, but online
rollback needs very fast restore/resim every time remote input differs from
prediction. The per-tick raw state is large:

- sim snapshot: 163,864 bytes,
- input state: 16,516 bytes,
- plus small cursor regions.

This may still be acceptable for a short rollback window:

- 8 frames: about 1.45 MB raw before dedup,
- 12 frames: about 2.18 MB raw,
- 15 frames: about 2.73 MB raw.

Memory is not the blocker for a 8-15 frame window. The blockers are:

- restore speed,
- resim speed,
- deterministic equality,
- correct input injection,
- avoiding UE/actor side effects during rewind.

Therefore use the ReplayScrub storage model as a prototype, not as the final
long-history timeline design.

## Determinism test required before implementation

Before any rollback design is credible, run this same-round round-trip test:

1. In local/offline versus mode, choose a stable active-round frame.
2. Capture snapshot `S0`.
3. Record input stream for frames `N..N+K`.
4. Run forward `K` frames, capture state hash `H1`.
5. Restore `S0`.
6. Re-inject the exact same inputs for `K` frames.
7. Capture state hash `H2`.
8. Compare `H1 == H2`.

Start with `K = 1`, then `2`, `8`, `15`, `60`.

Hash candidates:

- full HgCpuDirect output blob after each frame,
- selected chara state fields,
- BM frame counters,
- RNG globals,
- input cache/cursor fields.

If full blob hash differs but visible state matches, identify nondeterministic
bytes and either exclude them from the hash or add them to restore state.

## One-frame resim harness

Rollback needs a controlled “advance exactly one simulation frame” primitive.

Candidate direct path:

1. Pause/suppress normal UE presentation and prevent the normal actor tick from
   also advancing the same frame.
2. Prepare `FLuxBattlePerFrameTickArgs` with the corrected input pair.
3. Call `LuxBattle_PerFrameTick(args)` once.
4. Capture/hash state.
5. Repeat until caught up.

Candidate full battle-frame path:

1. Prepare corrected input pair.
2. Run the InputLog/FrameInput update if the chosen injection point needs it.
3. Call `LuxBattle_PerFrameTick(args)` once.
4. Call `LuxBattleManager_Tick_MainStateMachine_At1461(pBM, 1/60)` once, or call
   the narrower `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState`
   if testing proves the wrapper's other side effects are unnecessary.
5. Capture/hash state.

Candidate stock-cache path:

1. Inject desired local/remote input into the same source arrays consumed by
   online/offline input path.
2. Ensure `ALuxBattleFrameInputLog` cache has the target frame for both players.
3. Drive `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState` once
   with `master - lastApplied == 1`.
4. Let `LuxBattle_PerFrameTick` / `TickCharaMainSimulation` run once in normal
   order.
5. Capture the resulting state.

Avoid the disproven master-clock-bump shortcut. The cache writer, clock
increment, BM catch-up loop, and chara simulation must remain in 1:1 lockstep.

## Fast catch-up mode

During rollback correction, rendering should be skipped entirely. The existing
experimental Generate Timeline render-skip proves the high-level idea:

- `UWorld_Tick` runs before `UGameEngine_RedrawViewports`.
- Skipping redraw removes the expensive visual output while preserving game tick
  ordering.

For rollback catch-up we probably want a tighter version:

- freeze/suppress normal game presentation,
- run N manual sim frames on the game thread,
- do one final render after the corrected state is reached.

Do not use `UDemoNetDriver::GotoTimeInSeconds`. That path is async and rebuilds
replay checkpoints; it is the wrong granularity for live rollback.

## State-size estimate

For a 15-frame rollback window, raw snapshot storage is likely acceptable:

- HgCpuDirect only: `15 * 163,864 = 2,457,960 B` (~2.34 MiB).
- HgCpuDirect + InputLog window: `15 * 180,380 = 2,705,700 B` (~2.58 MiB).

Even a 60-frame lab buffer is only around 10 MiB raw before dedup. For rollback,
CPU time and correctness matter more than memory.

## Transport/protocol additions needed

Stock opcode 0/1 packets are input-only and have weak frame identity because
they use a 4-bit frame-low field. Rollback needs stronger metadata:

- absolute frame number,
- local input for that frame,
- last confirmed remote frame,
- optional input bitmask range for resend,
- per-frame checksum/hash,
- prediction/rollback stats for debugging.

If reusing stock transport, add a HorseMod-specific packet stream rather than
overloading SC6’s fragile opcode parser. The current online parser has known
peer-triggerable bounds and starvation bugs; do not expose new mod protocol
features through those decode paths without validation hooks.

## Recommended path

1. Build deterministic snapshot round-trip tests offline.
2. Build a direct `PerFrameTick(args)` one-frame step harness.
3. If hash tests fail, upgrade it to `PerFrameTick + BM MainStateMachine`.
4. Build a local two-player rollback lab with no network:
   - artificial delayed remote inputs,
   - prediction,
   - restore + resim,
   - hash compare,
   - visual correction.
5. Add instrumentation:
   - snapshot save/restore time,
   - one-frame resim time,
   - max rollback frames affordable under 16.67 ms,
   - hash mismatches and mismatching byte ranges.
6. If direct/full battle-frame stepping still fails determinism, try the InputLog/BM
   SimulationLoop harness.
7. Integrate GekkoNet only after local rollback works.

Most likely viable target: 8-12 rollback frames, 2-frame local input delay,
prediction as “last known remote input,” with same-round rollback only.

## Things not to do

- Do not try to use `UDemoNetDriver::GotoTimeInSeconds` for live rollback. It is
  an async replay checkpoint/packet rebuild path, not a per-frame rollback
  primitive.
- Do not use LuxMoveVM alone. It does not cover input decode/cache, BM cursors,
  round-state, physics/camera/timer/VFX state, or UE actor lifecycle.
- Do not assume replay cross-round restore lessons apply cleanly to online.
  Rollback should never cross round/object-lifecycle boundaries; it should run
  inside a stable live round.
- Do not ship any online parser extensions until slot bounds, packet size, and
  drain-loop caps are validated or guarded.
