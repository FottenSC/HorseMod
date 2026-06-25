# CppExporter Settings

Record the settings used for each manual export here.

## Current Export

- Date: 2026-06-25
- Ghidra install found locally: `E:\DevShitPosts\SC6Mods\Ghidra`
- Ghidra project/program: `E:\DevShitPosts\SC6Mods\SCVI_Modding` / `SoulcaliburVI.exe`
- Exporter: Ghidra C/C++ exporter / CppExporter
- Output folder: `E:\myMods\ghidraCalibur\exported`
- Suggested output file: `sc6_decompiled.cpp`
- Headless per-function timeout: 5 seconds
- Build expectation: none; Visual Studio browsing only
- Export status: completed on 2026-06-25 at 01:14 local time
- Output sizes:
  - `sc6_decompiled.cpp`: 252,585,894 bytes
  - `sc6_decompiled.h`: 125,287,700 bytes
  - Raw files were removed after the final successful browse/index rebuild.

## Notes

- The Visual Studio project is a utility project and excludes exported source files from build.
- Re-exporting should only require overwriting files under `exported/`.
- If the export produces multiple files or headers, leave them under `exported/`; the project uses recursive wildcards.
- Headless export helper: `E:\myMods\ghidraCalibur\tools\RunCppExporter.java`
- Repeatable export wrapper: `E:\myMods\ghidraCalibur\tools\export_ghidra_calibur.ps1`
- Browse-tree builder: `E:\myMods\ghidraCalibur\tools\build_browse_tree.py`
- Full refresh wrapper: `E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1`
- The original Ghidra project was locked by the open GUI, so this run exported from a temporary copied project under `ghidraCalibur\work`, then removed that temporary copy after export.

## Browse Tree

- Last generated: 2026-06-25
- Parsed CppExporter function bodies: 139,161
- Ghidra functions without parsed CppExporter bodies: 3,102
- Class-centric files: 3,289
- Functions placed in class-centric files: 5,835
- Primary browse folder: `E:\myMods\ghidraCalibur\browse`
- Primary index folder: `E:\myMods\ghidraCalibur\index`
- Full refresh: `E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1`
- Rebuild browse/index only: `E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1 -SkipExport`
- Keep raw CppExporter files for fast local rebuilds: `E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1 -KeepRawExport`
- Default successful refresh cleanup removes `exported/sc6_decompiled.cpp`, `exported/sc6_decompiled.h`, and `exported/*.log`, but keeps `exported/functions.csv`.
