# HorseMod

A SoulCalibur VI training mod that visualises hitboxes, hurtboxes, and body
volumes in real time, plus an in-game ImGui overlay with controls for camera,
freeze-frame / slow-motion, and a custom reset-position override.

## Features

- **Hitbox / hurtbox / body overlay** — per-player toggles, separate
  Foreground (always-on-top) and Persistent (trail) renderers.
- **Free-fly camera (F7)** — WASD + mouse / right-stick + triggers, with
  configurable move/look speed and FOV.
- **Camera lock** — freezes the battle camera in place by patching the
  engine's per-tick POV-commit instructions.
- **Speed control** — slow-motion / freeze-frame with a slider, plus
  single-frame step (F6).
- **Reset-position override (Training)** — capture both characters'
  current positions, then have the game's normal reset bind teleport
  them back to the captured pose instead of the round-spawn.
- **Hide weapons / Hide characters** — flicker-free visibility patches
  for capturing clean hitbox shots.
- **In-game ImGui overlay** — F2 (keyboard) or Back / Select (gamepad)
  toggles the overlay; D-pad / arrow-keys + L1/R1 to navigate.

## Requirements

- SoulCalibur VI on Steam
- One of:
  - [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) installed manually in
    the game's `Binaries/Win64` directory (manual install path), **or**
  - [unreal-shimloader](https://thunderstore.io/c/soulcalibur-vi/p/Thunderstore/unreal_shimloader/),
    which bundles UE4SS and is pulled in automatically when you install
    HorseMod via a Thunderstore mod manager.

## Installation

### Mod Manager (Thunderstore) — recommended

Install HorseMod from the SoulCalibur VI community page using a
Thunderstore-compatible mod manager (e.g. r2modman, Thunderstore Mod
Manager, or Gale).  The manager will pull in `unreal-shimloader` as a
dependency, which provides UE4SS and redirects the mod's files into the
game at launch — no manual file copying required.

### Manual

1. Install [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) into the game's
   `Binaries/Win64` directory.
2. Extract the zip so that the `HorseLab` folder lands inside
   `<Steam>/steamapps/common/SoulcaliburVI/SoulcaliburVI/Binaries/Win64/ue4ss/Mods/`.
   The final layout should be:

   ```
   <game>/Binaries/Win64/ue4ss/Mods/HorseLab/
     ├── enabled.txt
     └── dlls/
         └── main.dll
   ```

   (Note: the Thunderstore zip ships its payload under `shimloader/mod/HorseLab/`
   so unreal-shimloader can redirect it.  If you're installing manually
   without shimloader, grab the GitHub release zip instead, or copy the
   contents of `shimloader/mod/HorseLab/` from the Thunderstore zip into
   the path above.)

3. Launch SoulCalibur VI.  The mod loads automatically when UE4SS starts.
4. Press **F2** (keyboard) or **Back / Select** (gamepad) to open the
   overlay.

## Quick start

- **F2** — show / hide the overlay
- **F5** — toggle the hitbox overlay
- **F6** — single-frame step (auto-enables Freeze-frame on first press)
- **F7** — toggle free-fly camera

## Disabling the mod

Rename `enabled.txt` to `enabled.txt.DISABLED` (or delete it) inside the
`HorseLab` folder.  UE4SS will skip loading the mod on the next launch.

## Credits

- Several engine-level features (visibility patches, speed control, VFX
  off) port the cheat-engine work originally done by
  [@somberness](https://x.com/somberness) in the `SC6nepafu.CT` table.
- Built on top of [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS),
  [Dear ImGui](https://github.com/ocornut/imgui), and
  [PolyHook 2](https://github.com/stevemk14ebr/PolyHook_2_0).

## AI disclosure

AI tools were used in the creation of this mod.
