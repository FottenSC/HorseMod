# Side investigations and deferred work

These findings are retained for reuse and do not enlarge the active depth-seven rollback goal. Dates below identify evidence scope, not current qualification.

## Frame meter

The August 20 static handoff produced the native-key ledger, schema, generated C++ lookup and typed live-reader contract. It was runtime-unvalidated and had **no numeric advantage entries** because coordinate-to-tick, reaction-exit and command-release proofs remained open. Do not turn animation coordinates into actionable frame counts.

Lookup identity is style, bank, packed move, attack cell, contact mode, reaction context/row and contact coordinate. Move bits 15..12 select bank, 10..0 select slot; bit 11 is ignored and canonicalized away. Attack cells require an exact `0x70` stride from the selected Section-A base. Missing/invalid identities suppress numbers. Phase 3 means PostActive, not actionable. `totalFrames − recoveryLead − 1` is a locked animation coordinate, not elapsed recovery ticks. [Static handoff and regeneration commands](../history/2026-09-28-consolidation/investigations/2026-08-20-frame-meter-static-handoff.md); [parser documentation](../../tools/moveset_parser/README.md).

## Standalone combat reference

The portable-model investigation was narrowly scoped to v2.31 ActiveBattle, open plane, human P1 Raphael (14) / P2 Maxi (3), exact executable/assets. Other roles, characters, terrain and unresolved routes fail closed. Extend the existing `tools/moveset_parser` model; the old proposal is not a reason to start another emulator during rollback work.

The CALLCOND `0x01` audit closed 137 first-word predicate IDs over 23,288 authored sites, but 78 unresolved operand-producer instances kept the complete handler static-incomplete. IF `0x007F` consumes a xorshift draw even at guaranteed thresholds. A native differential oracle and reachable side effects remain necessary; a complete opcode-name list is not an executable contract. [RE ledger](../history/2026-09-28-consolidation/investigations/standalone-combat-re-ledger.md), [bounded plan](../history/2026-09-28-consolidation/investigations/standalone-combat-simulation-re-plan-2026-08-16.md). Generated domain data remains at [its original path](standalone-combat-callcond-01-domain-v1.json).

## Tira and asset identity

The August 27 audit rejected conclusions from a stale 2019 `hdr023.khd`. In its verified current bank, helpers `0x3250/0x3251` evaluate IF `0x007F` and toggle state-short `0x19`; `0x306F` can change stance without that probability helper. Report all stance changes separately from RNG-caused changes.

Native fighter/resource ID `0x23` at `+0x24C` differs from reflected replay enum value 16. The live stance word is `+0x19AE`. The checkpoint projection includes both fighters' 240 state shorts from `+0x197C`, while the native archive remains the restore writer. This closes a canonical-observation gap, not the runtime transition gate. Bind executable and asset hashes before reusing helper IDs. [Full Tira contract and exact asset identities](../history/2026-09-28-consolidation/investigations/tira-shared-rng-determinism-2026-08-27.md).

## Input latency

Static inspection found no extra whole frame hidden in SC6 input acquisition, Slate preprocessing, Lux scheduling or input-cache publication. A transition arriving after sampling naturally waits for the next sample. The remaining candidates concern render/RHI/presentation queues: `r.OneFrameThreadLag=0`, `r.GTSyncType=1`, `RHI.MaximumFrameLatency=1`, fullscreen/VSync variants, and separately disabling the RHI thread or bounded late controller sampling.

These were experimental candidates, not certified settings or additive frame savings. Guaranteed online delay reinvestment is zero until input-to-photon measurement. The old `0x902` flip/waitable patch, additive “5–6 frame” budget, fake-frame NOP, and raw Reflex-hook claims were rejected. A late poll must not invoke the whole stock poller twice. [Full static assessment and measurement protocol](../history/2026-09-28-consolidation/investigations/Manual_Investigations/removeDelay.md).

## DotVanisher

The original host timer delays battle-end watcher retirement; its producers are battle-result/end-to-lobby paths, not general watch admission. Client request deadlines, assignment/route failures and BattleSync loading are separate.

The later recovery handles a successful assignment arriving before the local native peer route exists. It retains only scalar identities, acknowledges immediately, uses the original bounded deadline, and invalidates on rejection/reset/replacement/teardown. It does not repair pre-assignment timeouts or loading completion. Thread affinity and native lifecycle must remain valid; installed detours require process restart for removal. Offline tests/builds do not establish live compatibility or explain the reported incident.

The September investigation calls the candidate `0.2.0-dev`; the [current component README](../../DotVanisher/README.md) records packaged `1.1.0` with offline validation only. Use that component's docs for operational instructions, not the historical version string. [Audit](../history/2026-09-28-consolidation/investigations/dotvanisher-audit-2026-09-13.md), [native failure paths](../history/2026-09-28-consolidation/investigations/dotvanisher-general-fix-2026-09-13.md), [implementation evidence](../history/2026-09-28-consolidation/investigations/dotvanisher-recovery-implementation-2026-09-13.md).

## Networking, historical seeking and old campaigns

August online-coordinator/release plans and the September round-361 development pass describe older source/configurations. The working tree has retired/deleted online implementation files; an old “compiled” milestone is not current support. Future networking should use the qualified local input/correction adapter, with separate two-process determinism, transport/session, delay/loss/reorder, prediction, desync and disconnect tests. Current local gates come first.

Historical seeking produced useful ownership, callback, recovery and continuation evidence, but its arbitrary-seek/500 ms/512 MiB requirements must not replace the active rolling depth-seven/16.7 ms/1 GiB contract. The [archive index](../history/2026-09-28-consolidation/README.md) retains those plans and failure ledgers. Third-party research checkouts remain under [external-rollback-projects](external-rollback-projects/); they were not re-audited as part of documentation consolidation.
