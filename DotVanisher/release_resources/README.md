# DotVanisher

DotVanisher is a small SoulCalibur VI UE4SS C++ mod that reduces false
spectator disconnects while a match is loading.

When the host has pending spectators, the vanilla game can time out the
watch queue and force spectators out even though the two players continue
into the match normally. This happens more often with slow storage or newer,
heavier stages. DotVanisher gives the host watch queue a bounded 90-second
grace window before the vanilla timeout cleanup is allowed to run.

## Features

- Softens the host spectator watch timeout during slow match loads.
- Leaves normal player match flow untouched.
- Leaves explicit spectator leave/cancel/end handling untouched.
- No ImGui, no settings UI, no HorseMod dependency.

## Requirements

- SoulCalibur VI on Steam.
- One of:
  - UE4SS installed manually in the game's `Binaries/Win64` directory, or
  - unreal-shimloader, which bundles UE4SS and is pulled in automatically
    when installing via a Thunderstore-compatible mod manager.

## Installation

### Mod Manager

Install DotVanisher from the SoulCalibur VI Thunderstore community page using
a compatible mod manager. The manager installs the unreal-shimloader
dependency and routes the mod files for UE4SS automatically.

### Manual

Manual users should place the mod at:

```
<game>/Binaries/Win64/ue4ss/Mods/DotVanisher/
  enabled.txt
  dlls/
    main.dll
```

## Notes

DotVanisher is intentionally narrow. It only detours the host watch tick
timeout path and only offsets the pending-watch timeout timer while spectators
are waiting, for up to 90 seconds per pending-watch epoch.

## Credits

Built on UE4SS and PolyHook 2. Reverse-engineering work came from the SC6
HorseMod investigation into the host spectator timeout path.

## AI disclosure

AI tools were used in the creation of this mod.
