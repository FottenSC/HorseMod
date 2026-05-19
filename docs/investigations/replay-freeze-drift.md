# Plan — Continue investigating freeze-drift; leave no stone unturned

## CONTEXT (updated this turn)

**User's empirical complaint** (latest message, paraphrased):
"freezing the game, the replay seems to keep playing in the background and when I unpause it just plays the inputs that would've played if I never paused. Be sure to be looking at the correct match replay and not the training mode. Rethink some of the function names in Ghidra."

**What this turn confirmed (read-only Ghidra audit, no edits made — plan mode):**

1. **Sites 19/20 are on the CORRECT system.**  An audit of all 45 functions in the binary whose names contain "Replay" (decompile + caller chain + memory-layout markers) showed every one of them is part of the MATCH-REPLAY pipeline.  None of them reference the training-mode markers (`ALuxBattleKeyRecorder`, `ELuxBattleKeyRecordState`, `trainingMode.recordType.slotNo`).  The two systems are hermetically sealed in this build.  So the user's hint was a red herring on the function-naming front: the names are right; the leak is on a DIFFERENT path that doesn't have "Replay" in its name.

2. **Match-replay class anchors located** (read-only).  Three classes register UClass metadata for match replay:
   - `ALuxBattleReplay`        — UClass at `0x14414d290`, instance size `0x3A0`, registered at `FUN_140166C40`
   - `ALuxBattleReplayPlayer`  — UClass at `0x14414d380`, instance size `0x3D0`, registered at `FUN_140166C70`
   - `ALuxBattleReplayRecorder`— UClass at `0x14414d3b0`, instance size `0x3D0`, registered at `FUN_140166CA0`
   These are SEPARATE actors from `ALuxBattleFrameInputLog` (the BM+0x478 actor that owns the cursor).  We have not audited their TickActor paths.

3. **`ALuxBattleFrameInputLog` is the BM+0x478 actor — confirmed.**  Class registration at `GetPrivateStaticClassBody_ALuxBattleFrameInputLog @ 0x1401606C0`, instance size `0x43E0` (= 17376 bytes), class hash `0x6A1F0EAF`.  Cursor layout per existing plate:
   - `+0x39C` `nPlaybackCursor` (UI scrubber)
   - `+0x3A4` `nMasterClock`     (INC'd by sites 14/15 — gated)
   - `+0x3A8` `pRecordedFrameBuffer` (0xC0-byte stride)
   - `+0x3B0` `nTotalRecordedFrames`
   - `+0x4410` `nDrainCursor`    (INC'd by "SimulationLoop catch-up" — UNGATED, writer location not yet pinned down)

4. **CRITICAL FINDING — the `OnBattleTickWhenPaused` delegate.**  SC6 has a deliberate "tick even when paused" delegate (signature class `OnBattleTickWhenPaused__DelegateSignature` at string `0x143385500`, lazy-initialized in `FUN_140957D80`, stored in `DAT_14414D0B0`).  In `LuxBattleManager_RegisterOnTickWhenPaused_Delegates @ 0x1403F8E70` SIX handlers are bound onto the world's PlayerController-equivalent (BM+0x410):
   1. `ALuxBattleCommonInput::OnTickWhenPaused`     — bound from BM+0x440
   2. `ALuxBattlePauseController::OnTickWhenPaused` — bound from BM+0x420
   3. `ALuxBattleFrameInput::OnTickWhenPaused`      — bound from BM+0x450
   4. **`ALuxBattleFrameInputLog::OnTickWhenPaused` — bound from BM+0x478** ← prime suspect
   5. `ALuxBattleTutorialManager::OnTickWhenPaused` — bound from BM+0x4E8
   6. `ALuxBattleSound::OnTickWhenPaused`           — bound from BM+0x520

   The existing plate on `0x1403F8E70` claims these handlers fire ONLY when UE4's standard `bGamePaused == true`, NOT during HorseMod's `speedval=0` freeze — and concludes "switching to engine pause is NOT a clean alternative."  **This claim is unverified.**  The plate was written based on assumption about UE4 tick semantics, not empirical observation.  The user's symptom is incompatible with the claim being true: something on the replay path IS advancing during HorseMod freeze.

5. **The un-audited tick path most consistent with the symptom: `ALuxBattleFrameInputLog::Tick` (the actor's standard `AActor::Tick` slot, not its custom vtable-648 entries).**  Sites 14/15/19/20 patch chara-side `vtable[648]` dispatches; they do NOT touch the FrameInputLog's own `vtable[~0x118]` Tick.  If the actor's normal Tick is doing the catch-up scrub on +0x4410 (drain cursor), nothing in our current freeze blocks it.

## NEXT-ITERATION WORK (when plan mode exits)

The plan has THREE concrete steps in order.  Each is small.  No more tangents.

### Step 1 — Identify the un-gated writer empirically (READ-ONLY Ghidra + one extension to ReplayWatchpoints)

The existing `ReplayWatchpoints` watches only 4 of the cursors.  Extend its watch set to include the OTHER cursor candidates that haven't yet been validated as gated:
- **+0x4410** drain cursor on FrameInputLog (the "SimulationLoop catch-up" target)
- **+0x39C** UI playback cursor (currently watched in some sets, needs to be in the SAME set as +0x4410 to compare)
- **+0x460 on BM** (decoded packet buffer head — written by stage-1 decoder; if site 20 transitively gates stage 1 then this should be 0 hits)
- **+0x3C0..+0x3D0 on chara** (first ring entry — written by stage 2; if site 20 works this should be 0 hits)

Procedure: switch `Horse::ReplayWatchpoints::resolve_targets()` to the new 4-address set.  Build + deploy.  Run ONE test cycle (F6 freeze 5 s → F6 unfreeze, with input display ON).  The watchpoint that fires identifies the writer's RIP.  Cross-reference to a Ghidra function.

If the +0x4410 watchpoint fires during freeze: the SimulationLoop catch-up writer is the leak — find it via xrefs to +0x4410 and add a new patch site (entry-RET when speedval==0).

If the chara+0x3C0 ring watchpoint fires during freeze: site 20 isn't actually gating stage 2 (or stage 2 has a bypass).  Re-examine site 20's trampoline placement.

If NO watchpoints fire during freeze: the simulation is genuinely frozen — the user's "duplicates after step" symptom is a render-rate UMG widget appending the same value repeatedly (the prior-turn theory in todo item #7).

### Step 2 — Locate `ALuxBattleFrameInputLog::OnTickWhenPaused` and its sibling 5 handlers (READ-ONLY Ghidra)

Even if Step 1 finds the leak, also identify the 6 OnTickWhenPaused handler functions for label-correctness:
- Decompile `LuxBattleManager_RegisterOnTickWhenPaused_Delegates @ 0x1403F8E70` — the 6 binding sites already point at function pointers stored on each subsystem's vtable.
- Each `FUN_1416BC040` / `LuxMove_AppendWeakObjectEntry_ToArray` call passes a function pointer indirectly via the actor pointer.  The actor's vtable contains the OnTickWhenPaused method at a fixed slot.
- For each handler, decompile and verify whether it does any "advance state" work that could be the leak.

Specifically focus on: `ALuxBattleFrameInputLog::OnTickWhenPaused` — does it INC a cursor?  Does it call any of `LuxReplay_*` functions?  If so, that's the leak even though the existing plate says these don't fire during HorseMod freeze.

### Step 3 — Add the new freeze patch site (only after Step 1 / 2 pin down the writer)

Following the same pattern as sites 10/12/19/20 (entry-RET freeze hook with `prepare_freeze_entry_hook`), add a new site to `SpeedControl.hpp`:
- Pick a stable AOB at the function entry (5–8 bytes covering the prologue)
- Verify orig_len doesn't straddle a relative jump or call
- Wire into `s_speedval==0` early-return

If the leak is `ALuxBattleFrameInputLog::OnTickWhenPaused`: the patch is `Site 21 = entry-RET on that function`.

If the leak is the +0x4410 SimulationLoop catch-up writer: the patch is `Site 21 = entry-RET on whichever function INCs +0x4410`.

If the leak is on `ALuxBattleReplayPlayer::Tick` (the secondary class we haven't audited): the patch is `Site 21 = entry-RET on that vtable[Tick] slot`.

### Verification

1. Build via `E:\myMods\build_and_deploy.bat` (clean compile).
2. Restart SC6, enter a replay.
3. F6 (freeze) → wait 5 s → F6 (unfreeze).
4. **Step 1 verification (ALWAYS run this first):** check UE4SS.log for `[Horse.ReplayWatchpoints] hit ...` lines.  At least one slot should fire during freeze if there is a leak.  The logged RIP identifies the writer.
5. **After patch deployment (Step 3):** repeat the freeze cycle.  Verify the input display shows the SAME state immediately after unfreeze that it showed at the freeze moment — no snap to wallclock-equivalent.  Verify NO new duplicate input rows appear after freeze step.

## ARCHIVE — earlier sections (kept for context)

## STATUS (after BM-resolution fix)

**WS1 (diagnostic instrumentation): WORKING — resolution and arming confirmed.**
- `HorseMod/horselib/ReplayWatchpoints.hpp` — header-only DR0..DR3 + VEH + 256-entry ring buffer.
- BM resolution rewritten to take BM as a parameter; dllmain supplies it via `Horse::Lux::battleManager().raw()` (UE4SS reflection).  The static-image chara global at `g_LuxBattle_CharaSlotP1` is a data-only struct without an initialized embedded UE4 sub-object at +0x388, so the canonical `LuxResolveBattleManagerFromComponent(chara+0x388)` path returns null — `Horse::Lux` sidesteps that by walking UE4SS's class index instead.
- Three test runs confirm successful arming (latest log lines):
  - `[Horse.ReplayWatchpoints] resolved: chara=0x7ff7162d56f0 bm=0x1a7fc2cba40 fil=0x1a70ff45560; targets=[0x1a70ff458fc 0x1a70ff45904 0x7ff7162d9b00 0x1a7fc2ccecc]`
  - `[Horse.ReplayWatchpoints] enabled — watching ...`
- **Visibility gap**: the ring buffer is in-memory only.  Individual VEH hits go to the ring (visible in the in-game ImGui table) but NOT to UE4SS.log.  Without log emission, post-mortem analysis from log files alone cannot identify the writer RIPs.

## REMAINING — Workstream 4: Per-hit log emission

### Why

The user has tested (F6 frame-step + freeze-disable cycle).  The watchpoints are armed, but neither I nor the user can inspect the captured writes from the log.  Adding log emission per hit closes the loop and gives post-mortem visibility.

### Constraint

We CANNOT call `RC::Output::send` from inside the VEH callback.  The VEH runs in arbitrary code context — whatever the game thread happens to be doing when a watched store retires.  `RC::Output::send` does fmt formatting + lock acquisition + file I/O, and any of those can deadlock against locks the game thread is already holding (e.g. UObject GC lock, FName table lock).  This is the standard "no I/O from a fault handler" rule.

### Design

Drain the ring buffer in the existing per-frame ImGui render callback (which already runs on the game thread between ticks, with no UE4 locks held).

1. Add `std::atomic<uint32_t> m_drain_cursor{0}` to `ReplayWatchpoints` — tracks how far the consumer has drained.
2. Add `void drain_to_log()` method:
   - Read current `m_head` once (acquire) — call it `cur_head`.
   - For each index from `m_drain_cursor` up to `cur_head`, copy the ring entry and emit `RC::Output::send<RC::LogLevel::Verbose>` with: timestamp, slot index (which of the 4 watched addrs), watched_addr, value_after, speedval, writer_rip, RVA.
   - Store `cur_head` to `m_drain_cursor` (release).
   - If the producer has lapped the consumer (cur_head - m_drain_cursor > kRingSize), log "OVERFLOW: lost N entries" and skip ahead.
3. Call `drain_to_log()` once per render-tab tick from the existing ImGui block in `dllmain.cpp` (just before the in-memory display loop, so the log and the ImGui table show consistent data).
4. Rate-limit: gate the per-line emission behind a checkbox `m_replay_watch_log_to_file{true}` (default ON for this debugging session) so the user can disable it if it spams.

### Critical files

- `HorseMod/horselib/ReplayWatchpoints.hpp` — add `m_drain_cursor`, `drain_to_log()`.
- `HorseMod/dllmain.cpp` — call `drain_to_log()` from the existing ImGui block in `render_time_tab()` (around the lines that already snapshot the ring for in-game display).

### Verification

1. Build via `E:\myMods\build_horse_mod.bat` (expect clean compile).
2. Restart SC6, enter a replay, enable "Watch replay cursors" (already known to arm).
3. Press F6 (freeze) → wait 5 s → F6 (unfreeze).
4. Inspect UE4SS.log — should see `[Horse.ReplayWatchpoints] hit slot=N addr=... val=... speedval=... rip=... rva=...` lines for each captured write.
5. The lines with `speedval=0.000` are the writers that leaked through during freeze — those are the patch-site-18 candidates.

**WS3 (stale label audit): complete.**
- `FLuxReplayState` struct: confirmed deleted in earlier turn — search returned empty.
- Site-17 plate (`0x1404353D0`): already updated to flag KeyRecorder as training-only with full HorseMod history.
- `HgCpuDirect` plate (`0x1403841E0`): already updated with corrected feasibility (round-trip determinism test as precondition).
- VTable648 plate (`0x1403E1FC0`): already updated to favour direct cursor write for arbitrary seek; also fixed sibling-reference wording (the gate field is on the same FrameInputLog, not a separate chara).
- VTable648_GatedBy4404 plate (`0x1403E2000`): rewrote to clarify the gate field is at `pInputLog+0x4404` (past partial layout), not a separate object — corrects the "chara+0x4404" misdescription.
- Parameter prototypes on sites 14/15: already `ALuxBattleFrameInputLog*`; struct fields decompile correctly (e.g. `pInputLog->nMasterClock_at0x3A4`).

**WS2 (Ghidra labelling backlog): complete.**
- BP trampolines `140973800` and `140973880`: already renamed; plate added for `140973880` (sibling of the existing detailed plate on `140973800`).
- All seven INFERRED_LuxBattleActor_Tick_* renames: already applied with `INFERRED_` prefix in earlier turn; **plates ADDED this turn for all seven** with tailored body summaries that flag where the inferred names DO NOT match the body (`InvokeSlotDecayCallbacks` is actually an alpha-ramp tick, `UpdateTrackingState` is a dither-fade tick, `InvokeEventDelegates` is a height-driven opacity curve — all visual-effect actors mistakenly given simulation-sounding names).
- Two remaining renames applied this turn:
  - `FUN_1404879e0` → `INFERRED_LuxBattleChara_Decay_MoveCountersAndApplySoulCharge` (decompile confirmed: contains `LuxBattleSubSys_ApplySoulChargeToPlayerEntities`).
  - `FUN_140487370` → `INFERRED_LuxBattleChara_Process_MoveSlotsAndDispatch` (decompile confirmed: per-player double-loop over move-slot lifecycle).
- Saved via `mcp__ghidra-mcp__save_program` — success.

## SECOND BREAKTHROUGH — found the actual cause of "duplicates after step"

After confirming sites 1-16 + VMFreezeByte + Site 19 freeze the simulation correctly (cursor at +0x3B0 has only ONE incrementer in the entire binary — PushInputs, gated by site 19; master clock at +0x3A4 has only TWO incrementers — sites 14/15), I went deeper into the architecture:

**The replay input has THREE stages, not two:**
1. **DECODE** — `LuxReplay_DecodeInputPackets @ 0x1403ed310`: parses replay-file packets into a decoded buffer at `BM+0x460` (the replay data block).
2. **CACHE FILL** — `LuxReplay_ConsumeDecodedInputPackets_AndUpdateCache @ 0x1403f63b0`: drains the decoded buffer and **WRITES into the chara's input ring at `chara+0x3C0..+0x43C0`**. The writer runs IN A LOOP, pulling decoded packets and writing 16-byte ring entries per slot. Each ring entry stores `(frameID, cursor, input, filled_flag)`.
3. **PUSH** — `LuxBattleChara_ReplayPlayback_PushInputsToActiveSlots @ 0x1403f6600`: reads cached entries from the chara ring and dispatches via `LuxMove_ForEachSlot_SendParam_IfActive` to active LuxMove slots. ← **Site 19 patched this.**

**The leak**: Site 19 only blocks stage 3 (PUSH).  Stages 1 (DECODE) and 2 (CACHE FILL) keep running during freeze (called via vtable paths that none of sites 1-19 reach).  Each frozen frame, the chara ring fills with another decoded input.  After 5 seconds of freeze at 60 fps, the ring has ~300 cached entries waiting.  On step (speedval=1 for one game frame), PushInputs ungates and runs its catch-up loop — which **pushes ALL 300 cached entries to the display in one step**.  That's exactly the "many duplicates after step forward" symptom.

**Verification of theory** (read-only Ghidra confirmations done this turn):
- The body of ConsumeDecodedInputPackets_AndUpdateCache writes to `[RBX + R13*0x1 + -0x8]` and `[RBX + 0]` and `[RBX + 4]` where `RBX = pChara + 0x3C8 + (cursor & 0x1FF) * 16`, `R13 = (uVar5 - cursor & 0x1FF) * 16` — that's exactly the ring at `chara+0x3C0+slot*0x2000+idx*0x10` per the PushInputs reader.
- The function's prologue is `57 48 83 EC 60` = `push rdi + sub rsp,0x60` (5 bytes) — perfectly suitable for a freeze_entry_hook with orig_len=5.
- 4 DATA xrefs to this function (vtable entries) — multiple classes dispatch to it.  One vtable path goes through site 11's gated function, but at least one other doesn't (otherwise the user wouldn't see the leak).

**The fix**: Add Site 20 = entry-RET freeze on `LuxReplay_ConsumeDecodedInputPackets_AndUpdateCache @ 0x1403f63b0`.  Same shape as sites 10/12 (orig_len=5).  When `speedval==0`, the function bare-RETs → cache fill stops → ring stays at the pre-freeze state → on step, only ONE new entry is decoded+cached+pushed → ONE input on display.

**Anchor AOB** (15 bytes — push rdi + sub rsp + security cookie + xor + store):
  ```
  57                    push rdi
  48 83 EC 60           sub rsp, 0x60
  48 8B 05 ?? ?? ?? ?? mov rax, [rip+security_cookie]
  48 33 C4              xor rax, rsp
  ```
  Use `kOrig20 = { 0x57, 0x48, 0x83, 0xEC, 0x60 }` for `prepare_freeze_entry_hook`.

**Files to modify**:
- `HorseMod/horselib/SpeedControl.hpp` — add site 20 sigscan, null-check, trampoline alloc, prepare_freeze_entry_hook call, m_patch20 declaration, patches[] enable list
- (No other file changes needed — site 20 follows the established sites 10/12 pattern exactly.)

**Verification end-to-end after build+deploy**:
1. Restart SC6, load a replay
2. Press F6 to freeze for several seconds
3. Press F6 to step forward once
4. Input display should show exactly ONE new input entry (or zero if no input was at that frame), NOT ~300

---

## BREAKTHROUGH — SC6 NATIVE FREEZE via VMFreezeByte (earlier this turn)

After exhausting per-function bare-RET patches (sites 1–18, with 17/18 rolled back), found SC6's INTERNAL freeze mechanism in Ghidra:

- **`g_LuxBattle_VMFreezeRecord.bVMFreezeByte` @ `0x1448462D0`** — single byte. When non-zero, `LuxMoveVM_GetTimeDilationScalar` returns `0.0` for ALL callers.

- **Verified the plate's claim by reading `LuxMoveVM_GetTimeDilationScalar`'s decompile**:
  ```c
  fVar1 = 0.0;
  if (g_LuxBattle_VMFreezeRecord.bVMFreezeByte == 0) {
      fVar1 = g_LuxBattle_VMFreezeRecord.flBaseAlpha;
  }
  return fVar1 * flMoveSlotScale;
  ```
  Confirmed: setting the byte to 1 → `fVar1 = 0.0` → return 0 → all integrators halt.

- **Coverage** (callers of `GetTimeDilationScalar`, all become `dt=0`):
  `LuxMoveVM_TickDriver`, `LuxBattle_TickCharaMainSimulation`, `LuxBattle_TickHitStopSchedulerAndInputMirror`, `LuxBattleChara_FinalizeTickPoseAndState`, `LuxMoveVM_ExecuteOpStream`, `LuxMoveVM_AdvanceLinkedMotionObject`, `LuxMoveVM_AdvanceLaneFrameStep`, `LuxMoveVM_AdvanceCharaAnimClipPlayer`, `LuxBattleChara_ApplyKnockbackForce`, `LuxBattleChara_IntegratePhysics`, `LuxBattle_TickCharaCollisionPhysics`, `LuxBattle_DispatchFootstepEvents`, `LuxBattle_DispatchWeaponTraceContactVFX`, `LuxBattle_SetupPoseFromENSTData`, `LuxBattle_BattlePhase_Tick`, plus more visual-FX selectors.

  This is comprehensive — entire MoveVM + physics + opcode interpreter halts.

- **Implementation deployed**: `frame_step_apply()` in `dllmain.cpp` writes `1` to `imageBase + 0x4862D0` when `target == 0.0` (HorseMod freeze engaged), `0` otherwise. Single-byte write, no VirtualProtect needed (.bss is writable). Sites 1–16 stay as defense-in-depth.

- **Caveat per Ghidra plate**: "Different visual systems may still tick because they run off UE4's deltaTime, not this scalar." If the input-display widget polls chara state on Slate's render-rate tick (independent of MoveVM), it could STILL show duplicates while reading frozen chara state. The native freeze fixes the simulation; widget-side polling is a separate (UI-layer) fix if needed.

- **Build + deployed**: `HorseLab/dlls/main.dll` updated. Awaiting user test.

---

## DEEP ANALYSIS — "duplicated inputs" symptom (prior turns)

The user's report after MANY watch-set rotations:

> "the inputs seem to be duplicated a bunch of times could it be an issue
>  with held inputs being inputted every frame or something to that effect.
>  Think deeply about a solution I don't wanna run a million tests"

### Empirical foundation (4 watch sets, 12 addresses, 0 leaks)

Across 4 separate test runs, EVERY address watched showed perfect freeze behaviour:
- **Set 1** (cursor / clock / drain / mirror): 0 writes during freeze
- **Set 2** (per-tick flag, nMoveStateByte, lastFrameID, frame advance): 0 writes during freeze
- **Set 3** (LFSR×2, prev-input snapshot, active-attack cell): 0 writes during freeze
- **Set 4** (buffer entries 0/100/500, MoveVM field): pending retest

**Conclusion: HorseMod's site-9 PerFrameTick blanket-freeze IS gating the entire chara/MoveVM/RNG/cursor pipeline correctly.**

### The duplicated-inputs symptom is a DISPLAY-LAYER artifact, not simulation drift

If the simulation is provably frozen on every cursor and per-tick field, the duplicates the user observes can only come from a code path that:
1. Reads input/cursor state at RENDER RATE (60+ fps), not sim rate
2. Has its own append-history logic that fires regardless of whether the simulation advanced
3. Is NOT gated by any of HorseMod's existing 16 sites

The most likely culprit is a UMG/HUD widget that displays the input history. Such widgets typically tick on the Slate render thread — independent of the chara Actor::Tick chain that site 9 gates. When the user freezes:
- Sim cursor stays at frame N
- Display widget reads "current input" each render frame (60 fps)
- Reads return the SAME value (frozen state)
- Widget appends to its history each read
- After 5 s freeze: history has ~300 copies of the same input → "duplicated"

### Targeted solution paths

The user wants ONE SHOT to fix this — not more empirical tests.  Three options ordered by feasibility:

**Option A (recommended): freeze the source the display widget reads.**

The display reads from one of:
- chara+0x215c (current input snapshot — already in struct)
- chara+0x3090..0x3194 (button rings — already in struct)
- BM+0x1450 dereference (input processor) → its own per-frame state
- A separate "displayed input" cache field we haven't found

If the display widget's read source is itself frozen during HorseMod freeze (which it should be per site 9), then the display would still poll but get the same value each frame.  For a HISTORY widget that appends per poll, that means N copies of the same input — **which IS what the user sees**.  So the symptom is NOT a leak — it's the display widget appending unconditionally.

Fix: **find the display widget and gate its Tick.**  Search candidates:
1. Ghidra search for UMG widget class names containing "Input", "Button", "Trail", "InputView".
2. Look at chara+0x3090 button-ring reader xrefs — anyone reading the rings is a candidate display feeder.
3. Look at BM+0x1450 (pInputProcessor) vtable methods — one of them likely fills a "display input" buffer.

**Option B: do nothing.** Accept that the input display visualizes the simulation's frozen state by appending per render frame.  The simulation IS correct; only the display is misleading.  Add a UI note: "the input trail will appear stuck during freeze; the simulation state is correct."

**Option C: skip the display fix and instead use HorseMod's freeze key to ALSO send a "clear display history" command.**  Practical workaround if the widget can't be found.

### Concrete next-step search list (read-only, low risk)

1. `mcp__ghidra-mcp__search_strings` for: `InputTrail`, `ButtonTrail`, `InputView`, `InputHistory`, `MoveDisplay`.
2. `mcp__ghidra-mcp__get_xrefs_to` on `chara+0x3090` (offset 12432) — anyone reading button rings is a display candidate.
3. Look at `LuxBattle_PreTickStateSnapshotAndRoundDecision` (called from PerFrameTick) — if it pre-snapshots input for a display buffer, that buffer could be the leak target.

### What this plan does NOT do

- Add more random watch-set rotations.  We have FOUR of them and the answer is consistent: simulation is frozen.
- Add patch site #18 blindly.  No empirical leak has been found.
- Touch the existing 16 patches.  They are working correctly.

### Build/deploy reminder

The watch set in `ReplayWatchpoints.hpp` was just swapped to set 4 (recorded buffer entries) and is already built + deployed.  If we want to investigate the display widget, the next step is the search list above (read-only Ghidra work) — no rebuild needed yet.

---

## Context

HorseMod has 15 active freeze patches (sites 1–16, with 17 rolled back) on Soul Calibur VI. Live testing shows replay watching still drifts during HorseMod freeze: with the in-replay input display enabled, the displayed inputs differ between (freeze enabled) and (freeze disabled) cycles — the replay simulation is still being mutated during freeze in some way our patches don't catch.

This session ran three parallel deep investigations (wallclock-driven path, exhaustive cursor writers, unhooked actor ticks). All three reached weak conclusions:

- **Wallclock hunt**: ruled out direct wallclock readers and FTimerManager (60 % conf), but couldn't locate `ALuxBattleFrameInputLog::OnTickWhenPaused` body.
- **Cursor writers**: claimed `LuxBattleChara_SetStageInitPhase_AndTrigger` was the culprit, but the argument is structurally flawed — site 16 patches the INC instruction *in-place*, so any caller reaching that INC is already covered. The agent didn't identify any new INC.
- **Unhooked ticks**: flagged `FUN_1404834d0` for "zeroing chara+0x3A8 every frame," but the +0x3A8 offset is `pRecordedFrameBuffer` only on `ALuxBattleFrameInputLog` — on other objects it's a different field. If FUN_1404834d0 actually zeroed the buffer pointer the game would crash, so it's operating on a different (still-unidentified) object. The lead is unverified.

**Conclusion:** static analysis has hit its limit. The leak is in a path that doesn't match obvious search patterns (no direct cursor writes, no obvious wallclock reads, possibly indirect through a vtable dispatch we can't statically trace). The next step must be **empirical**.

This plan covers three workstreams that run in sequence:

1. **Diagnostic instrumentation** — build a live logger that records writes to the replay-state offsets during a freeze cycle, with the writing function's RIP. This is the only way to definitively identify the leak.
2. **Ghidra labelling backlog** — apply the renames/plate comments/structs discovered this session that plan mode forbade.
3. **Stale label audit** — fix annotations made earlier in the session that newer findings contradicted (the InteractiveReplay misnomer is the worst offender, but there are others).

---

## Workstream 1 — Diagnostic instrumentation (HorseMod feature)

### Goal

When the user enables a debug toggle in HorseMod's ImGui menu, log every write to:
- `(BM+0x478) + 0x39C` (UI playback cursor)
- `(BM+0x478) + 0x3A0` (last frame ID)
- `(BM+0x478) + 0x3A4` (master clock)
- `(BM+0x478) + 0x4410` (drain cursor)
- `BM + 0x1463` (move-state byte)
- `BM + 0x1488` / `BM + 0x148C` (cursor mirrors)

For each write, capture: writer's RIP, value before, value after, current speedval. Store in a ring buffer; expose via ImGui ("recent writes" view).

### Mechanism — hardware breakpoints via VEH

Don't use VirtualProtect (page-granularity, would trap reads too, kills perf). Use **DR0–DR3 hardware data breakpoints** with a vectored exception handler:

1. New `Horse::ReplayWatchpoints` helper class (`HorseMod/horselib/ReplayWatchpoints.hpp`):
   - `enable()` — sets DR0..DR3 on up to 4 of the watched addresses (HW limit) via `SetThreadContext` on the game thread; installs a VEH via `AddVectoredExceptionHandler`.
   - `disable()` — clears DR registers, removes VEH.
   - VEH callback — when `STATUS_SINGLE_STEP` fires from a watchpoint, captures `ExceptionRecord->ExceptionAddress` (= writer's RIP), reads the watched value, sets the resume-flag in EFLAGS, returns `EXCEPTION_CONTINUE_EXECUTION`. Writes a record to a lock-free ring buffer.
2. Resolve target addresses at enable-time:
   - Get `g_LuxBattle_CharaSlotP1` (already known global)
   - Call `LuxResolveBattleManagerFromComponent @ 0x14045FDC0` (already in Ghidra) to get BM
   - Read `BM+0x478` for the FrameInputLog actor pointer
   - Compute the four cursor field addresses + the BM field addresses
3. ImGui surface:
   - Add a "Replay debug" tab in HorseMod's existing menu (parallel to Time tab).
   - Toggle "Watch replay cursors"
   - Live view of the ring buffer (most recent 256 writes, with timestamp/RIP/before/after/speedval)
   - Button "Clear log"
   - Button "Resolve writer RIP → function name" (basic GetModuleHandle + image-base subtract; user can paste into Ghidra)

### Critical files to add/modify

- `HorseMod/horselib/ReplayWatchpoints.hpp` (NEW) — the helper class
- `HorseMod/dllmain.cpp` (MODIFY) — instantiate `Horse::ReplayWatchpoints m_replay_watchpoints{}` alongside `m_speed_control`; add ImGui surface in the appropriate menu section near `m_speed_control` UI (around lines 2752–2849, the existing SpeedControl UI block)
- `HorseMod/CMakeLists.txt` (NO CHANGE expected — header-only addition)

### Existing utilities to reuse

- `Horse::SigScan::sig_scan_sc6` (`horselib/SigScan.hpp`) — for resolving any function addresses
- `Horse::CodeCave::allocate` (`horselib/CodeCave.hpp`) — if we end up needing a near-module trampoline (probably not for HW breakpoints)
- `Horse::ModSettings` (`horselib/ModSettings.hpp`) — for persisting the watchpoint-enabled flag if desired
- ImGui patterns from existing Time tab in `dllmain.cpp` lines 2632–2850

### Verification end-to-end

1. Build: `build_horse_mod.bat`
2. Launch SC6 with HorseMod.
3. Enter a replay; enable "Watch replay cursors" in HorseMod's new debug tab.
4. Press F6 (HorseMod freeze).
5. Wait 5 seconds.
6. Press F6 again.
7. Check the ring-buffer view for any writes during the freeze window. For each write, the RIP identifies the writer.
8. Cross-reference RIPs with Ghidra (or use the "Resolve" button output) to identify the function and decide the fix.

**Success criteria:** at least one writer is identified, OR the buffer is empty during freeze (meaning the leak is in something OTHER than direct cursor writes — e.g., a downstream consumer that reads from a buffer we should also watch, or a non-cursor field).

### Risks

- DR0–DR3 are limited to 4 simultaneous watchpoints. Pick the four most likely (suggest +0x39C, +0x3A4, +0x4410, BM+0x148C). Cycle through others on subsequent runs if needed.
- VEH must be installed BEFORE DR registers are set, or the first write will crash. Test enable/disable cycle in a non-replay context first.
- DR register state is per-thread — must hit the game thread specifically. The cockpit hook gives us that thread context. Use the existing per-frame cockpit-tick callback as the install/uninstall trigger.

---

## Workstream 2 — Ghidra labelling backlog

After plan mode exits, apply ALL pending labels from this session. The list (from the three agent reports + my prior turn):

### Function renames (apply via `mcp__ghidra-mcp__rename_function`)

Already-applied this session (verify they took):
- `FUN_14045FDC0` → `LuxResolveBattleManagerFromComponent` ✓
- `LuxBattle_InteractiveReplay_Tick` → `LuxBattle_RoundResultCinematic_StateMachineTick` ✓
- `LuxTrainingRecorder_Tick_ProcessStateMachine` → `ALuxBattleKeyRecorder_TickActor_TrainingModeStateMachine` ✓

To apply:
- `FUN_140973800` → `LuxBattleManager_BPTrampoline_execSetMoveState`
- `FUN_140973880` → `LuxBattleChara_BPTrampoline_execSetStageInitPhase`
- `FUN_1404834d0` → `LuxBattleActor_Tick_ProcessMoveFramesAndZeroAt3a8` (mark as INFERRED — this object is NOT the chara)
- `FUN_140483510` → `LuxBattleActor_Tick_LargeEventProcessor` (mark as INFERRED, large body)
- `FUN_140562090` → `LuxBattleActor_Tick_InvokeSlotDecayCallbacks` (INFERRED)
- `FUN_1405621f0` → `LuxBattleActor_Tick_UpdateTrackingState` (INFERRED)
- `FUN_140561e60` → `LuxBattleActor_Tick_InvokeEventDelegates` (INFERRED)
- `FUN_140561be0` → `LuxBattleActor_Tick_UpdatePhysicsAngle` (INFERRED)
- `FUN_140525940` → `LuxBattleActor_Tick_CheckRoundOverAndApplyTimers` (INFERRED)
- `FUN_1404879e0` → `LuxBattleChara_Decay_MoveCountersAndApplySoulCharge` (INFERRED)
- `FUN_140487370` → `LuxBattleChara_Process_MoveSlotsAndDispatch` (INFERRED)

### Plate comments to add (via `mcp__ghidra-mcp__set_plate_comment`)

For each renamed FUN_* above: a short plate stating "Auto-renamed — purpose INFERRED from decompile, NOT verified by string evidence. Cross-check before relying on the name."

For the BP trampolines (`140973800`, `140973880`): document that they are the BP UFunction native bridges, what they wrap, and that they are NOT gated by HorseMod freeze.

### Structs to create/extend (via `mcp__ghidra-mcp__create_struct`, `add_struct_field`)

The `FLuxReplayState` struct (948 B) created earlier needs to be EXTENDED if the actor really has fields out to +0x4424. Or — better — create a SEPARATE `ALuxBattleFrameInputLog` struct (the actual UE4 actor) that includes the full extent. Verify the actor instance size by finding `GetPrivateStaticClassBody_ALuxBattleFrameInputLog` first.

Pre-comments to add at known patch anchors (already done for sites 14/15/16, verify and extend for site 9, 10, 11, 12, 13).

### Critical files / addresses to reference

(All in Ghidra's saved program — no source files to modify.)

---

## Workstream 3 — Stale label audit

Items to correct or remove:

1. **`FLuxReplayState` struct** — the agent's plate said this is "the object at BM+0x478" but later analysis showed BM+0x478 is the `ALuxBattleFrameInputLog` UE4 actor (much larger than 948 B). Either rename the struct to "FLuxReplayState_PartialLayout" or replace it with an `ALuxBattleFrameInputLog` struct sized to the actor's actual extent.

2. **Site-17 plate comment on `0x1404353D0`** (KeyRecorder TickActor) — currently says "the primary leak — none of the chara/BattleManager Actor::Tick patches reach this autonomous recorder actor." This is WRONG (the function is training-mode only and hooking it caused the input-display regression). Plate has been updated this turn already; verify it took effect.

3. **`LuxBattle_HgCpuDirect_ExecMoveChangeAndPost @ 0x1403841E0` plate** — the original version overclaimed "the hard parts of rollback are SOLVED by Bandai Namco." Replaced this turn with a more honest version that flags the round-trip test as a precondition. Verify it took effect.

4. **`LuxBattleChara_VTable648_TickAndAdvanceReplayClock @ 0x1403E1FC0` plate** — has a "FUTURE: STEP BACKWARDS" section that's correct in principle but referenced the bit-driven approach as "preferred" — actually the direct-cursor-write approach is preferred for arbitrary seek. Update wording.

5. **The `(longlong * pChara)` parameter type** on the VTable648 functions (sites 14/15) — the actual parameter is the `ALuxBattleFrameInputLog` actor, not a chara. Once the FrameInputLog struct exists, set the prototype to `void(ALuxBattleFrameInputLog*)`.

### Critical files

- Ghidra database (saved via `mcp__ghidra-mcp__save_program`)

### Verification

After applying all label changes:
1. Spot-check 5 random renamed functions: read the plate, confirm it matches the function body.
2. Decompile one struct-typed function: confirm the field names appear correctly in the Ghidra decompile output.
3. Run `mcp__ghidra-mcp__save_program` and verify success.

---

## Order of operations (when execution begins)

1. **First**: Workstream 3 (stale label audit) — small, low-risk, prevents future me from being misled by stale info.
2. **Then**: Workstream 2 (labelling backlog) — codifies the session's domain knowledge into the Ghidra database.
3. **Then**: Workstream 1 (diagnostic instrumentation) — the actual bug-hunting work; benefits from the up-to-date Ghidra annotations done in steps 1–2.

If Workstream 1 identifies a leak: add a new HorseMod patch site (number 18+) with the same shape as the existing freeze hooks. The patch encoding will follow whichever pattern the leak's instruction allows (entry-RET, mid-function INC patch, MULSS-insert, etc.).

If Workstream 1 finds NO writes during freeze: the bug is in a downstream consumer or an indirect path. At that point, expand the watchpoints to additional offsets (+0x440C, +0x4424, BM+0x1450 input processor, etc.) and repeat.

---

## What this plan deliberately does NOT do

- **Does NOT add patch sites 17/18+ blindly** based on agent leads. The agents' "smoking gun" claims this turn don't hold up under scrutiny (see Context section). Speculative patches risk another KeyRecorder-style regression.
- **Does NOT attempt rollback netcode work yet**. Per the prior turn's corrected feasibility report, that's blocked on a snapshot-round-trip determinism test which is a separate, larger workstream.
- **Does NOT change the existing 16 patches**. They're load-bearing for the simulation freeze and shouldn't be touched without empirical reason.
