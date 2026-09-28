# HorseMod project instructions

- Documentation map and condensed subsystem knowledge: [docs/README.md](docs/README.md). Read the relevant topic instead of replaying archived handovers.
- Historical notes: [docs/history/depth-seven-reset-2026-09-14](docs/history/depth-seven-reset-2026-09-14). These are archived evidence, not current instructions.
- Keep AGENTS.md for important working rules dont append random shit.




# Project Phase

We have divided the project into 3 phases
Phase 1 — Local rollback correctness: account for relevant state through working capture, restore, resimulation and continuation in a defined combat scenario.
Phase 2 — Two-client synchronization: prove independent clients converge when inputs arrive late.
Phase 3 — Broader coverage and production readiness: expand supported situations, recovery, sustained operation and performance.

Currently we're on phase 1. Previously we have gone off the deepend and tried to do everything all at once which is why there is so much.

## Ownership and correctness

- Expected observations are comparison data only; never import them to manufacture deterministic agreement.
- Exact historical pixels are optional. Coherent actors/poses/HUD and all simulation feedback remain required. A held image proves paused display only.

## Build and tests

- Reuse `python tools/replay_test.py <stage>` and `build_cmake_LessEqual421__Shipping__Win64`. Extend existing tools narrowly; do not create another harness.
- During tooling edits, use affected `local --test` selections or `--changed-path` groups; use `--layer unit|workflow|native-contract` when the change is confined to that layer. Run broad checks at coherent checkpoints and preserve the full-suite requirement for live admission. See [the tooling guide](docs/rollback-tooling.md); selected or filtered passes never qualify G3.
- For locally reproducible defects, first add a production-boundary regression that fails before the fix. Use live tests only for native behavior local tests cannot establish. Avoid fixtures that grant the behavior under test.
- Use the shortest representative combat segment. Prefer existing evidence and compatible independent controls. Do not repeat full playback or qualification campaigns to debug one failure.
- Verify native signatures and exact game/runtime/framework/observer/replay identities before deployment. Match setup interventions, including intro skipping, in candidate and independent control.
- Keep bounded raw logs and reconstructible source contents, including untracked production/test files. Reports must link their retained logs, not overwritten working paths. Be mindful of disk space.
- Do not use UObject::IsReal in per-tick observation loops: its implementation scans the object array. Use validated indexed lookup.
- Do not add competing hooks at already owned native entries, including 1403FCA60 and 141D38300. Inspect current ownership before installing hooks.

## Repository and native evidence

- `HorseMod/`: C++ mod and deterministic core. `RE-UE4SS/`: engine integration. `tools/`: existing runner and local fixtures. `dump/`: native/asset evidence.
- Full game dump: `C:/Users/prest/Documents/SoulcaliburModding/SCVI Sound Tools/dump`; copy only files actually needed into the repository.
- `tools/BlueprintToCpp` produces pseudocode from cooked assets, not buildable mod code. Validate against Kismet bytecode/CFG or native traces.
- Lux uses X/Z ground and Y vertical; UE(X,Y,Z) = Lux(X,Z,Y) * 100.

## Ghidra

- Use the existing Soulcalibur program through native Ghidra MCP tools. Never import a second copy, edit .gpr files directly, or use scripts for database edits. Avoid currently unreliable snapshot endpoints.
- Use the applicable function/type skill when documenting or recovering types. Improve directly relevant names/types/comments while investigating; do not expand into unrelated annotation work.
- Function names: PascalCase verbs; globals: g_ plus typed prefix; labels: snake_case. Pass logical field names where MCP automatically supplies prefixes. Resolve undefined types before Hungarian variable names; use Ghidra builtins for local types.
- Structural/prototype changes precede comments because set_function_prototype wipes plate comments. Apply comments through native comment tools with actual newlines; bookmarks are not substitutes. Verify the result.
- For register-only or decompiler-artifact variables that cannot be typed, document the limitation instead of repeatedly forcing edits. Tool-exposure failures must be reported honestly, not represented as completed annotations.
