# GhidraCalibur

GhidraCalibur creates an immutable, browse-only Visual Studio workspace from the active `SoulcaliburVI.exe` Ghidra program. Decompiled text is analysis evidence and is never compiled as HorseMod source.

## Refresh

Keep Ghidra open on `SoulcaliburVI.exe`, avoid editing while the export is running, and run one command:

```powershell
E:\myMods\ghidraCalibur\tools\refresh_ghidra_calibur.ps1
```

The command:

1. verifies the MCP schema, executable hash, and active program;
2. saves through MCP and probes the checked-in exporter against the live program;
3. performs a read-only structured export through MCP and rejects concurrent program modifications;
4. validates every function body by address, byte bounds, and SHA-256;
5. generates and MSBuild-validates a Visual Studio utility workspace, with build outputs kept in staging;
6. publishes an immutable generation and atomically updates `current.json`.

Use `-OpenWorkspace` to open the newly published solution. The command always prints its exact path. `-AllowPartial` permits only the documented decompiler failure coverage gates to be overridden; corruption, cancellation, identity mismatches, and missing records remain fatal. Failed-run staging is preserved under `.staging/` for diagnosis.

## Generated layout

```text
current.json
generations/<content-id>/
  GhidraCalibur.sln
  GhidraCalibur.vcxproj
  src/                    canonical SC6-area and feature pseudocode shards
  include/                browse-only types/globals, imports/exports, and navigation headers
  .ghidra/data/           structured JSONL records (hidden from Visual Studio)
  .ghidra/index/          address, name, area and RE CSV indexes (hidden from Visual Studio)
  .ghidra/run/            nondeterministic logs, timing, and semantic change summary
  content_manifest.json   deterministic identity and artifact hashes
  .complete
```

Function bodies occur once. The deterministic classifier first groups named code by an explicit family, verified class, or stable name prefix, then names shards after that family or a singleton’s function label. For example, `src/gameplay/replay/codec/lux_replay/lux_replay_0001.cpp` groups replay-codec work without claiming its first function represents the whole file. Each function banner shows a compact list of known callers, callees, globals, and strings. Generic labels fall back to the module or `unknown`. Visible filenames intentionally omit addresses. Each shard header and the hidden `.ghidra` JSONL/CSV indexes retain the exact address range, per-function file link, line, hash, family, and selected shard stem. Generated `.cpp` and `.h` files are Visual Studio `None` items and are excluded from compilation and C++ IntelliSense builds. Raw records, logs, and indexes stay on disk under `.ghidra` but do not clutter Solution Explorer.

The current and two previous complete generations are retained. A workspace already open in Visual Studio remains unchanged when a new generation is published.

## Search and investigation context

After a generation is published, query it without touching Ghidra or the immutable workspace:

```powershell
E:\myMods\ghidraCalibur\tools\find_ghidra_calibur.ps1 -Query Decode
E:\myMods\ghidraCalibur\tools\find_ghidra_calibur.ps1 -Calls Replay -Area gameplay/replay
E:\myMods\ghidraCalibur\tools\find_ghidra_calibur.ps1 -Address 0x140001000 -Open
```

The command supports `-Query`, `-Address`, `-Calls`, `-CalledBy`, `-String`, `-Global`, `-Area`, `-Module`, `-Family`, `-Origin`, and `-Tag`. It returns the canonical source file and line alongside relationships and provenance. Its SQLite full-text cache lives only under `%LOCALAPPDATA%\GhidraCalibur\cache`; it is disposable and never written into a published generation.

`include/navigation/insights.h` is a browse aid for high fan-in functions, cross-subsystem calls, global/string hubs, and named entries into unknown code. Those sections are deliberately labelled as heuristics, not proof. Add durable reviewed investigation context to executable-bound `re_watchlist.json`; refresh validates it and produces `include/navigation/watchlist.h` and `include/navigation/entry_points.h`. An empty watchlist is valid.

Ghidra's raw decompiler rendering is not byte-stable for a small number of complex functions. A fully validated rerun reuses the current immutable generation when the executable, authoritative function metadata, exporter options, pipeline source hash, and type/global/string/comment/call artifacts are unchanged. Coverage gates still run on every rerun, but scheduler-sensitive rendering differences do not create workspace churn. The volatile Ghidra modification counter is recorded for audit but is not sufficient by itself. A real Ghidra or pipeline change always creates a new generation.

## Requirements

- Ghidra 12.0.4 with the expected SC6 program open.
- The local Ghidra MCP server, normally `http://127.0.0.1:8089`.
- Python 3.11, exposed through `py.exe -3.11` or `python.exe`.
- Visual Studio 2022 C++ build tools/MSBuild.
- At least 4 GiB free on the workspace drive.

Durable classification corrections belong in `classification_overrides.json`. Each address may optionally set `area` (for example `runtime/names` or `gameplay/combat`), `module` (for example `codec`), `family` (for example `lux_replay`), and `file_stem` (for example `replay_packet_decoder`) to override deterministic placement and the visible shard name. Do not hand-edit generated generations.

`ue_origin_registry.json` is separate and evidence-gated. Only a reviewed entry tied to the exact SC6 executable hash can route a function to `src/engine/ue4/`; all unlisted functions remain in SC6 folders. Each confirmed entry records the UE 4.17.2 reference anchor and independent binary evidence. Do not use names, strings, or namespaces alone to move code into the engine subtree.

## Tests

```powershell
python -m unittest discover -s E:\myMods\ghidraCalibur\tests -v
```
