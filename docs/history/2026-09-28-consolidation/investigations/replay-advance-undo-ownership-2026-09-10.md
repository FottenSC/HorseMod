> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Actual recovery controls demonstrated (2026-09-10)

Current late-recovery UI checkpoint (2026-09-10): runtime0f361ffc51222a14ae89f9018144efd48ab1c4b014f0bd4b1f0ee2581a780f6f / observercb5898f0f3c53b25b7f387b976fec75ce0acd6cc3500db672a32a06ea540b21f passed actual Windows Cancel at failed C214 after all ten CPU settlements, B220 unchanged hold, actual Windows Resume through host dispatch, then120 independently matching ticks/480 callbacks/Lux poses/HUD. Candidate3ad597af455d496786d98f269da5f0cc/control208df79bf8824c9d827a5f605d101757 cleaned up. Progress observers no longer disable host Resume; read-only recovery monitor retires before dispatch. B retained through reopening/teardown; no early commit or diagnostic GPU readbacks.120 ticks/frames in1994539us; resumed60TPS is not rollback latency. Both C++ tests/recovery contract and146 Python tests pass separately. seek-late-recovery-ui-audit.json links sealed reports/logs/source archives. Earlier API late-recovery, Step208->209, repeated holds and automatic fallback retain exact scopes below. Full index/checkpoint materialization, arbitrary seek/rounds, general lifecycle, full controls, strict sentinel, corpus and performance remain unfinished.

# Late settlement recovery demonstrated (2026-09-10)

Current late-settlement recovery checkpoint (2026-09-10): runtime115bd174bccdd82a40c2e14a76141cbe7262fe8a3f73c49a48e6e643ce55d9d9 / observer43bbe046e6caadd05f0ef41ee068a4e9599740b8254344396aa1f6f0ef280eb7 passed A170/B220->C214, all ten CPU owners settled, deliberate failure before commit, coherent CPU/render reopening, eight C-only particles retired, complete B220 undo and120 independent matching ticks/480 callbacks/native Lux poses/HUD. Candidatecce2cd6a341f45b19c39212f6f13c2fa/control60092e1a39b5449887ea4dff28da5e08 cleaned up.44 discarded ticks/intervals; no commit;353811866 conservative bytes; zero GPU diagnostic maps. Exact-hold readiness406902us; cancellation-to-B recovery109130us, separately measured; not practical rollback qualification. Reopening retains B and reacquires C after native teardown; trace/HUD C leases explicitly retire before recapture, frozen B scheduler ticks validate original epoch without global rewind, and observer B witness is separate from C hold scratch. Both C++ tests/recovery contract and145 Python tests pass separately. seek-late-settlement-audit.json links sealed reports/raw logs/reconstructible source; three preceding failures remain retained. Earlier UI Step208->209, repeated208/214 visual holds and expired205->170 automatic fallback remain identity-bound in their audits. Full index/materialization/arbitrary seek, remaining UI/lifecycle/strict sentinel/corpus/performance are unfinished.

Historical checkpoints below retain their individual identities and scopes.

# Current assembled seek checkpoint (2026-09-10)

Current retained-B UI Step checkpoint (2026-09-10): runtimeefe2c3481361ef213e1f947747ee27790c9fa22d97b730cbe085ba3a74ec8b0f / observered54b5a03678216329c0da2f347d7e508246045b7a5a7a867400b84499963807 passed actual Windows SendInput Step208->209 during CSettled with B220 retained, target tails completed209 before commit, then120 independently matching ticks/native Lux poses and24 active HUD updates. Candidate8652acb5826c43cd8c797bd2fed22545/controlf883d52863b74cb69bb544a1b44c034b cleaned up. Initial seek383902us; step16299us; capture120724us;353811866 owned bytes; zero diagnostic GPU maps. Not practical rollback performance or held209 visual proof. Both C++ tests/recovery contract and144 Python tests pass separately. Retained seek-retained-B-step-audit.json points to sealed raw logs/reports/reconstructible source. Earlier repeated208/214 visual coherence and automatic expired205->170 fallback remain tied to their identities in seek-retained-B-repeat/fallback audits. Post-advance B recovery passed separately. Late partial-CPU-settlement recovery, full index/materialization/arbitrary seek, remaining UI/lifecycle/strict sentinel/corpus/performance remain unfinished.

Successful retained-B commit, corrected-input continuation, repeated seeks with two inspected holds, and automatic fallback are now demonstrated in their retained bounded audits. The previous statement that commit remained unproven is historical. Exact-hold readiness excludes subsequent target completion/commit; resumed 60 TPS is separate. Late settlement recovery must reopen C metadata coherently before C-only teardown, while retaining complete B.

Historical checkpoints follow.

# Current assembled recovery checkpoint (2026-09-10)

Post-advance failure recovery now passes for A170 -> B220 -> A170 -> external C208 during target214. Cancellation completed C tails, retired seven C-only particles, recovered B220 and independently reproduced 120 ticks, 480 callbacks, native Lux poses and ten active HUD updates. Both processes cleaned up. `seek-retained-B-advance-recovery-audit.json` links retained reports/logs and exact reconstructible source provenance. No GPU diagnostic maps; resumed 60 TPS is not rollback latency evidence.

Successful commit remains unproven on this ownership implementation. Current integration validates retained B HUD owners separately from actual C backing/values and waits through target application/render completion before release. Both C++ tests and the controlled seek recovery contract pass; this is build evidence only. The next bounded live hypothesis is that C214 can settle all existing participants, retain B through target tails, then commit and independently reproduce 120 ticks. No new participants or pixel-equality requirements are introduced.

Historical checkpoints below retain their original scope and are superseded by this current status.

# B ownership through seek resimulation

The host recovery defect remains open. `Sc6ReplayHost.Seek.inl` commits and
releases the historical transaction before `AdvanceToTick`. The controlled
production-orchestration regression still fails after traversal 206: one commit,
no B owner, cancellation rejected. Publication-cancellation passes cannot cover
that transition. No live experiment was launched during this checkpoint.

## Implemented prerequisites, not an assembled recovery

| Owner | Current change | Evidence and limits |
|---|---|---|
| Semantic VFX handler | `Prepared::BeginExecution` relinquishes A allocation addresses while retaining B; settlement reads actual C and rejects overlap with B. Undo retires C only after verified B publication. Scratch and failed reads remain charged. | Local graph fixtures exercise reallocation, aliasing and damaged B. Prepared native ownership methods are not live-tested. |
| Timer/world values | `PreparedRestore::BeginExecution/SettleExecution` retain B arrays/delegates and adopt current C arrays for eventual retirement. Checks include callback ownership, independent allocations, capacity, manager/camera bindings, and an idle executing-timer record. | Build evidence only. Native `142187B20` rechecked: `+110` is equality with the physical engine epoch. B's admission relation is rebased at settlement; the global counter is never written. Render work remains a separate blocker. |
| Stage wind | Begin relinquishes A addresses; settlement validates the actual native linked list and rejects overlap with B. Partial B root publication remains retryable without freeing either graph. | Production transaction with controlled native memory: replace/free A node, settle changed C, reject B alias, interrupt B publication, retry, verify B image and allocation identity, retire C exactly once. Not native combat evidence. |
| Traces | Preparation now clones every dynamic array/map, including identical A/B containers. Execution handoff retains B references; settlement requires a caller-retained current trace image, exact current headers, fixed strong-pool membership, disjoint allocations and accounted weak references. | Build evidence only. All-container cloning changes the existing publication path and has not inherited previous live cancellation evidence. |
| CPU/GPU emitter storage | `Prepared::BeginExecution/SettleExecution` retain displaced B while replacing old A allocation metadata with the captured current C graph. Component/slot, module/LOD, full C graph and B payload validation remain required, with C/B and internal allocation overlap rejection. GPU retains its private header staging and existing in-place root undo. An originally null CPU B slot is supported separately from GPU. | Build evidence only; no native execution-handoff experiment. Enclosing emitter sets still need to call these methods, and common-owner completion must be handled before admission. |

These entry points do **not** grant execution permission. The host still uses
the existing held transaction. No `historical_restore_`, task, lifetime or
`blocks_resume` guard was removed. A settlement failure retains ownership and
does not authorize either retirement or resumed gameplay.

## Remaining integration blockers

| Owner or transition | Known defect in admitting advance today | Required closure |
|---|---|---|
| CPU/GPU emitter integration | Commit still destroys displaced B emitters/arrays in the current host. New execution methods are not wired through the enclosing emitter set. | Admit every prepared emitter together, account for recovery scratch, settle from current C, then use existing B undo/final retirement. Verify no stale A allocation is freed; preserve supported module/LOD and component lifetime checks. |
| Particle lifecycle | Commit destroys quarantined B-only components. Native completion can also destroy common owners: `141F6FF20` dispatches completion before deactivation/autodestroy; `141F7DA40` retaining emitter objects does not prevent subsequent component destruction. | Keep B-only roots excluded and alive through advance. Account for C-only births and common-owner retirement before permitting recovery. An UObject lease alone does not solve explicit native destruction. |
| Scheduler | Patches, prerequisite allocations and published fingerprints assume A's original physical epoch and storage. | Adopt current backing safely, retire C-only registrations with their owning participants, restore B's membership and admission predicates at the current physical epoch. No global epoch rewind or dropped task tails. |
| VFX manager and tile pools | Manager providers/arrays and pool checks assume the published A image. Native resimulation changes slots, counts and allocation ownership. | Preserve private B providers and buffers; validate/retire actual C storage, restore B's pool and component ownership coherently. |
| Render work and particle copy | The transaction's GPU/history/registry state deliberately blocks resume; dirty work and excluded birth registration currently finish at commit. | Separate execution admission from B retirement. Retain B resources and completion events through native writes; resolve C render publication and registry ownership before GPU undo. Timeout remains pending work, not cancellation. |
| Host orchestration | Seek commits/releases before advancing; failed advance has no undo owner and `commit_decided` rejects cancellation. | Only after all owners admit advance: keep B through resimulation and target readiness; settle pending work on failure/cancel, recover B, then release. Commit B retirement only after successful exact target readiness. |

The next assembled proof must inject failure **after actual resimulation work**,
finish pending application/render work under explicit ownership, recover the
original B coordinate, then independently match B+120. It must also cancel during
advance and verify that any ticks needed to finish C's pending interval do not
change the saved B recovery coordinate. Expected observations never become
simulation inputs. Changed-input continuations remain required.

Build and local evidence are retained under `replay-tests`:
`seek-execution-owners-build.json` and
`seek-execution-owners-recovery-gap.json`. Each references its retained raw logs
and hashes. The build passes both C++ selftests; the required host recovery
contract fails. Neither result is a completed seek or rollback-performance claim.

The later emitter handoff build is retained separately as
`seek-emitter-execution-build.json`, with its own runtime/observer identities,
test executable hashes and raw logs. It does not replace or upgrade the retained
failing host recovery experiment.

## Native particle quarantine audit at this checkpoint

Simply postponing `ParticleBirthSet::RetireAfterCommit` is not a complete
quarantine proof. `CaptureReplayVfx` enumerates registered particle components
using component `+188 bit0`. Scheduler exclusion clears the tick's registration
bit; it does not itself establish that the component's render proxy and other
consumers are dormant. A current-C inventory must distinguish explicitly retained
B owners without silently omitting live scene work.

Native MCP reinspection confirmed the destruction chain and signatures:

- `1408CECF0(component, bool)` returns void, clears Lux material roots and timing,
  then calls `141D99730` with the same bool.
- `141D99730(component, bool)` returns void; false bypasses child promotion, then
  calls `141D41970`.
- `141D41970(component, bool)` sets destruction-in-progress, invokes lifecycle
  callbacks, unregisters, removes owner membership, invokes destruction, and
  finally sets GUObjectArray pending-destruction `0x20000000`.
- `141D43340(component)` dispatches the physics, render and unregister virtuals
  selected by component `+188 bits2/1/0`.
- The Lux vtable base `14335DB28` is cross-checked by both destroy slot `+360`
  (`1408CECF0`) and tick slot `+300` (`1408D8A10`). Its `+288` points to
  `141F7B2F0`; this explicitly calls `FinalizeParticleEmitterInstances(..., true)`.
  Thus generic native unregistration **destroys emitter storage** and cannot be
  used as a harmless B-preserving detach.
- Its render-destruction slot `+2B0` is `141F71FD0`; it may finalize emitters when
  template-state bit `0x80` is set, before inherited render teardown. Render
  teardown cannot be assumed independent of emitter lifecycle either.

No pending-kill flag is cleared and no unregister/destroy hook was added. The
remaining particle admission must establish actual B-only producer/render
exclusion while retaining its owners, and account separately for common-owner
destruction and C-only births. This is the next native ownership issue to close,
not a reason to repeat a publication-cancellation campaign.

### Current execution-batch checkpoint

`PreparedEmitterSet` now exposes the child execution handoff and settlement.
Admission validates all displaced B graphs before handing off any A addresses,
reserves per-emitter retirement allowance, and retains the original image after
a partial admission. Settlement checks C against **all** B emitter allocations
and other C emitters, including roots; only the common GPU emitter's explicitly
shared root is permitted to overlap. Backing overlap still rejects. An undo
cannot begin until every admitted child has settled and B has been revalidated;
partial undo retries retain their progress. This is not host execution admission.

Build/runtime `221785308b89346d979a0d3bae290e57e68085979da9df8ed75747910d379d40`
and observer `246343776c680a334ac65fb4d7086ff9f53631e73810e02d6bc4f6ae3534e13d`
pass compilation and the two existing C++ selftests. Retained
`seek-emitter-batch-execution-build.json` names and hashes its retained logs.
No new native experiment ran, and these tests do not exercise the new native
emitter handoff. The host recovery defect remains open.

### Native view exclusion and remaining GPU-registry ownership

Read-only MCP inspection establishes a narrower alternative to unregistering B:

- `1414925B0` copies the view's set at `+8A8` through `141495B30`; the renderer
  constructor `1414DBA90` owns this copied view, not the source game-thread view.
- `1414A4050` adds hidden components' `+420` primitive IDs to that set. The actual
  insertion signature at `141487EE0` is `(set, int* result, uint32_t* key,
  byte* already_present) -> int*`, with the final output optional.
- `1414E5650` finds scene primitive IDs in `view+8A8` and clears their visibility
  bits. `1415107F0` also checks this set before invoking primitive relevance.
  The optional set at `+8F8`, gated by `+948`, has opposite inclusion semantics.

These observations support investigating native per-view exclusion without
component destruction; **no view-filter mutation or new hook is implemented**.
They do not establish complete producer exclusion, shadow/lighting behavior or
all feedback into simulation. Component `+674` already belongs to the retained
VFX image because last-render time affects particle admission. B-only scheduler,
dirty-work, attachment and render ownership still require an assembled proof.

The existing GPU registry exclusion is also insufficient for advance:
`ExcludeBirthRegistration` removes entries from the same native backing whose
addresses `BirthBinding` later requires for undo. C births may reuse or grow that
backing. Required next ownership change: retain B's native registry allocations,
give native execution separate registry backing, and settle actual C before
restoring B's registry and render indices. Merely accepting changed pointers or
copying B bytes through the new C header would weaken the recovery guarantee.

The subsequent build adds value-only ownership negatives covering cross-emitter
backing/root alias, explicit shared GPU roots with disjoint backing, incorrect
CPU/GPU root exceptions, overflow and cyclic buffer parents. Both C++ selftests
pass on runtime `9d39e42d5977d4c66db75cf18c61d8ded397ca2f86b4d025b4d0284ee4d9c180`
and observer `3c6d516b47f4bc4120eff99cd1e93d5a61b4d06dd144849e3f0903338999c556`.
`seek-emitter-batch-execution-tested-build.json` retains this evidence separately.
This still does not exercise native execution handoff or fix host recovery.

### Interior target decision versus storage retirement

An exact target may be externally held with pending simulation/world work.
Current owner settlement APIs require a completed application boundary. Therefore
the host cannot settle all C storage immediately at every successful target, nor
complete extra target ticks merely to retire B. The success decision must follow
exact target readiness; B storage can remain retained until an admitted retirement
boundary. Failure/cancellation before that decision instead completes C's pending
work, settles ownership and restores the **saved B coordinate**. Its completed C
tick must never overwrite the recovery coordinate. Host release/repeated-seek
handling must retain any deferred B owner just as it retains other in-flight
native resources. These transitions remain unimplemented and require the host
contract tests and bounded native failure experiment.

### Registry backing transaction integrated; execution still gated

`ReplaySparseRegistryStorage` now owns the native registry's slot and heap-flag
allocations. `Sc6ReplayParticleCopy` prepares a clone before publication, retains
the original B backing, performs native exclusion on the clone, and restores the
original allocation identities before restoring B's render indices. Both normal
and coordinate-rebuild commit paths retire the original registry backing only at
the enclosing commit. Finish refuses a published registry owner; unpublished
preparation is explicitly released. Native and metadata bytes remain charged.

The storage owner's execution handoff discards stale A allocation addresses.
Settlement validates actual C's sparse free chain, capacity, allocation ownership
and disjointness from B, then independently rechecks all retained B backing bytes.
An interrupted publication may recover B with incomplete A free links only while
the allocated backing bindings remain unchanged. This exception is unavailable
after execution handoff. The enclosing particle-copy execution path does **not**
yet call these execution methods; its epoch, component and resume guards remain.

Production-storage fixtures exercise native-style exclusion, replacing/freeing A
backing, C registration and inline-to-heap flags, C/B aliases, corrupted B,
write-protected undo and retry, and interrupted exclusion. Recovery verifies B's
actual original header, allocation identity and bytes; retirement frees actual C
once and never stale A. The commit fixture instead retires B and preserves C.
These are local ownership tests, not native gameplay or host recovery proof.

Ghidra MCP readback reconfirmed `1414F5500(registry, first, count) -> void` writes
free links and allocation flags without allocation calls. The cloned storage uses
the existing verified `1404A61C0(size_t) -> void*`, `140D46A00(void*) -> void` and
`140D50DC0(size_t, uint32_t) -> size_t` allocator routes. No Ghidra mutation or
new diagnostic GPU readback was introduced.

Build and both C++ selftests pass on runtime
`3ab14c2c1748a39d832ce18ebce9a4e99335d895d728981b322c168424ebc15e`, observer
`508b597e7a181260fdd0f6bd4deaa4eee0b6d2ae76f6d30ffc8fa0f11272c6d3`, framework
`f5fe7c823d208a4b407106d3e0f47904464d3b0703a654901d1ff9efa02c8023`.
`seek-registry-ownership-build.json` names and hashes its retained raw build and
selftest logs. No live run occurred; earlier publication/undo passes cannot be
inherited by this changed publication path. The host still commits before advance.
Scheduler, particle lifecycle and other render ownership remain required before
the bounded post-advance failure and B+120 experiment.

### Scheduler execution ownership implemented, not host-admitted

The scheduler now has `BeginExecution` and `SettleExecution` paths. Begin checks
published A and B's original private payloads before relinquishing A allocation
addresses. B level sets, prerequisites and patches stay retained. Settlement
requires completed tasks/world work, unchanged level/tick owner identities,
retained B births still excluded, and no remaining C-only tick owners. The caller
must retain the current C image and explicitly include dormant B ticks in it.
No membership exception silently admits an extra/replaced tick.

Current C set/prerequisite allocations are checked against B, each other, tick
headers and the quarantined B ticks' prerequisite backing. Scratch/native storage
must fit the reserved allowance. Only after complete validation are the current C
allocations adopted for retirement. B undo headers retain original allocation
identities; their two epoch stamps preserve B's captured admission predicates at
C's physical epoch. When the epochs are equal the original stamp bytes remain
unchanged. Undo verifies every native destination and retained B payload before
C may be reclaimed, including retry after a partial write. A container-order
reconstruction is explicitly rejected after execution handoff.

Particle birth tokens now expose a lifetime-only check for this completed-C
settlement. The existing `ValidateOwners` still additionally requires the original
epoch; its callers and hold semantics were not relaxed. The new lifetime method
does not grant execution permission or prove that B producers remain dormant.

Native `142163BA0` was rechecked through Ghidra MCP: both `+10` and `+14` stamps
are sign-extended before full-epoch equality tests. Its prerequisite-removal path
can mutate/shrink owning storage, confirming why old A addresses cannot be freed
after C execution. The global engine epoch is never restored.

The current build and two existing C++ selftests pass; exact identities and raw
logs are retained in `seek-scheduler-execution-build.json`. This is compilation
and regression evidence only. There is no scheduler execution fixture or native
post-advance B recovery proof, and the host does not call these new methods yet.
VFX manager/pool and render ownership, B quarantine/C-only lifecycle handling,
then host advance/recovery/retirement transitions remain required for assembly.

### Manager and tile-pool execution handoff (integration pending)

PreparedManager now retains the complete native B arrays, map backing and request
providers while native code owns A. Begin validates and fingerprints B, reserves
retirement storage, and discards A addresses. Settlement requires independently
captured quiescent C with the same surviving A component identities; it rejects
C-only or missing components until the enclosing lifecycle owner handles them.
It adopts actual C arrays/map/providers only after checking C/B, C/C and native
header overlaps, current payload/bindings, full retained B bytes and budget.
Undo restores B headers/component values and retires actual C; commit verifies B
before retirement. Unsettled execution cannot undo or commit. This has build
coverage only, no dedicated native manager handoff proof.

Tile pools use fixed native inline storage, not replaceable allocations. Begin
reserves maximum-size C free prefixes before admission. Settlement validates the
actual C emitter tile partition and pool identities, keeps the B prefix unchanged,
and records the current physical epoch without writing the engine clock. It
updates only the expected C prefix/owner mask; B recovery uses the existing guarded
native prefix writer and verification. Host calls are not wired yet.

Ghidra MCP readback confirms 1403ADD60(destination, source) returns destination and
clones the request provider. 1403A22D0(providerStorage) is a one-argument void
single-element destructor; it invokes the provider virtual destructor, releases
heap storage through Realloc(...,0,0), and clears count. Existing signatures agree.

Build and two existing C++ selftests pass on the exact identities in
seek-manager-pool-execution-build.json; retained raw logs are named and hashed.
The first build caught use of size() on the slot unique_ptr; corrected to the
captured constructed slot count before the retained passing build. No live run.

Remaining render dependencies are concrete: lighting's installed arrays and
point allocations may be replaced by C; visibility currently writes into original
native backing and retains B query references; reflection/material publications
and GPU coordinate buffers assume frozen A. These need execution ownership or
verified native reconstruction, not disabled equality guards. B particle producer
and render exclusion plus C-only destruction remain unassembled. Host commit is
still before AdvanceToTick, and its required recovery contract is unresolved.

### Render execution preparation, not host admission

Lighting now has guarded private Begin/Settle methods. Begin verifies detached B
maps/point allocations, then hands installed A addresses to native code. Settlement
captures actual C metadata and uniform leases, requires the same surviving native
primitive/proxy/slot set as B after C-only teardown, rejects C/B and C/C allocation
aliases, then adopts C for the existing undo/retirement writers. A failed partial
C capture remains retained. Unsettled execution cannot publish or finish. These
methods have build evidence only and are not called by the host.

GPU coordinate reconstruction now retains both A and B tile-input spans from their
leased images. A reversible preparation path keeps B pending across the native
141F95B10 rebuild and its GPU completion. Undo can regenerate B coordinates from
B tile inputs, validate native bindings/counts, and wait for completion before the
other GPU owners restore. Current C buffer count is only a binding observation;
it never supplies B tile inputs. Native 141F95B10 takes one packet pointer, releases
and initializes the two render-resource buffer owners, fills them from tile inputs,
and retains/releases the compiled resource. Submission is not completion. The
legacy commit path remains separate. This new execution preparation is not called
by the host and has no live or dedicated GPU fault-path proof.

Retained seek-render-execution-preparation-build.json binds the passing build and
two existing C++ selftests to runtime/observer/framework and raw logs. No live run.
The next render ownership work is visibility backing/query-reference transfer and
C settlement, then aggregate particle-copy execution admission, B quarantine and
C-only lifecycle teardown. Reflection/material bindings and world render tails
also need explicit checks before host ownership can change. Post-advance failure
recovery, exact-target deferred retirement and the original full seeker are still
unfinished.

### Visibility ownership integrated; quarantine and world handoff not admitted

Visibility publication now allocates separate A backing while retaining B's
original native map backing and displaced native query references. Existing
historical-key membership, resource/refcount, sampling-table and view admission
checks remain. Captured A allocation addresses are no longer mistaken for the
current native B bindings: the owned clone supplies A's actual pointers. Undo
restores original B backing before retiring A query ownership; commit frees B
backing and transfers A. Both native allocations are charged. Dirty flags precede
query-reference acquisition so an interrupted acquisition remains recoverable.
This changes ordinary publication and needs new live qualification.

The private visibility execution handoff transfers A backing/query references to
native ownership. Settlement captures and leases actual C, verifies its map and
material bindings, checks C/B and C/C allocation disjointness, verifies private B,
and adopts C for normal undo/retirement. The moved-from image's scalar lease count
and raw uniform pointers are explicitly cleared; a vector move alone duplicated
ownership. Lighting's moved-from capture marker is also cleared. These paths have
no host calls or dedicated native execution proof yet.

NativeReplayRendering now accepts a bounded list of retained B primitive IDs and
adds them to each owned view's native HiddenPrimitives set through verified
141487EE0(set, resultIndex, id, alreadyPresent) -> resultIndex. Native signature
bytes were checked via Ghidra. It does not change VFX slot IDs, native component
registration, or emitter flags. Installation/removal requires no pending renderer.
An 8 MiB conservative reservation is charged while enabled; shadow/subview growth
still needs bounded live verification before treating this as a total memory proof.
The filter is not enabled by the host. Native hidden-set consumers 1414E5650 and
1415107F0 support visibility/relevance exclusion; complete last-render-time and
other producer inactivity remain unproven until the bounded experiment.

ParticleBirth::ValidateQuarantined checks lifetime, disabled primary tick,
unattached registered ownership, all captured component spans, CPU/GPU emitter
graphs, and excluded GPU registry index. CaptureReplayVfx can omit only this
explicitly validated B set, requires every excluded owner to appear in the native
registered-object scan, and rechecks quarantine after enumeration. Ordinary
capture remains unchanged. C-only owners are not excluded by this mechanism.

World end-frame queue storage now has Begin/Settle handoff. Begin retains private
B queues while handing A to native execution. Settlement requires actual empty C
queues, unchanged B fingerprints, disjoint current allocations and budget; it
adopts current storage and keeps existing component/creation-membership guards.
These methods are not host-integrated or live-proven. Native141EF9EA0 confirms
queue insertion and component membership bits under the world critical section;
actual queue drain/settlement still needs native proof.

Exact current build/two existing C++ test identities and logs are retained in
seek-render-ownership-integration-build.json. No live campaign ran. Remaining:
aggregate render execution admission and reflection/current registry settlement;
world/physics repair ordering; B quarantine integration and C-only lifecycle;
host retained-B advance/cancel/failure/deferred retirement transitions; then the
bounded post-native-advance failure and independent B+120 recovery. Existing
publication-only cancellation evidence does not prove any of this.


## Aggregate render handoff checkpoint (build only)

Current render resimulation handoff checkpoint (2026-09-10): runtimef944d132f988f780ec8c2f0b49be6c42d325ebe929d5db78611076b5d9f227cd / observereaff2b3af6cf8e0340e9f28ae6b5f96b4682f64b2edacb382d1c317ba2304b23 passes build/two existing C++ regression tests. Aggregate render BeginExecution/SettleExecutionForUndo now preserves private B while adopting actual C registry/lighting/visibility backing at a completed boundary. C reflection history is leased for safe undo without new pixel snapshots. C-only retirement uses native provider compaction/component teardown and preserves C tile returns, distinct from B retirement. These paths are NOT host-admitted or live-qualified. Host STILL commits/releases B before AdvanceToTick; the requested post-advance recovery remains unresolved. Wind handoff wrappers and a timer-image count validation guard are also build-only. Retained seek-render-resimulation-handoff-build.json points to exact logs/binaries. No live campaign ran. Next is CPU/physics/quarantine admission and settlement, host commit/cancellation transitions, and bounded actual-advance failure with independent B+120 recovery. Full rewrite unfinished; preserve all ownership guards and strict entrypoint.

Partial render admission remains uncommitted. RestoreUndo rejects an execution handoff until actual-C settlement succeeds. CommitInstalled deliberately rejects the new execution state until success retirement is integrated; recovery settlement must not destroy C-only births on successful seeks. Registry preparation also supports a no-B-birth transaction without fabricating a birth witness. The admitted render callback is still required; no host execution guard has been relaxed. Reflection C leases validate native pooled-target type, actual resource/device/descriptor and shared in-place destination; they add no image readbacks. World held-image validation now rejects missing/additional timer records before indexing retained storage. The remaining integration must reserve recovery scratch before native C work, complete C-only native/render teardown, rebase only scheduler/timer admission relations, and verify B at the current physical epoch while keeping B simulation coordinates. No global engine counter writes are authorized.


## Seek-wide ownership contract and reconstructible source retention

The authoritative integration state is `ReplaySeekOwnership`, not the UI witness or independent commit/resume flags. Participant journals describe reversible allocation handoffs; they cannot grant execution or irreversible retirement.

| Ownership state | Required evidence before entry | B disposition |
| --- | --- | --- |
| BRetained | Complete original CPU/source/scheduler/physics and completed GPU undo | Private undo retained |
| APublished | A publication and held presentation complete; every participant validates | Private undo retained |
| ExecutionActive | Physics query/transform preparation, all CPU/render handoffs and B producer quarantine admitted within budget | Private undo retained; native owns evolving C |
| CSettled | Exact requested simulation traversal and callbacks complete | Private undo retained; interior application/render tails may still exist |
| TargetTailsCompleted | Native task, world, application and render tails complete, actual C allocation ownership settled | Private undo retained |
| Committing | Explicit success decision after all preceding states and C validation | Irreversible retirement begins; cancellation cannot pretend B still exists |
| Committed | Every retirement and GPU completion acknowledged | B released |

An exact interior hold is CSettled, not permission to commit. If its remaining native interval contains additional traversals, those tails must remain suspended while the user holds the exact target. They can complete on resume or a subsequent request; their actual completion tick is recorded separately from the target. Cancellation/failure before Committing enters RecoveryQuiescing, completes actual pending C work without pretending its tick is B, settles C ownership, and restores the saved original B coordinate before Recovered. Failure after the irreversible decision is terminal recovery failure, never a successful cancellation.

The production ownership class now rejects skipped transitions, early commit, recovery before quiescence, and recovery to the wrong coordinate. Its unit coverage is not assembled host or live proof. Host integration remains incomplete. Provisional execution flags have been replaced by explicit ownership, render, quarantine and capture states plus per-participant progress journals.

Source retention now writes actual tracked/dirty/untracked file contents, deleted-file records, generated inputs, fetched dependency inputs, configured Ninja recipes and build flags into a content-addressed archive. It checks membership/bytes again before publishing, validates the archive, and binds candidate DLLs and both C++ test executables to it through build-provenance.json. New live captures verify that binding before deployment. Installed compiler/SDK remain toolchain provenance, not workspace source. The first independently checked source-only archive is source-18c82b8def015e08969c131d519e8ffdb02e7d04d5f117d762a341d8be4284aa.zip (16,859 entries;114,368,628 uncompressed bytes); it contains actual GameImGui replay code, RE-UE4SS hooks, replay_test.py and replay_seek_state_selftest.inl. It does not retroactively bind older binaries or qualify the unfinished host integration.


## Connected ownership checkpoint — 2026-09-10

Runtime `d102518a4ca0c10a6ecb3b837bc00cff503bc77ee4545b649438a34d501e735a`, observer `1afb86fae6e54b3793b5898e4b8ad294ab104b7b19538bd8e91ab09c79bb6e1e`: build and both C++ tests pass. The production Seek.inl controlled-owner failure contract now reaches an advance without committing B, then admits cancellation after a completed traversal. This supersedes the earlier source-only status above; it is not a live native C/B recovery result. The retained report `seek-owned-execution-integration-checkpoint.json` points to a content-addressed raw contract log and the reconstructible archive.

BRetained is created after complete preparation. APublished follows completed CPU/GPU/display publication. Seek requests reversible handoff and waits for ExecutionActive before AdvanceToTick; it no longer commits/releases B first. Exact traversal readiness is CSettled. Release requests CompletingTargetTails, which can complete more ticks while preserving the original target coordinate; TargetTailsCompleted requires CPU/render settlement before the irreversible decision. Cancellation quiesces actual C and attempts recovery to the saved B tick, not the request's earlier interior origin. B-only retirement uses full quarantine/lifetime checks rather than resetting the global epoch.

The runtime path is connected, but no live run is authorized by these unit/build results. Integration work still required before the bounded attempt: update the observer's early-commit assumptions, add a deliberate interior advance failure, verify the assembled render/CPU handoffs and B physics/query recovery, and handle partial C settlement safely. A failure after some CPU participants adopt C currently rejects recovery rather than freeing potentially live C owners; this remains unfinished lifecycle handling. C-only births survive successful settlement; recovery removes only validated C-only owners, drains ordered render/GPU work, and then settles undo storage. No expected observations enter simulation.

Source retention no longer filters nonignored production/test inputs by filename extension or silently drops unknown external dependencies. The current archive contains 17,038 entries / 122,900,941 uncompressed bytes. It retains every configured consumed header and prebuilt input, including external SDK headers; only the installed compiler remains an external toolchain requirement. Before deployment, retained bytes (including generated/fetched/external inputs) and workspace membership must still match the build. The four user-named input categories were compared byte-for-byte with the archive. Fifty-four focused Python tests pass.


## First native retained-B execution attempt

Current retained-B execution checkpoint (2026-09-10): source manifests now retain actual tracked, dirty, untracked, generated and external consumed inputs in reconstructible content-addressed archives; deployment validates retained bytes and exact binaries. Production ownership transitions are connected and both C++ selftests plus the controlled advance-failure contract pass. Native run replay-fef45aae834148a8b461d299e09bd9bd reached A170 -> external C208 during target214, retained complete B220, accepted deliberate advance-failure cancellation, completed C tails and retired seven C-only particles. It then REJECTED changed lighting proxy id835 before B publication; B recovery/120 continuation remain UNPROVEN. Runtime ab767372a4b3301eaaae7ea33df12178d9c6a0df56ac2e06c05a7010e0bfd67e / observer c115d06f1ae0fe1c2968d40e77bc8b78ef483a0c540144ec30db7048f1109a9a. Both failed runs have complete owned-process/deployment cleanup; no independent control was launched. Retained seek-retained-B-C-proxy-failure.json and seek-retained-B-render-admission-failure.json point to their retained raw logs and source archives. Native empty hidden-set initialization fixed the earlier tick171 rejection. Current build adds bounded replaced-proxy inventory and stops repeated failed recovery commands; diagnostic retry is running, not proof. Original full seeker remains unfinished; keep complete B undo, strict seek, 512MiB and optional pixel diagnostics.

The empty hidden-set correction is grounded in 1411C5C10: a zero bucket count is initialized after the first insertion by 141487EE0. The next attempt passed that boundary. A changed component proxy remains a strict rejection: old proxy addresses cannot be followed merely because the component UObject is leased. The bounded inventory identifies all replaced owners at the actual settlement boundary; it does not rebind them or qualify their reconstruction.
