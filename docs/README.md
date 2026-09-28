# HorseMod documentation

Start with [current status](rollback-status.md), then open only the reference for the subsystem you are changing. The goal is local rolling rollback by seven native simulation ticks during combat in one retained match. Networking and historical seeking are deferred.

| Read when you need... | Document |
| --- | --- |
| Latest result, first blocker and next experiment | [Current status](rollback-status.md) |
| Native execution, checkpoints, inputs, RNG and recovery | [Runtime and inputs](investigations/runtime-and-inputs.md) |
| PhysX task safety, body identity and debris feedback | [Physics and debris](investigations/physics-and-debris.md) |
| Callbacks, particles, traces, materials, HUD and rendering | [Effects and presentation](investigations/effects-and-presentation.md) |
| Frame meter, standalone simulation, latency and DotVanisher | [Side investigations](investigations/side-investigations.md) |
| Test selection, commands, evidence and cleanup | [Tooling](rollback-tooling.md) |

## Evidence and history

The references consolidate the old handovers and investigations by topic. [The archive index](history/2026-09-28-consolidation/README.md) lists every relocated note, including the previous status and tooling pages. Original bytes are retained in its linked ZIP; readable archived copies have updated links and an archive notice. Old next-step instructions, schema versions, memory budgets and green results apply only to their recorded checkpoint.

[Current evidence reports](evidence/) and [older investigation evidence](investigations/evidence/) retain their locations. Loose native dumps are grouped under [native evidence](investigations/evidence/native/) with unchanged bytes and an [old-to-new path map](history/2026-09-28-consolidation/native-dump-relocations.json). Machine-consumed manifests, generated data, source snapshots, logs and third-party research checkouts retain their existing paths. The archive manifest maps old narrative paths for historical receipts.

## Keep this small

- Replace current status at a coherent checkpoint; retain superseded detail in evidence or history.
- Update the relevant topic reference when a finding changes, with its evidence scope and source link.
- Keep run IDs, hashes, transcripts and native dumps in evidence. Avoid another handover repeating status and references.
- Distinguish implementation, local tests, live observations and unresolved requirements. Summarization cannot turn a historical pass into a current pass.
