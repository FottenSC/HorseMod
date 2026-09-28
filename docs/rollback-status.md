# Current local rollback status - 2026-09-28

**G0 is RED; G0-G10 remain open.** The latest recorded full local suite stopped after 710 passes because an unrelated body changed from kinematic to free after admission and still entered the native PhysX start wrapper. The A617 inventory profile is blocked at preflight by G0. No live correction or deployment followed this checkpoint.

This page condenses the latest retained implementation checkpoint; documentation consolidation did not rerun the build, suite or game. Older green G0/G3 receipts apply to earlier snapshots. [The previous chronology](history/2026-09-28-consolidation/notes/rollback-status.md) preserves their scope and evidence.

## Implemented and locally tested

| Area | Established result | Limit |
| --- | --- | --- |
| Body inventory | Bounded locked scene scan, semantic diff, indexed component/serial and native scene-table slot; focused regressions and build pass | Point-in-time identity; does not exclude intervening writers/replacements |
| A617 probe | Once-per-session body inventory and ground-child rows at tick 617; two focused cases and build/two CTests pass | No live owner/flag/phase join; preflight rejects at G0 |
| Input adapter | Same-arrival edits grouped; duplicate host arrivals and duplicate/decreasing revisions reject; schedule/history owned before first forward request | Local contracts pass; live late-input/reset and complete-B recovery remain open |
| RNG | Private MoveVM CRT lane cloned after native warm-up; both cursors and epoch captured, policy v5/checkpoint 21 | Approved compatibility change needs modified controls; xorshift/LFSR remain authoritative and unsplit |
| Trace Start | Existing hook has before-mutation veto and bounded diagnostics | Containment only; reversible birth/death ownership and complete B unproved |

Latest [A617 evidence](evidence/rollback-a617-readonly-inventory-probe-2026-09-28.md) links the source-qualified build, selected tests, full-suite failure, raw log and blocked preflight. Supporting changes: [generation/scene slot](evidence/rollback-body-indexed-generation-2026-09-28.md), [grouping](evidence/rollback-rolling-correction-grouping-2026-09-28.md), [revisions](evidence/rollback-correction-revision-preflight-2026-09-28.md), [startup ownership](evidence/rollback-schedule-start-ownership-2026-09-28.md).

## First blocker and next work

`test_physics_dispatch_rejects_free_body_changed_after_entry` remains RED (fixture exit 136). Application-entry admission releases its scene lock. Start/substep/resume have no proved exclusion of later unrelated-body effects and no verified abort settling the popped task, dependent work, scene and application while preserving B. Holding, dropping, disabling or signaling the task is not recovery.

1. Prove complete before-effect writer/replacement exclusion for admitted owners, or implement exact native abort-to-quiescence and B recovery. Preserve the RED regression until that obligation is met. See the [writer audit](evidence/rollback-body-writer-before-effect-audit-2026-09-28.md) and [rejected no-scene hypothesis](evidence/rollback-physics-no-scene-settlement-2026-09-28.md).
2. After a specific fix, run affected checks, required full suite/native build, G0 certification and fresh preflight. The `a617-body-owner-before-publication` profile remains parked until admission succeeds.
3. Bind A617 ground-child generation, scene slot, body flag and effective phase. Follow the [remaining physics work](investigations/physics-and-debris.md#remaining-work); inventory alone cannot prove continuous safety.
4. Close G1 consumers/ownership, then execute A617->C624 with complete B and 120 independent continuation ticks before broader qualification. Preserve A210 anchors and the accumulated 408-cycle regression.

## Historical live knowledge

- Prior unchanged rolling600 compared 600 transactions/4,200 regenerated ticks. Rolling-window repeat/multiple-tick, pending-work and CPU-construction receipts were missing; normal rendering enabled did not qualify coherence or cost. [Coverage audit](history/2026-09-28-consolidation/notes/rollback-rolling600-coverage.md).
- A five-child debris-pose perturbation matched 120 independent continuation ticks after 407 corrections in its recorded configuration. Reflected readers/physics consumers remain open; `debris_motion` stays unresolved. [Dependency conclusion](evidence/rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md#effect-to-combat-dependency-conclusion).
- Selected C18 frame-172 BaseColor setters were CPU row no-ops. A later tick-211 create-flag Start returned null state/controller without invoking the attachment constructor. Neither supplies a positive lifecycle lease. [C18 receipt](evidence/rollback-g1-c18-row-dispatch-live-2026-09-27.md); [Start chronology](history/2026-09-28-consolidation/notes/rollback-status.md).

## Separate outcomes

| Outcome | Status |
| --- | --- |
| Observer/local prerequisites | Focused checks and native build pass; latest full suite RED |
| Simulation/lifecycle | No qualified current A617->C624 or complete changed-history campaign |
| Recovery/epoch | Complete B after this correction/rejected physics work and 120-tick continuation unproved |
| Presentation | No current normal-render rollback coherence qualification |
| Ownership | No complete simultaneous 1 GiB production accounting receipt |
| Performance | No qualified full-update distribution meeting 16.7 ms across required 600 cycles |

At the latest implementation checkpoint the journal was clean, independent process inventory found no game, and Steam remained running. This is recorded disposition, not a fresh process/mapped-DLL check. Use [tooling](rollback-tooling.md) before execution. [Active work order](rollback-netcode-local-readiness-plan-2026-09-22.md) and [acceptance](rollback-depth-seven-plan.md) remain unchanged in scope.
