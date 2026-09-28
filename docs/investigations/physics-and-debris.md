# Physics and debris

The first active blocker is task safety, not a missing constructor. Exact debris motion remains **unresolved** under the current state policy. [Status](../rollback-status.md) owns gate results; the [readiness plan](../rollback-netcode-local-readiness-plan-2026-09-22.md) owns acceptance.

## Why entry admission is insufficient

The bounded collision scan locks the scene, admits at most 64 actors per supported scene, and rejects unsupported collision-domain state. That lock is released before later start/substep/resume consumers. A production-boundary regression changes an unrelated actor from kinematic to free after entry; it still reaches the PhysX start wrapper (exit 136). The full suite remains RED for this reason.

The native body-value accessor copies the relevant flag from virtual `+0x178` into output `+0x9C`. But `SetSimulatePhysics` is not just a flag write: enclosing `142001C40` can detach a component and call `141FEF5A0` before writing body-instance `+0x74`, then synchronize via `142007730`. Static-mesh `142053F50` reaches it through component `+0x430`; skeletal bulk/per-body and reflected routes also exist. `14210F750` can destroy/rebuild skeletal bodies. Guarding a later copied flag cannot establish before-effect ownership. Actual A617 caller, generation and phase remain unobserved. [Writer audit](../evidence/rollback-body-writer-before-effect-audit-2026-09-28.md), [replacement audit](../evidence/rollback-physx-writer-owner-phase-2026-09-28.md).

No verified abort-to-quiescence is available. Disabling the tick or hiding world `+0x1C8` can complete the graph task while leaving scene completion references, dependent substeps, end-physics transform/notification publication and application tails unresolved. Scene `+0x160` may normally be nonempty once tasks are produced, so an entry quiescence predicate cannot be reused unchanged at dispatch. Holding/dropping/signaling a popped task is containment, not complete-B recovery. [No-scene hypothesis and exact native obligations](../evidence/rollback-physics-no-scene-settlement-2026-09-28.md).

## What the body inventory establishes

| Witness | Meaning and limit |
| --- | --- |
| Scene + actor/body/component | Process-local ownership and current membership, not stable logical identity |
| Indexed component + positive serial | Validated current object-array slot/generation; zero/unavailable serial remains provisional |
| Native scene-table slot | Detects reorder, but is not an actor generation |
| Type, role and copied flags | Distinguishes admitted ground children from other actors at that scan |
| Complete scan + semantic diff | Detects observed additions/removals/replacements/flag changes, not transient changes between scans |

The scan uses direct validated object-array lookup, not weak-reference creation or `UObject::IsReal`. Duplicate scene/actor keys, overflow and partial scans cannot masquerade as a full census. Focused native tests include a real in-scene kinematic-to-free change, reused-address/changed-serial and reordered scene-slot cases. These passing diagnostics do not close the separate failing dispatch test. [Inventory](../evidence/rollback-body-inventory-2026-09-28.md), [generation/slot regressions](../evidence/rollback-body-indexed-generation-2026-09-28.md), [native writer fixture](../evidence/rollback-native-body-writer-census-2026-09-28.md).

The once-per-session A617 probe logs complete body rows and ground-child component/root/root-actor/manager/ring-slot identities before application effects. It is implemented and locally tested, but its before-publication profile is blocked by G0. A prior A617 log identified a component and scene actor 48 without the five child identities or kinematic flag. It cannot resolve the join. [Probe](../evidence/rollback-a617-readonly-inventory-probe-2026-09-28.md), [historical phase evidence](../evidence/rollback-a617-actor-impulse-phase-2026-09-28.md).

## Debris state boundary

Ground debris is a constructed component with controller/lifetime and child-body ownership, not merely a pooled visual actor. `1408A4860` advances lifecycle; deactivation goes through `140CF2760` → `14089F5F0`, broadcasts manager `+0x388` OnVFxFinished and removes matching component records. Slot allocation (`+0x3E0`), group pruning, provider ownership, RNG draws and callbacks are shared behavior. Preserve those even if exact chunk trajectories are eventually excluded.

PhysicsOnly shapes still block Pawn, WorldStatic and PhysicsBody. Close actual filter `14204CF60`, unrelated free bodies, collision/notification/query/constraint paths, direct/reflected transform readers and indirect solver effects. `GetGroundDebris` returns a root object; that does not close its downstream consumers. Niagara's `141BC7F90` source selection / `141BC7760` transform read are not covered by a spawn/attach/registration-only observer. [Consumer review](../evidence/rollback-debris-combat-dependency-review-2026-09-22.md), [reader limit](../evidence/rollback-local-input-adapter-checkpoint-2026-09-28.md).

A retained five-child pose perturbation matched 120 independent continuation ticks after 407 corrections. This supports a bounded hypothesis, not general no-feedback classification. The 408th accumulated correction at A617→C624 remains required. Shared CRT feedback is native-proven, with the later approved MoveVM split described in [runtime/RNG](runtime-and-inputs.md#rng-and-floating-point); it does not resolve physics or lifecycle consumers. [Perturbation and feedback scope](../evidence/rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md#effect-to-combat-dependency-conclusion).

## Lifetime strategies and exact fallback

Choose the smallest consumer-supported strategy per domain: logical lifecycle plus coherent presentation reconciliation when proved; otherwise inactive physical retention with exact logical death, or exact reconstruction for demonstrated feedback. No PendingKill revival, dead-owner ticking/collision, suppressed callbacks or history imported from controls.

Existing body/shape/pose/material capture and private reconstruction remain useful. A native fixture compared reconstructed and independent bodies over 120 ticks with kinematic targets, simulation enablement and impulses; this is not assembled game rollback. Preserve these constraints:

- Resolve real helper/world/FPhysScene/native-scene identities and caller-held locks; nonzero body IDs are insufficient.
- Rebind filter component/actor indices to the actual replacement; verify membership after `141FEDEE0`, which may decline aggregate publication.
- `141FF9700` can overwrite restored values; `141FF2370 == true` means required shapes are missing/failure.
- BodySim motion history, broadphase/SAP pairs, marker IDs, free lists and query publication can affect later behavior. Pose bytes alone do not restore them. The SAP active-pair table is persistent CPU state, not a GPU-only owner.
- Support later active-body phases and safe removal as well as initial sleeping bodies. Constructor or private-ready receipts do not prove publication, continuation, B undo or retirement.

[Private-body handover](../history/2026-09-28-consolidation/notes/rollback-next-phase-handover-2026-09-22-private-physics.md), [ground lifecycle](../history/2026-09-28-consolidation/investigations/replay-ground-debris-restore-plan-2026-09-14.md), [marker/broadphase](../history/2026-09-28-consolidation/investigations/replay-marker-broadphase-ownership-2026-09-11.md).

## Remaining work

1. Cover every reachable creation/removal/replacement/flag writer before its first effect, bound to session, epoch, generation, scene and phase. Periodic inventory is insufficient.
2. Prove phase-specific start/substep/resume safety or exact native settlement and complete B. Keep the failing dispatch case RED until then.
3. After required checks/G0/preflight, obtain the A617 owner/flag/phase join; do not deploy from the current RED source.
4. Close reflected/physics/transform consumers and negative admission cases before relaxing exact motion. Then run A617→C624, changed births/deaths, independent continuation and B recovery under the active plan.

The [original bounded plan](../history/2026-09-28-consolidation/notes/rollback-body-tracking-plan-2026-09-28.md) preserves the detailed step order and earlier receipts.
