# Soulcalibur VI — Input Lag Investigation & Removal Notes

Working notes for understanding the SC6 input pipeline and identifying where input lag
can be reduced. Last updated 2026-05-13 (deep-dive session 4: NvAPI Reflex hook surface
mapped — 11 existing wrappers, library load chain documented, magic IDs identified;
FinishCurrentFrame consumers traced — works on D3D11 path; **r.RHICmdBypass confirmed
dead code (int never read per-frame), do not patch**; ALuxPlayerController tick group
**confirmed TG_PrePhysics** — no hidden frame; FD3D11Viewport struct (30 fields, 200
bytes) created and applied to all viewport functions; **DXGI flip-model byte patches
identified at byte level** — BufferCount/SwapEffect/Flags in Init + Resize with exact
addresses; **IConsoleVariable::Set vtable slot confirmed at +0x60** via existing setter
analysis; **NO `AllowTearing`/`WaitableObject`/`FLIP_DISCARD` symbols in binary** — flip-
model conversion requires invasive patching of pre-flip-model UE4 fork).

Companion to `memory/project_sc6_input_lag_analysis.md`,
`memory/project_sc6_perframetick_dispatch.md`, and
`memory/project_sc6_lag_removal_implementation.md` (the implementation-ready map with
exact byte patches and sample HorseMod code).

---

## TL;DR — what's actionable today

External measurement: **SC6 PS4 = 5.5 frames avg / 72% stability** (inputlag.science).
Worse than peers (T7 4.7f, SFV 4.5f, DBFZ 4.6f).

| Source | Frames | Lives in | Removable? | Cost |
|---|---|---|---|---|
| **r.OneFrameThreadLag** (default=1) | 1 | UE4 CVar | ✅ Set("0") via vtable+0x60 on `g_pCVar_OneFrameThreadLag_Var @ 0x1443B3528` | -10-15% FPS headroom |
| **DXGI MaxFrameLatency** (`RHI.MaximumFrameLatency`, default=3) | 1-2 | UE4 RHI | ✅ write 1 to `g_nRhiMaxFrameLatency @ 0x14407B088` | Frame-pacing jitter on GPU-bound systems |
| **Fullscreen-Exclusive vs Windowed** | 1 (windowed only) | SC6 user setting | ✅ Flip the in-game Window Mode dropdown | Lose Alt-Tab fluidity |
| **NVIDIA Reflex** (no native integration) | 1-2 (NVIDIA only) | HorseMod hook | ✅ Piggyback on existing NvAPI infrastructure | None on NVIDIA ≥R450 |
| **VSync** (`rhi.SyncInterval`, default=1) | 0.5-1 | display chain | ✅ Set("0") via vtable+0x60 on `g_pCVar_rhi_SyncInterval_Var @ 0x144351730` | Tearing |
| **r.FinishCurrentFrame** (default=0) | 1 | UE4 CVar | ✅ Set("1") via vtable on `g_pCVar_FinishCurrentFrame_Var @ 0x144166440` | High — full GPU stall |
| **DXGI Flip-Model conversion** | 1 (+ unlocks 3 more) | UE4 RHI | ✅ Byte patches in `FD3D11Viewport_Init/_Resize` | Medium-complex patch |
| **DXGI_PRESENT_ALLOW_TEARING** | 1 (after flip-model) | UE4 RHI | ✅ Byte patch Present flags arg | Depends on flip-model |
| **Waitable Swapchain wait** | ~1 (after flip-model) | UE4 RHI | ✅ Hook at FEngineLoop::Tick prologue | Depends on flip-model |
| **r.GTSyncType=2** (after flip-model) | <1 marginal | UE4 CVar | ✅ Set("2") via vtable on `g_pCVar_GTSyncType_Var @ 0x1443518D0` | Depends on flip-model |
| **Display hardware** | 1-2 | external | ✅ Game Mode / faster monitor | Hardware |
| Game-thread sim chain | **0** | SC6 binary | N/A (already 0) | — |
| ~~`r.RHICmdBypass`~~ | **0 (dead code)** | UE4 CVar | ❌ Int never read per-frame; only reachable via `r.RHISetGPUCaptureOptions` | **Skip** |
| ~~`t.MaxFPS`~~ | **0 (already 0)** | UE4 CVar | N/A — SC6's 60Hz cap is rhi.SyncInterval=1, not t.MaxFPS | N/A |

**The SC6 binary itself adds 0 frames of structural lag in offline live play.** Every
visible lag source is in UE4-RHI or hardware. The "input lag mod" should be a CVar
patch + DXGI hook + Reflex piggyback, NOT a chara/MoveVM code patch.

**Maximum-stack potential removal: ~5-6 frames** if every item is applied. **Practical
zero-friction bundle: ~3 frames** (OneFrameThreadLag + MaxFrameLatency + Reflex) with
no perceptual cost on NVIDIA hardware.

---

## Verified pipeline (offline live play)

```
OS pad/keyboard (RawInput/XInput poll, ~125Hz-1kHz)
  ↓
UE4 UPlayerInput accumulator (stock UE4, source path leaked @ 0x1439E1790)
  ↓ TG_PrePhysics (CONFIRMED 2026-05-13: ALuxPlayerController inherits stock UE4 default)
ALuxBattleFrameInput (BM+0x450) per-player records (stride 0x90)
  ↓ writes u32 to record[player]+0x3E0 each frame
ALuxBattleFrameInputLog (BM+0x478) per-player current array @ +0x3B8
  ↓
[unknown args-builder — see "unresolved" below]
  ↓ args[0]/args[1] = u64* per-player engine input
LuxBattle_PerFrameTick @ 0x1402DBC60
  ↓ writes g_LuxBattle_LatestEngineInput_PerPlayer @ 0x144855700 (u64[2])
  ↓ per-chara loop calls TickCharaInput
LuxBattle_TickCharaInput @ 0x140312510
  ↓ PUSH ring[(base + cursor) % 0x3D + player*0x3D] = latest_input
  ↓ INC cursor
  ↓ READ ring[old_cursor % 0x3D + player*0x3D]   ← SAME slot (base=0)
  ↓ writes chara+0x2150 (stick u32), chara+0x2158 (button u32)
  ↓ tail: decodes held-frame derivatives chara+0x215C..+0x2180
  ↓        via DAT_143E84400 / DAT_143E843F0 / DAT_143E84348 lookups
LuxBattle_TickCharaMainSimulation → MoveVM → hit resolution (same frame)
  ↓
return to UE4 frame, render, present
```

**CRITICAL: `g_LuxBattle_InputRingBaseOffset_PerPlayer @ 0x14470DED0` has no writer
in the binary** (verified by byte-pattern search of address literal). BSS-zero default
holds at runtime. Ring write index == ring read index → same-frame echo.
**Zero structural input lag from the ring buffer.**

The ring's lookback capability is engineered for the `move id == 3` "replay-from-partner"
training case (P1 at offset 0, P2 lagged by 60 frames). Not used in normal play.

---

## Key data structures

### `ALuxBattleFrameInput` @ BM+0x450 (class size 0x510)

The live input actor. Captures UE4 input each frame.

- Per-player records: stride **0x90** (144 bytes), indexed by playerIdx (0..N-1)
- `+0x3E0` per record: **u32 input bitmask** (the live input word)
- `+0x424` / `+0x428`: axis values
- `+0x42C` / `+0x430`: more axis values
- UProperties (from `FUN_140918470`):
  - `RepeatInterval` @ +0x390 (u32)
  - `RepeatDelay` @ +0x38C (u32)
  - `CanUpdateInput` (bool)
- 1 registered UFunction: `OnTickWhenPaused`

### `ALuxBattleFrameInputLog` @ BM+0x478 (class size 0x43E0)

The replay log + input cache.

- `+0x28..+0x34`: TArray<FInputRecord> (12-byte stride, chara-facing)
- `+0x38..+0x44`: TArray<FInputAxis> (24-byte stride, chara-facing)
- `+0x390`: `InputDelay` UProperty (u32) — **VERIFIED PERMANENTLY 0** in observed builds. Registered as a UProperty by `ALuxBattleFrameInputLog_RegisterProperties @ 0x1409188E0` but the only writer in the entire binary is the constructor's zero-init at `LuxMoveProvider_BaseData_Constructor @ 0x1403DC3FB`. No online setter, no training-mode handler, no UI toggle. CDO default = 0. See `## What's NOT in the input pipeline` for the 8WAYRUN mod implication.
- `+0x398`: int32 `nPerPlayerCount`
- `+0x39C`: u32 active-players bitfield
- `+0x3A0`: int32 `nLastFrameID`
- `+0x3A4`: int32 `nMasterClock` (INC'd each tick by VTable648 chain)
- `+0x3B8`: per-player u32 input array (stride 4) — **current snapshot**
- `+0x3C0`: FLuxReplayInputCacheEntry ring (16-byte stride, 0x200 entries × players)
- `+0x4400`: `dwOnlineActive` flag
- `+0x4404`: `bDoubleTickGuard`
- `+0x4410`: `nDrainCursor`

### `FLuxBattleTickInfo` UScriptStruct (size 0x58)

Lives at **ALuxBattleManager+0x1488** as `BattleTickInfo` StructProperty.

- `+0x00`: `InputRound` (u32) — runtime alias `nReplayLastFrameID`
- `+0x04`: `InputTime` (u32) — runtime alias `nReplayLastApplied` (master-clock cursor)
- `+0x08`: `ScBattleFrame` (struct)

**NOT** PerFrameTick's args struct. This is the replay catch-up state exposed via BP.

### `FLuxBattlePerFrameTickArgs` (size 0x18)

The actual args struct passed to `LuxBattle_PerFrameTick` (single `longlong*` arg = pointer
to this struct). Defined in Ghidra 2026-05-13.

- `+0x00`: `pInputP1` (qword) — *(u64*)pInputP1 → `g_LuxBattle_LatestEngineInput_PerPlayer[0]`
- `+0x08`: `pInputP2` (qword) — *(u64*)pInputP2 → `g_LuxBattle_LatestEngineInput_PerPlayer[1]`
- `+0x10`: `pCameraAxisStruct` (qword) — 24-byte camera/axis block read into `_DAT_14470d100..0x118`

Sources of `pInputP1`/`pInputP2` upstream NOT yet identified — see "Unresolved" section.
Strongest hypothesis: `&pBM->FrameInput->Records[N]+0x3E0` reinterpreted as `u64*`.

### `FLuxBattleInputRing` (488 bytes — created 2026-05-13)

The per-player input ring slot type. Single field `pAqwEntries: qword[61]`.
Applied at `g_LuxBattle_PerPlayerInputRing` as `FLuxBattleInputRing[2]` (976 bytes total).

The decompile now reads cleanly:
```c
g_LuxBattle_PerPlayerInputRing[player].pAqwEntries[(int)cursor % 0x3d] = latest;
```

### Four input-pipeline globals (all typed 2026-05-13)

| Address | Symbol | Type | Notes |
|---|---|---|---|
| `0x14485E750` | `g_LuxBattle_PerPlayerInputRing` | `FLuxBattleInputRing[2]` | 61 × u64 per player, 976 bytes total |
| `0x14485EB20` | `g_LuxBattle_PerPlayerInputRingCursor` | `uint[2]` | Monotonic write cursor; never reset between rounds; only writer is the case-1 INC in TickCharaInput |
| `0x14470DED0` | `g_LuxBattle_InputRingBaseOffset_PerPlayer` | `uint[2]` | **BSS-zero, no writer in entire binary** (verified by both Ghidra xrefs and address-literal byte-pattern search). Dormant lookback knob |
| `0x144855700` | `g_LuxBattle_LatestEngineInput_PerPlayer` | `qword[2]` | Per-frame latest controller word; written by PerFrameTick from args[0]/args[1]; cinematic writer also exists at `RoundResultCinematic_StateMachineTick` |

PRE comments on each global document semantics, writers/readers, and the
split-nibble bit layout. Plate comment on TickCharaInput documents the full
move-id dispatch and the held-frame derivative field map.

### `g_LuxBattle_LatestEngineInput_PerPlayer` @ 0x144855700

`u64[2]` — one per player. Format:

- **Low u32** (consumed at chara+0x2150, "stick state"):
  - bits 0-3: face buttons A/B/K/G (encoded as A=bit0, B=bit1, K=bit2, G=bit3)
  - bits 4-5: unknown
  - bits 6-9: secondary stick (XOR-and-mask sanitizer reads here)
  - bits 10-13: stick directions (mask 0x3C00; encoded events 0x09/0x0B/0x0D/0x0F)
  - bits 14-31: unknown (likely RB/LB/SE/ST macros)
- **High u32** (consumed at chara+0x2158, "button state"):
  - Similar layout for held/macro states

Bit-event mapping verified via `LuxBattle_BuildInputEventBytes_FromBitmasks @ 0x1403ECC90`:
- bit 0 pressed → event 1, held → event 2 (button A)
- bit 1 → 3/4 (B)
- bit 2 → 5/6 (K)
- bit 3 → 7/8 (G)
- bits 10-13 stick → encoded indices + 1-bit "held" suffix

---

## Per-chara held-frame derivative fields (chara+0x215C..+0x2180)

Computed at the TAIL of `LuxBattle_TickCharaInput` after the ring read populates
chara+0x2150/+0x2158. Three lookup tables drive the decode:
- `DAT_143E84400` — single-step nibble decoder (raw → "current direction id")
- `DAT_143E843F0` — iterate-N step table (single-frame state → compound state)
- `DAT_143E84348` — held-frame ushort table (compound state → frame counter)

| Offset | Field | Source |
|---|---|---|
| `+0x215C` | Decoded stick id | `DAT_143E84400[(stick>>10) & 0xF]` |
| `+0x2160` | **Prev-frame** decoded stick id | snapshotted by PerFrameTick before reset |
| `+0x2164` | Raw stick nibble | `(stick >> 10) & 0xF` |
| `+0x2168` | Decoded button id | `DAT_143E84400[(button>>10) & 0xF]` |
| `+0x216C` | Raw button nibble | `(button >> 10) & 0xF` |
| `+0x2170` | Compound button state after N-step iterate | `iterate(DAT_143E843F0, raw_btn, N)` |
| `+0x2174` | **Prev-frame** compound button state | snapshotted by PerFrameTick |
| `+0x2178` | Held-frame count for stick | `DAT_143E84348[iter_stick * 2]` |
| `+0x217C` | Compound stick state after N-step iterate | `iterate(DAT_143E843F0, raw_stick, N)` |
| `+0x2180` | Held-frame count for button | `DAT_143E84348[iter_btn * 2]` |

**N = 2 if `chara+0x16E4 == 0` (facing-left), else 6.** Likely encodes facing-direction
-aware command-matcher window size — different motion-recognition windows depending
on which way the chara faces.

### Prev-frame companion fields (snapshotted by PerFrameTick)

PerFrameTick @ 0x1402DBC60 runs **immediately before** TickCharaInput each frame
and does this:
```c
chara+0x2154 = chara+0x2150;   // prev raw stick word
chara+0x2160 = chara+0x215C;   // prev decoded stick id
chara+0x2174 = chara+0x2170;   // prev compound button state
// then zero out 0x2150/+0x2158/+0x2164/+0x216C/+0x2178/+0x2180
```

The AI / scheduler input path (TickCharaInput `default` case) uses these for
edge detection: `(prev ^ new) & new` gives the "newly pressed" mask without
needing a separate event log.

### Verified consumer of held-frame derivatives

**`LuxBattleChara_TickHitStateStateMachine @ 0x140308EC0`** reads `chara+0x2170`
(compound button state) at five distinct call sites (`0x14030903C`, `0x14030906A`,
`0x140309076`, `0x140309096`, `0x1403090BA`) in cases 2/3/5/6/7/8 of its hit-state
machine. Each read is passed to `LuxBattleChara_DecayHitstunSlideVelocity`.

**Effect**: this is the link between input and hitstun-slide animation decay.
Mashing buttons during hitstun increases the compound button state, which
accelerates the slide-velocity decay = faster tech / air-recovery.

### Other consumer family (large unverified MoveVM functions)

- `+0x2178` (stick held-frame): `FUN_1407CEDC0` (xref @ 0x1407D2237)
- `+0x2180` (button held-frame): `FUN_14066BC40` (xref @ 0x14066DD82),
  `FUN_140697AE0` (xref @ 0x14069B616)

These are the LuxMoveVM command-pattern matchers — the held-frame counters
gate move-recognition windows ("236 motion within 12 frames", etc.).

---

## TickCharaInput move-id dispatch (`chara+0x324`)

The function branches on the chara's current move id selector:

| Move id | Behavior |
|---|---|
| `0`, `0x2B` | Zero input — writes 0 to chara+0x2150/+0x2158 |
| `1` | LIVE OFFLINE PLAY: push `LatestEngineInput[player]` to ring at `(base+cursor)%61`, INC cursor, read back from `old_cursor%61` (same slot since base=0). Only path that WRITES the ring in normal play |
| `3` | TRAINING-PARTNER REPLAY: P1 reads partner's ring at `cursor%61`, P2 reads at `(cursor+0x3C)%61` (60-frame lagged playback). **The ONLY code path in the binary that uses the ring's lookback capability** (offset != 0) |
| `0x6A`, `0x6B`, `0x6C` | Live alternates (likely guard impact / tech states): copy `LatestEngineInput[player]` directly to chara, no ring touch |
| `default` | AI / scheduler input: read `g_LuxBattle_CCpuCommand_SelectedSlot_P0 + p*0x60`. Computes `(prev_input ^ new_input) & new_input` for newly-pressed mask via `chara+0x2154` |

This dispatch table is what makes the dormant `g_LuxBattle_InputRingBaseOffset_PerPlayer`
relevant — only the move-id-1 path would honor it. AI / cinematic / training-partner
paths bypass the ring entirely or use inline-constant offsets.

---

## Key functions

| Address | Name | Role |
|---|---|---|
| `0x1402DBC60` | `LuxBattle_PerFrameTick` | Per-frame sim core. Reads args[0..2], writes `g_LuxBattle_LatestEngineInput_PerPlayer`, calls TickCharaInput per chara |
| `0x1403D2A20` | `LuxBattleChara_VTable3_Thunk_LuxBattle_PerFrameTick` | `MOV RCX,RDX; JMP PerFrameTick` (8 bytes: `48 8b ca e9 38 92 f0 ff`). Drops caller's `this` in RCX, replaces with `args` from RDX. Installed at table slot 0x14327B678. Single body, single xref (DATA only — no code refs anywhere) |
| `0x140312510` | `LuxBattle_TickCharaInput` | Per-chara input ring writer + chara+0x2150 commit |
| `0x1403FBC70` | `LuxBattleChara_Tick_ComputeInputDelta_At394_AndCallFc520` | Computes pressed/released/held deltas from pInputLog records |
| `0x1403FC520` | `LuxInput_UpdateStateAndHoldTimers_ForPlayer` | Updates 32-bit hold timers from pInputLog state |
| `0x1403F2AB0` | `LuxBattleManager_UpdateInputCache_LocalMode` | Writes pInputLog+0x3C0 ring from +0x3B8 (offline) |
| `0x1403F2B60` | `LuxBattleManager_UpdateInputCache_OnlineOrLocal` | Same as above, online variant — gated by +0x4404 |
| `0x1403FE960` | `LuxBattleManager_UpdateCommandPlayerInput_At14c8_14d0` | Reads `(BM+0x450)+0x3E0+player*0x90` → BM+0x14C8 |
| `0x1403FE520` | `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState` | Catch-up driver for replay/online |
| `0x1403F6770` | `LuxOnline_DrainRingBuffer_DecodeInputPackets_AndUpdateCache` | Online-only writer of +0x3C0 cache (gates on +0x4400) |
| `0x1403F0720` | `LuxBattleManager_GetCachedRoundValue_ByIndex` | Cache reader (8WAYRUN patch site) |
| `0x1403F8F98` | `LuxBattleManager_RegisterOnTickWhenPaused_Delegates` | Registers 6 OnTickWhenPaused handlers |
| `0x1402708E0` | `CVar_Register_OneFrameThreadLag_Default1` | Registers `r.OneFrameThreadLag` (default 1) via `IConsoleManager::RegisterConsoleVariable` (vtable +0x10). Caches int-storage pointer at `_DAT_1443B3530` via vtable +0x38. Atexit cleanup: `FUN_143226430` |
| `0x140229B70` | `CVar_Register_GTSyncType_Default0` | Registers `r.GTSyncType` (default 0). Same registrar shape as OneFrameThreadLag: vtable +0x10 to `IConsoleManager::RegisterConsoleVariable`, vtable +0x38 to cache the `int32[2]` shadow at `g_pCVar_GTSyncType_Value @ 0x1443518d8`. CVar selects which downstream thread BeginFence syncs against (0=RT, 1=RHI, 2=GPU swap-chain flip). Atexit cleanup: `FUN_1432134A0` |
| `0x1417B7950` | `FFrameEndSync_AdvanceForOneFrameThreadLag` | Callback variant of FFrameEndSync advance. Thread-aware shadow read (idx 0=GT, idx 1=RT) → calls `FFrameEndSync_AdvanceTwoSlot(&g_FFrameEndSync_State, lag != 0)`. Only DATA xrefs (vtable slot at 0x143732A90 + .pdata at 0x144977930); dispatched indirectly, NOT from FEngineLoop::Tick |
| `0x1421876D0` | `FFrameEndSync_AdvanceTwoSlot` | UE4 `FFrameEndSync::Sync(bool bAllowOneFrameLag)`. 2-slot fence pipeline: BeginFence on `pFences[cursor]` → IsThreadProcessingTasks(GameThread=2) → optional ProcessThreadUntilIdle → conditional `cursor=(cursor+1)%2` → Wait on `pFences[cursor]`. With lag=1, Wait targets *last* frame's fence (overlap allowed); with lag=0, same slot (no overlap) |
| `0x1415E9510` | `FRenderCommandFence_BeginFence` | UE4 `FRenderCommandFence::BeginFence(bSyncToRHI)`. Enqueues a render-thread command that signals `*ppFence` when the RT drains past this point. TRefCountPtr ownership at Impl+0x48. **Reads `g_pCVar_GTSyncType_Value` @ 0x1415e95c1**: when `bSyncToRHI=1` AND `GTSyncType != 0`, takes the RHI/GPU-flip path; otherwise falls through to the standard RT-only dispatch. The CVar value is read via thread-aware shadow indexing `value[is_game_thread ^ 1]` |
| `0x1415EFEC0` | `FRenderCommandFence_Wait` | UE4 `FRenderCommandFence::Wait(bProcessGameThreadTasks)`. Blocks until `*(Impl+0x8) bit 26 (0x04000000)` = the done flag is set. Optional game-thread task pumping during the spin |
| `0x140396450` | `FEngineLoop_Tick` | Canonical per-UEngine-tick FFrameEndSync caller. CVar lookup @ `0x140396AD5`, state load @ `0x140396ADC`, direct call to `FFrameEndSync_AdvanceTwoSlot(&g_FFrameEndSync_State_EngineTick, ...)`. Reads game-thread shadow only (idx 0) |
| `0x1411F4F70` | `FD3D11Viewport_Init` | Swapchain ctor (`CreateSwapChain` + `MakeWindowAssociation`). Initializes cached MaxFrameLatency to 3. Does NOT call `SetMaximumFrameLatency` |
| `0x141201F80` | `FD3D11Viewport_PresentChecked` | Per-frame present entry. Reads `g_nRhiMaxFrameLatency`, applies via `IDXGIDevice1::SetMaximumFrameLatency` on change. Only call site for that DXGI method |
| `0x141202100` | `FD3D11Viewport_PresentSwapChain` | Calls `IDXGISwapChain::Present(syncInterval, 0)` (vtable +0x40) |
| `0x1412021A0` | `FD3D11Viewport_PresentDwmThrottled` | Windowed/DWM-throttled Present path (uses `DwmGetCompositionTimingInfo` + `DwmFlush`) |
| `0x14120E9F0` | `FD3D11Viewport_Resize` | `ResizeBuffers` driver. Does NOT re-apply MaxFrameLatency |
| `0x1401D6EF0` | `RegisterCVar_RHI_MaximumFrameLatency` | CVar registration, default 3 |
| `0x1401D77A0` | `RegisterCVar_D3D12_MaximumFrameLatency` | D3D12 mirror, unused in stock SC6 |
| `0x1415E2C90` | (`rhi.SyncInterval` reader) | Returns `*DAT_144351748[0 or 1]` depending on calling thread (game vs render) |

---

## What's NOT in the input pipeline (false leads)

- **8WAYRUN "1 fake frame" mod** (NOP at 0x1403F0751): patches `GetCachedRoundValue_ByIndex`. The NOPed instruction is `SUB EBX,[RDI+0x390]` — subtracts `InputDelay_at0x390` from the cache index. **Provably a placebo in ALL game modes** (verified 2026-05-13):
  - `+0x390` is permanently 0 — exhaustive byte-pattern search (`89/66/C7 ?? 90 03 00 00`) found exactly ONE writer in the entire binary: the constructor's zero-init at `0x1403DC3FB`. Round-init (`LuxBattleChara_InitPlayerBitmask_FromOnlineSession @ 0x1403FA330`), online drain, and matchmaking entry paths all skip `+0x390`.
  - The `vtable[0x658]` gate that fronts the SUB: InputLog uses `UObject_IsReadyForFinishDestroy_Default @ 0x1402D72F0` (returns 1 always); Sync overrides with `ALuxBattleFrameInputSync_IsNotInOnlineSyncHandshake @ 0x1403E9610` (returns 1 except during the brief online sync handshake). So the SUB *does* fire offline and in steady-state online — it just subtracts 0.
  - Online: NOP has the same null effect; no online setter writes `+0x390`. The earlier "desync risk online" hypothesis is REFUTED.
  - The "InputDelayFrame" string at `0x14338BFF0` belongs to a separate UStruct `LuxBattleOptionParam` (registered by `FUN_140992DA0`, field at offset 0) and is never propagated to `FrameInputLog+0x390`.
- **`ALuxBattleFrameInputLog::OnTickWhenPaused`**: only fires during UE4 engine pause (`bGamePaused=true`). HorseMod freeze (speedval=0) does NOT engage engine pause. Not in the live path.
- **`FLuxBattleTickInfo`**: name suggests it's PerFrameTick's args but it's actually the replay catch-up state struct at BM+0x1488. Not relevant to live input.
- **ALuxInputKeyEventListener**: class exists with only 1 BP-callable (`ChangeTargetPlayer`). Not the live input encoder despite the suggestive name.

---

## `r.OneFrameThreadLag` — full mechanism (Ghidra-verified)

The CVar that controls UE4's standard render-thread pipelining. Default = 1: game
thread allowed to advance 1 frame ahead of render thread (the canonical UE4 trade —
+1 frame input lag for +1 frame GT/RT overlap). Setting to 0 collapses the pipeline.

### `FFrameEndSync_State` struct (24 bytes, created and applied)

```c
struct FFrameEndSync_State {        // size 24, alignment 1
    void *pFences[2];   // +0x00  FRenderCommandFenceImpl* TRefCountPtrs
    int   nCursor;      // +0x10  0..1 toggle
    uchar p_pad14[4];   // +0x14  tail padding
};
```

### Two independent state instances

SC6 has **two** zero-init FFrameEndSync states (most UE4 builds have one):

| Instance | Address | Used by | CVar pointer cache |
|---|---|---|---|
| `g_FFrameEndSync_State` | `0x14435D580` | callback-dispatched (`FFrameEndSync_AdvanceForOneFrameThreadLag`) | `g_pCVar_OneFrameThreadLag_Value @ 0x14435D5A0` |
| `g_FFrameEndSync_State_EngineTick` | `0x1441457D8` | `FEngineLoop::Tick` inline | `g_pCVar_OneFrameThreadLag_Value_EngineTick @ 0x1441457F8` |

Each has its own atexit cleanup (`FUN_1432151C0` / `FUN_1431EB400`) and its own
`_Init_thread` guard for lazy first-touch CVar lookup. **Both must be ticked** by
their respective paths for the pipeline to advance consistently; modifying the CVar
affects both because they read the same `IConsoleVariable` int storage.

### CVar value layout (FConsoleVariableData<int> shadow array)

The int pointer returned by `IConsoleVariable::GetValuePtr` (vtable +0x38) points to
a 2-int shadow array: `[0] = GameThread copy`, `[4] = RenderThread copy`. Lock-free
read for the render thread.

### The 2-slot fence algorithm (`FFrameEndSync_AdvanceTwoSlot @ 0x1421876D0`)

Verified against UE4 source — `FFrameEndSync::Sync(bool bAllowOneFrameThreadLag)`:

1. `FRenderCommandFence::BeginFence(&pFences[cursor], 1)` — enqueue a render-thread
   "signal me" command on the current slot.
2. `fGameThreadBusy = FTaskGraphInterface::Get()->IsThreadProcessingTasks(GameThread=2)`
   via TaskGraph vtable +0x20.
3. If `!fGameThreadBusy`: `ProcessThreadUntilIdle(GameThread=2)` via vtable +0x30 —
   drain pending game-thread tasks before stalling.
4. If `bAllowOneFrameLag != 0`: `nCursor = (nCursor + 1) % 2` (compiled as
   `(x+1) & 0x80000001` + negative-fixup — the MSVC signed-modulo idiom).
5. `FRenderCommandFence::Wait(&pFences[cursor], !fGameThreadBusy)` — wait on the
   (possibly flipped) slot.

With `lag=1`: step 4 flips the cursor → Wait targets **last frame's** fence (almost
done → short stall, current dispatch flies). With `lag=0`: no flip → Wait targets
the fence **just enqueued by step 1** → GT blocks until RT signals it → no overlap.

### The thread-aware reader (`FFrameEndSync_AdvanceForOneFrameThreadLag @ 0x1417B7950`)

Same algorithm as the FEngineLoop inline block, but with a key twist:

```c
fOnGameThread = !GIsGarbageCollecting
                OR (GetCurrentThreadId() == GGameThreadId);
// XOR ^1: true → idx 0 (game-thread copy), false → idx 1 (render-thread copy)
int lag = g_pCVar_OneFrameThreadLag_Value[(ulonglong)fOnGameThread ^ 1];
FFrameEndSync_AdvanceTwoSlot(&g_FFrameEndSync_State, lag != 0);
```

This variant is callable from either thread (picks the right shadow). The
FEngineLoop variant assumes GT and just reads `*g_pCVar_OneFrameThreadLag_Value_EngineTick`
(idx 0).

### Per-tick call sites

- **Canonical (game-thread):** `FEngineLoop::Tick @ 0x140396450` calls
  `FFrameEndSync_AdvanceTwoSlot(&g_FFrameEndSync_State_EngineTick, ...)` inline.
  CVar value load at `0x140396AD5`, state address load at `0x140396ADC`.
- **Indirect callback:** `FFrameEndSync_AdvanceForOneFrameThreadLag` has **zero**
  direct code xrefs. Two data xrefs:
  - `0x144977930` — `.pdata` RUNTIME_FUNCTION (Windows x64 exception unwind metadata,
    12-byte triples of RVAs). Not a call site.
  - `0x143732A90` — `.rdata` vtable slot (8-byte function pointer) at index ~50 of
    the vtable starting at `0x143732900`. The owning class is constructed at
    `FUN_141796CD0` — a multi-inheritance object assigning 4 vtables
    (`PTR_FUN_1437328D8`, `PTR_FUN_143732900`, `PTR_FUN_1437327E8`, `PTR_FUN_143732780`).
    Exact UE class identity not pinned; likely a render-fence-owning subsystem
    (viewport/scene renderer/media player). Calls are issued through this vtable
    slot, not a direct call.

### What `r.OneFrameThreadLag = 0` actually does

1. The 2-slot toggle stops happening — both `g_FFrameEndSync_State.nCursor` and
   `g_FFrameEndSync_State_EngineTick.nCursor` stay at 0 forever.
2. `FRenderCommandFence::Wait` is always called on the same slot that
   `BeginFence` just kicked off → the game thread fully serialises with the render
   thread every frame.
3. Result: -1 frame of input lag, but the GT loses its ability to start frame N+1
   while RT finishes frame N. On GPU-bound hardware this is a hard FPS drop; on
   CPU-bound hardware it's nearly free.
4. SC6 specifically: the game is normally GPU-light (~2-3ms RT per frame on modest
   hardware), so the cost is usually small — but it shows up immediately as a
   harder 16.67ms frame-time floor with no room for GT spikes to hide.

### HorseMod implementation notes

**Preferred path** — write 0 directly to the int storage:
- Both `g_pCVar_OneFrameThreadLag_Value` and `g_pCVar_OneFrameThreadLag_Value_EngineTick`
  ultimately point to the **same** `FConsoleVariableData<int>` shadow array (lazily
  populated on first read of each global). One write to `IConsoleVariable::Set("0")`
  via vtable updates both shadow ints.
- AOB pattern for the CVar storage will need to be derived; static-address approach
  is simpler — find the `IConsoleVariable*` (cached at `DAT_1443B3528` once
  `CVar_Register_OneFrameThreadLag_Default1` runs) and call its `Set("0")` via vtable.

**Surgical fallback** (if the CVar Set path is fragile) — NOP-out the consumer's
toggle, forcing lag=0 unconditionally:
- At `FFrameEndSync_AdvanceTwoSlot`, the `if (bAllowOneFrameLag != 0)` test guards
  the cursor update. Forcing the test to fail via instruction patch makes cursor
  stuck at 0 → equivalent to `r.OneFrameThreadLag = 0`.
- This patches a single instruction and works regardless of whether the CVar was
  ever read. But it disables the runtime knob, which is bad for testing.

**Don't combine** — patching both the CVar and the consumer is redundant and adds
risk. Pick one. CVar approach (Set via vtable) is recommended.

### Interaction with HorseMod's existing systems

- **WorldTickGate freeze (Site 9):** while frozen, `FEngineLoop::Tick` still runs
  the FFrameEndSync block — the cursor keeps toggling, the render thread still
  drains. No leak. Freeze + r.OneFrameThreadLag=0 stacks safely (the freeze gates
  Actor::Tick paths, not the engine-level fence).
- **Replay scrub:** UDemoNetDriver's `GotoTimeInSeconds` runs entirely on the game
  thread and produces a burst of replication work. With `lag=1` that work overlaps
  the previous frame's render; with `lag=0` each scrub-tick step has to wait for
  RT completion. For a single scrub jump (one frame's worth of catch-up) the
  difference is negligible.
- **Online disable predicate:** unaffected. The CVar/FFrameEndSync layer is
  upstream of any netcode.

---

## `r.GTSyncType` — sync-target selector (companion to OneFrameThreadLag)

The CVar that picks **which downstream thread/queue** the game thread synchronizes
against inside `FRenderCommandFence::BeginFence`. Default = 0: sync to render thread
(canonical UE4). Mode 1: sync to RHI thread (commands queued to GPU driver). Mode 2:
sync to GPU swap-chain flip (lowest input-to-photon latency; needs DXGI flip support).

### Registrar (verified 2026-05-13)

`CVar_Register_GTSyncType_Default0 @ 0x140229B70` — static CRT-init constructor, no
caller. Mirrors the OneFrameThreadLag registrar in shape:

1. Lazy-init `g_pIConsoleManager @ 0x14415cd80` if null.
2. `IConsoleManager::RegisterConsoleVariable(L"r.GTSyncType", 0, helpText, 0)` via
   vtable `+0x10`. Result stored at `g_pCVar_GTSyncType_Var @ 0x1443518d0`.
3. Wrapper pointer (typed-T CVar adapter) installed at `g_pCVar_GTSyncType_Wrapper @ 0x1443518c8`.
4. `IConsoleVariable::GetValueAddress` via vtable `+0x38` → caches `int32[2]` shadow
   address at `g_pCVar_GTSyncType_Value @ 0x1443518d8`. Two slots = game-thread copy
   (idx 0) and render-thread copy (idx 1), selected by `is_game_thread ^ 1`.
5. `atexit(FUN_1432134A0)` for shutdown cleanup.

### Consumer (single site)

`FRenderCommandFence::BeginFence @ 0x1415E9510`, value-load instruction at
`0x1415e95c1`. The relevant fragment:

```c
lVar5 = g_pCVar_GTSyncType_Value;
if (bSyncToRHI != 0) {
    bOnGT  = !GIsGarbageCollecting || GetCurrentThreadId() == GGameThreadId;
    iMode  = ((int*)lVar5)[bOnGT ^ 1];          // 0/1/2 from the right shadow slot
    if (iMode != 0) {
        // Build an RT task that ALSO waits on RHI / GPU completion
        // (paths via FUN_1415EBDC0 or FUN_141af7da0 depending on GC state)
        ...
    }
}
// Mode 0 OR bSyncToRHI=false: standard RT-only dispatch via FUN_140d34880
```

So GTSyncType is **only consulted when the caller passes bSyncToRHI=1**, and even then
mode 0 is a no-op (falls through to the plain RT-only path).

### Interaction model with `r.OneFrameThreadLag`

| | `OneFrameThreadLag=1` (default) | `OneFrameThreadLag=0` |
|---|---|---|
| **`GTSyncType=0`** (default) | Stock UE4. GT runs 1 frame ahead of RT. RT-only sync. **+1f lag, +throughput.** | GT serializes with RT each frame. RT-only sync. **-1f lag, -throughput.** |
| **`GTSyncType=1`** | GT runs 1 frame ahead, but the fence it eventually waits on is the **RHI-thread** fence (one stage closer to GPU). Marginally more "real" pipelining. | GT waits for RHI submission each frame. **-1f vs default**, snappier than mode-0+lag-0 by the RHI submit latency. |
| **`GTSyncType=2`** | GT runs 1 frame ahead, fence waits on GPU swap-chain flip. Strongest pipelining; requires DXGI flip support. | GT waits for GPU flip each frame — **lowest input-to-photon latency the engine can express**. Requires flip-model swapchain. |

Compositional rule:
- **OneFrameThreadLag picks _which_ fence to wait on** (this frame's vs. previous frame's).
- **GTSyncType picks _what_ that fence represents** (RT signal vs. RHI submit vs. GPU flip).

Verified in the `FFrameEndSync_AdvanceTwoSlot` decompile (Key Functions table entry
@ `0x1421876D0`): its `BeginFence` call passes `bSyncToRHI=1`, so the GTSyncType
read at `0x1415e95c1` **does** fire on every FFrameEndSync tick. Modes 1 and 2
therefore propagate into the actual main-loop sync.

### SC6 default

`g_pCVar_GTSyncType_Value` is zero-initialized. SC6 issues no `r.GTSyncType` console
command in any path I've audited, and there's no in-game UI for it. So stock SC6
runs **mode 0** — the FFrameEndSync pipeline syncs against the render thread only,
exactly the canonical UE4 default.

### HorseMod implementation notes

**The interesting combo is `r.OneFrameThreadLag=0` + `r.GTSyncType=2`.** This is
the lowest-latency configuration the engine can produce without binary patching:
each frame GT waits for GPU present-flip completion before issuing the next frame.
Conservatively gives a "Reflex-lite" reduction below the stock `lag=0,GTSyncType=0`
floor.

Cost / risk:
- **Mode 2 requires a DXGI flip-model swapchain.** SC6's `FD3D11Viewport_Init @ 0x1411F4F70`
  uses `CreateSwapChain` — needs verification whether the resulting swapchain is
  flip-model (`DXGI_SWAP_EFFECT_FLIP_DISCARD/_SEQUENTIAL`) or legacy bitblt. If
  legacy, mode 2 silently degrades or no-ops. Mode 1 (RHI sync) has no such
  hardware requirement.
- **GPU-bound headroom required.** Same caveat as `OneFrameThreadLag=0`: if SC6 is
  GPU-bound, this collapses pipelining → frame-rate stability suffers.
- **Same write path as OneFrameThreadLag.** The CVar `Set(...)` route via the
  cached `IConsoleVariable*` at `g_pCVar_GTSyncType_Var @ 0x1443518d0` updates both
  shadow slots atomically. Same plumbing, separate CVar.

**Recommended ordering** if combining with the existing tier-1 patches:
1. `r.OneFrameThreadLag 0` (verified working, -1f).
2. `RHI.MaximumFrameLatency 1` (verified pipeline, -1..2f).
3. `r.GTSyncType 1` (RHI sync) as a safe intermediate.
4. `r.GTSyncType 2` only if the flip-model swapchain is confirmed and GPU
   headroom is present — optional, marginal gain over mode 1 in most cases.

Steps 1 and 2 dominate the achievable reduction. Step 3/4 is a small additional
shave; not a separate frame in the budget table.

---

## Removal strategies, ranked

### Tier 1 — Console / RHI tweaks (safest, biggest win)

**1. `r.OneFrameThreadLag 0`** — removes 1 frame
- See `## r.OneFrameThreadLag — full mechanism` above for the full Ghidra-verified
  algorithm, the two parallel state instances, and the per-tick call sites.
- Mechanism summary: collapses the FFrameEndSync 2-slot fence pipeline so the game
  thread waits for the render thread every frame instead of pipelining N+1 ahead.
- Downstream: GPU utilization drops; on bottlenecked hardware (CPU-bound at 60fps),
  frame-rate stability may collapse. SC6 is usually GPU-light, so the practical cost
  is small.
- Verdict: **safe gameplay-wise**, costs perf headroom. Recommend if hitting 60fps
  cap consistently.
- HorseMod implementation: at startup, locate the `IConsoleVariable*` cached at
  `DAT_1443B3528` (populated by `CVar_Register_OneFrameThreadLag_Default1 @ 0x1402708E0`),
  call its `Set("0")` via vtable. Both state instances pick up the change on next read.

**2. Lower DXGI MaxFrameLatency** — removes 1-2 frames
- Mechanism: DXGI swapchain queues up to N frames for present. Default 3 (DXGI default; UE4 inherits, SC6 never overrides).
- Downstream: reduces frame-pacing buffer. GPU-bound systems will see hitches.
- Verdict: **safe gameplay-wise** (no sim impact); needs healthy frame budget.

**Confirmed pipeline** (verified 2026-05-13):
- CVar `RHI.MaximumFrameLatency` registered at `RegisterCVar_RHI_MaximumFrameLatency @ 0x1401D6EF0`.
- Value storage: `g_nRhiMaxFrameLatency @ 0x14407B088` (int32, default 3).
- Swapchain creation in `FD3D11Viewport_Init @ 0x1411F4F70` — calls `CreateSwapChain` and `MakeWindowAssociation` but **does NOT call `SetMaximumFrameLatency`**. SC6 relies on the DXGI default of 3.
- The only call site for `IDXGIDevice1::SetMaximumFrameLatency` is inside `FD3D11Viewport_PresentChecked @ 0x141201F80`:
  - Reads `g_nRhiMaxFrameLatency` every present (`mov eax, [rip+disp]` @ `0x14120201A`)
  - Compares against cached `viewport+0x48` (initialized to 3 in the ctor)
  - On mismatch: `QueryInterface(IID_IDXGIDevice)` then `dxgi->SetMaximumFrameLatency(value)` via vtable `+0x60` (slot 12) at `0x141202086`.
- Actual `IDXGISwapChain::Present(syncInterval, 0)` lives in `FD3D11Viewport_PresentSwapChain @ 0x141202100` via vtable `+0x40`.

**HorseMod implementation (recommended)**: write 1 to `g_nRhiMaxFrameLatency @ 0x14407B088`. The existing CVar sink in `PresentChecked` detects the change and calls `SetMaximumFrameLatency(1)` on the next present. No detour needed.

**AOB pattern** for the CVar load in `PresentChecked` (use if rebasing breaks the static address):
```
8B 05 ?? ?? ?? ?? 39 43 48 74 ?? 89 43 48 4C 8D 44 24 40 48 8B 43 18 48 8D 15
```
The RIP-relative dword after `8B 05` resolves to `g_nRhiMaxFrameLatency`.

D3D12 mirror (unused in stock SC6 but documented): `g_nD3D12MaxFrameLatency @ 0x14407B19C`, CVar registered at `RegisterCVar_D3D12_MaximumFrameLatency @ 0x1401D77A0`.

**3. Fullscreen-Exclusive mode** — removes 1 frame (windowed-mode users only)
- Mechanism: bitblt-model swapchain (`DXGI_SWAP_EFFECT_DISCARD = 0`, confirmed at byte level — see Tier 3 DXGI section) goes through the DWM compositor in windowed mode, adding 1 frame. Fullscreen-exclusive bypasses DWM entirely.
- Verified `IDXGISwapChain::SetFullscreenState` callsites:
  - Shutdown: `FD3D11Viewport_Destructor @ 0x1411F5F30` calls `SetFullscreenState(FALSE, NULL)` near `0x1411F5F60`.
  - Toggle: `FD3D11Viewport_ConditionalToggleFullscreen @ 0x1411F7FB0` called from `FD3D11Viewport_Resize` when fullscreen flag changes.
- Verdict: **no HorseMod patch needed**. User flips the in-game Window Mode option from Windowed/Borderless to Fullscreen.
- Cost: lose Alt-Tab fluidity in windowed-borderless multi-monitor workflows. No sim impact.
- HorseMod adjacent work: surface this as a HUD/log warning when running in non-fullscreen-exclusive mode.

**4. NVIDIA Reflex via NvAPI piggyback** — removes 1-2 frames (NVIDIA only)
- Mechanism: `NvAPI_D3D_Sleep` aligns CPU/GPU at frame start so the game thread starts processing input *just* before the GPU needs the next frame's commands. Driver-side, not engine-side.
- SC6 has NO native Reflex integration (verified — no `NvAPI_D3D_SetSleepMode`, no `Reflex*` strings). But it **does have NvAPI infrastructure** already loaded for HDR/SLI features:

  | Symbol | Address | Purpose |
  |---|---|---|
  | `NvApi_LoadAndInit` | `0x140030FF0` | `LoadLibrary("nvapi64.dll")` |
  | `NvApi_EnsureLoaded` | `0x140031090` | Public init entry (force-load via this call) |
  | `g_ahNvApi_Modules` | `0x1443D5D10` | HMODULE cache (array, indexed by lib slot * 8) |
  | `g_pfnNvApi_QueryInterface` | `0x1443D5CF0` | Cached `nvapi_QueryInterface` fnptr |
  | `g_dwNvApiRefCount` | `0x1443D5D38` | Atomic refcount around every wrapper call |

  Existing 11 NvAPI wrappers at `0x140031110..0x140031960` (DRS_CreateSession, DRS_LoadSettings, Disp_HdrColorControl, DRS_DestroySession, D3D_GetCurrentSLIState, DRS_FindProfileByName, DRS_CreateProfile, DRS_SetSetting, DRS_SaveSettings, D3D11_SetDepthBoundsTest, DRS_FindApplicationByName). None are Reflex magic IDs.

- Magic IDs HorseMod must resolve (NOT in SC6's existing table — resolved fresh via `g_pfnNvApi_QueryInterface`):
  - `NvAPI_D3D_SetSleepMode = 0xAC1CA9E0` — set once after device creation
  - `NvAPI_D3D_Sleep = 0x852CD1D2` — call once per frame at frame-top

- D3D11 device location: `FD3D11DynamicRHI + 0x70` (confirmed via `FD3D11Viewport_PresentChecked` decompile). Capture via PolyHook2 detour of `FD3D11DynamicRHI_InitD3DDevice @ 0x1411FF900` epilogue.

- Hook point for per-frame Sleep: `FEngineLoop::Tick @ 0x140396450` prologue. Reflex Sleep MUST run on the game thread *before* input is polled — FEngineLoop::Tick runs in a tight `while (!GIsRequestingExit)` loop and is upstream of every input read.

- Verdict: **clean implementation, low risk**.
- Cost: nothing on NVIDIA ≥R450. No-op on AMD/Intel (QueryInterface returns NULL).
- Implementation pattern:
  ```cpp
  // 1. Force-load NvAPI (it's already in SC6's binary as a load-on-demand DLL)
  ((void(*)())0x140031090)();   // NvApi_EnsureLoaded(0)
  auto qi = *(NvApiQI_t*)0x1443D5CF0;
  if (!qi) return;   // No NVIDIA hardware

  // 2. Resolve Reflex fnptrs
  auto pfnSetSleepMode = qi(0xAC1CA9E0);
  auto pfnSleep        = qi(0x852CD1D2);
  if (!pfnSetSleepMode || !pfnSleep) return;   // Driver < R450

  // 3. Detour InitD3DDevice (0x1411FF900) epilogue to capture device at this+0x70
  // 4. Detour FEngineLoop::Tick (0x140396450) prologue to call pfnSleep(device)
  ```

- AMD users get nothing from this patch directly. AMD Anti-Lag is driver-side toggle, not API-callable from the game in stock UE4. HorseMod can't help AMD users beyond suggesting they enable Anti-Lag in Adrenalin software.

- Reflex Boost: setting `bLowLatencyBoost = true` in the `NV_SET_SLEEP_MODE_PARAMS_V1` struct keeps GPU clocks high during input-wait. Marginally faster, slightly higher power. Worth A/B testing.

**5. VSync off** — removes 0.5-1 frame
- Pure visual tradeoff, common competitive choice.
- Implementation: Set("0") via vtable+0x60 on `g_pCVar_rhi_SyncInterval_Var @ 0x144351730` (the IConsoleVariable* cached by `CVar_Register_rhi_SyncInterval @ 0x1402289D0`). The value flows through `FUN_1415E2C90 @ 0x1415E2C90` into `FD3D11Viewport_PresentSwapChain` as the `Present(syncInterval, 0)` first arg.
- Side effect: uncaps SC6's framerate (because `t.MaxFPS` defaults to 0 — VSync IS SC6's frame cap). SC6's simulation is locked at 60Hz regardless, so running at >60Hz only smooths visuals.
- For tearing-free VSync-off, combine with the flip-model conversion (Tier 3) + `DXGI_PRESENT_ALLOW_TEARING` flag.

### Tier 1b — Asymmetric mode-switch (preserves offline feel)

**Design goal**: keep offline play bit-identical to stock so existing muscle memory transfers cleanly; reduce online latency to match offline as closely as possible.

**Frame budget breakdown** (London-to-London friend, RTT ~15ms, D=2):

```
Component                       Offline    Online
─────────────────────────────────────────────────
Input poll → game thread          1 fr       1 fr
Netcode delay D                   —          2 fr   ← only this differs
Game thread → render thread       1 fr       1 fr
Present queue K                   3 fr       3 fr
Monitor scan-out + panel        ~1 fr      ~1 fr
                                  ────       ────
Total                             6 fr       8 fr
```

Online ≈ Offline + D. The "online portion" of latency is just D — typically 2-3 frames for low-ping matches, scaling up with distance. Most of the perceived heaviness is the *same local pipeline that exists offline*.

**Mode-switch strategy**: drive `g_nRhiMaxFrameLatency` between 3 (offline) and 1 (online) based on the existing online-state predicate that HorseMod's `WorldTickGate` already uses.

```
Mode                           Total latency (D=2)
──────────────────────────────────────────────────
Stock offline (K=3)              ~6 frames    ← baseline players know
Stock online (K=3)               ~8 frames    ← +D, feels heavy
Patched offline (K=3, no-op)     ~6 frames    ← unchanged, preserved
Patched online (K=1)             ~6 frames    ← matches offline at D=2
                                              (D=3: 1f heavier; D=4: 2f heavier;
                                               D=5+: can't fully close)
```

**Why mode-switch over global lowering**:
1. Zero regression risk for offline — players' muscle memory preserved exactly.
2. Maximum gain where it matters — online is where the latency complaints live.
3. GPU-bound jitter risk only manifests online, where variable framerate from network jitter masks it.
4. No new user-facing settings.

**Why this doesn't "give the netcode a free frame"**: a common misconception is that lowering local K frees up budget for the netcode delay D. It does not — D is set by physical RTT and unaffected by render-pipeline depth. The savings are perceptual (photons reach eyes sooner), not netcode-budget.

The redistribution between perceived latency and netcode robustness is a *manual* trade — if a HorseMod user wanted more network jitter tolerance instead of snappier feel, they could bump the configured online input delay up by the same amount K was cut. Not automatic.

**Caveats**:
- Users running Nvidia Reflex / Radeon Anti-Lag globally already have K≈1 effectively. For them the offline "feel they know" is already snappier than stock, so the preservation premise breaks down. Patch is harmless (no-op overlap).
- On GPU-bound hardware, K=1 online can stall the render thread → game thread blocks → inputs sent to network slower → **netcode gets worse**. HorseMod should expose a user override to fall back to K=2 or K=3 online for this case.

### Tier 2 — Aggressive CVar paths (more cost)

**1. `r.FinishCurrentFrame = 1`** — removes 1 frame
- Mechanism: reorders the per-frame loop so `Present()` runs BEFORE the GPU sync/flush of `FUN_1412111A0`. Effect: CPU stalls until this frame's GPU work completes before next-frame command queue can fill. Removes the render→GPU pipeline depth.
- Verified consumers (sub-agent B audit 2026-05-13):
  - D3D11 path: `0x141208970` (caches int* at `0x1442AF0B8`, branches on `+4` = render-thread shadow slot) — **active on SC6**
  - D3D12 path: `0x14124C420` (caches at `0x1442AF390`) — inactive (SC6 ships D3D11)
  - OpenGL path: `0x1415CC980` (caches at `0x144344558`) — inactive
  - Vulkan path: `0x141589480` — **lazy-inits but never reads** (no-op on Vulkan)
- HorseMod implementation: Set("1") via vtable+0x60 on `g_pCVar_FinishCurrentFrame_Var @ 0x144166440`.
  - Cached by registrar `CVar_Register_FinishCurrentFrame_Default0 @ 0x1401BBE30`.
  - Int storage at `g_pCVar_FinishCurrentFrame_Value @ 0x144166448` (don't write directly — bypasses the 2-slot shadow update).
- Risk: low for correctness (CVar is `ECVF_RenderThreadSafe`, safe to flip mid-frame).
- Cost: HIGH for performance — full pipeline serialization. Only worth it on hardware that has perf headroom to spare. Stacks additively with `r.OneFrameThreadLag = 0` (the two CVars affect independent stages: GT↔RT for OFTL, RT↔GPU for FCF).
- Verdict: optional Tier-2 add-on for hardcore latency hunters. Don't enable by default.

**2. Surgical OneFrameThreadLag bypass (fallback)** — alternative to CVar Set
If `r.OneFrameThreadLag`'s CVar Set path is fragile for some reason, the consumer site at
`FFrameEndSync_AdvanceForOneFrameThreadLag @ 0x1417B7950` reads the CVar shadow and calls
`FFrameEndSync_AdvanceTwoSlot(&g_FFrameEndSync_State, lag != 0)`. We could NOP-out the
consumer call or force the second arg to 0 (or, more surgically, patch the
`if (bAllowOneFrameLag != 0)` cursor-toggle gate inside `FFrameEndSync_AdvanceTwoSlot @ 0x1421876D0`
so the cursor stays at 0 forever — equivalent to `r.OneFrameThreadLag = 0` regardless of the CVar).
But this is rough and disables the runtime knob; CVar `Set("0")` via the cached `IConsoleVariable*`
at `DAT_1443B3528` is cleaner. See `## r.OneFrameThreadLag — full mechanism` for the complete
dispatch picture.

---

### Tier 3 — DXGI Flip-Model Conversion (heavy lift, unlocks 3 more frames)

**Status:** byte-level patch addresses fully verified 2026-05-13. Implementation is feasible
but requires PolyHook2 mid-function detour due to a 4-byte → 7-byte instruction expansion.

**Mechanism:** SC6's UE4 fork uses pre-flip-model APIs:
- `IDXGIFactory::CreateSwapChain` (the legacy DXGI 1.0 entry point)
- `DXGI_SWAP_EFFECT_DISCARD` (bitblt model, value 0)
- No `DXGI_PRESENT_ALLOW_TEARING`, no `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`

**Verified absent** from binary strings: `AllowTearing`, `WaitableObject`, `FLIP_DISCARD`, `DXGISwapChain2`, `DXGIFactory2`, `FRAME_LATENCY`. **SC6's UE4 fork pre-dates flip-model integration** (matches an early-mid UE4.x version).

Converting to flip-model unlocks:
1. **Drop 1 DWM compositor frame** in windowed mode (matches Tier-1 #3 fullscreen-exclusive without forcing fullscreen).
2. **`DXGI_PRESENT_ALLOW_TEARING`** flag becomes available → VSync-off without tearing artifacts in windowed.
3. **`IDXGISwapChain2::GetFrameLatencyWaitableObject`** → fine-grained frame-pacing primitive (often −1 frame beyond `MaxFrameLatency=1`).
4. **`r.GTSyncType=2`** (GPU-flip sync mode) becomes functional — currently degraded/no-op on bitblt swapchain.

#### Init-path patches (`FD3D11Viewport_Init @ 0x1411F4F70`)

Stack frame layout: `DXGI_SWAP_CHAIN_DESC` base = `RBP - 0x41`.

```
0x1411F5220  C7 45 E7 01 00 00 00       MOV [RBP-0x19], 1           ; BufferCount
                                        ↓ PATCH BYTE AT 0x1411F5223: 01 → 02
                                        (flip-model requires BufferCount ≥ 2)

0x1411F5227  44 89 6D FB                MOV [RBP-0x05], R13D        ; SwapEffect = R13D = 0 (DISCARD)
                                        ↓ REWRITE NEEDED to FLIP_DISCARD = 4
                                        4-byte instr cannot hold a 4-byte immediate
                                        Solution: PolyHook2 mid-function detour at
                                        0x1411F5227, replicate 11 displaced bytes
                                        (this + the Flags MOV below) with new values

0x1411F522B  C7 45 FF 02 00 00 00       MOV [RBP-0x01], 2           ; Flags = NONPREROTATED
                                        ↓ PATCH BYTES AT 0x1411F522E..F522F: 02 00 → 02 09
                                        Result: Flags = 0x902 =
                                          DXGI_SWAP_CHAIN_FLAG_NONPREROTATED (0x2)
                                        | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (0x800)
                                        | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT (0x100)
```

CreateSwapChain call: `0x1411F5257 FF 50 50` (vtable+0x50, slot 10 of `IDXGIFactory`).

**The earlier plate-comment on `FD3D11Viewport_Init` saying "SwapEffect = 2 (FLIP_DISCARD or DISCARD)" was WRONG** — that immediate is Flags, not SwapEffect. Fixed in the plate comment 2026-05-13.

#### Resize-path patches (`FD3D11Viewport_Resize @ 0x14120E9F0`)

Must also patch — otherwise the swapchain falls back to BufferCount=1 on first window resize, breaking flip-model.

```
0x14120EC95  C7 44 24 28 02 00 00 00   MOV [RSP+0x28], 2            ; Flags (stack arg 6)
                                       ↓ PATCH BYTES AT 0x14120EC99..F: 02 00 → 02 09

0x14120ECA0  BA 01 00 00 00             MOV EDX, 1                  ; BufferCount (arg 2)
                                       ↓ PATCH BYTE AT 0x14120ECA1: 01 → 02

0x14120ECAE  41 FF 52 68                CALL [R10+0x68]              ; IDXGISwapChain::ResizeBuffers
                                       (vtable+0x68, slot 13)
```

#### Present-path patches (`FD3D11Viewport_PresentSwapChain @ 0x141202100`)

After flip-model is active, enable the tearing flag for proper VSync-off:
```
IDXGISwapChain::Present(syncInterval, 0)
  → second arg hardcoded 0
  ↓ PATCH: rewrite second arg to 0x200 (DXGI_PRESENT_ALLOW_TEARING)
```
Exact byte not pinned in this audit — locate at `FD3D11Viewport_PresentSwapChain @ 0x141202100` near vtable+0x40 CALL site.

#### Optional: SetFullscreenState handling

`FD3D11Viewport_ConditionalToggleFullscreen @ 0x1411F7FB0` calls `SetFullscreenState(viewport.fFullscreen, output)` from the Resize path. Under flip-model this becomes borderless-fullscreen (acceptable). For TRUE tearing-windowed mode (the most snappy configuration), NOP this call.

#### Verified vtable slot reference (for HorseMod implementation)

**`IDXGISwapChain` vtable** (verified via SC6's existing call sites):
- `+0x40` `Present(syncInterval, flags)` — slot 8
- `+0x50` `SetFullscreenState(BOOL, IDXGIOutput*)` — slot 10
- `+0x58` `GetFullscreenState(BOOL*, IDXGIOutput**)` — slot 11
- `+0x68` `ResizeBuffers(BufferCount, W, H, Format, Flags)` — slot 13
- `+0x70` `ResizeTarget(DXGI_MODE_DESC*)` — slot 14

**`IDXGIDevice1` vtable** (verified via `FD3D11Viewport_PresentChecked`):
- `+0x60` `SetMaximumFrameLatency(UINT)` — slot 12

**`IDXGIFactory` vtable** (verified via `FD3D11Viewport_Init`):
- `+0x50` `CreateSwapChain(IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**)` — slot 10

**`FD3D11Viewport` struct** (Ghidra struct created 2026-05-13, 200 bytes, applied to 6 viewport functions):
- `+0x18` `pD3DRHI` (FD3D11DynamicRHI*)
- `+0x40` `Hwnd` (HWND)
- `+0x48` `nCachedMaxFrameLatency` (init=3)
- `+0x54` `fFullscreen` (uint8)
- `+0x58` `dwFormatMode` (uint32)
- `+0x60` `pSwapChain` (IDXGISwapChain*)
- `+0x68` `pBackBuffer` (FD3D11BackBuffer*)
- `+0x70` `pOutput` (IDXGIOutput*)
- `+0xC0` `pCustomPresent` (FCustomPresent*; Oculus VR hook)

**`FD3D11DynamicRHI` struct** (Ghidra stub, verified offsets):
- `+0x68` `pDXGIFactory` (IDXGIFactory*)
- `+0x70` `pDevice` (ID3D11Device*)
- `+0x78` `pImmediateContext` (ID3D11DeviceContext* wrapper)

#### Implementation approaches (ranked by risk)

1. **Surgical byte patches (lowest code, recommended):**
   - Byte patch at `0x1411F5223`: `01 → 02` (Init BufferCount)
   - Byte patches at `0x1411F522E..F522F`: `02 00 → 02 09` (Init Flags = 0x902)
   - PolyHook2 mid-function detour at `0x1411F5227`, replicate 11 displaced bytes with new values (SwapEffect=4, Flags=0x902)
   - Byte patch at `0x14120ECA1`: `01 → 02` (Resize BufferCount)
   - Byte patches at `0x14120EC99..F`: `02 00 → 02 09` (Resize Flags)
   - Locate Present flags byte in `FD3D11Viewport_PresentSwapChain`, patch to `0x200`
   - Optional: NOP `SetFullscreenState` call in `0x1411F7FB0` for tearing-windowed
   - Net: ~6 byte patches + 1 mid-function detour

2. **PolyHook2 inline hook on `CreateSwapChain` call (cleaner UE5-like):**
   - Detour `0x1411F525A` (the CALL [RAX+0x50] site)
   - Intercept the `IDXGIFactory*` and `DXGI_SWAP_CHAIN_DESC*` args
   - QI factory to `IDXGIFactory2` (Win8+)
   - Call `CreateSwapChainForHwnd` with `DXGI_SWAP_CHAIN_DESC1` (BufferCount=2, FLIP_DISCARD, ALLOW_TEARING+WAITABLE_OBJECT flags)
   - Resulting `IDXGISwapChain1*` satisfies the `IDXGISwapChain*` vtable contract (inheritance)
   - More complex but doesn't conflict with the existing UE4 plumbing

3. **PolyHook2 hook on `FD3D11Viewport_Init` entry (largest hook surface):**
   - Detour `0x1411F4F70`, call original, then after return replace `viewport->pSwapChain` with a freshly-created flip-model swapchain.
   - Release the original. Lets the rest of UE4's plumbing run untouched.

**Caveats:**
- Flip-model changes `SetFullscreenState` semantics. On modern Windows, it becomes borderless-fullscreen. If HorseMod wants TRUE fullscreen-exclusive, more work is needed.
- BufferCount=2 (or 3 for triple-buffer) is the minimum. Stay at 2 unless frame-pacing analysis shows benefit.
- Must test: window resize, fullscreen toggle, multi-monitor with `FullscreenDisplay=` cmdline.

### Tier 4 — Things to NOT do

- **Don't apply the 8WAYRUN patch** (NOP at 0x1403F0751). All-modes placebo — target instruction subtracts a field that is permanently 0, so NOP changes nothing offline OR online. Don't burn a patch slot on it.
- **Don't enable `r.RHICmdBypass`** (registrar `CVar_Register_RHICmdBypass_Default0 @ 0x140228320`). Verified 2026-05-13: the int storage at `g_pCVar_RHICmdBypass_Value @ 0x144344B58` has **zero per-frame readers**. Only reachable via `r.RHISetGPUCaptureOptions` debug command. The actual per-frame bypass switch is `g_bRHICommandListBypass @ 0x14419718D` (5+ readers in RHI dispatcher functions). Forcing that flag = 1 directly is RACY (a render-thread reader may read mid-write); writing 1 also disables the multithreaded RHI entirely, which SC6's render code wasn't tested for. Skip entirely.
- **Don't try to write `g_LuxBattle_InputRingBaseOffset_PerPlayer` to non-zero (for lag REMOVAL).** Already zero; nothing to remove. (Conversely, it IS a clean knob for lag INJECTION experiments — see HorseMod note below.)
- **Don't bypass `LuxBattle_TickCharaInput`.** Same-frame echo is already optimal.
- **Don't disable `r.OneFrameThreadLag` if you're already GPU-bottlenecked** — frame-rate stability will suffer.
- **Don't try `r.GTSyncType = 2` without flip-model first.** Mode 2 requires DXGI flip-model swapchain. SC6's bitblt swapchain makes mode 2 silently degrade or no-op. Land Tier 3 first.

### HorseMod-relevant side observations from the input pipeline audit

- **Lag-injection knob (for testing)**: writing nonzero to `g_LuxBattle_InputRingBaseOffset_PerPlayer[p]` will inject structural input lag for player `p`'s move-id-1 path **only**. AI, cinematic, training-partner, and the 0x6A/0x6B/0x6C alternates all bypass it. Useful for controlled-experiment verification of perceived-lag claims without code patching.
- **Replay scrub correctness**: after a backward seek, the held-frame derivatives at chara+0x215C..+0x2180 will hold stale values until the N-step iterate (N=2 or N=6 frames) walks them through `DAT_143E843F0`. This is the source of the brief input-state flicker on the first 1-2 frames after a seek even when chara position/anim are correct. Forward seeks don't hit this because the iterate runs each frame in TickCharaInput as part of normal play.
- **Mash-tech behavior**: hitstun-slide decay scales with `chara+0x2170` (compound button state) via `LuxBattleChara_DecayHitstunSlideVelocity`. If HorseMod ever wants to disable mash-tech for analysis (e.g. measuring "true" recovery times), zeroing `chara+0x2170` post-TickCharaInput is the targeted hook.
- **Frozen chara behavior**: while WorldTickGate Site 9 has PerFrameTick paused, `g_LuxBattle_LatestEngineInput_PerPlayer` retains its last value (the writer is gated by PerFrameTick entry). TickCharaInput does not run either, so chara+0x2150 also stays frozen. This is *correct* behavior for a freeze — the chara doesn't drift from "phantom" input — but means held buttons remain logically held during the freeze window.

---

## Unresolved (PerFrameTick args-builder)

`LuxBattle_PerFrameTick @ 0x1402DBC60` is invoked through a thunk at `0x1403D2A20`.
Further investigation 2026-05-13 fully characterized the thunk and its containing
table but did not produce a caller. Documented here so the dynamic-hook attempt
below isn't wasted re-deriving these facts.

### Thunk (verified)

- Address: `0x1403D2A20`, label `LuxBattleChara_VTable3_Thunk_LuxBattle_PerFrameTick`
- Bytes (8): `48 8b ca e9 38 92 f0 ff`
  - `48 8b ca` — `MOV RCX, RDX`
  - `e9 38 92 f0 ff` — `JMP rel32` → `0x1402DBC60`
- Member→static adapter: dispatcher passes `(this, args)` in `(RCX, RDX)`; thunk
  overwrites `RCX = args` and tail-jumps to PerFrameTick(args).
- One xref in the whole binary: the data pointer in slot `0x14327B678`. No code refs.

### Containing function-pointer table (verified)

Range: `0x14327B640..0x14327B738` (terminates at `0x14327B740`, which is the start of
`LuxMove_VDtor_Free388and398` — a destructor for an unrelated 0x3B0-byte class).

Slot 3 of this table = PerFrameTick thunk. The slot is Ghidra-labelled
`VTable_LuxBattleChara_Slot3_PerFrameTick`. Two consistent base interpretations:

- **Ghidra's "Slot3" indexing** → table base = `0x14327B660` (slot 3 = +0x18 = PerFrameTick).
  Slots 0..2 are thunks to `0x1402DB380` / `0x1402DAEB0` / `0x1402DB3A0` (unnamed).
- **User-side counting** (base = `0x14327B640`) → PerFrameTick lives at slot 7 (+0x38).
  Slot 0 (+0x00) is a thunk to `LuxBattle_ReinitCharaSlotForMove_SubsequentRound @ 0x1402DA710`.

Both work — the table is the same; only the indexing convention differs.

All slots decoded are thunks of the same family: optional `48 8b ca`/`8b ca`/`8b ca 49 8b d0`
adapter + `e9 rel32` into `0x1402Dxxxx` (battle-control functions). Two placeholder slots:
`KHitArea_UpdateFromAnimCell_Stub` (the binary's generic no-op) and
`UObjectBase_IsDestructionPending_Default`.

### What's been ruled out

- **No code references the table.** Ghidra has zero xrefs to `0x14327B640`, `0x14327B660`,
  or any slot. Absolute-address byte-pattern searches (`40 b6 27 43 01`, `60 b6 27 43 01`,
  `78 b6 27 43 01`) all return zero matches — confirms dispatch uses RIP-relative LEA,
  whose disp32 is unique per call site and unsearchable.
- **Neither chara constructor writes the table.** `ALuxBattleChara_Constructor @ 0x1403AB8D0`
  writes only `*chara = &ALuxBattleChara_vtable`; `ALuxCharaActorBase_Constructor @ 0x140440FB0`
  writes only `&PTR_FUN_1432a3020` + the three mesh component slots. Nothing stores
  `0x14327B660` into any chara field. So this is NOT a normal C++ secondary-vtable pointer.
- **`ALuxBattleChara::TickActor` does NOT call PerFrameTick.** `0x1403D0590` overrides
  vtable slot 131 (+0x418); its plate confirms it runs independently of PerFrameTick
  ("TickActor runs during HorseMod freeze while PerFrameTick is the only thing actually
  paused").
- **`ALuxBattleManager::Tick_MainStateMachine_At1461` does NOT call PerFrameTick.**
  `0x1403FBF30`'s plate explicitly states "Sits in parallel with LuxBattle_PerFrameTick
  (which is on chara->Actor::Tick)."
- **`PerFrameTick` is not a `UFunction`** (`Z_Construct_UFunction_*PerFrameTick*` does
  not exist). Not BP-callable.

### Eliminated UClass candidates (from prior audit)

- `ALuxBattleManager` — 24 BP natives, none match the thunk address cluster.
- `ULuxBattleFunctionLibrary` — separate native table at `0x14337CC70`.
- `ALuxInputKeyEventListener` — 1 native (`ChangeTargetPlayer`).

### Args struct (verified shape, unverified source)

`FLuxBattlePerFrameTickArgs` (24 bytes) — see Key Data Structures section.

Strongest hypothesis for args sources (unverified): `args[0]`/`args[1]` point to
`&pBM->FrameInput->Records[N]+0x3E0` reinterpreted as `u64*`. `args[2]` points to a
24-byte camera struct on BM.

### To resolve (dynamic approach)

Static analysis has been exhausted. The dispatch is invisible to Ghidra — most likely
a separate `FTickFunction` (non-`PrimaryActorTick` sub-tick) or a callback registered
through code Ghidra hasn't traced.

**One-frame dynamic hook resolves it.** HorseMod's Site 9 already detours PerFrameTick's
entry. Augment Site 9 with a one-time logging shim that captures:
- The return address from `[RSP]` at entry (caller RIP).
- The original `RCX` value (caller's `this`).
- The `RDX` value (args pointer; deref to confirm `g_LuxBattle_LatestEngineInput_PerPlayer`
  matches `*args[0]`/`*args[1]`).

Single frame of capture identifies the caller, the args-builder location, and the
sources of `args[0]`/`args[1]` — closes this entire investigation.

---

## Implementation Quick Reference

### IConsoleVariable / IConsoleManager vtable slots (verified 2026-05-13)

**`IConsoleManager` vtable** (singleton `g_pIConsoleManager @ 0x14415CD80`):
- `+0x08` `RegisterConsoleVariable<float>` — used by `t.MaxFPS`
- `+0x10` `RegisterConsoleVariable<int>` — used by every int CVar
- `+0x90` `FindConsoleVariable(const TCHAR*)` — name lookup

**`IConsoleVariable` vtable** (instance interface):
- `+0x00` dtor
- `+0x38` `GetValueAddress()` → `int*`
- `+0x40` `GetFloatRefOnAnyThread()` → `float*`
- `+0x60` **`Set(const TCHAR*, EConsoleVariableFlags)`** ← slot 12, the HorseMod-relevant setter
- `+0x68` `GetInt()`

### CVar IConsoleVariable* addresses (HorseMod can use directly after registrar runs)

| CVar | IConsoleVariable* | Int Storage | Default |
|---|---|---|---|
| `r.OneFrameThreadLag` | `0x1443B3528` | `0x14435D5A0` (canonical) / `0x1441457F8` (engine-tick) | 1 |
| `RHI.MaximumFrameLatency` | (registrar at `0x1401D6EF0`) | `g_nRhiMaxFrameLatency @ 0x14407B088` | 3 |
| `r.FinishCurrentFrame` | `g_pCVar_FinishCurrentFrame_Var @ 0x144166440` | `0x144166448` | 0 |
| `r.RHICmdBypass` | `g_pCVar_RHICmdBypass_Var @ 0x144344B50` | `0x144344B58` (dead — don't write) | 0 |
| `r.GTSyncType` | `g_pCVar_GTSyncType_Var @ 0x1443518D0` | `g_pCVar_GTSyncType_Value @ 0x1443518D8` | 0 |
| `rhi.SyncInterval` | `g_pCVar_rhi_SyncInterval_Var @ 0x144351730` | `g_pCVar_rhi_SyncInterval_Value @ 0x144351748` | 1 |
| `t.MaxFPS` | `g_pCVar_t_MaxFPS @ 0x1443B3570` | `g_pCVar_t_MaxFPS_Value @ 0x1443B3578` (float*) | 0.0 |

### Sample HorseMod CVar setter

```cpp
struct IConsoleVariable;
typedef void (*CVarSetFn)(IConsoleVariable*, const wchar_t*, int);

static inline void CVarSetByPtr(void* pCVarVar, const wchar_t* val, int flags = 0x8000000) {
    if (!pCVarVar) return;
    auto vtable = *(void***)pCVarVar;
    auto setFn = (CVarSetFn)vtable[12];   // slot +0x60 = Set
    setFn((IConsoleVariable*)pCVarVar, val, flags);
}

// Usage at HorseMod init:
CVarSetByPtr(*(void**)0x1443B3528, L"0");   // r.OneFrameThreadLag = 0
*(uint32_t*)0x14407B088 = 1;                 // RHI.MaximumFrameLatency = 1
CVarSetByPtr(*(void**)0x144166440, L"1");   // r.FinishCurrentFrame = 1 (optional, high FPS cost)
```

The `0x8000000` flag is `ECVF_SetByConsole` (the priority tier used by all existing in-binary setters).

---

## Cross-references

- `memory/project_sc6_input_lag_analysis.md` — full per-layer breakdown, 8WAYRUN
  mod analysis, structural-zero-lag proof
- `memory/project_sc6_perframetick_dispatch.md` — the failed args-builder hunt,
  FLuxBattleTickInfo struct, BM-side input actor map
- **`memory/project_sc6_lag_removal_implementation.md` — implementation-ready map
  with exact byte patches, vtable slot reference, and sample HorseMod code**
- `memory/project_sc6_online_netplay_map.md` — online delay-based netcode (separate
  lag source from offline)
- `memory/project_sc6_replay_system_map.md` — replay-state offsets, useful for
  understanding the FrameInputLog cache mechanism

---

## Glossary of frequently-confused things

- **`ALuxBattleFrameInput`** (BM+0x450, size 0x510) — LIVE input actor for offline play
- **`ALuxBattleFrameInputLog`** (BM+0x478, size 0x43E0) — REPLAY LOG + input cache
- **`FLuxBattleTickInfo`** (BM+0x1488, size 0x58) — REPLAY CATCH-UP STATE struct
- **`ALuxInputKeyEventListener`** — appears unused/minimal, NOT the live input handler
- **`g_LuxBattle_LatestEngineInput_PerPlayer`** (0x144855700) — `qword[2]` per-player current engine input, written by PerFrameTick
- **`g_LuxBattle_PerPlayerInputRing`** (0x14485E750) — `FLuxBattleInputRing[2]`, 61-slot u64 ring per player, populated each frame by `TickCharaInput`. Same-frame echo (base offset = 0).
- **`g_LuxBattle_PerPlayerInputRingCursor`** (0x14485EB20) — `uint[2]` monotonic write cursor; only writer is the case-1 INC in TickCharaInput; never reset
- **`g_LuxBattle_InputRingBaseOffset_PerPlayer`** (0x14470DED0) — `uint[2]` BSS-zero base offset; **no writer in entire binary**; dormant lookback knob
- **`FLuxBattleInputRing`** — 488-byte struct (single field `pAqwEntries: qword[61]`); per-player slot type for the input ring
- **`r.OneFrameThreadLag`** — UE4 CVar (default 1) controlling render-thread pipelining. **This is the biggest removable lag source.** Mechanism fully documented in `## r.OneFrameThreadLag — full mechanism`.
- **`r.GTSyncType`** — UE4 CVar (default 0) selecting which downstream thread `FRenderCommandFence::BeginFence` synchronizes against: 0=render thread, 1=RHI thread, 2=GPU swap-chain flip. Companion to OneFrameThreadLag (picks WHAT the fence syncs against; OFTL picks WHICH fence to wait on). Documented in `## r.GTSyncType — sync-target selector`. Mode 2 needs DXGI flip-model swapchain (see Tier 3).
- **`r.FinishCurrentFrame`** — UE4 CVar (default 0). When 1, reorders the per-frame loop so Present runs before GPU sync/flush, forcing CPU to wait for GPU completion each frame. Removes 1 frame of GPU-pipeline depth at high FPS cost. Active on SC6's D3D11 path; verified consumer at `0x141208970`. Set via vtable+0x60 on `g_pCVar_FinishCurrentFrame_Var @ 0x144166440`. Stacks additively with `r.OneFrameThreadLag = 0`.
- **`r.RHICmdBypass`** — UE4 CVar (default 0). **DEAD CODE in SC6**. The int storage `g_pCVar_RHICmdBypass_Value @ 0x144344B58` has zero per-frame readers. Only reachable via `r.RHISetGPUCaptureOptions` debug command. The actual per-frame bypass byte (`g_bRHICommandListBypass @ 0x14419718D`) is read 5+ times in RHI dispatch but writing it = 1 directly is racy and disables the multithreaded renderer. **Skip.**
- **`rhi.SyncInterval`** — UE4 CVar (default 1). SC6's actual frame cap: VSync at 60Hz. Setting to 0 uncaps the framerate (sim still 60Hz-locked). Set via vtable+0x60 on `g_pCVar_rhi_SyncInterval_Var @ 0x144351730`.
- **`t.MaxFPS`** — UE4 CVar (default 0.0f = uncapped). Not contributing to SC6's frame cap (VSync is). `g_pCVar_t_MaxFPS @ 0x1443B3570`, `g_pCVar_t_MaxFPS_Value @ 0x1443B3578` (float*).
- **DXGI flip-model conversion** — converts the swapchain from `DXGI_SWAP_EFFECT_DISCARD` (bitblt) to `DXGI_SWAP_EFFECT_FLIP_DISCARD` (flip-model). Unlocks `DXGI_PRESENT_ALLOW_TEARING`, `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, and `r.GTSyncType=2`. Requires byte patches in `FD3D11Viewport_Init` (BufferCount, SwapEffect rewrite, Flags) + `FD3D11Viewport_Resize` (same). See Tier 3.
- **NVIDIA Reflex piggyback** — SC6 has no native Reflex but already loads NvAPI for HDR/SLI. HorseMod resolves Reflex magic IDs (`SetSleepMode = 0xAC1CA9E0`, `Sleep = 0x852CD1D2`) via existing `g_pfnNvApi_QueryInterface @ 0x1443D5CF0`, captures D3D11 device at `FD3D11DynamicRHI+0x70` via `FD3D11DynamicRHI_InitD3DDevice @ 0x1411FF900` hook, calls `Sleep(device)` at `FEngineLoop::Tick @ 0x140396450` prologue. -1 to -2 frames on NVIDIA ≥R450.
- **`FD3D11Viewport`** — 200-byte struct (Ghidra struct created 2026-05-13). 30 fields. Key offsets: +0x18 pD3DRHI, +0x40 Hwnd, +0x48 nCachedMaxFrameLatency, +0x54 fFullscreen, +0x58 dwFormatMode, +0x60 pSwapChain, +0x68 pBackBuffer, +0x70 pOutput, +0xC0 pCustomPresent. Applied to all `FD3D11Viewport_*` functions.
- **`FD3D11DynamicRHI`** — Ghidra stub. +0x68 = IDXGIFactory*, +0x70 = ID3D11Device* (Reflex argument), +0x78 = ID3D11DeviceContext*.
- **IConsoleVariable::Set** — vtable+0x60 (slot 12), signature `void(IConsoleVariable*, const TCHAR* value, EConsoleVariableFlags flags)`. The universal HorseMod CVar-write path. Confirmed via decompile of `FUN_1430C3B20` (the existing `r.FinishCurrentFrame` setter inside `r.RHISetGPUCaptureOptions`).
- **IDXGIDevice1::SetMaximumFrameLatency** — vtable+0x60 (slot 12). The `FD3D11Viewport_PresentChecked @ 0x141201F80` path detects `g_nRhiMaxFrameLatency` changes and calls this on the device. HorseMod doesn't need to call it directly — just write to `g_nRhiMaxFrameLatency`.
- **`g_pCVar_GTSyncType_Var`** (`0x1443518d0`) / **`g_pCVar_GTSyncType_Value`** (`0x1443518d8`) / **`g_pCVar_GTSyncType_Wrapper`** (`0x1443518c8`) — the IConsoleVariable*, cached int32[2] shadow address, and typed-T wrapper for `r.GTSyncType`. Written once by `CVar_Register_GTSyncType_Default0 @ 0x140229B70`. Value is read at exactly one consumer site: `0x1415e95c1` inside `FRenderCommandFence_BeginFence`.
- **`g_pIConsoleManager`** (`0x14415cd80`) — the `IConsoleManager*` singleton. Lazily constructed by `FUN_140d34a60` on first CVar registration; vtable slot `+0x10` is `RegisterConsoleVariable`. Used by `~30` CVar registrars across UE4 subsystems (FEngineLoop, scalability, RHI, etc.).
- **`FFrameEndSync_State`** — 24-byte struct (created in Ghidra): `void *pFences[2]; int nCursor; uchar p_pad14[4];`. Backs the 2-slot fence pipeline that gates the game thread on the render thread.
- **`g_FFrameEndSync_State`** (`0x14435D580`) — callback-variant FFrameEndSync state. Driven by `FFrameEndSync_AdvanceForOneFrameThreadLag` dispatched indirectly through vtable slot at `0x143732A90`.
- **`g_FFrameEndSync_State_EngineTick`** (`0x1441457D8`) — canonical game-thread FFrameEndSync state. Driven inline by `FEngineLoop::Tick` once per UEngine tick.
- **`g_pCVar_OneFrameThreadLag_Value`** (`0x14435D5A0`) / **`g_pCVar_OneFrameThreadLag_Value_EngineTick`** (`0x1441457F8`) — `int *` pointers into the same `FConsoleVariableData<int>` shadow array: `[0]=GameThread copy, [4]=RenderThread copy`. Lazily populated on first read.
- **`FRenderCommandFence_BeginFence`** (`0x1415E9510`) / **`FRenderCommandFence_Wait`** (`0x1415EFEC0`) — the UE4 render-fence primitives the FFrameEndSync 2-slot algorithm uses. Fence done bit lives at `*(FRenderCommandFenceImpl+0x8) bit 26`, refcount at `+0x48`.
- **`RHI.MaximumFrameLatency`** — UE4 CVar (default 3) controlling the DXGI present-queue depth. Value stored at `g_nRhiMaxFrameLatency @ 0x14407B088`. Only consumer is `FD3D11Viewport_PresentChecked`, which calls `IDXGIDevice1::SetMaximumFrameLatency` when the value changes from cached. Writable at any time — change takes effect next present.
- **K** (shorthand in budget tables) — DXGI present-queue depth = `RHI.MaximumFrameLatency`.
- **D** (shorthand in budget tables) — SC6 netcode input delay (delay-based netcode). Set per-match, scales with RTT. Independent of K.
