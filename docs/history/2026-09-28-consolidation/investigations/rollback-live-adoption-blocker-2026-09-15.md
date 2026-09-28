> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Fresh trace adoption uses the wrong ownership image

## Finding: P1, surviving fresh child cannot complete final commit release

`Sc6ReplayHost::ReleaseFreshParticleOwnership` in `Sc6ReplayHost.ParticleOwners.inl` calls:

```cpp
operation.undo.traces->ReleaseExecutedFreshChildOwnership(child, commit);
```

For a surviving committed child, that method requires `OwnsNativeChild(ref)`. The latter first requires `Contains(ref)` in its receiver's saved states, then checks actual native strong membership. Complete B intentionally does not contain the newly reconstructed identity: construction and preparation explicitly reject adding a fresh child already present in B. Consequently the host's B receiver always rejects the surviving-fresh-child adoption branch, even when the native membership is correct.

The host reaches this call after native retirement and display completion. A rejection is reported as `commit_fresh_owner_adoption` and marks the operation/interior failed after the irreversible commit boundary. It is therefore a completion blocker for any supported correction in which a reconstructed trace child survives to C. This is separate from the current pre-publication mapping failure and the earlier animation-pointer finding. It does not establish that a child survives in the particular 338-to-345 attempt.

## Reproduction

Extended the existing `replay_trace_storage_selftest.cpp` production-method fixture in a retained audit directory. No production or shared test source was changed, no game was launched, and no deployment or Ghidra database changes were made.

The added case sets the release-boundary bookkeeping to a settled committed live child, preserves its actual single native strong entry, and verifies that B lacks the identity while the image containing the fresh child owns it. This is a release-boundary test, not a simulation or full-commit fixture: it deliberately does not claim to execute the seven ticks or GPU completion.

- [Existing production storage fixture passes](../../../../artifacts/rollback-forward-audit-20260915/adoption/baseline-result.txt).
- [B receiver fails](../../../../artifacts/rollback-forward-audit-20260915/adoption/red-result.txt), [extended fixture](../../../../artifacts/rollback-forward-audit-20260915/adoption/adoption-red.cpp).
- [Same boundary with the image containing the current owner passes](../../../../artifacts/rollback-forward-audit-20260915/adoption/current-owner-result.txt), [receiver-only diagnostic variant](../../../../artifacts/rollback-forward-audit-20260915/adoption/adoption-current-owner.cpp).
- [Retained actual release and membership methods](../../../../artifacts/rollback-forward-audit-20260915/adoption/trace_storage_image_methods.inl).
- [Host call site](../../../../artifacts/rollback-forward-audit-20260915/adoption/Sc6ReplayHost.ParticleOwners.inl), [source inventory and hashes](../../../../artifacts/rollback-forward-audit-20260915/adoption/source-manifest.json).

## Why existing checks miss it

The storage fixture's full execution branches destroy the fresh child before release. Dead-child release does not require `OwnsNativeChild`, so those tests pass through B. The host release fixture substitutes a `TraceRelease` method that adopts whenever its `ready` flag is true; it does not represent B/C membership. These checks cover useful phase and completion guards but do not compose the real host receiver with the real live-child membership check.

## Narrow remediation

Recheck the moving checkout first. Use the retained actual C trace image for committed surviving-child ownership validation, or pass explicit current ownership proof into the release API. `operation.execution->traces` is the current trace image used for settlement; verify its lifetime through final release before choosing it. Do not add fresh children to complete B and do not remove native-membership validation.

Add a failing-before/passing-after regression that composes the host call with actual membership validation. Cover both dead and surviving fresh children, recovery, failed lease release/retry, and repeated cleanup. The current-owner diagnostic above identifies the wrong receiver; it is not a proposed production patch by itself.

This review also checked the recovery branch: dead-child recovery does not take the `OwnsNativeChild` branch, so this specific receiver mismatch is a live-commit problem. No separate recovery defect is claimed.
