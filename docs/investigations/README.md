# Investigation references

These four topic files are the normal reading set. Current gate status and next work live in [rollback-status.md](../rollback-status.md).

| Topic | Contents |
| --- | --- |
| [Runtime and inputs](runtime-and-inputs.md) | Tick phases, checkpoints, revisions, RNG, recovery, native replay |
| [Physics and debris](physics-and-debris.md) | Body inventory, writer races, task settlement, feedback, reconstruction |
| [Effects and presentation](effects-and-presentation.md) | Event hub, particles, traces, materials, animation, HUD, GPU completion |
| [Side investigations](side-investigations.md) | Frame meter, portable combat, Tira, latency, DotVanisher, deferred online work |

The [archive](../history/2026-09-28-consolidation/README.md) contains the original long-form notes and handovers. Follow a source link before changing the relevant native behavior; summarized addresses and annotations still need verification against the admitted binary.

Supporting files are grouped separately:

| Location | Purpose |
| --- | --- |
| [evidence/native/](evidence/native/) | Archived native dumps; original bytes preserved |
| [evidence/](evidence/) | Retained observations and run evidence |
| [generated_movement/](generated_movement/) | Generated movement data |
| [external-rollback-projects/](external-rollback-projects/) | Third-party research checkouts |

The three `deterministic-*-manifest.json` files stay here because tooling reads these exact paths. `standalone-combat-callcond-01-domain-v1.json` is generated evidence linked by the standalone-combat reference. Old native-dump paths are recorded in the [relocation map](../history/2026-09-28-consolidation/native-dump-relocations.json).
