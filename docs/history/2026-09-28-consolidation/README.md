# Archived documentation - September 28, 2026

These are historical investigations, handovers and superseded status pages. Their old next-step instructions and test results are not current instructions. Start at the [documentation map](../../README.md), [current status](../../rollback-status.md), or [condensed investigations](../../investigations/README.md).

92 older narrative files were relocated here; the three rewritten entry pages are also retained. Readable Markdown copies have rebased links and an archive notice. [Original document bytes](original-notes.zip) preserve all 95 documents plus the original root AGENTS.md; [manifest.json](manifest.json) records original/archive paths, sizes and SHA-256 hashes. A follow-up grouped 24 loose native dumps under `docs/investigations/evidence/native/`; the [relocation map](native-dump-relocations.json) preserves their original paths and byte hashes. Raw run logs, machine-consumed manifests and third-party checkouts remain at their original locations.

The summaries are selective working references. Detailed addresses, rejected hypotheses, historical defects and run-specific evidence remain in the documents below. Use the manifest to resolve old narrative paths embedded in immutable receipts. [Validation results](validation.json) record link checks, original/archive checksum verification and retained historical-link limitations.

## Plans, status and tooling

- [README.md](notes/README.md) - Rollback documentation map
- [deterministic-simulation-goal.md](notes/deterministic-simulation-goal.md) - Deterministic Simulation Rewrite Goal
- [replay-acceptance-matrix.md](notes/replay-acceptance-matrix.md) - Rollback documentation has moved
- [replay-acceptance.md](notes/replay-acceptance.md) - Rollback documentation has moved
- [replay-review-ownership-2026-09-12.md](notes/replay-review-ownership-2026-09-12.md) - Bounded review and ownership checkpoint
- [replay-seeking-acceptance-deferred-2026-09-14.md](notes/replay-seeking-acceptance-deferred-2026-09-14.md) - Retained-session seeker acceptance matrix
- [replay-testing.md](notes/replay-testing.md) - Reproducible rollback testing
- [replay-tool-output.md](notes/replay-tool-output.md) - Replay command output
- [rollback-body-tracking-plan-2026-09-28.md](notes/rollback-body-tracking-plan-2026-09-28.md) - Bounded physics-body tracking plan — 2026-09-28
- [rollback-local-readiness-handover-2026-09-22-2212.md](notes/rollback-local-readiness-handover-2026-09-22-2212.md) - Rollback local-readiness handover — 2026-09-22, 22:12 CEST
- [rollback-netcode-readiness-review-2026-09-22.md](notes/rollback-netcode-readiness-review-2026-09-22.md) - Review checkpoint for the replacement local-readiness plan
- [rollback-next-phase-handover-2026-09-21.md](notes/rollback-next-phase-handover-2026-09-21.md) - Next-phase handover: local depth-seven rollback
- [rollback-next-phase-handover-2026-09-22-private-physics.md](notes/rollback-next-phase-handover-2026-09-22-private-physics.md) - Continuation prompt: finish local rollback, then networking
- [rollback-next-phase-handover-2026-09-22.md](notes/rollback-next-phase-handover-2026-09-22.md) - Rollback/replay continuation handover — 2026-09-22
- [rollback-rolling600-coverage.md](notes/rollback-rolling600-coverage.md) - Retained rolling600 coverage audit
- [rollback-status-history-2026-09-20.md](notes/rollback-status-history-2026-09-20.md) - Current external blocker: Steam cloud-sync dialog holds a queued launch
- [rollback-status-history-2026-09-26.md](notes/rollback-status-history-2026-09-26.md) - 2026-09-24 - G1 first trace continuation correlation; lease remains NO-GO
- [rollback-status-history-2026-09-27.md](notes/rollback-status-history-2026-09-27.md) - Latest G1 checkpoint - selected C18 material-slot live receipt - 2026-09-27
- [rollback-status.md](notes/rollback-status.md) - Current local rollback status — 2026-09-28
- [rollback-test-workflow-2026-09-27.md](notes/rollback-test-workflow-2026-09-27.md) - Rollback test workflow checkpoint — 2026-09-27
- [rollback-testing-handover-2026-09-21.md](notes/rollback-testing-handover-2026-09-21.md) - Handover to the rollback/replay implementation agent
- [rollback-tooling-validation-2026-09-26.md](notes/rollback-tooling-validation-2026-09-26.md) - Rollback tooling implementation and validation — 2026-09-26
- [rollback-tooling.md](notes/rollback-tooling.md) - Rollback tooling contracts
- [rollback-work-block-2026-09-28.md](notes/rollback-work-block-2026-09-28.md) - Depth-seven rollback: bounded work block (2026-09-28)
- [testing-improvements-2026-09-20.md](notes/testing-improvements-2026-09-20.md) - Testing infrastructure implementation — September 20
- [testing-startup-hypothesis.md](notes/testing-startup-hypothesis.md) - Independent startup compatibility

## Native and subsystem investigations

- [2026-08-20-frame-meter-static-handoff.md](investigations/2026-08-20-frame-meter-static-handoff.md) - SC6 frame meter static handoff (v1)
- [AGENTS-stale-replay-guidance-2026-09-13-8c3b7d58.txt](investigations/AGENTS-stale-replay-guidance-2026-09-13-8c3b7d58.txt) - HorseMod — SC6 reverse-engineering project
- [removeDelay.md](investigations/Manual_Investigations/removeDelay.md) - Soulcalibur VI Latency Removal and Online Reinvestment Investigation
- [deterministic-native-bulk-snapshot-hybrid-2026-08-24.md](investigations/deterministic-native-bulk-snapshot-hybrid-2026-08-24.md) - Deterministic Native Bulk Snapshot Hybrid
- [deterministic-native-contract-agent-plan-2026-08-23.md](investigations/deterministic-native-contract-agent-plan-2026-08-23.md) - Deterministic native contract: reverse-engineering agent plan
- [deterministic-native-contract-results-2026-08-23.md](investigations/deterministic-native-contract-results-2026-08-23.md) - Deterministic native contract results
- [deterministic-online-coordinator-2026-08-24.md](investigations/deterministic-online-coordinator-2026-08-24.md) - Deterministic online coordinator milestone
- [deterministic-online-round361-gate-2026-09-04.md](investigations/deterministic-online-round361-gate-2026-09-04.md) - Deterministic online round-361 development gate — 2026-09-04
- [deterministic-simulation-contract-2026-08-23.md](investigations/deterministic-simulation-contract-2026-08-23.md) - Deterministic simulation contract (2026-08-23)
- [deterministic-simulation-failure-ledger-2026-08-30.md](investigations/deterministic-simulation-failure-ledger-2026-08-30.md) - Deterministic simulation qualification failure ledger
- [deterministic-simulation-final-takeover-plan-2026-08-28.md](investigations/deterministic-simulation-final-takeover-plan-2026-08-28.md) - Deterministic simulation rewrite — revised final takeover plan
- [deterministic-simulation-outstanding-plan-2026-08-25.md](investigations/deterministic-simulation-outstanding-plan-2026-08-25.md) - Deterministic Simulation Rewrite — Outstanding Implementation Plan
- [deterministic-simulation-takeover-handoff-2026-08-24.md](investigations/deterministic-simulation-takeover-handoff-2026-08-24.md) - Deterministic Simulation Rewrite Takeover Handoff
- [dotvanisher-audit-2026-09-13.md](investigations/dotvanisher-audit-2026-09-13.md) - DotVanisher code audit — 2026-09-13
- [dotvanisher-general-fix-2026-09-13.md](investigations/dotvanisher-general-fix-2026-09-13.md) - DotVanisher: general-fix investigation — 2026-09-13
- [dotvanisher-recovery-implementation-2026-09-13.md](investigations/dotvanisher-recovery-implementation-2026-09-13.md) - DotVanisher watch-assignment recovery implementation
- [original-viewport-rollback-2026-09-15.md](investigations/original-viewport-rollback-2026-09-15.md) - Original viewport rollback publication
- [replay-acceptance-status-history-through-2026-09-13.txt](investigations/replay-acceptance-status-history-through-2026-09-13.txt) - replay-acceptance-status-history-through-2026-09-13
- [replay-active-hud-checkpoint-2026-09-12.md](investigations/replay-active-hud-checkpoint-2026-09-12.md) - Active HUD checkpoint integration
- [replay-advance-undo-ownership-2026-09-10.md](investigations/replay-advance-undo-ownership-2026-09-10.md) - Actual recovery controls demonstrated (2026-09-10)
- [replay-announcement-ownership-2026-09-13.md](investigations/replay-announcement-ownership-2026-09-13.md) - Retained-session announcement ownership
- [replay-checkpoint-coverage-next-2026-09-13.md](investigations/replay-checkpoint-coverage-next-2026-09-13.md) - Next checkpoint coverage experiment
- [replay-checkpoint-preparation-fallback-2026-09-12.md](investigations/replay-checkpoint-preparation-fallback-2026-09-12.md) - Checkpoint preparation fallback
- [replay-checkpoint-static-audit-2026-09-06.md](investigations/replay-checkpoint-static-audit-2026-09-06.md) - Replay checkpoint static audit — 2026-09-06
- [replay-checkpoint-static-followup-2026-09-06.md](investigations/replay-checkpoint-static-followup-2026-09-06.md) - Replay checkpoint follow-up: animation publication and timer identity
- [replay-clean-request-failure-ui-2026-09-11.md](investigations/replay-clean-request-failure-ui-2026-09-11.md) - Clean failed-request UI recovery
- [replay-cross-round-physics-restore-plan-2026-09-10.md](investigations/replay-cross-round-physics-restore-plan-2026-09-10.md) - Existing physics state: cross-round transaction scope
- [replay-ground-debris-restore-plan-2026-09-14.md](investigations/replay-ground-debris-restore-plan-2026-09-14.md) - Ground-debris ownership: bounded integration plan
- [replay-host-seek-qualification-2026-09-09.md](investigations/replay-host-seek-qualification-2026-09-09.md) - 2026-09-11: current full-index restore boundary
- [replay-initial-hud-ownership-2026-09-12.md](investigations/replay-initial-hud-ownership-2026-09-12.md) - Initial replay boundary: HUD ownership
- [replay-live-reopening-2026-09-11.md](investigations/replay-live-reopening-2026-09-11.md) - Live reopening qualification after testing resumed
- [replay-marker-broadphase-ownership-2026-09-11.md](investigations/replay-marker-broadphase-ownership-2026-09-11.md) - Marker graph and broadphase ownership
- [replay-native-emitter-retirement-2026-09-12.md](investigations/replay-native-emitter-retirement-2026-09-12.md) - Native CPU emitter retirement during a retained seek
- [replay-native-session-entry-2026-09-12.md](investigations/replay-native-session-entry-2026-09-12.md) - Native replay entry and host checkpoint placement
- [replay-native-session-exit-2026-09-12.md](investigations/replay-native-session-exit-2026-09-12.md) - Retained replay session exit
- [replay-particle-checkpoint-storage-2026-09-12.md](investigations/replay-particle-checkpoint-storage-2026-09-12.md) - Particle checkpoint storage: bounded decision
- [replay-preparation-retry-2026-09-14.md](investigations/replay-preparation-retry-2026-09-14.md) - Preparation retry ownership
- [replay-private-active-HUD-B-2026-09-13.md](investigations/replay-private-active-HUD-B-2026-09-13.md) - Active damage-type B ownership
- [replay-private-trace-child-B-2026-09-13.md](investigations/replay-private-trace-child-B-2026-09-13.md) - Private B trace-child ownership
- [replay-render-drained-cancellation-2026-09-11.md](investigations/replay-render-drained-cancellation-2026-09-11.md) - Cancellation at Render::Drained
- [replay-render-reopen-atomic-2026-09-11.md](investigations/replay-render-reopen-atomic-2026-09-11.md) - Fully settled render reopening: atomic admission
- [replay-reopening-recovery-2026-09-11.md](investigations/replay-reopening-recovery-2026-09-11.md) - Offline recovery reopening checkpoint
- [replay-retained-end-native-finish-2026-09-12.md](investigations/replay-retained-end-native-finish-2026-09-12.md) - Retained replay end: native finish side effects
- [replay-seeker-implementation-handoff-2026-09-08.md](investigations/replay-seeker-implementation-handoff-2026-09-08.md) - Replay seeker implementation handoff — static fork review, 2026-09-08
- [replay-seeker-static-deepening-2026-09-08.md](investigations/replay-seeker-static-deepening-2026-09-08.md) - Static deepening during the testing pause
- [replay-stage-tick-registration-2026-09-11.md](investigations/replay-stage-tick-registration-2026-09-11.md) - Stage tick-registration ownership
- [replay-status-history-through-2026-09-13.txt](investigations/replay-status-history-through-2026-09-13.txt) - replay-status-history-through-2026-09-13
- [replay-tps-audit-2026-09-11.md](investigations/replay-tps-audit-2026-09-11.md) - Replay TPS harness audit — 2026-09-11
- [replay-trace-retirement-integration-2026-09-12.md](investigations/replay-trace-retirement-integration-2026-09-12.md) - C-only trace retirement in the existing seek transaction
- [replay-trace-visible-B-reconstruction-2026-09-13.md](investigations/replay-trace-visible-B-reconstruction-2026-09-13.md) - Repeated-seek trace reconstruction
- [replay-unpaced-application-2026-09-09.md](investigations/replay-unpaced-application-2026-09-09.md) - Bounded unpaced application admission
- [replay-vfx-handler-history-2026-09-09.md](investigations/replay-vfx-handler-history-2026-09-09.md) - Repeated seek: VFX handler slot history
- [replay-visibility-release-fault-2026-09-11.md](investigations/replay-visibility-release-fault-2026-09-11.md) - Uncertain native release must not retry
- [replay-world-ownership.md](investigations/replay-world-ownership.md) - Current causal rendering checkpoint
- [rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md](investigations/rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md) - Rollback Beta Remaining Native Boundaries — Static RE Handoff
- [rollback-effects-event-hub-investigation.md](investigations/rollback-effects-event-hub-investigation.md) - Rollback Effects and Lux Battle-Event Hub Investigation
- [rollback-forward-blockers-2026-09-15.md](investigations/rollback-forward-blockers-2026-09-15.md) - Forward investigation: first depth-seven restore
- [rollback-live-adoption-blocker-2026-09-15.md](investigations/rollback-live-adoption-blocker-2026-09-15.md) - Fresh trace adoption uses the wrong ownership image
- [rollback-particle-lifetime-2026-09-14.md](investigations/rollback-particle-lifetime-2026-09-14.md) - Seven-tick particle lifetime integration
- [rollback-reassessment-progress-2026-09-05.md](investigations/rollback-reassessment-progress-2026-09-05.md) - Rollback reassessment: implementation and evidence ledger
- [rollback-remediation-handoff-2026-08-06.md](investigations/rollback-remediation-handoff-2026-08-06.md) - Rollback remediation handoff — 2026-08-06
- [rollback-runtime-rewrite-2026-09-05.md](investigations/rollback-runtime-rewrite-2026-09-05.md) - Runtime coordination and qualification rewrite
- [rollback-support-epoch-recovery-2026-09-22.md](investigations/rollback-support-epoch-recovery-2026-09-22.md) - Supporting audit: rolling epoch transition and failure recovery
- [rolling-historical-trace-children-2026-09-15.md](investigations/rolling-historical-trace-children-2026-09-15.md) - Historical trace children in the rolling window
- [round-end-replay-2026-09-15.md](investigations/round-end-replay-2026-09-15.md) - Round-end instant replay — native investigation
- [round-end-replay-rollback-coverage-2026-09-15.md](investigations/round-end-replay-rollback-coverage-2026-09-15.md) - Round-end replay versus current rollback coverage
- [standalone-combat-re-ledger.md](investigations/standalone-combat-re-ledger.md) - Standalone combat RE ledger
- [standalone-combat-simulation-re-plan-2026-08-16.md](investigations/standalone-combat-simulation-re-plan-2026-08-16.md) - Standalone SC6 combat simulation: reverse-engineering handoff
- [tira-shared-rng-determinism-2026-08-27.md](investigations/tira-shared-rng-determinism-2026-08-27.md) - Tira shared-RNG determinism contract — 2026-08-27
