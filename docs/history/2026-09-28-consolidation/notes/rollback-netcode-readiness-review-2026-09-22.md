> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Review checkpoint for the replacement local-readiness plan

This checkpoint reviews work performed after the private-physics handover, including the task **Continue rollback qualification** (`01a0c8da-8530-73d2-ad40-79a17dd41fbb`). It is a planning/evidence review, not a new qualification result. The task was idle when read. Recheck task/process/journal state before implementation.

Primary continuation plan: [rollback-netcode-local-readiness-plan-2026-09-22.md](../../../rollback-netcode-local-readiness-plan-2026-09-22.md). The top of rollback-status.md still describes the earlier handover and is behind the reviewed work. Do not conclude that body/shape restoration remains untested merely from that older heading.

## Work since the handover

| Work | Implemented/proven | Still unproven |
| --- | --- | --- |
| Reconstructed initial body continuation | Existing SDK fixture compares reconstructed and independently authored bodies for120 ticks, including two kinematic targets, simulation enablement and linear/angular impulses | Assembled game rollback and all later active phases |
| Initial body installation | Private restoration passed live for five children at A617/B624 | A scene/render publication and boundary equality |
| Shape restoration | Regression failed at shape property0x3C; owned pose/filter/flags/offset packet and typed component/actor index rebinding added; geometry/material checks precede writes; private shape restoration passed live | Full restored scene/solver/render operation |
| Native material values | Same-pointer mutation regression failed before the fix; packet now checks friction/restitution/combine/flags and user-data identity before shape writes; build/native checks passed | No live qualification of this later change; not arbitrary mutable material restoration |
| Scene identity | New regression rejects a changed nonzero engine scene ID; latest build is RED, with failure at that check | Producer guard fix/integration/green proof outstanding at review |
| Publication investigation |141FEDEE0 publishes actor arrays, can silently return on wrong aggregate scene; caller supplies locks. Shape filter producer141FF5660 encodes component index in simulation word2 and owner index in query word0 | Publication is not completed by these annotations |

Relevant source locations:

- `HorseMod/horselib/deterministic/ReplayGroundInitialBodyState.hpp`
- `HorseMod/horselib/deterministic/ReplayGroundShapeState.hpp`
- `HorseMod/horselib/deterministic/ReplayGroundPrivateBody.hpp`
- `HorseMod/horselib/deterministic/Sc6ReplayGroundColdPhysics.inl`
- `HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl`
- `tools/replay_ground_initial_kinematic_selftest.inl`
- `tools/replay_ground_capture_selftest.cpp`
- `tools/replay_physics_markers_selftest.inl`

## Retained evidence

- [Thread progress snapshot](../../../evidence/rollback-post-handover-review-2026-09-22.json): conversation reports and changed-file inventory; not a replacement for executable receipts.
- [Shape/body source and native evidence](../../../evidence/rollback-ground-shape-state-2026-09-22.json).
- [Native shape summary](../../../evidence/rollback-ground-shape-native-2026-09-22.md), [publication/filter native record](../../../evidence/rollback-ground-publication-native-2026-09-22.json). Some descriptive material statements predate the subsequent value guard; current source/retained build identity takes precedence.
- [Initial-body live manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/4eb04156b3c066c28c7c4e6e04877915bded12086dbe3570793a4d4ca5ed984e.json).
- [Latest reviewed shape live manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3c861825bd59d14b7f70d754baf6cbe37deb2f94b3b854b862af02c71ceb235a.json), [compact immutable console](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4c51019c1f781ed5c220bd34464eb1dbcba9332e40bb1366779e196ecde99b42.log): run replay-0bc49b48f8954ebe8910c876c44ba27f, code7 preparation rejection at target617/B624,407 completed cycles, cleanup complete/games_remaining0. Scene/render publication did not pass.
- [Material-value green build console](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/985bedc0028c147f32c33286c2942d9c9df531243624b91e12fa1ddaefb37ea0.log). This predates the newer red regression.
- [Latest scene-identity RED manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a3edd7d814c368f5b46535104070eb5d92b31b7a35fc3dedfb834fda4ed4a533.json), [native failure log](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/local-regressions-bce7af9e7793920c39d250b049d88e2917e467281ccd43fb3e4548deedd9faf0.log). Runner status says native regressions failed and live launch prohibited; the compact first-failure excerpt misleadingly quotes the two passing CTests. The retained native log identifies the actual failing check.
- [Saved annotation inventory](../../../evidence/rollback-native-annotations-2026-09-22-pass1.json):47 functions and17 partial types, with remaining gaps. This is a prior snapshot; subsequent Ghidra edits may supersede individual entries.
- [Debris combat dependency review](../../../evidence/rollback-debris-combat-dependency-review-2026-09-22.md): shared RNG/lifecycle is real; direct chunk-motion feedback into combat not established; exclusion remains conditional.
- [Particle event feedback](../../../evidence/rollback-particle-event-feedback-2026-09-22.json): separate admission/lifecycle work remains.

## Decision for the next agent

Finish the outstanding narrow scene-identity safety regression if it is still red. Then prove the state boundary before investing further in historical debris physics. Preserve completed reconstruction as a fallback. Prioritize logical debris state with qualified nonexact chunk motion if the outgoing consumer audit and negative tests support it. Otherwise evaluate safe inactive retention, or finish exact reconstruction for the proven feedback subset.

Do not omit native RNG draws, callbacks, shared slot/group changes or particle receivers. Do not suppress object destruction without a replacement logical lifecycle. Do not follow the stale Ghidra annotation claiming presentation UCRT draws are isolated: current HandleRand always forwards the native original first.

No new live run/build was executed for this review. No production/test files were edited. Overall simulation/observer validity, lifecycle, recovery, epoch/reset, presentation, ownership and performance acceptance remain open. Existing narrow successful proofs are preserved; networking is still deferred until the replacement plan's gates pass.
