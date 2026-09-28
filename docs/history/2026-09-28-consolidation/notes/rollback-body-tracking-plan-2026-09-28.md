> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Bounded physics-body tracking plan — 2026-09-28

## Objective and boundary

Identify every native physics actor in the active scene(s) that can be consumed during the retained seven-tick combat window. Distinguish the admitted ground children from unrelated actors, preserve validated owner/scene identities, and account for creations, removals, replacements and simulation-state changes between admission and the final consumer. This is a prerequisite to either a complete writer exclusion or a native abort-to-quiescence contract. Inventory evidence alone does not qualify G0 or complete B.

## Execution order

1. **Inventory the bounded scene.** Extend the existing locked collision scan, which already rejects scenes above 64 actors, to emit a fixed-capacity read-only witness for every scanned actor in both supported scenes. Record scene, actor, body, resolved component, native type, copied simulation flag and admitted-ground role. Mark the witness complete only after both scene scans and membership checks finish. Capture partial witnesses on rejection; never present them as a complete census. Reject overflow or duplicate scene/actor keys. Avoid allocations and object-array scans in the physics loop.
2. **Compare snapshots.** Add a bounded semantic diff that identifies the first addition, removal or changed role/type/body/flag/owner. Treat addresses as process-local ownership witnesses. A pointer match cannot establish object generation or exclude a create/remove/create interval. Record scene and tick/phase with the caller's witness, and require indexed game-object generation before promoting a ground-child join to proven identity.
3. **Track changes between scans.** Audit all native body creation/removal/replacement and simulation-flag writer paths that can reach the admitted scene. Instrument verified before-effect boundaries with session, epoch, tick, phase, owner generation and scene. Correlate each event with inventory deltas. A writer route that cannot be observed or excluded remains an explicit G0 failure; periodic snapshots alone cannot rule out a transient body.
4. **Check each consumer phase.** Use a phase-specific read-only scene scan before start, substep and resume; the application-entry queue-quiescence check cannot be reused after normal task production. Verify lock, scene identity and task ownership for each phase. Do not call a task rejected before its native wrapper, and do not count a terminal or held task as complete-B recovery.
5. **Decide safety and test.** If all relevant writers are excluded before their first effect, enforce that invariant and run the direct changed-body/replacement/insertion regressions. Otherwise implement and test exact native task, event, scene and application settlement at start, substep and resume. Keep the current RED G0 case until one route proves recoverable B. Only then run the full suite, build, G0 certification and profile preflight before the shortest live A617 owner/phase join. After that, repeat the seven-tick correction and independent continuation checks.

## Current bounded implementation and acceptance

This work block implements steps 1–2 through the existing production collision scan and a production-boundary regression. The test must fail before the scan emits witnesses, then pass with a complete before/after inventory and a changed unrelated-body row. A baseline, changed, or complete rejected scan emits bounded rows for later inspection. The old task-dispatch regression remains RED. No deployment or live correction follows from this diagnostic pass. Retain source-qualified logs, the existing checkout/build and all unrelated work.

## Execution checkpoint

Steps 1–2 passed their selected regression and native build; [the retained receipts](../../../evidence/rollback-body-inventory-2026-09-28.md) separate that result from the still-RED task-dispatch case. Step 3's writer coverage and step 4's phase-specific task/scene ownership remain unproved. The first-effect path can perform component work before changing the copied physics flag. Per step 5, G0 certification, deployment and live correction are prohibited until a complete structural exclusion or verified native settlement makes the existing counterexample recover complete B.

The [native writer test](../../../evidence/rollback-native-body-writer-census-2026-09-28.md) demonstrates that a real in-scene kinematic-to-free change is observable by the next complete scan. It does not establish when the UE writer can run, cover replacement/creation paths, or resolve the popped-task completion obligations. Those remain the next dependent proof, with the selected dispatch regression RED.
