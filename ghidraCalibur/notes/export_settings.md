# Structured Export Settings

- Schema: `ghidra-calibur-export/v1`
- Supported Ghidra: 12.0.4
- Default program: `SoulcaliburVI.exe`
- Default project: `E:\DevShitPosts\SC6Mods\SCVI_Modding\SCVI_Modding.gpr`
- Default MCP: `http://127.0.0.1:8089`
- Default per-function timeout: 5 seconds
- Failure retry timeout: max(20 seconds, four times the normal timeout), batched once before final records are written
- Body shards: at most 250 functions or 8 MiB
- Visible workspace: deterministic SC6-area and feature `src/` shards (for example `codec_0001.cpp`) plus browse-only `include/` headers; addresses remain in shard headers and hidden `.ghidra` indexes
- PE metadata: external functions are emitted as deterministic `imports.jsonl` records grouped into `include/imports/`; actual external entry points are emitted as `exports.jsonl` and `include/exports/sc6_exports.h`
- Normal coverage ceiling: at most 250 failures and at most 0.25% of body-eligible functions
- Publication: immutable `generations/<content-id>` plus atomic `current.json`
- Retention: current plus two previous complete generations
- Source consistency: MCP `save_program` followed by read-only `/run_script`; publication is rejected if the Ghidra program modification number changes during export
- Stable reruns: reuse the existing generation only after full validation when executable identity, pipeline source hash, options, authoritative function metadata, and core artifact hashes match

The content manifest intentionally excludes timestamps, host paths, durations, retry diagnostics, and logs. Those fields are stored in `.ghidra/run/run_report.json`, `.ghidra/run/diagnostics.jsonl`, and `.ghidra/run/logs/` so identical program content and exporter settings retain the same deterministic generation identity. MSBuild validation writes only to disposable staging output.
