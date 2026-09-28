# Selected C18 vector-row dispatch receipt — Astra implementation, 2026-09-27

Implemented the optional dispatcher child of the existing selected C18 `material_calls` receipt. Local RED/GREEN and nearby controls passed. This handoff records observation-only code and local evidence; root's later build and bounded live result are recorded separately.

Root launched this coding session with `-m gpt-6-astra`, `model_reasoning_effort="xhigh"` and `desktop.daybreak-enabled.enabled=false`. Its temporary CLI prompt/transcript was removed during documentation cleanup; the runner's immutable RED/GREEN logs, manifests, source snapshots and this handoff retain the reviewable code/test evidence. Root's subsequent [build, full suite and live result](rollback-g1-c18-row-dispatch-live-2026-09-27.md) supersede the pre-live wording in this handoff.

The native boundary is root's [mutation/hook contract](rollback-g1-c18-row-dispatch-native-contract-2026-09-27.md). The earlier [slot-return implementation](rollback-g1-c18-mid-slot-implementation-astra-2026-09-27.md) and [selected live receipt](rollback-g1-c18-mid-slot-live-2026-09-27.md) remain intact. No Ghidra, whole-project build, broad test suite, deployment or game run was performed here. AGENTS.md and rollback-status.md were not edited. Unrelated dirty/untracked work was preserved.

## Exact changed files

Five existing implementation/test files were changed, plus this handoff:

- [NativeReplayMaterialTaskGuard.hpp](../../HorseMod/horselib/deterministic/NativeReplayMaterialTaskGuard.hpp): optional dispatcher detour, bounded child records, entry/return correlation and memory accounting.
- [NativeReplayVfxCompletionObservation.Collection18.inl](../../HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl): optional `material_calls.row_dispatch` serialization.
- [replay_control.py](../../tools/deterministic_qualification/replay_control.py): strict child receipt validation with older sidecar compatibility.
- [replay_vfx_completion_observation_selftest.cpp](../../tools/replay_vfx_completion_observation_selftest.cpp): changed/equal native fixture paths, dispatcher forwarding and lifetime/fault controls.
- [test_vfx_completion_observation.py](../../tools/deterministic_qualification/tests/test_vfx_completion_observation.py): existing runner/fixture extensions, real PolyHook coverage, parser and legacy evidence controls.

`NativeReplayVfxCompletionObservation.hpp`, the cold-material wrapper signature checks and root's consumer-host PolyHook stub were not changed. Comparing runner-retained RED and final GREEN source manifests finds exactly these five changed source paths; the five working files also hash-match final retained source.

## Hook and receipt behavior

Source ownership search across HorseMod, tools and RE-UE4SS found no other owner of `141F05150`. The new optional hook belongs to the existing process-pinned material guard. The six mandatory CPU-veto hooks and existing Start/helper/getter/lower-setter ownership are preserved. No hook was added at wrapper `141F45940`.

Installation checks the complete mapped dispatcher prefix:

```text
40 53 41 56 48 81 EC 88 00 00 00 80 3D DE C6 44 02 00
```

The actual PolyHook source at `C:/Users/prest/AppData/Local/HorseMod/fetched_deps/polyhook2-src` uses the eight-byte destination-holder instruction as its minimum. Its disassembler and branch-expansion preflight round the dispatcher's two PUSHes and SUB to 11 bytes. The RIP-relative CMP beginning at +11 must stay at its native address. The existing exact-install adapter rejects an expanded span before patching and checks the resulting target, copied span, VALLOC2 scheme and nonzero trampoline. The optimized real-library fixture verifies a copied 11-byte prologue with no displaced instruction. Unexpected signature, branch expansion, failed install, scheme or span closes optional receipt readiness. Previously published hooks remain pinned and forward; optional failure does not disable the six mandatory guards or grant rollback admission.

Within the selected ordinary-forward C18 interval, a dispatcher entry must have one active selected setter and preserve its getter/helper/Start IDs, thread and nesting. It records the dispatcher MID, caller/RVA, full packed FName and four float bits. Only return PC `base+0x1F1E528`, matching MID and a stable matching row tuple can match. Republisher return PC `base+0x1F1A5AA` is recorded as unmatched and invalidates this proof. Same-thread nesting alone is insufficient: the existing verified getter-to-setter link and helper/provider linkage are required.

The reflected row is borrowed at entry only. Two bounded 24-byte reads copy FName and color; no row pointer, MID payload, GUID or proxy is retained. Return and serialization never dereference the borrowed row or returned MID. Foreign-thread and unattributed entries invalidate coverage without borrowing their rows. No lock spans native forwarding. Each wrapper restores incoming `GetLastError`, calls native once, then restores native's outgoing `GetLastError` after bookkeeping.

The new version-1 child retains up to 64 distinct dispatches, including their setter/getter/helper/Start ancestry, optional reentry parent, ordinal and entry/return sequence. Its per-setter `cpu_row_write_branch` is:

- `true`: the entire selected receipt is complete and has a matching dispatcher child.
- `false`: the entire selected receipt is complete and that setter has no dispatcher child.
- `null`: coverage is incomplete or invalid, including foreign calls, reentry, overflow, invalidation, unreadable/unstable/mismatching rows or installation failure.

Two sequential dispatches under one setter remain distinct; recursive dispatch invalidates the result. The parser recomputes counts, ancestry, nesting and per-setter outcomes and rejects unsupported completion claims or extra row-pointer/tick fields. Older sidecars without this child remain readable. The previous aggregate `material_calls.vector_row_write_proven=false` is retained; only the new per-setter CPU-branch field carries this narrower claim.

Storage remains inside the existing 1 MiB material-guard and 4 MiB observer reservations. Guard accounting now includes nine pinned detours and 64 fixed dispatcher records; the observer's existing copied-correlation and serialization bounds still fit. Production static assertions passed in the optimized native fixture. There is no new harness, unbounded event storage, per-tick UObject::IsReal scan or change to complete-B, lifetime, resource/GPU, deferred-retirement or production memory-ceiling rules.

## Retained local evidence

The regression was added before production changes. Its native fixture independently took one changed path and one equal path, forwarded both setters and one dispatcher, then failed because production had no dispatcher child. The runner stopped at that first failure. Production implementation and narrow controls then turned it GREEN.

| Selection | Result | Immutable evidence |
| --- | --- | --- |
| Initial production-boundary changed/equal regression | RED: 1 failed, stopped at first failure | [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f9c001b73d21f0db6a6a4c754e5ac0126f59fb9ce685be75434d92ce0c8b9ee8.log), [manifest and command](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f8a59371e6ee2f025cf3bc9ea0fab15481c5feeef2e5f8747e28fc4f402a72ed.json) |
| New receipt plus previous Start/helper/slot correlation controls | GREEN: 64 passed | [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e27f339082f925ec51c1d196664292401efa292c79f2e335ed8174933685cf48.log), [manifest and command](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/1aa8f537a041f741b0b6f9888cb5db0f25251e0268df163fb166329e384f993d.json) |
| Nearby guard/startup, selection, capacity, writer and consumer containment controls | GREEN: 162 passed, native-contract layer | [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/cbd94a76efeb0fb60af088b723a3beb4b7f150efe2736771e323ef7cb8fb54f3.log), [manifest and command](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/b2022a42e86af1d5a35a2ff00b0e98f48828ea0df5ae522e5324abb3453bd21c.json) |
| Final new receipt selection after retaining child stdout/stderr | GREEN: 30 passed: 29 native-contract, 1 unit | [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b00c7f36da6a6638b3209a78154690a93d8f6026f32a19416fd6ccfd61bd4025.log), [manifest and command](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c0c41a17d62f5b2de6ff89c635030a9b1f39ef94f7ef07b706c9e16ce5ddd6eb.json) |

Counts overlap; they are not additive unique coverage. All selections used `python tools/replay_test.py local --test ...`, with `--layer native-contract` for the confined native selection. The manifests retain exact expanded commands. The final selection names are `test_collection18_selected_row_dispatch`, `test_collection18_row_dispatch_fail_closed`, `test_collection18_row_dispatch_real_detour_controls` and `test_collection18_row_dispatch_legacy_live_receipt` in the test file above.

Controls cover changed/equal, both changed, both equal, republisher, MID/FName/color mismatch, unreadable and changing row, reentry, concurrent foreign dispatch, orphan dispatch, multiple distinct dispatches, overflow, invalidation, close while pending, full-prefix signature failure, install failure, unexpected scheme/span and parser tampering. The close control retains an in-progress sidecar with a pending unreturned child and unresolved setter result. Rows become PAGE_NOACCESS during native dispatch and are freed before helper return; native assertions check forwarding and incoming/outgoing LastError. The SHA-pinned previous live sidecar parses without gaining dispatch proof.

The `/O2 /MD` fixture links the existing real PolyHook/Zydis/Zycore/asmjit/asmtk libraries for getter, lower setter and dispatcher detours. It exercises actual getter branches/trampolines, synthetic native changed/equal setter behavior and native CALL return PCs. It does not execute the original game's setter body; that instruction-level meaning comes from root's native contract.

All 29 native fixture logs, final sidecars and pending sidecars are retained by hash in this [artifact index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a61ded4e2b8ebf5b2fd0e9a453a728298b09dba222f3211325a7c715673e704e.json). Representative actual-PolyHook evidence:

- Changed/equal: [native log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/99dafb11d05c03d4daf506870ef69c0eec05ff4c71674611d6d2c19c369b45f6.log), [sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e5b3daa1815db30dfbf21e65053241f60938623ee04e51bc5e2de363a8f3ad68.json).
- Both changed, distinct MID/setter IDs: [native log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a03b46c95eaf0e490bdc7a4d76314fe341f82aa05fdc913aa04612202ad65fe1.log), [sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/20c0eea8a19ea00488c14ce75622e5b70b15f2f9ed1bb2952fbae5726820a6ef.json).
- Both equal: [native log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/71580895113384f2484a3ec2bd1d16da45f74fa4ed5b855f319df1359dc16b56.log), [sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4037fe3d82a6b6ca4e48be0b4a33443c7d4f115235feb00d3504c13084509e10.json).

Reconstructible runner source snapshots: [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-e6f662a5e950adf17f07d205a85cfe74822e050cfcbb6d56f2fe27048f295b67.json), [64/162 GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-cdea9e5b6b9797539e72ea64ca203712d0522708df1d57f3954aef30df0a03f9.json), [final GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-c22827af4090f8126ce316c51e8b6d0c7df03cd289d3defe225298527fa1a0f7.json). The last source change was test-only retention of child logs. This handoff was written afterward.

## Root follow-through and remaining limits

Root should build in the existing layout, verify mapped game/runtime/framework/observer/replay identities and the full prefix/11-byte VALLOC2 installation, then capture the selected interval. Inspect each linked setter separately: complete ancestry and matched tuple at `+0x1F1E528` supports `true`; complete coverage with zero children supports `false`; incomplete coverage must remain `null`. The prior two setters are a target to measure, not expected observations imported into production.

A matched child proves only execution of the CPU row-write branch. It does not establish old-value inequality under concurrent writes, owner generation or lease, material source asset, chosen-color source, exact native tick, writer exclusion, proxy/task/render/GPU completion, presentation-only classification, rollback admission or retirement permission. Those native/lifetime/completion facts remain unresolved. No further reverse-engineering fact was needed to implement this entry under the supplied contract; actual mapped-game installation and behavior remain root's live verification.

Local observer validity is demonstrated by these focused tests. Simulation, required lifecycle, visual coherence, recovery, live performance and qualification are not measured here.
