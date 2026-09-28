# Effects, callbacks and presentation

Effects can change shared state even when their output looks cosmetic. Preserve logical execution and native ownership; reconcile only consumer-proven presentation. [Current status](../rollback-status.md) owns qualification claims.

## Event hub and semantic callbacks

Global `14470D188` points to the embedded dispatcher of `ULuxBattleEventListenerHub`, not a terminal VFX renderer. The hub has 41 callback collections of stride `0x70`. `ProcessAndCompactCallbackEntries` (`141D38300`) runs subscribers in reverse registration order, tracks recursion and compacts dead/disabled entries after the outer dispatch unwinds. Provider/query slots differ from broadcasts.

Blanket hub suppression would skip handler state, provider calls, Blueprint effects and graph maintenance. Delayed replay can use a different listener graph and later gameplay state. Run required receivers once per simulated traversal and roll back their effects before repeating history. Deduplicate only independently qualified external presentation terminals. Count equality does not establish listener identity, generation or owned mutable state. [Corrected event model and registration ledger](../history/2026-09-28-consolidation/investigations/rollback-effects-event-hub-investigation.md#corrected-object-model).

## Particle lifecycle

Completion `141F73990` → receivers `141FA0B80` → spawn `141FD3E30` precedes world dispatch. Dropwater Water_01 death End causing Water_02 spawn is concrete internal feedback. The host already runs completion; a missing observation is not evidence that execution was skipped.

| Required coverage | Native detail |
| --- | --- |
| Component delegates | spawn/burst/death/collision at `+0x850/+0x860/+0x870/+0x880` |
| AEmitter delegates | `+0x398/+0x3A8/+0x3B8/+0x3C8` |
| World manager / callback objects | world `+0xC0` identity/vtable and each authored callback's mutable owner |
| Typed rows | spawn/death/collision/burst at `+0x918/+0x928/+0x938/+0x948`, strides `0x40/0x50/0x78/0x48`; script `+0x958`, stride `0x40` |

Empty component delegates do not exclude other routes. Pointer arrays do not capture callback objects. Persistent stage components need the same coverage as reconstructed effects. Unknown routes reject before unsafe publication; rejecting an eligible supported cycle still fails qualification. [Particle admission ledger](../evidence/rollback-particle-event-feedback-2026-09-22.json).

CPU native destruction must finish its peer notifications/slot clearing before allocation records retire; a null slot alone is insufficient. GPU construction must own the real modules, TypeData, emitter membership, shared tile pools, coordinates and render resources. A detached or empty constructor cannot prove independent live rendering. GC leases do not stop explicit native destruction. Keep the known tick 6415→6417 PendingKill regression. [Particle lifetime](../history/2026-09-28-consolidation/investigations/rollback-particle-lifetime-2026-09-14.md), [CPU retirement](../history/2026-09-28-consolidation/investigations/replay-native-emitter-retirement-2026-09-12.md).

Handler-owned slot history is future state: one repeated-seek failure appended duplicate slot 7 until a native group limit destroyed the new emitter. Restoring the manager alone did not restore its handler map. [Causal slot-history result](../history/2026-09-28-consolidation/investigations/replay-vfx-handler-history-2026-09-09.md).

## Trace births, Start and finish

Trace state includes attachment/controller topology, child actors/meshes, curves, animation, materials, scheduling and VFX slot history. Native `1408D95A0` retains curve samples consumed by `1408DA260`; attachments drive dependent effects and `1408D9C20` stops slots. The whole trace system cannot be dismissed as pixels.

`StartLuxTraceComponentTrace` (`1408D8C40`) can mutate state before late delegate binding. Its existing owned hook precedes first write `1408D8C65`; the current veto is containment, not reversible birth ownership. A completion receiver must be indexed-live and an exact member of retained GC ownership, but that admission does not reverse new Start effects or prevent explicit destruction. [Before-effects boundary](../evidence/rollback-g1-before-effects-boundary-2026-09-27.md), [finish-owner check](../evidence/rollback-g1-finish-owner-admission-2026-09-27.md).

Native object allocation publishes an index and calls creation listeners before constructors/owner membership finish. `ConstructLuxTraceAttachmentObject` (`1408C8F40`) returns after constructor/PostInitProperties, but a null factory pair alone cannot generally exclude an allocated attachment. The later observed tick-211 create-flag Start returned null and did not call the constructor: that occurrence is not a positive birth example. Zero-serial observations remain provisional against same-address replacement. [Construction/Start chronology](../history/2026-09-28-consolidation/notes/rollback-status.md).

Fresh children need typed mesh/animation/render destinations and the correct ownership image at commit. B intentionally lacks newly constructed C identities; using B's saved membership to adopt a surviving C child is invalid. These older local defects have retained reproductions; recheck current callers before reapplying fixes. [Historical children](../history/2026-09-28-consolidation/investigations/rolling-historical-trace-children-2026-09-15.md), [adoption finding](../history/2026-09-28-consolidation/investigations/rollback-live-adoption-blocker-2026-09-15.md).

## Materials and render completion

The selected C18 frame-172 path reached Start, the BaseColor helper, two getters and two setters. Both setters took no changed-row branch: **CPU row no-ops for that occurrence only**. This resolves that narrow question, not effect ownership, earlier initialization, later finish, or complete B. [Live receipt](../evidence/rollback-g1-c18-row-dispatch-live-2026-09-27.md).

MID CPU values and backing arrays, render-proxy rows, refresh tasks and GPU resources are distinct owners. Native MID vector rows are `0x28` bytes at `+0xB8`; proxy rows are `0x18` bytes at proxy `+0x138`. The `0x208` MID and `0x168` proxy payload sizes do not include backings, caches, commands or simultaneous A/B/C retention. Vector update and refresh have different queued callbacks carrying raw proxy pointers.

MID BeginDestroy submits release and a render-command fence at `+0x1B0`; its false-argument fence is not a general GPU-completion receipt. FinishDestroy can defer proxy deletion and release caches. Flushing a deferred batch clears/submits it, not completes it. No pointer snapshot or submitted command authorizes early free/PendingKill revival. [Native MID/proxy lifecycle and partial types](../evidence/rollback-g1-mid-proxy-lifecycle-2026-09-27.md).

## Animation, HUD and coherent rendering

Animation evaluation, pose publication and enclosing tick completion are separate. Owner-shared update-rate state, component binding, pending workers, interpolation and timer callback identity/self-rearm can affect future execution. Reconstructing deadlines or copying a pose does not restore these relationships. [Animation/timer audit](../history/2026-09-28-consolidation/investigations/replay-checkpoint-static-followup-2026-09-06.md).

HUD findings are causal, not just appearance fixes: requested visibility controls native Cockpit ticking; restoring widget values alone failed. Damage listener latches at `+0x3A8..+0x3AC` represent future delivery. Active damage-type players are owned through widget active-player arrays, not arbitrary UObject children. B parking must preserve live players, clocks, callbacks and backing storage. For announcements, native removal releases viewport ownership; retaining a detached Slate pointer is not safe undo. [HUD visibility](../history/2026-09-28-consolidation/investigations/replay-active-hud-checkpoint-2026-09-12.md), [damage latches](../history/2026-09-28-consolidation/investigations/replay-initial-hud-ownership-2026-09-12.md), [active B](../history/2026-09-28-consolidation/investigations/replay-private-active-HUD-B-2026-09-13.md), [announcements](../history/2026-09-28-consolidation/investigations/replay-announcement-ownership-2026-09-13.md).

Paused display uses the original window/swap chain: save native buffer contents to owned scratch, copy the retained image, draw overlay/Present, restore contents, and await completion before releasing scratch/resuming. Failure/timeout still retains resources. A held image proves paused display only; coherent actors, effects, poses and HUD under normal rendering remain required. [Viewport ownership](../history/2026-09-28-consolidation/investigations/original-viewport-rollback-2026-09-15.md).

Corrected capture needs its own transaction-owned context while B remains live; finishing/reusing the ordinary particle-copy owner could destroy undo state. Shared allocation credit requires exact retained COM identity/extent; private B/C, scratch and in-flight/deferred ownership stay charged. Production peak is 1 GiB, bounded diagnostics at most 2 GiB. [Capture ownership](../history/2026-09-28-consolidation/notes/rollback-rolling600-coverage.md#changed-input-capture-dependency), [storage/accounting](../history/2026-09-28-consolidation/investigations/replay-particle-checkpoint-storage-2026-09-12.md).
