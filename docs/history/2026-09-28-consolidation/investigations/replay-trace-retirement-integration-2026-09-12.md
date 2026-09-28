> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# C-only trace retirement in the existing seek transaction

Schema91 now implements this bounded integration. Build/local method tests pass; native C11000 retirement and complete B recovery remain unproven. Preserve the eleven-participant transaction, private B, existing native retirement paths and fresh completion barrier.

## Native closure and the pending live witness

| Dependency | Established | Admission still needed |
|---|---|---|
| State partition | C retains all109 A/B states and adds one strong child per root; root1 has an attachment and live VFX slot1604. | Recompute exact identities/multiplicities per operation. Never admit by counts alone. |
| Actor and mesh | Exact LuxTraceMeshActor/SkeletalMeshComponent, one owned mesh, no overlaps, mesh physics-state bit clear, empty ReceiveDestroyed and component EndPlay. | Actor EndPlay/delegate, world navigation/network contexts, streaming manager destinations; current bounded witness reads these. |
| Attachment | Exact LuxTraceAttachComponent, unregistered, no parent/children or activation delegates. Native DestroyComponent still removes owner membership. | Identify its actor owner and unique owned-component membership; require it is not the owner's root and has no pending tick event. Current witness reads these. |
| VFX provider | Native140899030 resolves slot id at record+8 and releases the retained request on compaction. Trace completion normally deactivates the attachment. | Resolve the live id to an admitted C-only particle birth before any teardown. Retire its provider/component through the existing recovery path before destroying the referenced attachment. Do not emit an authored manager destruction event into discarded C. |
| Weak arrays | Native14213CC20 removes matching *live targets*, decrements each weak occurrence and preserves other entries. | Keep the native child strong pool alive throughout all weak-array removals so an expired-null comparison cannot remove unrelated expired entries. Verify multiplicities before/after. |
| Final strong release | Native141D0A460 removes matching child strong entries. Destructor1408CED30 releases cached-static weak ownership and frees three payload allocations. It does not destroy actor/attachment. | Prove no payload-storage alias with A/B or another child, and cached controller belongs to a proven live retained owner. Remove strong entries last; never dereference payload afterward. |
| Completion | Actor/component teardown unregisters ticks and queues render work; pre-teardown GPU drain cannot certify it. | Existing fresh ordered native render/GPU drain, then fresh C capture before B installation; no old C graph validation after destruction. |

Native callees are retained in `artifacts/replay-live-recovery-20260911/trace-retirement-closure-20260912.json`. The no-op audio-streaming removal slot is1402D2BC0; texture streaming uses14214AFB0, which removes the actor/primitive from streaming maps and clears streaming flags. This is native cleanup, not a reason to snapshot historical texture residency. Unknown streaming implementations must reject. Navigation removal can update octree/maps and dirty work; an exact actor class alone does not prove that branch empty.

## Implemented integration, awaiting native qualification

1. Represent the existing child-array slot as a strong-reference dynamic image for C as well as empty A/B. Keep initial historical nonempty-child publication unsupported until its separate lifetime obligation is implemented. No new capture participant or renderer image.
2. Add execution-only subset validation: every A/B retained owner and its storage must remain, C additions must satisfy the bounded retirement contract, and all child/source backing must be disjoint from private B. Keep initial `Prepare` strict. Capture C is observation, never a source of expected gameplay state.
3. Settlement may adopt newly observed weak references only after their current native child strong ownership is established. Reopening may drop their metadata only when no displaced-B references belong to it and the native child pool still owns the controller. Preserve atomic bookkeeping and original controls.
4. Carry one bounded retirement journal in the existing execution owner. Preflight the entire trace and particle retirement set before the first teardown. Preserve enough identities and phase information to avoid repeating a successful native lifecycle call; ambiguous native failure retains owners and fails safely.
5. Retire existing hidden trace rendering, then the existing C-only VFX provider/component set. Remove only the C-child weak entries while native strong ownership is intact; destroy each admitted actor and attachment using exact native callees; remove child strong entries last. Retained A/B object and backing checks remain mandatory. A partially retired C image must not be used as a live graph.
6. Submit the existing fresh completion barrier, release the old capture registrations, capture current C without the retired children, settle and install complete B. Direct undo of a settled image with live C children must reject rather than overwrite strong-array headers.

## Focused proof

Local coverage must exercise duplicate weak/strong occurrences, foreign/A/B aliases, cached-controller ownership, no removal of unrelated expired weak entries, partial failure at each journal boundary, reopening twice, and prohibition on payload access after final strong release. Actual-header/native-memory evidence must be distinguished from callback fixtures.

The assembled live test remains A170/B2504 -> C11000 -> cancel -> complete B2504 recovery ->120 independently matching gameplay ticks/callbacks/poses/HUD, followed by changed-input coverage. Earlier C5750 recovery controls remain exact-identity evidence only. A failed admission is not a completed restore. Pixel equality is optional.

The additional production reservation should be bounded journal metadata, with no new historical GPU image/readback. Record its actual sizeof/allocation charge, native retirement elapsed time, fresh GPU wait and full recovery cost. Include all source/test bytes in the next retained manifest. Do not use resumed60TPS as capture/restore/resimulation performance.

## Current checkpoint

Candidate0c4452bd96b24ce2855b055f4b129198 (runtime109562fc/observered845fde) closed the read-only world witness. It still rejected before retirement and cleaned up completely with zero games. The actors implement only MatineeAnimInterface (offset388), so the non-null navigation system is not reached through the actor interface path. There is one matching replay world context with zero drivers, world type1, no editor transaction, and only the audio streaming manager's no-op removal. Actor ReceiveEndPlay and multicast are empty. The attachment is one member of the existing LuxTraceManager, not its root, with no pending primary event. [Retained report](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/trace-world-retirement-domains.json) links its retained raw log and executed sources. The completed live probe was removed.

Schema91 runtime758c8c20/observer2609e73c builds; both C++ suites/native physics fixture and152 focused Python tests pass. [Build and reconstructible sources](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/trace-retirement-integration-build-stage.json) include the actual new production inl, test fixture and runner-test bytes, verified against the archive. The focused test compiles the production retirement method verbatim with controlled native-call/GC surroundings: success, duplicate weak entries, unrelated expired/live B references, no payload access after last strong release, and17 post-mutation fault boundaries pass. This is method-level fixture evidence, not native teardown, complete B recovery, or full lifetime/admission proof.

The in-operation journal occupies bounded inline storage within the existing32MiB execution envelope and512MiB total accounting. No historical renderer image or GPU readback is added. Private-B storage is revalidated before recovery retirement. Historical nonempty child publication remains unsupported. Live native preflight and C11000 cancellation/recovery are next; a surviving unknown owner or unsupported native domain must reject before teardown.

## Native recovery result

Current C11000 trace recovery checkpoint (2026-09-12): schema91 runtime1c145096/observer2609e73c candidate113abc9b32ec4a43b0b76010328d3395 PASSES deliberate late settlement failure, native retirement of2 C-only traces and10 particles, exact surviving-manager membership, fresh render/GPU completion, complete B2504 recovery and120 independent ticks/480callbacks/Lux poses plus10 inactive HUD observations against nativecc297d14c24f4c88bf835b30647feb8e. Both cleanup complete/zero games. trace-retirement-complete-B-stage.json links retained candidate/native reports, raw logs and reconstructible sources. Earlier streaming-signature and owner-coverage failures retained; native RET0 checked exactly, sibling lifecycle is not invoked by exact-pointer removal. Local actual-method/wrapper fixtures cover17 fault boundaries and sibling-loss/stale-member rejection;152 Python tests/both C++ suites/native physics pass. Capture115789us, A publication including B154047us,339811874 conservative bytes; C11000 readiness28314082us is NOT completed-seek or rollback cost. Zero diagnostic GPU maps; desktop visual coherence and changed-input coverage not newly proven. Full indexed late-target completion/checkpoint coverage/performance and full seeker remain unfinished.




Native exact-pointer component removal is retained in `artifacts/replay-live-recovery-20260911/trace-manager-removal-native-20260912.json`. The complete pre/post manager-member identity witness replaces a class allowlist, without restoring the manager set. It occupies at most8448 stack bytes and adds no persistent checkpoint/GPU allocation. Native actual recovery passed; missing desktop review remains explicit.
