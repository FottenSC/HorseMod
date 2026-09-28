> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Clean failed-request UI recovery

Memo18 was verified against current production `ObserveSeekHold`: clean Failed requests could not reach the existing deferred Release path through Resume. Cancel rejects when no historical transaction exists, and Step/another seek remain excluded while the failed wrapper owns execution.

The observer now routes explicit Resume for Failed with no historical transaction or checkpoint restoration through the existing Release operation. All pending-work, particle, surface, completion and deferred-retirement checks remain in that owner. Failed requests cannot single-step. Dirty failures cannot enter this route and still require B recovery. Failure evidence stays visible until explicit release acknowledgement. The overlay explains the distinction between cancellation/recovery and dismissing a clean failed request.

The local selftest includes the actual production Seek.inl, including Begin, ObserveSeekHold, AdvanceSeek and Release. Its restore backend is a fixture that rejects Request before transaction creation. Coverage drives UI step/cancel/resume counters, blocked particle ownership, blocked pin retirement, Completed acknowledgement, unchanged simulation/publication counters and a successful next request. A dirty failure with Resume remains retained. This is production dispatch coverage with a mocked restore backend, not a live request-failure proof.

No checkpoint state, GPU capture, native lifetime guard or transaction ownership phase changed. Live testing remains disabled by the user. [Retained local evidence](../../../../artifacts/replay-clean-failure-ui-20260911/audit.json) records exact binaries, source archive and logs.

The next offline recovery investigation is memo02's partial render adoption/reopening: current source confirms lighting, visibility and registry can complete individual handoffs before a later failure. Their aggregate flags cannot by themselves describe retry progress. Any fix must preserve private B, account for capture-only C leases and individual handoff completion, and leave C-only teardown prohibited until every relevant owner is native-owned again. No partial-render guard was relaxed in this change.
