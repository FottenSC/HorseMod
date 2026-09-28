> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Forward investigation: first depth-seven restore

Read-only production review on 2026-09-15. No game launch, deployment, production edits, or Ghidra database changes. The active implementation is moving; recheck source before applying findings.

## Reproduced boundary gap: animation node-memory identity

`Sc6ReplayTraceProjection.inl`, `ProjectChildBinding`, `Kind::Proxy`, offset 0xA0 accepts the same pointer nullness and equal metadata at 0xA8. It does not establish the identity of the replacement node-memory base.

The existing production-method fixture passes unchanged. Adding one case that substitutes 0x12345678 for the replacement proxy's node-memory base, retaining matching metadata, fails the assertion that this invalid binding must reject. The production helper accepts it. This is an injected validation gap, **not proof that the native constructor supplies the wrong pointer**, and not the established cause of `child_projection_binding` in the live run.

Native `141C93E00` calculates a typed animation node address as proxy+0xA0's pointer plus a signed reflected-property offset. Pointer identity therefore affects actual animation destinations. Do not simply assume the base must equal the animation UObject: verify its native initialization/ownership and seal the exact prepared base or independently owned region. Revalidate prepared animation destination mappings against that base before publication. Preserve metadata and alias checks.

Evidence:

- [Unchanged fixture passes](../../../../artifacts/rollback-forward-audit-20260915/baseline-result.txt).
- [Added rejection case fails](../../../../artifacts/rollback-forward-audit-20260915/result.txt).
- [Extended existing fixture](../../../../artifacts/rollback-forward-audit-20260915/projection-audit.cpp).
- [Retained production binding method](../../../../artifacts/rollback-forward-audit-20260915/trace_child_projection_binding.inl), [image projection method](../../../../artifacts/rollback-forward-audit-20260915/trace_child_project_image.inl), and [source digest](../../../../artifacts/rollback-forward-audit-20260915/source.sha256).
- [Native node resolver, read through Ghidra MCP](../../../../artifacts/rollback-forward-audit-20260915/native-node-resolver.c).

## Prioritized early checks

1. **Current mapping rejection:** use the field-level diagnostics already added by the active task. The retained `trace-creation-flags-native` run prepared two fresh children successfully and then rejected `child_projection_binding` before A publication. Its old log cannot identify the field; do not infer it from this separate injected gap.
2. **Preparation failure after native creation:** verify scheduler transposition, both child retirements, a newer successful GPU drain, and restoration of the original B boundary as one sequence. The last retained run exits after preparation failure and GPU cleanup activity; process cleanup alone is not successful in-game B recovery. Current cleanup code already contains changes, so avoid duplicating that fix. Prefer a production host-boundary fixture with two acquired children and failure immediately before mapping completion, then one bounded native confirmation.
3. **Death during corrected execution:** exercise the assembled handoff from projected A to the survivor view, C capture, fresh-child death validation, commit, and final ownership release. Existing isolated survivor tests establish that dead child payloads are not read, but do not prove the complete host sequence. Include one child dying and one surviving to catch wrong inventory/view routing. Keep original B recoverable throughout.
4. **Display publication and exact landing:** verify target-tail completion and original-window publication precede commit, and require tick 345 throughout these tails. A held B image and successful Present submission are insufficient; completed GPU work and regenerated-C coherence need separate receipts.

These are targeted checks, not four confirmed production bugs or new prerequisites beyond the active plan. The first confirmed gap above can be addressed locally while the native mapping diagnosis proceeds. The remaining items identify where composed behavior still needs evidence.

## Latest live evidence inspected

[Raw log](../../../../artifacts/rollback-owner-integration-20260915/trace-creation-flags-native.UE4SS.log), [report](../../../../artifacts/rollback-owner-integration-20260915/trace-creation-flags-native.json): run `replay-79efc91e96f54392b0d10114d2a6075d`, A338/B345, rolling_cycles=0. Two acquisitions report code=0 and prepared=true. A publication was not reached. Reported process cleanup is complete, games_remaining=0; in-game B continuation was not established.
