> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Replay checkpoint follow-up: animation publication and timer identity

This continues [the first static audit](replay-checkpoint-static-audit-2026-09-06.md) for **Rebuild replay seeking system**, task `01a07378-5e21-7d12-b8f6-1e21626e687f`. Investigation used the same existing `SoulcaliburVI.exe` Ghidra program through native MCP. Addresses are absolute, image base `140000000`; executable SHA-256 is recorded in the first audit. No game launch, runtime change, replay test or new correctness-gate evidence is claimed.

## Main findings

1. Animation worker completion, pose publication and enclosing tick completion are separate boundaries. Publication checks object identity and transform count, but no replay generation. Historical restoration must account for pending publication.
2. Blueprint timer set/clear resolves callback identity. Self-rearm can find the executing record and retain its handle. Reconstructing deadlines without callback-owner state is incomplete.

These are constraints for a later cross-world restore. They do not invalidate the existing bounded same-task result or justify removing its lease checks.

## Animation binding and teardown

`141DD1000` calls `RegisterComponentAnimRateTracker` and stores its result at component `A48` (`141DD1019`). The related master-pose path `141DD6E00` maintains weak membership and tick prerequisites. `141DD1140` performs component cleanup, unregisters a nonnull tracker, and clears `A48` (`141DD116C`). These are base skinned-component paths; not every caller should be typed as the skeletal subclass.

The skeletal teardown `141DA6840` waits for event `EC8`, clears/releases that reference, and clears captured identities `E60/E68`. It then handles the **separate clothing event at D40**, clears main/linked/postprocess animation instances, releases clothing simulation state, and reaches base unregistration. Reflection at `141AC2320` identifies `ClothingSimulationFactory`; `D40` must not be mislabeled as animation evaluation work.

`141DAFE00` consumes the tracker and records the low 32 bits of the engine epoch at component `EF4` on the admitted pose-tick path. Enabled animation delta includes tracker additional time at `14`; the skip path invokes instance virtual `260`. This stamp truncates, unlike the tracker update's modulo-`FFFFFFFF` calculation. They are separate clocks.

## Evaluation task graph

Producer `141DA86B0` establishes this chain:

1. Parallel eligibility requires CVar `144392C88`, worker capability, admitted evaluation, a nonnull tick-function argument, matching group bytes at argument `8/A`, and a nonnull completion reference at `18`.
2. `HandleExistingParallelAnimationEvaluationTask @ 141DA0A30` handles existing work before new capture. Its nonblocking path reports an existing event without waiting; its blocking path waits and calls completion. A true return is not universally a publication witness.
3. Capture the current main animation instance and mesh at component `E60/E68` and set evaluate/interpolate/duplicate flags.
4. Construct an evaluation worker through `141D99550` / `141D8C8A0`; retain its event at component `EC8`.
5. Construct a completion task through `141D993B0` / `141D8C830`, with the worker event as prerequisite. Its execution body `141D9B890` weak-resolves the component and calls `CompleteParallelAnimationEvaluation @ 141D96320` with publication enabled.
6. Attach the completion task's event to enclosing tick completion through `14215FBF0` and `140F2EFF0`.

Both tasks hold weak component identity at task `10` and an event at `20`. TLS pooling reuses allocations: a task address is not a stable lifetime identity. The `100` pool-slot stride is not proof of exact task-object size.

The synchronous branch calls `PostProcessSkeletalMeshEvaluation @ 141DA6EC0` with the same component-owned context. Worker completion, publication, postprocessing and enclosing tick completion therefore need distinct witnesses. A retained manager task alone does not describe every downstream animation transaction.

## Publication predicate and writes

`CompleteParallelAnimationEvaluation @ 141D96320` clears/releases the component event reference first. **It does not itself wait for the worker.** Publication requires all of:

- Caller requested post-evaluation.
- Captured instance `E60` equals current main instance `AB0`.
- Captured mesh `E68` equals current mesh `910`.
- Context transform count `E78` equals the selected component count at `928 + index(944)*10`.

This body compares no replay coordinate or evaluation generation. On success it swaps owning transform headers, curve storage and root translation between context and component/cache destinations, then calls `141DA6EC0`. Both paths clear captured instance/mesh identities afterward.

| Context component offset | Published storage | Destination |
|---|---|---|
| `E70` | component-space transform array header | `920 + index(940)*10`, or cache `B30` when interpolation is selected |
| `E80` | bone-space transform array header | `AE8`, or cache `B20` |
| `E90` | three-float root translation | `B7C` |
| `EA0` | 32-byte owning curve representation | `AF8`, or cache `B40` |

**Inference:** old work could satisfy these identity/count checks after restoring an earlier state of the same objects, then publish over restored buffers. This is a static hazard, not a reproduced divergence or proof that the narrow same-task experiment encounters it. A future cross-world restore needs a verified publication boundary and coherent task ownership. Clearing `EC8` is not an established cancellation operation.

Postprocessing also consumes URO state. `GetAnimRateInterpolationAlpha @ 141DC7630` returns `0.25 + 1/(2*max(2,evaluationRate))` in mode 0, and clamped `delta/(delta+timeOffset)` in mode 1; the negative/unordered comparison selects zero. Other modes return zero. `141DA6EC0` uses the result while blending cached/current transforms and curves and updating downstream consumers. Restoring only the last scheduling stamp cannot reproduce this state.

### Verified partial layout

New `FParallelAnimationEvaluationContext_Partial` covers `68` bytes from component `E60`, ending before the event:

| Relative offset | Field |
|---|---|
| `00/08` | captured instance / mesh pointers |
| `10/20` | component-space / bone-space transform array headers |
| `30` | three-float root translation |
| `3C` | four unknown bytes |
| `40` | opaque owning curve storage, 32 bytes |
| `60/61/62/63` | interpolate / evaluate / duplicate-transform / duplicate-curve bytes |
| `64` | four unknown bytes |

This is a checked partial extent, not a claim about full C++ ABI alignment or pointer-free snapshot data. It was applied only to verified uses. `USkeletalMeshComponent_AnimationPartial` now names `A48`, `E60`, `EC8`, and `EF4`. Its existing linked-instance pointer at `AB8` had an erroneous four-byte field extent despite an eight-byte pointer type. Qword loads in animation/pose/teardown established the correction to eight bytes. Count `AC0`, capacity `AC4`, other named fields and overall `FF0` extent were retained.

## Blueprint timer identity

Registry entry `143A9F100` binds `K2_SetTimerDelegate` to thunk `1425974B0`, which calls `SetWorldTimerForScriptDelegate @ 141ED3D90`. Reflection construction `14256B960` independently establishes the name and parameters.

The body validates nonempty function name, live weak target and target function; resolves its world/manager; then finds an existing handle **by delegate identity before calling the lower setter**. It supplies first delay `-1` and copies owning callback storage. `K2_ClearTimerDelegate` follows registry `143A9EFC0` -> thunk `1425963E0` -> body `141ED3100`, performs the same validation, finds the handle and clears through `14217E560`.

`FindWorldTimerHandleByScriptDelegate @ 14217F550` searches the embedded **executing** record first, then active, paused and pending arrays. `FindWorldTimerInArrayByScriptDelegate @ 14216A6D0` advances by `C0` and returns the first match. Equality requires the 64-bit function name plus either matching weak-object index/serial or **both weak references failing native validity**. The public wrappers' live-target checks matter: equality alone does not prove a valid callback owner.

Consequently a callback rearming itself can find its executing record and reuse the numeric handle. Modeling every rearm as a fresh timer would be wrong. Callback identity, owner state, handle references and manager admission phase must be classified together. This supplies the native link beneath the already-audited replay scene polling/pause/finish Kismet paths; it does not prove which callbacks ran in any particular live capture.

The native GameState callback `141E49C30` independently demonstrates self-rearm: conditionally increment owner `3D8`, invoke virtual `6B8` for the observed net-mode branch, compute a settings-dependent rate, and set through owner-held handle `3E0`. Raw thunk `141E42C40` jumps through virtual `6C0`; base vtable `1438D9EB8` maps it back to this callback. Base `6B8` is empty, but subclass overrides remain unclassified. Repeated settings reads and floating-point operations must not be algebraically canceled. No missing function boundary was created for the thunk.

New `FWorldTimerDelegate_Partial` describes the setter's verified `A0` argument: native owning storage `00..3F`, existing `FScriptDelegate_Partial` at `40..4F`, owning function storage `50..9F`. `SetWorldTimerWithUnifiedDelegate @ 14217E8E0` now uses that aggregate instead of a misleading Lux transform-provider argument type. Opaque ranges remain opaque; old Lux-specific names on shared copy helpers do not make these timer arguments Lux objects.

## Next bounded experiments for the main task

- Preserve current same-task lease checks. Before expanding restoration across worlds, observe one relevant mesh's `EC8`, captured instance/mesh, evaluation flags, publication entry/exit and enclosing tick completion across adjacent worlds. Determine actual parallel/URO branch use before designing support for it.
- Begin with a stable component registration generation. Reject a candidate boundary with unclassified pending animation publication; draining work can itself mutate state and must be an explicit boundary choice, not a hidden repair.
- For one scene timer, correlate callback owner identity/FName, handle, executing/active/pending phase and callback-owned state before/after one invocation. Verify self-rearm behavior before implementing timer reconstruction.
- Test changed lifetime, exit/re-entry, scene change and cleanup separately. These static results fill no execution, exact-seek, lifecycle or performance gate.

## Ghidra changes and verification

This follow-up adds seven meaningful function names/prototypes: animation completion, animation postprocessing, interpolation alpha, script-delegate set, identity lookup in manager/array, and the unified setter. It adds two partial structs, corrects the linked-instance pointer extent, applies verified component fields, and adds relevant variable types/names and plate/PRE/EOL comments. Producer, completion-task, teardown and callback analyses remain explicitly partial where full cleanup was not performed.

Core typed consumers were re-decompiled after structural edits. Annotation completeness was checked; remaining generic dynamic values, opaque callable storage and SIMD swap temporaries were not assigned speculative types to improve scores. Scores measure annotation quality, not behavioral correctness. No Ghidra scripts, project-file edits, second import or snapshots were used. Independent Hermes review remains unavailable as described in the first audit.
