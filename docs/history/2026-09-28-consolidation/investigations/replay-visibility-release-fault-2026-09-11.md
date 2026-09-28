> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Uncertain native release must not retry

While tracing initial partial C settlement, current source showed that `ReleaseVisibilityImage` sets `capture_owner_fault_` on SEH but did not check it on re-entry. Native release leaves the pointer slot intact. If a fault occurs after decrement, retrying the same pointer can decrement twice. `Finish` already rejected this uncertainty, but settlement could reach the release method independently.

The release method now rejects immediately when ownership is uncertain. Particle-copy binding admission and lighting final retirement also reject the same fault. This retains the existing fail-closed interpretation across re-entry; it does not invent a way to recover an interrupted destructor or clear the fault to resume.

The actual production visibility-release method is shared with a local Windows SEH fixture. Its native-call substitute decrements a resource count and then raises an exception. Each material slot and the query path is tested. Repeated calls cannot decrement again, separate B leases remain untouched, and fault-free cleanup remains idempotent. The test proves local release control flow at an uncertain side effect; it does not execute actual RHI destructors or prove live lifecycle recovery.

[Retained evidence](../../../../artifacts/replay-visibility-release-fault-20260911/audit.json) links exact build/source identities and test logs. No live game was launched. No new snapshots, GPU readbacks, native reference acquisitions or checkpoint fields were added.

## Consequences for partial C recovery

An ordinary capture/admission rejection with known lease ownership is distinct from an exception inside native release. Only the former can enter a reversible capture-only cleanup path. The latter must retain resources and prohibit further mutation until the session can be safely terminated; that lifecycle remains unqualified.

Current capture-only inventory:

- Lighting C can own uniform references before `captured` becomes true. Its copied allocation addresses remain native-owned; cleanup must not free those addresses. `FinishLighting(false)` currently retires A/B uniforms, not C, so it cannot be reused as the partial-C cleanup operation.
- Visibility C can own material references before query capture. `leases` counts acquired query references; `queries.size()` also includes pointers validated before acquisition. Cleanup must use the lease count, with completed decrements recorded.
- Reflection C owns its native retained target plus a COM texture reference per successful slot. A later validation can reject after that slot was retained. Clearing only an aggregate flag cannot release these owners.
- Capture charges accumulate in `witness_.bytes` before allocations. Retrying capture must either reuse charged storage with explicit accounting or safely retire/refund only accounted resources; resetting counters from observed process memory is not valid.

These facts keep initial partial render settlement rejected. Safe cleanup and handoff of adopted domains must be integrated before permitting C-only teardown and fresh GPU drain. The prior fully-settled atomic reopening fix remains separate, as does the unknown late trace actor/callback/overlap topology. Pixel-perfect history is not a gate.
