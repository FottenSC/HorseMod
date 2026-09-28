# Ground debris: combat dependency investigation

Read-only subagent investigation requested by the user, reviewed on 2026-09-22. No production changes, native database mutations, build or live experiment were performed for this investigation. Addresses below identify evidence to recheck, not a claim that every consumer is closed.

## Conclusion and limits

No verified path was found from chunk position/velocity into damage, hit detection, fighter movement or ring-out logic. This is not proof of absence. Existing physics admission makes a one-way cosmetic-motion domain plausible. Exact motion may be excluded after the remaining consumers are closed; the whole debris subsystem cannot simply be omitted.

## Shared RNG: confirmed source behavior, native callsites reported

- Placement at 140895CC0 calls imported rand for base yaw and per-child jitter; return RVAs 895D6E and 896105.
- MoveVM opcode 0x50006 in 140365900 calls the same import at 140366FEE, returning to 140366FF4, and uses the result to select command execution.
- [UcrtRandBroker.cpp](../../HorseMod/horselib/deterministic/UcrtRandBroker.cpp) HandleRand calls original rand BEFORE allowlist/mode checks and returns that result. The allowlist counts diagnostic draws; it does not isolate streams. The parent independently read this source during plan preparation.
- Capture reads native thread-local PTD+28; restore uses native srand. [PresentationTerminals](../../HorseMod/horselib/deterministic/DeterministicHookSet.PresentationTerminals.inl) routes thread ID and caller RVA through this broker.
- Therefore placement on the same thread advances RNG used by fighting scripts. Thread/order in the actual current window must be witnessed. The old isolated-presentation-RNG plate on 140895CC0 is stale. No omission-induced live combat divergence was tested.

## Physics admission

Ground constructor 1408A2CA0 sets object channel5 (PhysicsBody), ignores all channels then blocks5/2/0 (PhysicsBody/Pawn/WorldStatic). Activation sets collision2 (PhysicsOnly). Pawn contact is explicitly permitted; do not claim it ignores fighters.

Verified child vtable1436CEFB0 targets: +598 ->142052E40; +5A8 ->142052ED0 ->142002200 (BodyInstance+22F); +780 ->142052F50 ->142002370; +778 ->142052F80 ->1420023F0. Enum meanings are available in HorseMod/include/SoulCaliburVI/Engine/Public.

[GroundDebris host](../../HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl) admission rejects unrelated freely simulated bodies, scene-query handles, triggers, constraints, notification consumers, overlaps and transform observers. It evaluates actual native filter14204CF60 on both sides and requires admitted active-pair flags401. These are supported-case restrictions, not general engine facts, and must remain valid through corrected execution and continuation.

[Older B6538 inventory](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/native-B6538-collider-feedback-inventory-audit.json) reports70 shapes/53 actors, five debris as the only freely simulated bodies, zero notification requesters and85 eligible filtered pairs. It is not an A617 receipt.

## Shared lifecycle

1408A4860 root tick -> delegate+810 -> reflected thunk140CF2760 -> manager virtual+600 ->14089F5F0. The final function finds the ground slot, dispatches manager+388 OnVFxFinished with the slot ID, then removes all matching ground records. Manager vtable143356F68+600 points to14089F5F0.

- 140896410 advances manager+3E0 shared with particles, retaining request/provider ownership.
- 140898E90 also dispatches+388 before compaction.
- 1403BA410 reads ground existence through14089B940, prunes VFX groups and stops/fades oldest particle/ground groups when limits are exceeded.
- Listener1408D3F20 matches the finished ID, writes trace state+D8=-1, invokes attachment virtual+238, and removes weak membership at+3F0.

These are verified shared lifecycle effects. Downstream combat feedback from all listeners and reflected/script GetGroundDebris consumers remains unresolved. A class name containing Trace is not evidence of hit detection.

## Debris self-simulation

14089FF80 reads root/child pose and applies impulses to those children. That establishes self-simulation, not a combat dependency. Fade14089FAE0 ->14089FE30 ->14089FC70 changes opacity, phase and child collision/simulation before logical deactivation. No fighter-outcome write was identified in inspected manager tick1408A4510.

## Required next proof

Close active listener/reflected consumers; witness same-thread RNG order; maintain no-feedback predicates across A->C and continuation. Then deliberately vary only admitted chunk motion against independent controls and compare combat, actual RNG, shared lifecycle and future continuation. Negative tests must detect newly introduced feedback. Keep visual coherence and resource ownership checks independent. No acceptance gate is promoted by this report.
