> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Fully settled render reopening: atomic admission

Memo02's reopening defect was verified in current source: `ContinueExecutionForUndo` called lighting, visibility, then registry handoff. A visibility or registry rejection could follow a successful lighting handoff. The aggregate remained settled, but retry called lighting Begin again and rejected its already executing state.

The correction preflights lighting and visibility with their unchanged admission checks before invoking registry continuation. `ReplaySparseRegistryStorage::ContinueExecution` performs all its checks before clearing current ownership metadata; it has no failing operation after that mutation. After registry success, lighting and visibility transfer only existing allocation pointers, map pointers, native-reference bookkeeping and flags. They perform no native calls, allocation, reference release, GPU submission or fallible validation. Therefore an ordinary rejection leaves the whole settled chain unchanged; success hands off every domain and requires a fresh GPU completion.

The original Begin methods use the same preflight and transfer functions, preserving their previous admission. The common methods are in `Sc6ReplayParticleCopy.Reopen.inl`, included by production and a bounded local test fixture. No additional ownership flags, checkpoint participants or snapshots were added.

The fixture executes those production methods and forces lighting, visibility, private-B and registry validation failures, each twice. It checks installed pointers, native-reference bookkeeping, registry transfer count, all settled flags and GPU-completion state. After removing the fault, exactly one complete handoff succeeds. A repeated completed handoff rejects without transferring twice. Initial partial settlement remains rejected.

The validation backend and registry operation in this fixture are substitutes. It proves production orchestration and metadata atomicity at those supplied validation results; it does not prove actual native resource binding, GPU completion, or in-game B continuation. Existing native registry tests separately exercise allocation ownership and B undo, but are not an assembled render-reopening qualification. [Local evidence](../../../../artifacts/replay-render-reopen-atomic-20260911/audit.json) retains exact source/binary identities and logs.

## Remaining scope

This closes the ordinary rejection/retry ordering defect after fully successful render settlement. It does not admit initial partial C settlement: capture-only lighting/visibility/reference leases still require safe cleanup, and the host's clean Drained guard remains intact. No flag is forced to Settled on failure. No C-only teardown is admitted by this change.

The next implementation must distinguish an adopted C image from capture-only C leases, release only those leases through their existing cursor-aware native owners, and preserve retry state if any release fails. Before admitting teardown, every domain must be verifiably native-owned, private B intact, CPU participants reopened and the ordered drain complete. That work remains separate from the unknown actual C11000 trace owner/callback/overlap topology.

Memory impact: no new checkpoint or operation fields, allocations or diagnostic readbacks. Handoff adds no GPU waits or reference decrements. Capture/restore timings are not measured here. Live testing remains disabled by the user; no new live qualification is claimed.
