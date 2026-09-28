# G1 selected trace before-effects boundary — 2026-09-27

## Baseline

The existing checkout is dirty; no files were reset. `python tools/replay_test.py status` recorded a clean deployment journal (`replay-51cc1ab2a02549329960393f43eb70e2`) and current compatible build/local manifests. A separate process inventory found no Soulcalibur game process; Steam PID 9240 remained running. The latest retained [project build](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/44bcce19b91873f119c38b8c4b9cdf4435dfe7e40cd9f320f3e77e775a9b0797.log) passed two CTests and the [full local suite](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c08cc8e4a566f375160ae9ae8477a6e58fd289c946205cf259517a5b3b4fbbb5.log) passed 1,209 tests. These are retained source-compatible baseline receipts, not live rollback qualification.

Root reran the selected `test_vfx_finish_dispatch_revalidates_live_completion_rows` production-boundary regression through `python tools/replay_test.py local --test ...`. Both native-contract cases passed; the [immutable console log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/80c96e8da8d7c8f0ba35d139f227a33aa519d35dcbeba3bdfb62a14d8ab60e04.log) records the selection. The checked-in readiness description calling this particular case RED is stale. The test proves the dispatcher rejects same-count live receiver/FName replacement before its native snapshot; it does not establish B recovery or producer ownership.

## Native consumer and producer contract

All addresses refer to the existing `SoulcaliburVI.exe` Ghidra program. Another open program was current, so each program-scoped MCP call named `SoulcaliburVI.exe` explicitly.

- `StartLuxTraceComponentTrace` at `1408D8C40` resets component runtime/timing, resolves or creates trace entries, updates material parameters and scheduling, and later calls `BindLuxTraceVfxFinishedAndResetFadingTraces` at `1408CDBB0`. The late binder adds a weak named receiver to manager `+0x388` and also retires fading state: manager slot, state `+0xD8`, attachment `+0xB8`, completion membership `+0x3F0`, actor/mesh visibility, and trace latch. Its presence is therefore not a before-first-effect lease.
- `ALuxVFxInstanceManager_OnParticleSystemFinished` at `14089F870` finds the first matching primary `+0x3E8` record, broadcasts its slot through `+0x388`, then removes all matching primary records.
- `DispatchMulticastScriptDelegateAndPruneInvalidEntries` at `1403D74D0` copies live 0x10-byte weak/FName rows, resolves each receiver and function and invokes `ProcessEvent` synchronously. The copy makes callback iteration stable; it does not retain the receiver or reverse the enclosing producer's effects.
- The reflected trace receiver enters `ULuxTraceComponent_OnActiveTraceVFxFinished` at `1408D3F20`. A matching state has its slot `+0xD8` cleared, its attachment virtual `+0x238` invoked, and the exact weak pair removed from component `+0x3F0`.

The existing `ReplayVfxFinishDispatch` validates the live delegate header, every row, indexed receiver/function, manager and epoch before forwarding to native. Its failure route is terminal containment. It cannot recover B after an unsupported trace start has already mutated state. Root updated and saved the `1408D8C40` Ghidra plate comment to record this before-effects boundary, verified the readback/decompilation, and saved the program. No function prototype or struct was changed.

## Implication for the next implementation

The smallest supported strategy must either admit and own the full enclosing trace-start transaction before its first mutation, including new children and later native/render work, or retain exact reversible state for those effects. A guard added only at `1408CDBB0`, manager `+0x388`, or dispatcher `1403D74D0` cannot establish that contract. The selected C18 `BaseColor` calls were CPU row no-ops in one live occurrence, but earlier material construction, finish callbacks, owners and completion remain separate dependencies. No seven-tick transaction, complete-B recovery, independent continuation or normal-render qualification is claimed here.
