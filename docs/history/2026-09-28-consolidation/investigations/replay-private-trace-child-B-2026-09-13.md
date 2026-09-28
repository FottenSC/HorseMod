> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Private B trace-child ownership

The 1 GiB later-target sequence captured four checkpoints, but the two later
placements lost native particle owners. The first seek (2511 to 5574) completed;
the next request, from B5695 to 6417, rejected before A publication. See
`one-gib-late-scheduler-membership-failure-audit.json` in the retained replay-test
directory. No independent control or recovery of that failed request is claimed.

## Proven constraints

- Scheduler preflight sees A296/B310 registrations and 13 particle births. The
  remaining registration belongs to a newly created trace-child skeletal mesh.
- The trace image already captures that child's values, bindings, private array
  contents and UObject leases. `PrepareStorage` separately rejects nonempty
  historical child strong-reference arrays. Relaxing scheduler counts alone
  would conceal a second unsupported ownership boundary.
- Native controller vtable `143362590` has payload destructor `1408CED30` and
  deleting destructor `1408CD060`. Rechecked through Ghidra: the payload destructor
  releases the cached parent weak reference and three heap arrays. It does not
  destroy the actor/attachment or operate the VFX slot. Explicit actor and
  attachment retirement remains necessary.
- Existing C-child retirement verifies native signatures, world/streaming and
  callback domains, component membership and VFX attachment coverage. It calls
  native weak removal, actor destruction, attachment destruction and strong-array
  removal. Its current membership checks require the *published* child array;
  it cannot be reused against displaced B storage without an ownership contract.
- Host irreversible retirement is followed by ordered render/GPU retirement.
  B must remain recoverable through target application/render settlement before
  that irreversible decision.

## Bounded integration work, not permission to relax admission

1. Derive B-only child membership from existing A/B images and the actual native
   strong array. Require exact controller identity/count, parent ownership,
   nonaliased payloads, actor/component lifetimes and existing retirement-domain
   checks. Initially historical A child pools remain unsupported.
2. Preserve B's strong-array allocation and its reference ownership privately.
   Freeze the B-only child by excluding only its proven scheduler registration
   and quarantining its primitive through existing render ownership. Prove that
   captured B-only values do not change during A/C execution.
3. Undo must republish B's arrays, values, registration and rendering without
   reconstructing a destroyed controller or importing expected observations.
   The ownership proof must outlive trace-array undo because scheduler undo runs
   afterward.
4. Commit needs native retirement against *private* B storage, not temporary
   substitution of the live C arrays. Account for parent weak-reference release,
   last-strong destruction, partial failure and exactly-once cleanup. Preserve C
   membership and hold leases/resources through the final GPU completion.

The smallest local proof must execute production preparation/publication/undo
and retirement with controlled native dependencies, including partial acquisition,
pending work, repeated cleanup and poisoned operations. Then use the shortest
native reproduction of the active B-child shape with cancellation and 120
independent continuation ticks; do not replay the full index to rediscover the
same admission failure.

## Remaining unknowns and their resolving observations

| Unknown | Resolving observation |
|---|---|
| Does scheduler exclusion plus render quarantine stop all B-child consumers? | Frozen B-only values/bindings across native A/C execution, plus exact restored B continuation. |
| Can displaced child references be retired without changing C or losing a parent weak reference? | Actual production local reference-count/allocation checks followed by native retirement, surviving-membership checks and fresh GPU completion. |
| Does native B5695 have the same child topology as the repeated-seek B5695? | A bounded native capture at that combat boundary using existing diagnostics; no new capture fields. |
| Can a useful checkpoint be placed after the observed particle invalidation? | A bounded replacement placement, explicit ownership samples and successful later restore. Capture alone is insufficient. |

Checkpoint payload growth is not justified. Any additional fixed transaction
metadata and retirement reservations must enter the existing aggregate ledger.
Report capture, preparation, resimulation, completion/retirement and GPU waits
separately where measurable. The current ready-byte witness is not a proven
allocation high-water mark. The 2 GiB comparison remains pending; larger-budget
success cannot qualify the 1 GiB production ceiling or rollback performance.

## Native uninterrupted B reproduction

Run `replay-41956ba94ee04ef6a97ef7e6bd75cc40` reaches native B5695 without indexing or preceding seeks. A170/B5695 has the same296/310 scheduler counts,13 particle births and109/110 trace-state counts. It rejects before A publication. This supports the general B-child hypothesis; B recovery remains unproven. The retained `native-B5695-trace-child-admission-failure-audit.json` links exact identities, raw evidence and cleanup. The existing runner now accepts this bounded coordinate and budgets original B, discarded A-to-C work and120 resumed ticks; no new framework or captured fields.

## Private native retirement implementation

The existing retirement journal now distinguishes published C storage from transaction-owned private B headers. Local production-method tests retain C headers/backing, reject overlapping allocations and references still consumed by C, release cached-parent weak ownership, check surviving manager membership and inject every17 native-call failure in both modes. A new C-weak-reference negative test first reproduced an omission and now rejects before any native call. Native removal functions were rechecked: they compact entries and update count without reallocating array backing. The private path is not yet called by historical storage preparation or host commit; existing historical-child and scheduler guards remain. `trace-private-retirement-local-audit.json` retains exact builds and the focused log. No live recovery claim follows from this local proof.

## Assembled private B integration (implementation checkpoint)

The existing trace Prepared transaction now clones A while retaining displaced B child/weak headers and allocations. Its bounded proof accounts for exactly B-minus-A child states, native strong membership, frozen child values and controller/lifetime guards. Scheduler exclusion uses an exact callback-issued owner inventory; original epoch and prerequisites stay strict. Existing render quarantine accepts only the matching retained B trace primitive and binding. No checkpoint payload or historical shading participant was added.

Execution settlement distinguishes common A state, privately retained B children and native C-created children. Cached-parent weak ownership is counted from verified child references, not from observed count drift. Undo restores B trace storage before scheduler undo; its owner witness survives that handoff. Commit preflight runs before the enclosing irreversible boundary. B particle consumers retire before trace actor/attachment destruction, matching the already verified C recovery order; the existing fresh GPU drain retains all image/resource leases through completion.

Local production-method coverage now includes budget rejection, partial allocation after weak acquisition, refused publication while the child tick is admitted, held and post-execution complete B undo, repeated cleanup, original-epoch/prerequisite ownership, changed private child rejection and native private-child commit without modifying C arrays. Native lifecycle/capture dependencies are controlled and explicitly scoped in the fixture. These checks do not prove actual B5695 freezing, render coherence, cancellation or independent continuation. The last native evidence still rejects before publication on the prior runtime.

Next bounded attempt: native B5695 -> A170 -> C2510, cancel at Render::Drained, recover B and compare120 independent ticks. Success requires the private child to remain frozen during execution, correct owner/header recovery and fresh GPU completion. Any first failure is retained and investigated before retry; no full index or budget comparison is warranted until this mechanism works.
