> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Preparation retry ownership

Update2026-09-14: the historical failure and proposed next run below are resolved on their retained exact identities. `preparation-retry-qualified-audit.json` proves fallback after B6538 recovery; `round3989-qualified-1g-audit.json` subsequently proves3989 itself restorable at5574/6417/11000, with120 independent continuation ticks each. Verified current-B visibility bindings and empty-A marker handling passed without new capture fields. This does not prove a cancelled B6538+120 continuation on that build. See the current acceptance matrix for remaining work.

The retained `round3989-natural-fallback-recovery-failure.json` and its raw log in the existing build's `replay-tests` directory identify candidate `replay-fe003c41877c4fde92a450b6a95f0042`, runtime b997f745 / observer 63d68bee. Full indexing retained five checkpoints. Natural preparation fallback from3989 to2511 completed5574 and6417, with120 observed continuation ticks each; no independent control ran because11000 failed. At B6538,3989 rejected `marker_target_graph_empty` before A publication. Preparation reported ground/render recovery, then2511 and170 rejected the combined `scene_filter` check. Cleanup complete; this is not independent B-recovery qualification.

The visibility diagnostic separately identifies a main-query key with no active A primitive and a current B primitive (binding Current). That cache-admission issue is unresolved; no guard has been removed and no additional render history captured.

## Locally reproduced recovery defects

The existing shipped-PhysX fixture originally had no simulation-event callback. With a callback registered through native scene descriptor+10, production re-add leaves one wake-set entry. The new empty-notification assertion fails on the broken production implementation (`ground-callback-reopen-red-run.log`). Native EC190 queues this bookkeeping whenever a callback exists; native E7670 dispatches only cores with actor flag4. EE850 clears BodySim notification bits and both sets without dispatch. Its actual shipped16-byte signature is checked before admission.

Recovery now requires an empty initial notification domain. Before EE850, every dense entry must resolve to a recovered body's exact core/BodySim/scene binding, with no actor notification flag, duplicates, or out-of-bounds backing. Foreign and callback-eligible entries reject without clearing or dispatch. Native completion is verified before the operation reports Recovered; a failed native call poisons ownership. No expected observation is installed.

The next production-boundary test reproduced another rejection (`ground-callback-retry-run.log`): immediate re-removal cannot assume newly re-added bodies have completed a simulation update. Native148140 consumes activation handles at graph+190/+198; observed re-added nodes are edge-free, flags20. Native pending broadphase creation belongs to those same selected bodies. The removal guard now admits only selected edge-free activation receipts and selected creation bits, retaining strict rejection of unrelated activation, edge/deactivation work, dirty nodes, and foreign creation. Native remove cancels unpublished creation; the removal packet excludes those handles and permits an empty removal packet only when captured unpublished creation explains it. Existing native task completion, ID retirement and storage detachment still run.

`ground-callback-retry-cycle.log` exercises two complete remove/recover/retire cycles without simulation between them, foreign-activation/creation rejection, notification rejection/retry, no callback dispatch, and120 independently matching ticks for both the re-added and unaffected bodies. Both static and kinematic collider variants pass with zero outstanding native allocations. Local physics evidence does not establish game-wide B recovery or live fallback.

## Scope and next experiment

No new checkpoint participant, renderer snapshot, GPU readback or simulation step was added. One1024-byte creation bitmap per removal guard is included by the existing sizeof-based operation accounting (up to two scene operations). Native notification cleanup reuses existing set storage; full runtime latency and native peak ownership still need measurement.

Next: existing bounded full-index sequence5574/6417/11000/2517. Hypothesis: after3989 rejects at B6538, native preparation recovery can immediately admit2511 and complete11000 with B undo preserved. Detailed admission labels distinguish a different failure. Stop on first failure; independent native comparison follows only candidate completion. Do not count local fixtures or resumed60TPS as seeker or rollback qualification.
