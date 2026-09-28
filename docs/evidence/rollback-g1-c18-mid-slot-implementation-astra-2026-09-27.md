# G1 selected C18 material-slot receipt â€” coding handoff, 2026-09-27

Implemented the observation-only extension through the existing process-pinned `NativeReplayMaterialTaskGuard`. Root owns Ghidra, the project build, deployment and live validation. This checkpoint contains local compiled fixture results only. It does not advance G1/G2 or admit historical restoration/retirement. The launching process was explicitly configured for GPT-6 Astra, Xhigh, `desktop.daybreak-enabled.enabled=false`.

## Native basis and hook ownership

The implementation uses root's [native hook contract](rollback-g1-mid-slot-native-hook-contract-2026-09-27.md), [first live correlation](rollback-g1-c18-start-helper-live-2026-09-27.md), and [Ghidra checkpoint](rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md). A repeated HorseMod/tools/RE-UE4SS source search found no other owner of the new entries. No Ghidra database work was performed by this coding agent.

Only GetMaterial `141DC8220` and the lower setter `141F1E420` receive new detours. The count getter is unhooked. Wrapper `141F45940` remains unpatched, preserving `Sc6ReplayColdParticleMaterials.inl`'s direct-call signature check. The existing Start/helper owner and six mandatory CPU-veto hooks remain in place.

Startup checks the complete supplied 19-byte getter, 16-byte lower-setter and 23-byte wrapper prefixes, plus the selected provider vtable's getter target. New detours use VALLOC2 only, without code-cave or longer in-place fallback. A preflight uses the existing x64Detour disassembler and branch expansion algorithm to refuse any unexpected overwritten range before publication; target, selected scheme, trampoline and actual patch size are checked afterward. This checkout's x64Detour uses its **eight-byte destination holder** as the minimum for its six-byte indirect jump. Therefore the verified copied spans are **10 getter bytes** (TEST, JS, CMP) and **11 lower-setter bytes** (through SUB RSP,28h). JS requires relocation; the getter's JGE stays in place. An initial exact-size assertion expecting six lower-setter bytes failed in the real-library control and was corrected after inspecting that implementation.

Installation rejection closes the new diagnostic: `material_calls.ready=false` and `complete=false`. A published partial hook remains pinned and forwards, and startup cannot retry it. This diagnostic failure does not relabel the six mandatory CPU-veto hooks or change their existing admission semantics. The fixture explicitly covers partial getter-only installation and a branch that would expand the getter's copied span.

## Receipt and limits

The existing version-1 `start_helper_correlation` is preserved and extended with optional version-1 `material_calls`. Historical sidecars without this child remain readable. Child storage holds at most **128 calls** across the one selected broadcast. Each row retains an event-sequence ID, ordinal, helper/Start IDs, nested child parent ID, thread, entry/return sequence, caller addresses/RVAs, provider and slot or MID/FName/color pointer/four exact float bits, and a scalar getter result. No returned MID is dereferenced or treated as a generation or lease.

A linked setter requires all of the following:

- One current helper with its direct Start parent on the selected thread, and the independently checked actor/provider pointer links from the first receipt.
- GetMaterial's actual provider equals that helper's provider; its return PC is `+0x8D5892`, with slot order starting at zero.
- The lower setter returns to `+0x1F45957`. The wrapper's own caller must be `+0x8D58DD`. The second return cell is read at lower-entry return-cell **+0x40**, derived from the verified wrapper's `SUB RSP,0x38` and CALL; it is read only after matching the direct return PC and startup wrapper signature.
- The latest returned, unconsumed getter under that helper equals the setter MID, with matching helper FName and all four color bits. This is the runtime association supported by root's `RAX -> RDI -> RCX` flow, not object ownership.

Reentry, foreign hooked calls during the interval, wrong routes/arguments, repeated nonnull getter pointers, overflow, invalidation and incomplete return pairing prevent a complete receipt. Earlier row links are entry-time observations; consume them only with the enclosing complete receipt. Concurrent mutation by uninstrumented writers is **not excluded**. Count calls, override-versus-asset source, actual vector-row change, allocation generation/lifetime, native tick and GPU/resource completion are not claimed. The existing first receipt's `mid_writes_observed=false` and unresolved fields are unchanged.

Both hooks forward native exactly once, preserve return values and incoming/outgoing GetLastError, and perform return bookkeeping using owned metadata only. An atomic inactive fast path avoids the lock and borrowed reads outside selection. No native call runs under an observer lock; material hooks never acquire the VFX lock. No UObject::IsReal was added.

The guard remains reserved at **1 MiB** including eight detour/trampoline allowances, checked by static assertion. The VFX observer remains **4 MiB**, including its enlarged correlation scratch copy; 32 KiB was transferred from its serialization capacity to accommodate that copy. Existing serialization failure/overflow handling remains fail closed. These reservations do not account native resources as freed. Complete-B, rollback admission, retirement and GPU/resource guards were not relaxed.

## Local RED/GREEN and retained source

The original production-boundary [RED log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7f4042cf88f9d52b2f70453ccd83e2691aa479297f003d00b8d659a121d28525.log) and [RED source snapshot](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-6f73ad0d9091fa9b66fce479dd91f7f1adbe7eea9bf924db44b3b1ed82edb4e3.json) predate implementation. The only scope adjustment was dropping optional count-hook expectations; native count calls still forward three times. The defect remains tested by two actual slot returns and a setter after native inputs change from the entry census.

- **34 focused GREEN**: actual slot-return receipt (controlled hooks and `/O2` real PolyHook), 17 fault/lifetime controls, two real-detour expansion/partial-install controls, and 13 original Start/helper controls. [Log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3c70b49eab67a26beb23d95d1d7ba5e9626c44714aebbf022c674361d93ff126.log), [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/6e0e5551ca37d26ea074564ee1220420fa89b50bffb0a5efa2c28efa3c5024af.json).
- **134 nearby GREEN**: mandatory material guard startup/controls, selected producer/retirement boundaries, zero-serial/provider census, shape capacity, writer receipts and combat-selection rejection. [Log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9ece4bc08ed05a6a0799ec2b859a86fbd39e2ebcfe1031056dcd4d88ed3abde6.log), [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/08323c7699c22d7f04539a4ba9e629f068c33b18be60a424f086b8751d3d29a0.json). Subsequent changes added real-detour test coverage and one parser consistency check, covered by the final focused run; production C++ was unchanged.
- [Final reconstructible production/test source snapshot](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-b37fffd9b5e190a48b6d6c71980811fd8280c08b9ab82a51daab982bd7e6cff9.json), including the untracked files. The evidence links in this handoff were updated afterward.

Retained fixture sidecars: [controlled-hook success](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f2ed69666bb7e2831f5ab00f48456b11ad045bba259db9630f0ba74d27cf4356.json), [optimized real-detour success](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e381e578c0972862f09e7fc0a504dd4b2a9653597f28aae5b990ea2f712c83cf.json), [expanded-span rejection](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/92e7feb0ab6d96abe9a21541be81cdb67044571223c83145aff7045353f4a004.json), [partial-install rejection](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/fa6e6be2e71b14f6b3b3adcbfb3fa9a03774fdf68778811b5b140d2e9e8659bb.json).

The existing fixture supplies only external native services/storage. It executes real synthetic-image call PCs and the verified wrapper stack copy; no return-address intrinsic or production receipt is mocked. The real-library variant links the already built PolyHook/Zydis/asmjit dependencies from the existing build layout, and checks relocated negative-slot JS, native out-of-range JGE, and fallthrough before selection, using distinct native tail probes that reject an incorrectly taken or skipped branch. A revoked-MID-page control passes without an observer dereference. Parser corruption checks reject altered ancestry, pointers, caller RVAs, slot, FName/bits, return pairing and positive ownership/mutation/source/tick claims. One fixture invalidation control initially attempted to consume an intentionally invalidated diagnostic return; its expectation was corrected without weakening production rejection.

Reproduce with `python tools/replay_test.py local --layer native-contract` plus these repeated `--test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::<name>` selections: `test_collection18_actual_material_slot_returns_and_setter`, `test_collection18_material_slot_fail_closed`, `test_collection18_material_detour_rejects_expansion_and_partial_install`, and `test_collection18_start_helper_correlation`.

No broad suite, project build, deployment or live game was run. Observer behavior is locally tested; live selected getter/setter behavior, simulation, coherence, recovery, lifetime/resource ownership and performance remain unmeasured by this change. Root's next build/live check must establish mapped hook installation and a complete selected receipt. Further native work is still needed for exact tick, chosen color/source asset and readers, actual setter mutation branch, provider body/lifecycle and completion/ownership before any exclusion or recovery admission. No additional hook-contract RE is currently blocking this implementation.

## Exact changed files

The preexisting dirty/untracked first correlation implementation and unrelated work were preserved. This task changed these six production/test files and added this handoff:

1. [NativeReplayMaterialTaskGuard.hpp](../../HorseMod/horselib/deterministic/NativeReplayMaterialTaskGuard.hpp)
2. [NativeReplayVfxCompletionObservation.hpp](../../HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.hpp)
3. [NativeReplayVfxCompletionObservation.Collection18.inl](../../HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl)
4. [replay_control.py](../../tools/deterministic_qualification/replay_control.py)
5. [replay_vfx_completion_observation_selftest.cpp](../../tools/replay_vfx_completion_observation_selftest.cpp)
6. [test_vfx_completion_observation.py](../../tools/deterministic_qualification/tests/test_vfx_completion_observation.py)
7. `docs/evidence/rollback-g1-c18-mid-slot-implementation-astra-2026-09-27.md` (this file).

`AGENTS.md`, `docs/rollback-status.md`, cold-wrapper signature checks, and root's existing native/live evidence files were not edited.
