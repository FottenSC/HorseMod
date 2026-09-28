# Physics task no-scene branch: bounded settlement check

Read-only check in the existing `SoulcaliburVI.exe` Ghidra program on 2026-09-28. No database edit, hook, live run or deployment occurred. This tested one new G0 hypothesis: whether the native start-tick no-scene/disabled branch could settle a popped rejected task without running physics and preserve complete B.

The narrow graph-task part does complete normally. `14215D250` skips the virtual tick callback when tick-function `+0xD` is disabled, then clears its `+0x18` task binding. `14215ED20` calls that body, completes the graph event through `140D20A70`, releases task `+0x40`, and returns the allocation to its TLS pool. `142018570` also returns before scene work when world `+0x1C8` is null.

That is **not** an abort-to-quiescence contract for a live non-null scene:

* `142027E40` normally calls `142029916` to publish scene active state and completion references, then creates/schedules dependent physics work. Skipping it changes scene and event history; the graph task's own completion does not settle those absent obligations.
* `142018350` reads world `+0x1C8` again at end physics. If the scene pointer is restored, it can call `142017820`, which releases scene `+0x160`, publishes body transforms and drains notifications even though the start tick was skipped. If the pointer stays null, other application consumers see a false absent scene. Neither branch proves a reversible B state.
* A substep can already exist independently of the popped start task. `142028440` creates an outer completion task, `142055C40` publishes `+0xC8`, schedules a dependent repeat through `141E77F10`, advances counters, dispatches callbacks and invokes scene simulation. `142055BC0` releases the repeat reference only on its normal count/final branch. Disabling a world tick does not unwind those references or scene work.
* `142024D10` fetches active native scenes, collects outputs, releases `+0x130/+8*index`, clears active flags and flushes deferred work. A tick-function completion event does not prove this scene/application tail finished.

**Decision:** do not mutate world `+0x1C8` or tick-function `+0xD` to force native completion. It would execute a normal graph completion with an unproved scene/application history and could be mistaken for complete-B recovery. The existing changed-free-body regression remains RED. A valid G0 solution still needs complete writer/lifetime exclusion before effects, or a verified native operation that settles the exact popped task, dependent substeps, scene state and application tail while retaining B.
