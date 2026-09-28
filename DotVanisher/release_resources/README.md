# DotVanisher

DotVanisher adds bounded spectator connection recovery and host battle-end grace to Soulcalibur VI.

## What's new in 1.1.0

- Adds recovery for a successful watch assignment that arrives before the assigned player's connection route is available locally.
- Preserves the original request deadline and acknowledgment handling. Duplicate pending assignments do not restart the deadline.
- Handles cancellation, session changes and repeated attempts so an old assignment cannot be applied to a different tracked connection.
- Adds executable and hook compatibility checks for the new recovery paths.
- Keeps the existing 90-second host battle-end grace as a separate feature.

The new recovery runs on the machine receiving the watch assignment. A room host who is spectating can be that machine; installing on the host does not modify remote clients.

## Validation and limits

Version 1.1.0 has passed compilation, offline tests with simulated native callbacks, and checks against the supported executable. **It has not been live-tested in an online match.**

This targets a specific connection-ordering failure. It does not fix every loading-dots problem, extend initial connection-request timeouts, or repair missing loading data. Recovery requires the original connection-owning thread to continue running.

The older host timer applies to watcher cleanup after a battle ends. Earlier descriptions presenting that timer as general slow-loading protection were too broad.

## Requirements

- Soulcalibur VI on Steam, using the executable supported by the mod's compatibility checks.
- UE4SS, either installed manually or through the included unreal-shimloader dependency.
- No HorseMod dependency, settings UI or ImGui overlay.

## Installation

### Mod manager

Install through a Thunderstore-compatible mod manager for Soulcalibur VI. The package declares unreal-shimloader as a dependency and uses its mod layout.

### Manual

With UE4SS already installed, place the mod files at:

```text
<game>/Binaries/Win64/ue4ss/Mods/DotVanisher/
  enabled.txt
  dlls/
    main.dll
```

## Updating or disabling

Close the game before updating. Restart the game after disabling or removing DotVanisher: its native hooks remain active until the process exits, including when UE4SS removes the mod object.

Unsupported executable builds or conflicting recovery hooks disable the new recovery behavior. Check the UE4SS log for messages beginning with `[DotVanisher]`.

## Credits

Built on UE4SS and PolyHook 2, with reverse-engineering work from the HorseMod project.

## AI disclosure

AI tools were used in the creation of this mod.
