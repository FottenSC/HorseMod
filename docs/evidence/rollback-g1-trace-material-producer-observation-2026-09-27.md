**G1 selected trace Start/helper observation: implemented, locally tested, no lease.**

The existing process-pinned `NativeReplayMaterialTaskGuard` now owns six entries:
its four retained task entries plus Start `1408D8C40` and the nested raw-MID
helper `1408D5840`. Their exact void Win64 ABIs and 48-byte prefixes come from
root's native verification in the same session and the
[material native evidence](rollback-g1-material-task-cpu-guard-2026-09-27.md).
No second owner, callback-executor hook, class-layout guess or pointer census
was introduced. Existing ownership of 1403FCA60/141D38300 and the 29-entry
trace guard was inspected and left unchanged.

Entry bookkeeping precedes Start's first native write at 1408D8C65 and the
helper's provider read/GetMaterial at 1408D585A/1408D588C. Both wrappers forward
every argument exactly once and preserve incoming and outgoing Win32 LastError.
They release the metadata lock before native code and read no object or borrowed
payload at entry or return. No required native call is suppressed or delayed
behind a producer/retirement wait.

The fixed 128-slot active-call table records a unique generation, same-thread
parent, family and depth. Snapshots expose per-family entry/return/active totals,
same-family reentry count, entries overlapping another thread, currently tracked
threads, caller-thread depth and maximum tracked depth. Parentage is dynamic
thread ancestry only; it does not identify a component/actor/MID relationship.
Completed call slots are recycled; this is not a durable per-call event log.
Overflow or unmatched ancestry leaves completeness unavailable. Depth/thread
statistics after coverage loss cannot describe untracked frames completely.

Each producer entry advances the operation's existing sequence. A receipt
opened while either family is active is rejected permanently. An older valid
receipt reports CpuPending during execution and Unsupported after producer exit,
including when no material task builder ran. A helper without a recorded Start
on its thread makes coverage loss sticky but still forwards. Partial startup,
entry during startup, record exhaustion and sequence/counter overflow also fail
closed. Native exceptional exits do not manufacture a successful return receipt;
an unmatched call remains outstanding rather than being treated as cancelled.

All six signatures and both existing callback-table associations are checked
before the first material hook. A module pin precedes publishing entries.
Partial detours/trampolines stay process-owned and usable for forwarding; a
failed attempt cannot be retried into empty coverage. Tests cover a producer
that returns before installation finishes and one that remains active across
the end of installation. The existing startup-domain checks remain necessary;
this does not reconstruct native entries that began before a hook was published.

The fixed guard reservation remains **1 MiB**, including 1024 existing task
records, the new active-call table, metadata and six detour/trampoline allowances.
The compile-time bound remains below that reservation. There is no new heap
allocation per producer, thread-local production allocation, or memory-ceiling
increase. Root still owns production build and live cost validation.

The architecture challenge produced a concrete negative result: these hooks
are **not an operation-scoped lease**. The fixture lets a real hooked producer
run immediately after the production command predicate unlocks, before its
previous Clear sample is consumed. It observes `sampled_command=1` even though
the operation's current state is now Unsupported. The subsequent C-only check
vetoes all native retirements and retains B. Reentry and concurrent producers
also continue to run normally; their recording does not exclude them. Changing
the historical admission gate would therefore be unsafe.

`HistoricalRestoreSupported()` stays false at Request and direct Prepare.
`InspectCOnlyRetirement` still rejects every result, including ProducerUncovered
for a fresh idle baseline. Fresh CPU observation after Start/helper return is
not proof that previously produced resources are gone. The prior C-only veto,
atomic sticky failure, complete-B retention and render-command/release guards
are unchanged. The `HORSE_TRACE_MID_BORROW_RED=1` positive-admission test was not
changed or rerun without a resolving lifetime contract.

Changed files in this invocation:

- `HorseMod/horselib/deterministic/NativeReplayMaterialTaskGuard.hpp`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- This evidence file. No status file or commit.

Retained production-boundary receipts:

| Run | Result | Immutable evidence |
| --- | --- | --- |
| First Start/helper regression before production changes | RED. Before Start's write, before lookup, while holding the raw borrow, after helper return and after Start return, the old guard reported `state=0 builders=0 tasks=0 late_admitted=1 command_allowed=1`. Start/helper each forwarded once; prior C-only containment still prevented retirement. | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/82e4da22cebf8bf98155b1a0d70a726ddde8aecd9d915b114f9ad4cda53c9807.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/eb1195520ed269211a341902527bd7dbfc078b60de9936a34559f586fb0fe055.log) |
| Same regression after production hook/receipt change | GREEN. Active Start/helper reports CpuPending, late receipt is rejected and command predicate denies; returning only the helper keeps Start active. Start return leaves the original receipt Unsupported. | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e29533a0e60e6385fb860144252919b343652a5343ed894512b2ae6e1ab59455.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6e8a33e054bd3f39cae75b9fda29a5ae1c807adde759ef9b1b64b5659fd20d5c.log) |
| Final affected selection | GREEN: **48 native-contract cases**, including the preceding 34 cases, eight new producer cases and six additional startup cases. | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/55337f4257ec5cee450c537e320e10ca6fc97127442e86c658af47d85b50b01e.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c7ef36636b0ca5771ab6c1c1a503030845b77c602c2c632e7cb8456d76ebfff8.log) |

The new cases cover exact argument/LastError forwarding, no borrowed-input reads
on return, Start/helper entry and exit, same-thread Start/helper reentry at depth
four, two concurrent producer threads, late operation receipt rejection, stale
Clear sampling, orphan helper calls, 130 nested Starts exceeding 128 record
slots, closed optional diagnostics, and vector/refresh builders reached from
the selected helper. Both independent task callbacks can return without granting
material or GPU completion. Additional startup cases corrupt each new prefix's
48th byte, fail each new detour, and enter producers during installation. Partial
hooks forward once and cannot be promoted to successful coverage by a retry.

All executions used `python tools/replay_test.py local --layer native-contract`
with exact `--test` selections in the existing VFX fixture. The final eight
selected nodes are recorded in the final manifest. No full file selection or
broad suite was run. Tests compile production headers and host extracts against
controlled external native-shaped functions; they do not execute the game's
actual Start, helper, MID teardown or GPU work.

The pre-fix source is retained in
[source-906f45ab…](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-906f45ab85395fe01bbf8f37cdcb757e8326755d55aec4a87203ab7eafd22b86.json).
Final production/test source is retained in
[source-42f6211c…](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-42f6211c6846e0c72077679edda84fa24fecb347469c9f7725cae3fcda1619d6.json),
fingerprint `d09f16a8dae293985bc36f63ec84d6d8c263340ee01856425a383d67dcbe4dc6`.
This evidence document was added afterward and changes no tested source.
Recorded DLL identities are the prior build, not verification of this source.

The next blocking owner is provider/MID/proxy mutation and retirement, not
another task-return counter. Root needs a contract joining selected Start/helper
borrows to provider replacement, SetMaterial, reset/reinitialization and native
BeginDestroy/FinishDestroy before any physical teardown, with an explicit
same-thread reentry policy. +F8/+100 ownership/replacement remains unresolved.
The Start hook is early enough to observe selected effects, but it neither owns
those effects nor captures their complete B undo. Direct/indirect callers and
other producers remain outside a complete producer census.

Separately, selected resources need operation-linked ownership and completion
through +F0 release/deletion tasks, both native fences, the deferred-array task,
shared zero-reference queue/delayed storage and GPU consumers. The root's native
array-consumer/task identities narrow that investigation; callback return is
still not GPU/resource completion. No positive historical admission is justified
until that contract and B reconciliation are implemented and verified.

Observer validity: 48 scoped cases pass; G1 remains open. Native simulation,
required lifecycle, coherent complete-B recovery, normal rendering and performance
were not measured. No game launch, deployment, production build, broad suite,
Ghidra edit, status-file change or commit occurred. All invoked fixture commands
returned; runner cleanup remains reported as unknown.
