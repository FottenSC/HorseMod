# Soulcalibur VI Latency Removal and Online Reinvestment Investigation

**Status:** Static reverse-engineering complete for the principal SC6/UE4 input, online-sync, render-thread, RHI, and D3D11 presentation paths. Runtime latency certification has not yet been performed.

**Last updated:** 2026-08-20

## Objective

Find latency that can be reliably removed from Soulcalibur VI, especially during online play, and determine whether verified local latency savings can be deliberately reinvested as additional online input-buffer room without making the game feel slower than stock.

The intended trade is:

```text
remove N frames from the local input-to-display pipeline
                         +
add N frames to online input delay / network safety margin
                         =
approximately stock total responsiveness with more tolerance for late packets
```

This trade is valid only when `N` is measured end-to-end. A queue setting that permits two fewer frames is not proof that two frames were occupied or removed during normal gameplay.

## Executive conclusion

No additional steady-state frame attributable to SC6's native input acquisition, Slate preprocessing, Lux actor scheduling, cache publication, or BattleManager consumption was found. SC6 orders the inspected stages before or within the same engine frame's battle simulation. An input transition that arrives after the current Slate sampling boundary naturally waits for the next frame; that ordinary sampling phase is not evidence of a hidden queue or removable frame.

The realistic savings are in UE4's render/RHI/presentation pipeline. The strongest candidates are:

1. `r.OneFrameThreadLag=0`
2. `r.GTSyncType=1`
3. `RHI.MaximumFrameLatency=1`
4. Exclusive fullscreen
5. Optional VSync-off presentation

Two lower-priority avenues also survive static review:

- `r.RHIThread.Enable 0` coherently removes the separate RHI execution queue, at the cost of all RHI-thread overlap. This is an alternative to synchronizing the game thread to that queue, not an additional guaranteed frame.
- A battle-only late XInput resample could capture controller transitions that arrive after UE's normal once-per-frame platform poll but before `FrameInput` consumes the Lux bitfield. Its theoretical benefit is only the remaining fraction of the current tick, and the stock poller cannot safely be called a second time wholesale.

These settings constrain overlapping queues and cannot be assigned additive frame values from static analysis. The currently defensible guaranteed reinvestment budget is therefore **zero frames until runtime measurement**. The best working hypothesis is that a carefully selected bundle can recover approximately one repeatable frame on a healthy 60 FPS system, with larger gains possible only when the stock renderer or driver was actually queued.

Several older claims have been rejected:

- DXGI maximum latency `3 -> 1` does not guarantee two saved frames.
- The former `5-6 frame maximum stack` and `3 frame practical bundle` were unsupported sums of overlapping upper bounds.
- The old flip-model patch used the wrong waitable-object flag and is unsafe.
- `DXGI_PRESENT_ALLOW_TEARING` permits tearing; it does not make VSync-off inherently tear-free.
- The proposed NVIDIA Reflex hook was not a complete or certified Reflex integration.
- `r.FinishCurrentFrame=1` cannot be assumed to stack as another full frame after other queue controls.
- The apparent independent scheduling of input actors is not a hidden frame; SC6 explicitly constructs the complete tick dependency graph.

## Confidence terminology

Each candidate is judged on two separate questions:

1. **Mechanism confidence:** Does SC6 definitely read the setting and execute the expected control path?
2. **Saving confidence:** Does that mechanism reliably reduce input-to-photon latency by a repeatable amount during real gameplay?

Ratings:

- **Very high:** Proven by exact SC6 native control flow or an authoritative API contract.
- **High:** Strong static evidence with only ordinary runtime confirmation missing.
- **Medium:** Mechanism exists, but effective runtime behavior depends on queue occupancy, driver, presentation mode, or load.
- **Low:** Proposal is incomplete, unmeasured, or known to degrade/fall back.
- **Invalid:** Existing implementation plan contains a technical error and must not be used.

## Verified stock frame path

### Live and online input ordering

`FEngineLoop_Tick @ 0x140396450` establishes the engine-level ordering:

```text
ProcessSlatePlatformInput @ 0x1410BE9B0 (call @ 0x140396845)
  -> ProcessSlateApplicationInputPreprocessorsAndUsers @ 0x1410AA630
     (call @ 0x14039684D)
  -> GEngine/world tick virtual call (call @ 0x1403968AD)
```

The platform-input call polls Windows/XInput state before the world tick. The inspected Lux keyboard callbacks, `HandleLuxBattleInputProcessorSideKeyDown @ 0x1404C3420` and `HandleLuxBattleInputProcessorSideKeyUp @ 0x1404C3B10`, synchronously update active-key state through `UpdateLuxBattleInputProcessorActiveKeyState @ 0x1404C3480`. The Lux input preprocessor's vtable slot `+0x08` resolves to `CalculateLuxBattleInputEdgesFromActiveKeys @ 0x1404D0480`, which folds active keys into the logical input bitfield and computes pressed edges during the same Slate preprocessor pass. No timer, frame counter, or deferred Lux input queue was found in this path.

The battle-side path then reads those processor bitfields through:

```text
UpdateALuxBattleFrameInputPlayerSlotStates @ 0x1403F5CD0
  -> UpdateLuxBattleFrameInputSlotStateFromProcessor @ 0x1403FC640
  -> GetLuxBattleInputProcessorLeftBitfield @ 0x1404BE600
     / GetLuxBattleInputProcessorRightBitfield @ 0x1404BE650
  -> ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30
  -> UpdateFrameInputLogCacheOnlineOrLocal @ 0x1403F2B60
  -> ProcessFrameInputSyncCacheAndAdvanceGameTime @ 0x1403E2000
  -> LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520
```

This proves the inspected acquisition, preprocessing, actor dependency, cache-publication, and consumption order. It does not prove how early within an OS/hardware sampling interval an arbitrary physical transition becomes observable.

`BuildLuxBattleManagerActorTickDependencyGraph @ 0x1403F8A20` constructs this prerequisite chain:

```text
PauseTicker
  -> CommonInput
  -> PauseController
  -> ShortcutController
  -> ReplayPlayer
  -> FrameInput
  -> FrameInputLog / FrameInputSync
  -> ReplayRecorder
  -> KeyRecorder
  -> BattleManager simulation
  -> TimeManager
  -> battle characters
```

`AddActorTickPrerequisiteActor @ 0x141C0CC00` is the UE Actor helper used for equivalent prerequisite relationships. `AActor+0x28` is the PrimaryActorTick region.

Consequences:

- Input production and FrameInputSync complete before BattleManager simulation.
- BattleManager does not consume a snapshot left over from an independently scheduled prior frame.
- Manually invoking one of these actors earlier risks double-ticking or violating downstream presentation state; it does not fix a stock missing prerequisite.

### Online synchronization transaction

`ProcessFrameInputSyncPipelineAndAdvanceUpdateTime @ 0x1403FDB80` performs the online source transaction in this order:

1. Update RTT samples.
2. Update adaptive input lag.
3. Drain received packet data into the synchronization cache.
4. Refresh current input.
5. Run the online input publication flow.
6. Encode/send the input stream.
7. Process online pause/state events.
8. Increment `UpdateTime`.

After the pause gate admits simulation, `ProcessFrameInputSyncCacheAndAdvanceGameTime @ 0x1403E2000` advances `GameTime`. `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520` then consumes the appropriate synchronized input row in the same actor tick.

The network callback clones/enqueues packet data. The game thread drains that receive queue before current input refresh and simulation. No additional one-frame packet batching stage was found in the transport sender.

### Native online delay

The adaptive target is:

```text
clamp(ceil(RTT_ms * 0.03) + InputLagOffset,
      InputLagMin,
      InputLagMax)
```

Observed defaults:

```text
InputLagMin    = 2 frames
InputLagMax    = 15 frames
InputLagOffset = 2 frames
InputSizeSend  = 4 rows
InputSizeReply = 16 rows
```

The ordinary local `FrameInputLog.InputDelay` field is forced to zero online and is not a removable online delay source. The historical `8WAYRUN` NOP patch subtracts a field that remains zero and therefore does not remove latency.

`UpdateFrameInputSyncInputLagTowardRttTarget @ 0x1403F8390` changes the effective `InputLag` toward the clamped target by at most one per update. The gameplay reads found for that field are the online-flow admission comparison in `ProcessFrameInputSyncOnlineInputFlow @ 0x1403F8410` and the state-event frame calculation in `ProcessFrameInputSyncOnlineStateEvent @ 0x1403FDFA0`; the other references are initialization/property registration.

## Candidate reliability summary

| Candidate | Mechanism confidence | Repeatable saving confidence | Realistic normal-play saving | Recommendation |
|---|---|---|---:|---|
| `r.OneFrameThreadLag=0` | Very high | Medium-high | `0-1` frame | First-line experiment |
| `r.GTSyncType=1` | High | Medium | `0-1+` depending RHI lead | First-line experiment |
| DXGI maximum latency `3 -> 1` | Very high | Low-medium | `0-2`, often `0` on an unqueued PC | First-line experiment; measure occupancy |
| Exclusive fullscreen | Very high | Medium-low | `0-1` frame | User-selectable baseline |
| VSync off | Very high | Medium | Phase/presentation-path dependent; no static bound | User preference; not network budget |
| `r.FinishCurrentFrame=1` | Very high | Low-medium | `0-1` frame | Diagnostic only |
| `r.GTSyncType=2` | High for dispatch, low for effective backend | Low | Unknown; may fall back | Do not budget |
| NVIDIA Reflex via raw NVAPI | Medium | Low | Workload dependent | Redesign and certify first |
| AMD Anti-Lag 2 SDK | Not integrated | Low | Workload dependent | Future vendor path |
| Flip model | Concept high; old patch invalid | Low | `0-1+` conditional | Redesign from scratch |
| Waitable swapchain | Not implemented | Low | Potentially useful | Requires complete frame-start wait path |
| `r.RHIThread.Enable 0` | Very high | Low-medium | `0-1+` depending RHI lead and frame-time cost | Diagnostic alternative to `GTSyncType=1` |
| Battle-only late XInput resample | High for sampling opportunity; implementation absent | Low | `0` to less than one tick | Future input-path experiment |
| Online pause-bubble override | Very high | High for qualifying successful recovery transitions | Exactly one recovery tick; zero steady-state | Optional network-recovery test |

## Candidate 1: `r.OneFrameThreadLag=0`

### Verified mechanism

Registrar:

```text
CVar_Register_OneFrameThreadLag_Default1 @ 0x1402708E0
```

Main synchronization primitive:

```text
FFrameEndSync_AdvanceTwoSlot @ 0x1421876D0
```

The frame-end state contains two render fences and a cursor. Each call begins a fence on the current slot.

- With the CVar enabled, the cursor changes before the wait, so the game thread waits for the older fence.
- With the CVar disabled, the cursor remains on the just-submitted fence, so the game thread waits for the current render-thread boundary.

This definitively removes permission for the game thread to remain a full frame ahead of the render thread.

### What is not guaranteed

The setting permits up to one frame of GT/RT lead; it does not prove that the render thread is always one frame behind. If the render thread has already reached the fence, disabling the lag produces little or no latency change for that frame.

Expected saving:

```text
best/queued case: approximately 1 frame
already-caught-up case: approximately 0
```

### Reliability and cost

- Gameplay correctness risk: low.
- Performance risk: medium.
- Main failure mode: loss of GT/RT overlap exposes CPU/render spikes and may cause missed 60 Hz deadlines.
- Reinvestment eligibility: only after tests demonstrate a stable lower input-to-photon result during heavy scenes.

### Preferred implementation

Use `IConsoleVariable::Set("0")` through the registered CVar. Do not combine that with an instruction patch of the cursor branch. The CVar preserves runtime toggling and updates the thread-aware shadow values.

## Candidate 2: `r.GTSyncType=1`

### Verified mechanism

Registrar:

```text
CVar_Register_GTSyncType_Default0 @ 0x140229B70
```

Consumer:

```text
FRenderCommandFence_BeginFence @ 0x1415E9510
```

SC6's frame-end synchronization passes `bSyncToRHI=1`, so `r.GTSyncType` is consulted in the real main-loop path.

Modes from SC6's embedded help text:

```text
0 = synchronize game thread to render thread
1 = synchronize game thread to RHI thread
2 = synchronize game thread to GPU swapchain flip on supported platforms
```

Mode 1 advances the synchronization boundary beyond render-command generation to the RHI thread. This addresses the parallel-rendering case where the render thread appears caught up while RHI/driver work remains queued.

### Interaction with OneFrameThreadLag

The settings control different dimensions of the same fence:

```text
OneFrameThreadLag -> which frame's fence is waited on
GTSyncType        -> what completion point that fence represents
```

Test both useful configurations independently:

1. `OneFrameThreadLag=1`, `GTSyncType=1`: retains a frame of overlap but anchors it to RHI progress.
2. `OneFrameThreadLag=0`, `GTSyncType=1`: waits for the current frame's RHI boundary; lower latency but more serialization.

### Reliability

- Control-path confidence: high.
- Expected benefit: workload dependent.
- It may outperform `OneFrameThreadLag=0, GTSyncType=0` when RHI lead is the dominant queue.
- It overlaps with DXGI maximum latency, Reflex/Anti-Lag, and `FinishCurrentFrame`.

## Candidate 3: `RHI.MaximumFrameLatency=1`

### Verified mechanism

Storage:

```text
g_nRhiMaxFrameLatency @ 0x14407B088
default = 3
```

Consumer:

```text
FD3D11Viewport_PresentChecked @ 0x141201F80
```

On a changed value, SC6 queries `IDXGIDevice1` and calls:

```text
IDXGIDevice1::SetMaximumFrameLatency(value)
```

The actual Present call is in:

```text
FD3D11Viewport_PresentSwapChain @ 0x141202100
```

### Critical interpretation

The API value is the maximum number of frames the system may queue. It is not the number that is continuously occupied.

```text
stock maximum = 3
new maximum   = 1
upper-bound reduction = 2 frames
actual reduction = max(0, actual_old_occupancy - 1)
```

Examples:

| Old occupied depth | Depth after setting 1 | Possible reduction |
|---:|---:|---:|
| 1 | 1 | 0 frames |
| 2 | 1 | 1 frame |
| 3 | 1 | 2 frames |

A high-end computer locked to 60 FPS may already operate at depth one because another engine fence, driver low-latency mode, or presentation throttle prevents deeper queuing. Such a system receives no guaranteed saving from changing the maximum.

### Reliability and cost

- Correctness risk: low.
- Performance/frame-pacing risk: low-medium when adequate GPU headroom exists, high near saturation.
- Failure mode: Present blocks earlier, exposing GPU spikes and causing missed refreshes.
- It must be evaluated with driver low-latency features both disabled and enabled.

## Candidate 4: exclusive fullscreen

SC6 uses a legacy bitblt swapchain and contains real `IDXGISwapChain::SetFullscreenState` paths in its D3D11 viewport implementation.

Exclusive fullscreen bypasses the ordinary windowed DWM composition route. This can reduce latency, but not by a fixed frame on every Windows/driver/display configuration.

Variables include:

- Windows fullscreen optimizations
- overlays
- display scaling
- driver presentation mode
- whether windowed presentation was already keeping up
- monitor refresh and VRR state

SC6 also has a windowed DWM-throttle path:

```text
FD3D11Viewport_PresentDwmThrottled @ 0x1412021A0
RHI.SyncWithDWM backing storage @ 0x1442AE914
```

The static default of `RHI.SyncWithDWM` is zero, so the explicit `DwmFlush` loop is not necessarily active in a default session. Windowed bitblt output still passes through DWM, but the old claim of one mandatory extra frame was too strong.

Recommendation: include exclusive fullscreen in the baseline test matrix and log actual presentation mode. Do not automatically award one frame.

## Candidate 5: VSync off

Verified path:

```text
rhi.SyncInterval
  -> SC6 thread-aware CVar reader
  -> FD3D11Viewport_PresentSwapChain
  -> IDXGISwapChain::Present(syncInterval, 0)
```

At 60 Hz, VSync-on can delay a completed frame until a vertical blank. `Present(0, 0)` requests unsynchronized DXGI presentation and permits tearing, so its effect depends on completion phase, presentation mode, DWM/driver behavior, and queue state. Static control flow does not prove immediate physical scanout.

Expected effect:

```text
minimum: approximately 0
typical isolated VBlank-wait opportunity: less than one refresh interval
observed end-to-end effect: presentation-path and workload dependent
```

This is not a structural frame that should be converted into network delay. It also changes visual behavior and should remain a user-facing preference.

`DXGI_PRESENT_ALLOW_TEARING` does not eliminate tearing. It legally enables tearing for supported windowed flip-model swapchains. VRR may make the result visually tear-free while inside the VRR range, but that is a display/driver outcome rather than an API guarantee.

## Candidate 6: `r.FinishCurrentFrame=1`

### Verified mechanism

The active D3D11 consumer is in the surrounding viewport present routine at `0x141208970`.

The CVar changes ordering between Present and:

```text
FUN_1412111A0
```

That function polls a D3D11 event query until GPU work completes or a timeout/terminal condition occurs. Enabling `FinishCurrentFrame` therefore serializes the CPU/render path against GPU completion more aggressively.

### Reliability

- Mechanism confidence: very high.
- Fixed saving confidence: low-medium.
- Performance cost: potentially high.

It affects an adjacent boundary to `OneFrameThreadLag`, but that does not make its latency saving automatically additive. Once `GTSyncType=1`, maximum latency 1, or Reflex-like pacing has reduced the same queue, the event-query wait may have little remaining latency to remove.

If the extra wait causes SC6 to miss 60 FPS, total latency and online behavior become worse. Keep it as a diagnostic toggle, not a default feature.

## Candidate 7: `r.GTSyncType=2`

SC6 accepts mode 2 and forwards it through its frame-fence task. However, effective GPU-flip synchronization depends on platform support and valid presentation statistics.

SC6's stock windowed bitblt swapchain does not provide the same windowed flip statistics as a flip-model swapchain. In unsupported circumstances UE falls back or degrades toward mode 1 behavior.

The mode might work differently in exclusive fullscreen, but that has not been dynamically verified in SC6. There is no `rhi.SyncSlackMS` string in the binary, so this fork should not be assumed identical to later documented UE implementations.

Do not assign mode 2 a saving until runtime instrumentation proves:

1. It does not fall back to mode 1.
2. It obtains valid flip timing.
3. Its game-thread start point changes consistently.
4. The change improves input-to-photon latency without missed refreshes.

## Candidate 8: NVIDIA Reflex

### What SC6 provides

SC6 loads `nvapi64.dll` for existing NVIDIA functionality and exposes a valid D3D11 device. The relevant NVAPI query IDs are publicly defined:

```text
NvAPI_D3D_SetSleepMode = 0xAC1CA9E0
NvAPI_D3D_Sleep        = 0x852CD1D2
```

### Why the old proposal is not certified

Calling only `SetSleepMode` and `Sleep` at a guessed frame-top location is insufficient to claim a robust Reflex integration.

Missing work includes:

- correct parameter-structure versioning
- return/status handling
- confirmed sleep placement immediately before the intended input/simulation work
- latency markers and PCL reporting
- device destruction/recreation handling
- support and mode reporting
- interaction with VSync and presentation modes
- NVIDIA Reflex verification utility and checklist

Expected savings are also conditional. Reflex primarily helps when CPU work is queued ahead of a GPU-bound pipeline. On a GPU-light system, or after other queue controls have already collapsed the queue, the marginal benefit can be near zero.

Recommendation: if pursued, start from NVIDIA's supported Streamline Reflex integration route and treat it as an alternative pacing strategy to compare against the generic CVar/DXGI bundle, not an extra `1-2` frames to add on top.

## Candidate 9: AMD Anti-Lag

Driver Anti-Lag can operate on D3D11 titles but is controlled by the user's AMD driver profile. HorseMod cannot assume it is enabled or measure its internal state reliably.

AMD Anti-Lag 2 now exposes an SDK with D3D11 support and places the pacing point immediately before user controls are sampled. A future integration should use the official SDK and its support checks rather than attempt to emulate driver behavior.

As with Reflex, benefit is expected primarily in GPU-bound/queued conditions and overlaps with other CPU-ahead controls.

## Candidate 10: flip-model conversion

### Status: old patch invalid

The archived investigation proposed swapchain flags `0x902` and described them as:

```text
NONPREROTATED | FRAME_LATENCY_WAITABLE_OBJECT | ALLOW_TEARING
```

That interpretation is wrong.

Relevant values are:

```text
DXGI_SWAP_CHAIN_FLAG_NONPREROTATED                 = 0x1
DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH             = 0x2
DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT = 0x40
DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO              = 0x100
DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING                 = 0x800
```

Therefore `0x902` is `ALLOW_MODE_SWITCH | FULLSCREEN_VIDEO | ALLOW_TEARING`; it does **not** create a frame-latency waitable object. SC6's stock descriptor uses one buffer, discard swap effect, and flags `0x2` (`ALLOW_MODE_SWITCH`) in `FD3D11Viewport_Init @ 0x1411F4F70`.

### Other missing requirements

A correct D3D11 flip-model implementation must account for:

1. At least two buffers.
2. Supported flip-model format and no direct swapchain MSAA.
3. Explicitly rebinding the D3D11 backbuffer after Present.
4. Calling `ResizeBuffers` appropriately after fullscreen changes.
5. Querying `DXGI_FEATURE_PRESENT_ALLOW_TEARING` before requesting tearing support.
6. Maintaining compatible flags across resize/recreation.
7. Device-lost handling.
8. Overlay/custom-present compatibility.
9. Windowed, borderless, fullscreen, resize, Alt-Tab, HDR, multi-monitor, and capture tests.

The previous byte-patch approach changed descriptor fields without proving these surrounding invariants. It must not be used.

### Correct future direction

Prefer an explicit swapchain-creation interception that constructs a valid `DXGI_SWAP_CHAIN_DESC1` through `IDXGIFactory2::CreateSwapChainForHwnd`, then adapts SC6's viewport lifecycle around the resulting `IDXGISwapChain1/2`. This is more work but gives controlled feature checks and error handling.

Flip model by itself does not guarantee a frame. Its strongest benefit occurs when Windows engages DirectFlip/Independent Flip or when it enables a correctly implemented waitable-object pacing path. PresentMon must confirm the actual presentation mode.

## Candidate 11: frame-latency waitable object

Creating the swapchain with the waitable flag is only the first step. A correct implementation must:

1. Query `IDXGISwapChain2`.
2. Call `IDXGISwapChain2::SetMaximumFrameLatency`, not only the device-level method.
3. Retrieve `GetFrameLatencyWaitableObject()`.
4. Wait on the handle **before** beginning the next frame's input/simulation/render work.
5. Wait before the first rendered frame as required by the API.
6. Close/recreate the handle with the swapchain.
7. Avoid unsupported D3D11 exclusive-fullscreen use.

The ideal wait location would be immediately before input sampling, after confirming that blocking there cannot interfere with engine message pumping or online packet receive processing.

No such complete path currently exists in HorseMod. Do not count any saving from merely setting a descriptor flag.

## Candidate 12: disable the RHI execution thread

SC6 exposes a real runtime mode command:

```text
r.RHIThread.Enable 0 = off
r.RHIThread.Enable 1 = dedicated RHI thread
r.RHIThread.Enable 2 = task threads
```

The command handler parses the integer and calls:

```text
SetRHIThreadMode @ 0x1415EEEF0
```

The setter is not a cosmetic CVar write. It drains/stops the existing RHI execution state, updates mutually exclusive dedicated/task-thread mode flags, and restarts the rendering infrastructure. `(false, false)` is the coherent off mode. `FRHICommandListExecutor::ExecuteInner`-equivalent logic at `0x1415DB1E0` dispatches command lists only when RHI-thread execution is active; otherwise it executes the command stream without that separate queue.

This can remove RT-to-RHI lead rather than merely waiting for it at the game-thread fence. The tradeoff is severe: RHI work moves onto the render path, CPU/RHI overlap disappears, and a missed 16.67 ms deadline can cost more latency than the removed queue. It also substantially overlaps with `r.GTSyncType=1`, whose purpose is to retain the RHI thread while preventing the game thread from outrunning its completion boundary.

Recommended use:

- Treat `r.RHIThread.Enable 0` as a diagnostic comparison and possible low-end/CPU-headroom-specific mode.
- Compare it against `GTSyncType=1`, not on top of an assumed saving from `GTSyncType=1`.
- Prefer the supported mode command over writing the underlying mode bytes.
- `r.RHICmdUseThread=0` reaches a narrower dispatch branch, but it does not perform the complete stop/restart lifecycle and is therefore not the preferred user-facing control.

Related switches were rejected as normal latency features:

- `r.RHICmdForceRHIFlush=1` waits after every task sent to the RHI thread. Its own help marks it as issue diagnosis, and the consumer at `0x1415DB1E0` performs immediate task waits. It can destroy batching and throughput.
- `r.RHICmdFlushRenderThreadTasks=1` and its BasePass/PrePass/ShadowPass/TranslucentPass/VelocityPass variants wait for parallel render tasks inside individual passes. They serialize work within a frame but do not establish a later input sample or a distinct final-frame queue limit.
- `r.RHICmdUseDeferredContexts=0` disables a D3D11 parallel command-translation path. No independently buffered completed frame was found behind this control; reducing concurrency may instead lengthen the render critical path.

## Candidate 13: battle-only late XInput resampling

SC6 imports `XInputGetState` at `0x1429ED13C`, and it has exactly one native caller:

```text
LuxApp_PollAllPadsAndDispatchInputEvents @ 0x140E1EC00
  <- ProcessWindowsGameDeviceState @ 0x140E09B70
```

The poller samples each connected XInput slot, maps the result into 24 UE button states, synchronously dispatches changed button/analog events, updates repeat deadlines, sends rumble, and handles connection notifications. `FEngineLoop_Tick` calls this platform-input path before Slate preprocessing and the world tick. Later in the same engine frame, `FrameInput` reads the Lux processor bitfields before BattleManager simulation.

That ordering contains no hidden queued frame, but it does leave an ordinary sampling window:

```text
normal XInputGetState poll
  -> Slate preprocessing / intervening engine work
  -> FrameInput reads Lux bitfield
```

A controller transition that occurs inside that window is not visible until the next engine poll. A second, battle-specific sample immediately before Lux edge calculation or `FrameInput` publication could admit that transition into the current simulation tick. The maximum possible gain is less than one 60 Hz tick, with an average gain dependent on where the original poll and late sample fall within the frame.

Do not call `LuxApp_PollAllPadsAndDispatchInputEvents` a second time unchanged. That would dispatch ordinary UI/controller callbacks during the world tick, advance held-button repeat scheduling twice, submit force feedback twice, and process connection changes at a re-entrant point. A viable implementation needs a side-effect-bounded path that:

1. Polls only the battle controller's XInput slot.
2. Reuses the verified button/stick thresholds and Lux logical-slot ownership mapping.
3. Publishes one current battle snapshot immediately before edge calculation/input-cache refresh.
4. Preserves press/release edges exactly once and does not double-dispatch Slate events.
5. Leaves keyboard, DirectInput, menu, replay, and non-battle input on the stock path.

This is a fractional-frame responsiveness improvement, not a source of a whole frame that can be safely reinvested into online `InputLagOffset`.

## Other newly inspected controls that do not remove a hidden SC6 frame

- `t.MaxFPS` is registered with a default of `0.0` (uncapped) in `CVar_Register_t_MaxFPS @ 0x140270A40`; its cached float is read by `UEngine_GetMaxTickRate @ 0x142177DC0`. The executable contains no setter that gives it a nonzero cap. An external configuration can still set it, but the stock binary does not reveal a second 60 FPS sleep queue to remove here.
- `bSmoothFrameRate` and `bEnableMouseSmoothing` occur in UE configuration/property metadata. Mouse smoothing changes mouse-axis processing, not XInput digital-button sampling, and neither string identifies an extra battle-input frame buffer.
- `bDisableLowLatencyUpdate` is reflected on the motion-controller component class alongside `CurrentTrackingStatus`, `Hand`, `PlayerIndex`, and `IsTracked`. It is the VR motion-controller late-update feature, not SC6's ordinary XInput latency control.
- `r.Vulkan.WaitForIdleOnSubmit` is explicitly Vulkan-only. The verified SC6 presentation path under investigation is D3D11.
- No `rhi.SyncSlackMS` string exists in this build, so later UE low-latency-pacing recipes that depend on that CVar cannot be transplanted by name.

## Online recovery-only candidate: remove the pause-resume bubble

`ProcessFrameInputSyncPauseGateAndCharaAudioState @ 0x1403E9EE0` captures whether `PauseTime` was nonzero at entry, processes the new event, and returns:

```text
consumedOnlineStateEvent == 1 || entryPauseTime != 0
```

On the event-zero recovery path, character/audio hold state and `PauseTime` are cleared only if the BattleManager and target character resolve successfully.

On a successful event-zero recovery transition where those objects resolve:

1. Enough synchronized data is available.
2. If the BattleManager and target character resolve, the function clears character/audio hold state.
3. Under that same resolution precondition, it clears `PauseTime`.
4. It still returns `true` because `entryPauseTime` was nonzero.
5. `GameTime` remains blocked for one additional actor tick.
6. Simulation resumes on the next tick.

Overriding the return to false when the post-call state is unpaused would remove exactly one tick after qualifying network stalls.

This is not a steady-state frame and cannot be reinvested as input delay. It may be deliberate hysteresis. The event-zero condition already requires synchronized lookahead, which makes the experiment plausible, but it needs:

- packet loss tests
- burst loss tests
- packet reordering tests
- jitter tests
- repeated pause/resume transitions
- round transitions and rematches
- state-hash comparison between both peers
- monitoring for immediate re-stalls

Ship only behind an experimental toggle until certified.

## Netcode resilience improvement: increase sent input history

This does not save latency, but it can improve network quality without adding input delay.

`SendFrameInputSyncCachedRangePacket @ 0x1403F8710` sends a cached row range. Stock `InputSizeSend=4`; the packet decoder derives row count from packet size rather than assuming four rows.

Approximate packet payload sizes:

```text
4 rows  -> 18 bytes
8 rows  -> 26 bytes
16 rows -> 42 bytes
```

Increasing the history to 8 or 16 rows gives later packets more ability to repair isolated or burst packet loss. The size remains far below ordinary MTU limits.

Required validation:

- both peers using the mod
- modded versus stock peer behavior
- maximum accepted decoder count
- receive cache wrapping
- packet capture verifying exact wire size
- burst loss recovery time
- bandwidth at 60 packets/second

This should be considered independently of the frame-saving work.

## Additivity and overlap

The principal latency controls apply to a sequence of related queues:

```text
Game thread
  -> render thread
  -> RHI thread
  -> driver/GPU work queue
  -> DXGI present queue
  -> DWM / scanout
```

Approximate control boundaries:

| Control | Primary boundary |
|---|---|
| `r.OneFrameThreadLag` | GT versus RT frame fence |
| `r.GTSyncType=1` | GT fence completion extended through RHI |
| `r.GTSyncType=2` | GT start coordinated with presentation timing |
| `r.FinishCurrentFrame` | RT/CPU waits on GPU event completion |
| DXGI maximum latency | Driver/present queue maximum |
| Reflex / Anti-Lag | CPU frame start paced against GPU demand |
| Waitable swapchain | Frame start paced against presentation availability |
| `r.RHIThread.Enable 0` | Removes the separate RT-to-RHI execution queue and its overlap |
| Late XInput resample | Moves the controller sampling boundary closer to battle input publication |
| Exclusive fullscreen / flip model | OS composition and scanout path |
| VSync | Earliest permitted scanout/present synchronization point |

Because downstream backpressure propagates upstream, constraining one queue can empty another. Individual maximum reductions must never be summed without measuring the complete stack.

Examples:

- If maximum latency 1 already blocks Present early, Reflex may have little queue left to remove.
- If `GTSyncType=1` holds the game thread near RHI progress, `FinishCurrentFrame` may be largely redundant.
- If `OneFrameThreadLag=0` waits on the current RHI-targeted fence, the driver queue may already stay shallow.
- If a driver low-latency feature already limits CPU lead, changing DXGI's maximum from 3 may show no effect.

## Recommended experimental configurations

Do not jump directly from stock to an all-options bundle. Measure marginal and combined effects.

### Baseline

```text
OneFrameThreadLag = 1
GTSyncType = 0
MaximumFrameLatency = 3
FinishCurrentFrame = 0
driver low-latency features disabled
```

Record separate baselines for:

- exclusive fullscreen, VSync on
- exclusive fullscreen, VSync off
- borderless/windowed

### Generic low-latency candidates

Test in this order:

1. Maximum latency 1 only.
2. `GTSyncType=1` only.
3. `OneFrameThreadLag=0` only.
4. `OneFrameThreadLag=1` + `GTSyncType=1` + maximum latency 1.
5. `OneFrameThreadLag=0` + `GTSyncType=1` + maximum latency 1.
6. Add `FinishCurrentFrame=1` only as a diagnostic comparison.

Additional diagnostic comparisons:

7. `r.RHIThread.Enable 0` versus `GTSyncType=1`; do not assume their savings add.
8. A side-effect-bounded late-XInput prototype versus the stock single poll; treat any result as fractional-frame sampling improvement.

### Vendor pacing

Test separately from the generic aggressive bundle:

- NVIDIA Reflex integration versus stock queue controls.
- AMD Anti-Lag/Anti-Lag 2 versus stock queue controls.
- Then compare each with maximum latency 1 to quantify overlap.

## Runtime instrumentation requirements

Internal timestamps cannot independently certify input-to-photon latency, but they can identify where queue lead changes.

Log once per game frame:

```text
QPC timestamp
engine frame number
input sample timestamp / input transition id
FrameInput actor entry/exit
FrameInputSync receive-drain entry/exit
UpdateTime
GameTime
SendTime
SyncTime
InputLag
PauseTime
BattleManager simulation entry/exit
render submission boundary
RHI submission boundary where observable
Present entry/exit
Present result and sync interval
current mode/settings
```

Derived online metrics:

```text
local lead       = SendTime - GameTime
remote lead      = SyncTime - GameTime
buffer margin    = min(local lead, remote lead)
stall duration   = consecutive ticks with zero simulation advance
recovery bubble  = PauseTime cleared but GameTime still blocked
```

Also record:

- CPU, render-thread, RHI, and GPU frame times
- missed 16.67 ms deadlines
- present mode from PresentMon
- queue depth/latency data where the driver exposes it
- GPU utilization and clocks

## Input-to-photon certification protocol

Use a high-speed camera or a hardware latency device. Present timestamps alone do not include scanout position and panel response.

### Test stimulus

Use an electrical/button LED or another visible input-transition reference synchronized with a deterministic, high-contrast on-screen response. A gameplay move can be used only after proving that its animation response begins on a deterministic simulation frame.

### Sample size

For each configuration and scene:

- at least 200 input transitions for exploratory median/p95 comparisons
- thousands of transitions, or a justified confidence-interval method, before reporting or making decisions from p99
- randomized timing relative to the 60 Hz frame boundary
- warm system/driver state
- identical camera, stage, characters, resolution, and graphics options

### Workloads

Test:

1. Quiet training-stage state.
2. Effects-heavy gameplay.
3. CPU-heavy replay/character interactions.
4. Real online match.
5. Artificial latency without loss.
6. Artificial jitter and burst loss.

### Report

For each configuration report only the statistics supported by its sample count:

```text
median input-to-photon
p95
p99 (certification-sized sample only)
minimum / maximum
standard deviation
missed-refresh rate
average and p99 frame time
GPU utilization
observed presentation mode
```

A change is not certified by its minimum or best sample. Reinvestment decisions should use a conservative lower bound on repeatable savings under the worst supported workload.

## Rule for reinvesting frames online

Only increase `InputLagOffset` by one when all of the following are true:

1. The selected low-latency configuration saves at least one full 60 Hz frame (`16.67 ms`) consistently enough under supported conditions.
2. The p95/p99 frame-time behavior does not cause additional missed refreshes.
3. The saving remains present during online play.
4. Driver low-latency settings do not make the saving disappear without HorseMod detecting or accounting for that configuration.
5. The setting remains effective across fullscreen/windowed transitions and device recreation.
6. Both local input-to-photon and remote packet timing remain stable.
7. The effective `InputLag` field actually increases by one after its one-step convergence and is not prevented by `InputLagMax` clamping.
8. Both peers use the intended offset when claiming symmetric additional network margin.

If one frame is certified:

```text
InputLagOffset: 2 -> 3
```

The target rises by exactly one frame unless clamped by `InputLagMax`, but the effective `InputLag` changes by at most one per update. `InputLag` is not serialized in opcode-1 input packets, so a modded/stock pairing remains wire-format compatible; however, only the configured peer gains and pays for the additional send-ahead. Do not describe a mixed-peer result as symmetric reinvestment.

Then compare:

```text
stock renderer + offset 2
low-latency renderer + offset 2
low-latency renderer + offset 3
```

The third configuration succeeds only if it approximately restores stock total responsiveness while reducing stalls or increasing remote-input margin.

Do not dynamically spend a saving based only on an instantaneous frame-time estimate. Delay changes affect synchronized frame indices and should use the game's existing controlled adaptive transition behavior. Prefer fast increases during deteriorating network conditions and slow decreases after sustained stability.

## Recommended implementation order

1. Build telemetry and latency-toggle infrastructure.
2. Certify `RHI.MaximumFrameLatency=1` alone.
3. Certify `r.GTSyncType=1` alone and with maximum latency 1.
4. Certify `r.OneFrameThreadLag=0` alone and in the combined bundle.
5. Establish fullscreen/VSync presentation baselines.
6. Decide whether one repeatable frame exists to reinvest.
7. Increase sent input history to 8 or 16 and validate interoperability.
8. Test a one-frame `InputLagOffset` reinvestment.
9. Test the pause-recovery bubble override separately.
10. Evaluate official NVIDIA Reflex and AMD Anti-Lag 2 integrations as alternative pacing modes.
11. Revisit flip model only as a complete swapchain-lifecycle project, not a descriptor byte patch.
12. Prototype battle-only late XInput sampling only after the renderer queue work, with strict edge/ownership invariants.

## Current decisions

### Pursue

- Runtime latency telemetry
- High-speed input-to-photon A/B measurement
- `RHI.MaximumFrameLatency=1`
- `r.GTSyncType=1`
- `r.OneFrameThreadLag=0`
- Fullscreen/VSync mode matrix
- Input history 8/16
- Optional pause-recovery experiment
- `r.RHIThread.Enable 0` as a diagnostic alternative configuration
- Side-effect-bounded battle-only late XInput sampling as a future experiment

### Do not ship by default yet

- `r.FinishCurrentFrame=1`
- `r.GTSyncType=2`
- `r.RHICmdForceRHIFlush=1`
- `r.RHICmdFlushRenderThreadTasks=1` and per-pass variants
- `r.RHICmdUseDeferredContexts=0`
- raw NVAPI Reflex hook
- automatic online delay reinvestment

### Reject in current form

- old `0x902` flip-model/waitable-object patch
- claim that maximum latency 3 always contributes three frames
- claim that every queue-control saving stacks additively
- claim that `ALLOW_TEARING` is inherently tear-free
- 8WAYRUN fake-frame NOP
- `r.RHICmdBypass` latency proposal
- bypassing Lux input/simulation functions

## Authoritative external references

- Microsoft: [IDXGIDevice1::SetMaximumFrameLatency](https://learn.microsoft.com/windows/win32/api/dxgi/nf-dxgi-idxgidevice1-setmaximumframelatency)
- Microsoft: [For best performance, use DXGI flip model](https://learn.microsoft.com/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)
- Microsoft: [DXGI swapchain flags](https://learn.microsoft.com/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_chain_flag)
- Microsoft: [DXGI Present flags](https://learn.microsoft.com/windows/win32/direct3ddxgi/dxgi-present)
- Microsoft: [IDXGISwapChain2::GetFrameLatencyWaitableObject](https://learn.microsoft.com/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)
- Epic: [Low-Latency Frame Syncing](https://dev.epicgames.com/documentation/unreal-engine/low-latency-frame-syncing-in-unreal-engine)
- NVIDIA: [Streamline Reflex integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideReflex.md)
- NVIDIA: [NVAPI public interface table](https://github.com/NVIDIA/nvapi/blob/main/nvapi_interface.h)
- AMD: [Anti-Lag 2 SDK](https://gpuopen.com/anti-lag-2/)

## Related local evidence

- `E:\DevShitPosts\SC6Mods\SC6ModdingDocs\docs\sc6\input-system.md`
- `E:\DevShitPosts\SC6Mods\SC6ModdingDocs\docs\sc6\structures.md`
- Ghidra program: `SoulcaliburVI.exe`
- Analyzed executable size: `71,737,344` bytes
- Analyzed executable SHA-256: `F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`
- Archived prior notes: `C:\Users\prest\Desktop\investigations\RemoveDelay_old.md`

## Final position

The project has several credible mechanisms for reducing local rendering latency, but static analysis proves queue limits and synchronization boundaries—not actual occupied latency. No frame should be added to the online buffer until a complete configuration demonstrates a repeatable end-to-end saving.

The most likely productive route is:

```text
MaximumFrameLatency=1
+ GTSyncType=1
+ measured choice of OneFrameThreadLag 0 or 1
+ appropriate fullscreen/VSync mode
```

If this bundle certifies one full frame, that frame can be reinvested into `InputLagOffset` while separately improving packet-loss resilience through a larger transmitted input history. That is the first realistic path toward better online robustness without making controls feel slower than stock.
