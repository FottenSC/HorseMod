# GhidraCalibur

Browse-only Visual Studio workspace for Soulcalibur VI decompiler output.

Ghidra's C/C++ exporter writes raw files into `exported/`. The browse generator splits that raw output into `browse/` and writes search indexes into `index/`, then `GhidraCalibur.sln` opens those smaller files in Visual Studio for search, navigation, bookmarks, and side-by-side reading with `HorseMod`.

The most useful generated views are:

- `browse/classes/` - conservative class-centric files such as `ALuxBattlePauseController.cpp`.
- `browse/named/` - broad system buckets such as Replay, Battle, Input, Move, UI, Online, and UE.
- `browse/address_ranges/` - fallback chunks for generic or unknown functions.

Generated function blocks include a compact header with the current Ghidra name, inferred class when available, category, signature, address, and raw export lines.

The project is not meant to compile. Decompiled output from Ghidra is analysis text, not source code.

## Export

Run:

```powershell
E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1
```

For a faster pass that only rebuilds the browse tree from an existing export:

```powershell
E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1 -SkipExport
```

By default, a successful full refresh deletes the large raw CppExporter files after `browse/` and `index/` are built. Keep them only when you want fast local browse-tree experiments without rerunning Ghidra:

```powershell
E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1 -KeepRawExport
```

Open:

```text
E:\myMods\ghidraCalibur\GhidraCalibur.sln
```

The Visual Studio project includes `browse/**/*.cpp` with source files excluded from build. It intentionally does not include the giant raw `exported/sc6_decompiled.cpp` and `.h` files.

## Regeneration

Re-export whenever Ghidra names, prototypes, types, or comments improve. Treat `exported/`, `browse/`, and `index/` as generated output.

The normal successful refresh keeps `exported/functions.csv` for audit/indexing, but removes:

```text
exported/sc6_decompiled.cpp
exported/sc6_decompiled.h
exported/*.log
```

Do not hand-edit generated files unless the edit is temporary scratch work.
