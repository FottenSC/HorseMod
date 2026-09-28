> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Offline recovery reopening checkpoint

User disabled live testing pending confirmation. No game launch or deployment change was performed for this checkpoint. The full seeker remains unfinished.

## Implemented and locally verified

Memo11 is confirmed in the current production header. Source registration now accepts already Executing reopening after checking bindings, private B and the current native receiver domain. It does not dereference stale A storage or adopt C during reopening. Settled reopening still validates the adopted C allocation. Direct Undo from Executing still rejects; fresh settlement is required.

The actual production-header tests cover an earlier participant failure, repeated reopening after settlement, invalid bindings, corrupt private B, native receiver removal and reallocation, and exact B recovery with once-only retirement. Empty and registered B are covered.

Memo12 is confirmed. Trace reopening now validates metadata before any bookkeeping change. The original A/B control prefix remains retained; controls discovered only during C settlement are forgotten before C is released. Such rows must have no displaced B references, must be expired, and must have a verified C lease. No native weak count is changed and no strong reference is acquired. A subsequent capture rediscovers surviving C controllers. This prevents stale controller addresses after C's lease and the last native weak entry are released. Original A/B lease-count safeguards remain strict.

The production helper tests cover late rejection and retry without partial metadata, missing C leases, displaced B references, and final weak-owner deletion. A separately compiled exact production reopening method reproduces C-only success, unchanged metadata on later rejection, and repeated success. Its surrounding validation is a fixture; it is not full trace-transaction or live proof.

Build runtime `3047a2796c95e53b4a4b4bdfecc80f8ee0461b396494288a7dbdf9a24dde2b83`, observer `e22e8f2e3e6902b359817a5f0e89646f7c3b5958b211f1a10606f65b0e39a28f`: both C++ suites, shipped-native physics fixture, and 104 runner/source-retention tests pass. [Audit and retained raw evidence](../../../../artifacts/replay-reopening-fixes-20260911/audit.json) link reconstructible production/test sources and test logs. The first extracted-method compile failed from command quoting; correcting the fixture invocation resolved it without production changes.

## Current trace blocker and bounded next observation

The last live candidate remains `replay-93f675afc1bc406abdf028c5a03a0923` on runtime `c93d9120`, failing `traces_C/image_shape` at 11000 with B retained. World_C and scheduler_C passed; registration B undo did not. No independent control ran. This is not successful recovery.

The 109/109/111 A/B/C state counts do not identify added and missing owners or prove B is a subset of C. Failure-only reporting now partitions exact state/controller identities and prints actor, mesh, animation and attachment identities plus root/collection membership from existing copied bindings and dynamic arrays. It never dereferences historical backing, reads historical native counts, or adds a capture participant. Output is bounded; totals and copied-coverage flags expose omitted detail. This instrumentation is built, not live exercised.

Native findings in review memos01/13 establish child append, actor destruction before strong-entry retirement, ReceiveDestroyed/delegate dispatch, overlap removal, tick unregistration and animation joining. Actual C11000 callback, overlap and attachment topology remains unknown. Existing snapshots can establish the owner partition on the next authorized attempt; they cannot establish these uncaptured live teardown domains retrospectively.

Before any C-only actor retirement is admitted, the actual owner set needs exact native types and nonaliasing A/B identity checks, an empty or owned callback/overlap closure, joined animation work, and fresh render/GPU completion. Do not relax image_shape, live strong-pool membership, generation or lifetime guards. Do not use whole-component teardown or restore historical UObject headers.

## Cost and next qualification

No checkpoint payload or GPU resource was added. The prepared trace transaction adds one size_t and uses its already-reserved control scratch for atomic planning. Failure reporting allocates no snapshot buffers or GPU readbacks. No new measured capture/restore/GPU-wait cost is claimed. Native counts and private B remain unchanged by reopening; C-only retirement is still unsupported.

Once live testing is authorized: use the bounded early-participant failure/reopening retry to prove complete B continuation, then establish the actual late trace partition and teardown closure. Other review memos remain prioritized follow-up, including partial render settlement, checkpoint coverage, changed-input coverage and lifecycle liveness. Existing controls remain tied to their exact identities. No full qualification campaign or lighting-history expansion is required before resolving these bounded mechanisms.
