# Cog Debug Tooling Evaluation - 2026-06-28

## Verdict

Cog is valuable as reference material, not as a dependency to vendor into
HorseMod.

The best long-term value is to mine Cog for UI/tooling patterns that fit the
existing injected-mod architecture:

- command palette / console command registry for HorseMod actions,
- persistent named ImGui layouts,
- category-filtered diagnostic log and debug-draw controls,
- small plot/event timeline widgets for replay and KHit diagnostics,
- lightweight notifications/status to replace transient log hunting.

Do not port Cog wholesale. Its integration model assumes a source Unreal
project, Unreal Build Tool modules, UWorldSubsystem lifecycle, Slate viewport
widgets, and UE5-era engine/plugin APIs. HorseMod runs inside SC6 as a UE4SS
C++ mod with its own DXGI Present, WndProc, and XInput hooks. Replacing that
runtime surface would create more risk than value.

## Evidence From Cog

- Repository: <https://github.com/arnaud-jamin/Cog>
- License: MIT.
- Latest shallow-cloned commit during this investigation:
  `cb1b435f3bb5aca41ff559863927a151b80537ac`
  (`2026-04-11 23:47:14 -0400`, "Merge pull request #74 from akambojTC/fix-skeleton-context-menu").
- GitHub metadata checked 2026-06-28: 597 stars, 86 forks, 4 open issues,
  default branch `main`, pushed at `2026-04-12T03:47:14Z`, MIT license.
- Sample project targets Unreal `5.5` (`Cog.uproject` `EngineAssociation`).
- README describes Cog as Unreal debug tools built on Dear ImGui, with windows
  for core engine, Enhanced Input, Gameplay Ability System, and AI; persistent
  layouts; debug log/draw APIs; server-side debug control; and NetImgui.
- Integration guide says Cog now integrates through a project `UWorldSubsystem`
  and is stripped/disabled for shipping-style builds except `CogCommon`.
- Module dependencies include `Core`, `CoreUObject`, `Engine`, `Slate`,
  `SlateCore`, `ApplicationCore`, `RenderCore`, `InputCore`, `NetCore`,
  plus optional `GameplayAbilities`, `GameplayTags`, `EnhancedInput`,
  `AIModule`, and `CommonUI`.
- `CogImgui` uses `UGameViewportClient::AddViewportWidgetContent` and Slate
  widgets for ImGui input/render integration.
- `UCogSubsystem` owns windows, settings, layouts, shortcuts, menu rendering,
  and Cog console commands.
- `FCogDebug` / `FCogDebugTracker` provide the interesting portable ideas:
  value plots, event tracks, debug categories, selection-filtered visibility,
  and a debug-shape abstraction.

## Evidence From HorseMod

- HorseMod already has an in-process ImGui integration in
  `HorseMod/horselib/GameImGui/` using:
  - DXGI swapchain Present hook,
  - WndProc hook,
  - XInput gate,
  - callback registration via `Horse::GameImGui::register_tab`.
- `HorseMod/CMakeLists.txt` deliberately delay-loads `d3d11.dll`, `dxgi.dll`,
  `d3dcompiler_47.dll`, and `xinput1_4.dll` because ordinary import loading
  broke Steam Input behavior when the overlay was disabled.
- `HorseMod/dllmain.cpp` already has first-class domain windows:
  Hitboxes, Camera, Time, Labbing, Replay, and General.
- HorseMod already draws domain geometry through `LineBatcherBackend`, not
  stock `DrawDebug*` helpers. That backend knows SC6's `ULineBatchComponent`
  offsets and has explicit persistent-trail lifecycle management for freeze /
  frame-step behavior.
- Replay work already has a custom timeline scrub UI and strict replay seek
  tests. Cog's timeline/plot ideas may improve observability, but should not
  replace the existing replay implementation.

## Fit Analysis

### Good Fits

1. Command palette / console:
   A Cog-style command registry could wrap existing HorseMod actions:
   toggle hitboxes, generate replay timeline, seek playhead, copy pose, reset
   layout, dump diagnostics, and jump tabs. This would reduce hotkey sprawl.

2. Named layouts:
   Cog's layout save/load model maps well to labbing workflows:
   "KHit audit", "Replay scrub", "Camera setup", "Online-safe minimal".
   Implement using Dear ImGui ini save/load or HorseMod's existing settings
   file, not Unreal config objects.

3. Diagnostic categories:
   Cog's category-filtered logging/debug-draw model would be useful for
   ReplayScrub, KHit audit, stage boundary, RNG, input history, and native
   replay trace. It would give runtime control without recompiling or
   opening the UE4SS log constantly.

4. Plots and event tracks:
   Cog's `Plot`, `StartEvent`, and `StopEvent` API shape is a good reference
   for displaying replay tick rate, seek queue/land latency, timeline
   generation speed, input-log cursor drift, and hitbox active windows.
   A HorseMod-native ring buffer is enough; no need to import Cog's UE
   tracker types.

5. Notifications:
   Lightweight in-overlay notifications would help surface failures such as
   "native seek pending", "timeline stale", "online-safe gate active", or
   "GameImGui disabled by kill switch" without burying them in logs.

### Poor Fits

1. Whole-plugin vendoring:
   Cog's UBT module graph and UE5 subsystem model do not match a UE4SS C++
   injected mod in SC6's shipped executable.

2. Rendering/input layer:
   Cog's Slate viewport integration is not a replacement for HorseMod's
   Present/WndProc/XInput hooks. HorseMod's current layer carries hard-earned
   Steam overlay and Steam Input compatibility constraints.

3. Actor selection/inspector as-is:
   Cog's inspector assumes comfortable access to Unreal reflection and source
   project object lifecycles. A HorseMod inspector would need to be built
   around UE4SS object wrappers, SC6 dump offsets, and SEH-safe reads.

4. GameplayAbility / EnhancedInput / AI windows:
   These are useful in Cog's sample ecosystem, but low-value for SC6 unless a
   specific SC6 subsystem investigation needs them.

5. Server replication / NetImgui:
   SC6 online safety and mod parity constraints make Cog's server-control
   model a bad default. NetImgui could become interesting for headless tooling,
   but it is not a near-term fit.

## Recommended Adoption Path

1. Add a tiny HorseMod command registry.
   Keep it header-only and independent of Unreal console APIs. Commands should
   be callable from ImGui buttons, hotkeys, and eventually a command palette.

2. Add named ImGui layouts.
   Start with 4 slots mirroring Cog's model: save, load, reset. Persist next
   to existing HorseMod settings.

3. Add a diagnostic hub tab or modal.
   Expose log/category toggles and live counters for KHit audit, ReplayScrub,
   NativeReplayTraceHook, RNG, input history, and GameImGui.

4. Add a minimal plot/event ring.
   First plots:
   - replay resume tick rate,
   - native seek queue-to-land latency,
   - timeline generation frames/sec,
   - input-log cursor / master-clock delta.

5. Revisit object inspection only after the above is stable.
   Treat it as a separate reverse-engineering project, not a Cog port.

## Bottom Line

Cog can bring value by showing what a mature Unreal debug cockpit feels like.
The right move is to copy the product ideas and some API shapes, then implement
them natively inside HorseMod's existing GameImGui and line-batcher architecture.
The wrong move is to import Cog's plugin stack or replace HorseMod's overlay
hooks.
