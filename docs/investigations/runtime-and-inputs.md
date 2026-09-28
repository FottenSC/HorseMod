# Runtime, checkpoints and inputs

Condensed native-contract, world-execution and recovery findings. This is a reference, not a qualification receipt. [Status](../rollback-status.md) resolves chronology; [the active plan](../rollback-netcode-local-readiness-plan-2026-09-22.md) defines acceptance.

## Execution boundary

A native simulation tick is distinct from an authored input sample, manager batch, world update and application/render interval. `1403FE520` handles manager clocks, input production/filtering, repetition and round decisions; `1402DBC60` is the inner battle traversal. Preserve pre-filter publication and the actual callback filter at `141D38300`; post-filter equality cannot simply be assumed. Character `+0x324` is the current MoveVM move ID, not an input-source mode switch.

| Owner | Pending work after an interior suspension |
| --- | --- |
| Battle manager | Repeat/input/round decisions, handler lease and outer tail |
| Tick body `14215D250` | Callback completion before clearing scheduled-task binding `+0x18` |
| Tick task `14215ED20` | Completion event, references and pooled allocation; forwarding the wrapper can invalidate its pointer |
| Tick groups/world | Dependencies, newly spawned ticks, physics, latent actions, timers, controllers, memory marks, GC and rendering |
| Application/render | Target completion without silently executing another simulation tick |

A dummy event or `FinishTickTaskManagerFrame` cannot replace pending work. `UWorld_Tick` uses RCX/RDX/XMM2; `+0x784` is the current group, and `+0x780 == 0` does not prove return. Resumable execution must own native memory-mark/arena lifetimes and restore the ambient arena on each yield. [Task/arena details](../history/2026-09-28-consolidation/investigations/replay-world-ownership.md#execution-boundaries); [call order](../history/2026-09-28-consolidation/investigations/deterministic-native-contract-results-2026-08-23.md#per-frame-call-order).

## Checkpoints and state classification

The `1403841E0` writer / `140384540` reader use a bounded `0x28018` HgCpu stream with local pointer relocation. Typed supplements/canonical projections cover omissions, identity and verification. Opaque bytes are local reconstruction data, not portable peer checksums. Successful serialization does not capture the enclosing Unreal world. [Hybrid rationale](../history/2026-09-28-consolidation/investigations/deterministic-native-bulk-snapshot-hybrid-2026-08-24.md).

- Restore authoritative combat and required shared behavior exactly: inputs, MoveVM, RNG, physics feedback, animation timing, births/deaths, allocation order and callbacks.
- Reconcile qualified presentation coherently only after closing its outgoing dependencies. Visual names and pixel differences do not classify a field.
- Runtime bindings need valid identity, membership and completion, not cross-process address equality.
- Unresolved consumers remain included or rejected. Expected observations are comparison-only.

Matrix-bank rotation is state: `14030B630` selects `(current + 2) % 3`, publishes old current as previous, and does not clear/copy the destination. Native readers can overlap typed supplements, so final recapture matters. UObject retention is not protection against explicit destruction; pointer equality is not generation proof. [Restore-order ledger](../history/2026-09-28-consolidation/investigations/deterministic-native-contract-results-2026-08-23.md#production-candidate-regions-in-restore-order); [overlap audit](../history/2026-09-28-consolidation/investigations/replay-checkpoint-static-audit-2026-09-06.md).

## Rolling transaction and inputs

At each eligible T after warm-up: retain **B=S(T)** and old input revision, publish **A=S(T−7)**, execute exactly seven native ticks, finish required C/application/render work, commit, then retire. Perform the ordinary forward update and retain the next checkpoint. Keep at least eight logical boundaries plus B, operation, scratch and deferred/in-flight ownership. Ring eviction cannot retire an owner still referenced by another participant.

The local adapter records tick, publication, round and sample coordinate at `RepeatDecision`, preserving first consumption across repeats. Revision buffers are immutable shared owners. Admission checks session/epoch, expected revision, exact A and first affected traversal. Same-arrival edits are grouped into one request/revision; duplicate request arrivals and duplicate/decreasing revisions reject. Startup owns schedule/history before its first forward request. Commit invalidates/rebuilds old-revision checkpoints; cancellation restores B's history and eligibility. [Adapter](../evidence/rollback-local-input-adapter-checkpoint-2026-09-28.md), [grouping](../evidence/rollback-rolling-correction-grouping-2026-09-28.md), [startup](../evidence/rollback-schedule-start-ownership-2026-09-28.md).

This authored-replay adapter is not a tested device-sampling/prediction adapter. Out-of-window predicates exist, but complete stall/reject/session recovery and live reset remain open. The earlier epoch audit identified cancellation, re-warm and retirement-reporting risks; recheck these source-specific findings rather than assuming fixes. [Epoch/recovery audit](../history/2026-09-28-consolidation/investigations/rollback-support-epoch-recovery-2026-09-22.md).

## RNG and floating point

| Family | Durable finding |
| --- | --- |
| xorshift96 | Three words at `14470E2C8`; gameplay, camera and effects share draw ordering |
| LFSR | 25 words plus cursor at `14485EB30` / `14485EB94`; cursor 25 is a valid refill sentinel |
| LCG / wind-combined | Separate state; wind also needs valid graph topology; some recurrence writers are inlined |
| UCRT | Ground placement and tile-emitter initialization share native CRT with MoveVM opcode `0x50006` |
| Floating point | Historical probes found stable controls/x87 but changing MXCSR sticky flags; preserve caller environment on all exits |

**Later compatibility decision:** September 27 split MoveVM's CRT lane after native warm-up, retaining both cursors and epoch (policy v5/checkpoint 21). Matching modified controls are required; outcomes may differ from stock. This supersedes old descriptions of an entirely unsplit broker. Xorshift/LFSR remain unsplit, and lifecycle work cannot be omitted on the strength of the CRT change. [RNG/FP details](../history/2026-09-28-consolidation/investigations/deterministic-native-contract-results-2026-08-23.md#rng-and-fp-contract); [shared CRT route](../evidence/rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md#effect-to-combat-dependency-conclusion); [split chronology](../history/2026-09-28-consolidation/notes/rollback-status.md).

## Recovery, reset and native replay

B survives publication, corrected execution, CPU/GPU settlement and target tails. New C-only retirement requires a fresh completion barrier; an earlier fence cannot cover later submissions. Reopening preflights all participants before transferring ownership. An uncertain release remains poisoned: retry may double-decrement. Historical bounded seek cancellations demonstrated B recovery on their exact builds, not today's seven-tick transaction. [B ownership](../history/2026-09-28-consolidation/investigations/replay-advance-undo-ownership-2026-09-10.md), [atomic reopening](../history/2026-09-28-consolidation/investigations/replay-render-reopen-atomic-2026-09-11.md), [uncertain release](../history/2026-09-28-consolidation/investigations/replay-visibility-release-fault-2026-09-11.md).

Session exit defers the native stop callback until host owners are released. Reset stops old-epoch admission, settles/recovers work, drains retirement, invalidates history and warms up again. No restore crosses a destroyed session. [Session exit](../history/2026-09-28-consolidation/investigations/replay-native-session-exit-2026-09-12.md).

Automatic round-end replay uses sparse battle snapshots and recorded inputs, saves post-round B, replays earlier state, then restores B. This is native evidence, not complete active-combat coverage. The cinematic recorder lacked an explicit checkpoint participant or universal inactivity proof in the audit. [Mechanism](../history/2026-09-28-consolidation/investigations/round-end-replay-2026-09-15.md), [coverage limit](../history/2026-09-28-consolidation/investigations/round-end-replay-rollback-coverage-2026-09-15.md).
