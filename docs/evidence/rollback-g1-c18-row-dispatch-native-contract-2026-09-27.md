# G1 selected C18 vector-row mutation discriminator — native contract, 2026-09-27

Root inspected `SetMaterialInstanceVectorParameter` `141F1E420` and `DispatchMaterialInstanceVectorProxyUpdate` `141F05150` in the existing `SoulcaliburVI.exe` Ghidra program through native MCP. The [selected live receipt](rollback-g1-c18-mid-slot-live-2026-09-27.md) already identifies two slot-linked lower-setter calls. This contract identifies the smallest next observation that distinguishes a changed CPU row from an equal-value setter call. It gives **no** ownership or rollback permission.

## Exact mutation branch

The setter scans the contiguous 0x28-byte `VectorParameterValues` array at MID `+0xB8` by full 64-bit FName. It may append a row. At `141F1E4E1` through `141F1E514`, four `UCOMISS` comparisons test the supplied floats against the row at `+8`. Equality jumps to `141F1E553` and returns without the vector proxy dispatch. The other branch executes `MOVUPS [RAX+8],XMM0` at `141F1E51F`, then calls `DispatchMaterialInstanceVectorProxyUpdate` at **`141F1E523`**, whose return PC is **`141F1E528`**. A call at that exact return PC, nested under one of the selected setter invocations with the same MID and FName/color, proves that this setter executed the CPU row-write branch. It does not prove that the old bits differed from the new bits after all possible concurrent writes, or that later tasks finished.

Ghidra reports only two direct callers of the dispatcher: `141F1E523` from the setter and `141F1A5A5` from `UpdateMaterialInstanceRenderProxyParameters`, the republisher. The dispatcher receives `RCX=UMaterialInstanceDynamic_Partial *` and `RDX=FVectorParameterValue_Partial *`. The row has FName at `+0`, four floats at `+8`, GUID at `+0x18`; the reflected `VectorParameterValue` registration at `14262C490` validates size `0x28` and these offsets. On the threaded route, the dispatcher copies FName, color and up to three **raw** proxy pointers into a queued command. Its entry is before task or GPU completion. The direct route calls proxy update synchronously, but still does not establish operation-scoped ownership or all render completion.

## Hook and receipt boundary

No current HorseMod, RE-UE4SS or tools source contains an owner for `141F05150` or `DispatchMaterialInstanceVectorProxyUpdate`; current source was searched before proposing an observation hook. The existing process-pinned `NativeReplayMaterialTaskGuard` owns the selected Start, helper, getter and lower-setter hooks. Any optional dispatcher detour belongs to that owner, must forward every native call exactly once, preserve `GetLastError`, and fail closed if installation or the selected receipt is incomplete. It must not alter mandatory CPU-veto/complete-B/retirement guards or hook the already signature-checked wrapper `141F45940`.

Native MCP disassembled the dispatcher entry as:

| Address | Bytes | Instruction |
| --- | --- | --- |
| `141F05150` | `40 53` | `PUSH RBX` |
| `141F05152` | `41 56` | `PUSH R14` |
| `141F05154` | `48 81 EC 88 00 00 00` | `SUB RSP,88h` |
| `141F0515B` | `80 3D DE C6 44 02 00` | `CMP byte ptr [144351840],0` |

The current checked-in PolyHook's eight-byte destination-holder minimum should round this straight-line prologue to **11 bytes** with VALLOC2. This is a hypothesis to verify with the actual `x64Detour` implementation and optimized real-library fixture, exactly as for the previous getter/setter hooks; no patch may be accepted on an unexpected copied range, fallback scheme or mapped-game prefix. The first 18 bytes above are available for a full-prefix signature check. The existing wrapper and selected helper signatures/caller ancestry must still be checked.

An observation-only receipt should bind the dispatcher entry to the **currently active selected lower-setter call**, require return PC `base+0x1F1E528`, equal MID, full FName and four bits read from the still-live row, and preserve the existing getter→setter/Start/helper ancestry and selected thread. Record zero, one or more dispatches separately for each linked setter. A matched dispatch proves its row-write branch; a complete selected interval with zero matched dispatches supports equal-value no-op for that interval only. Calls from the republisher, foreign threads, reentry, ambiguous ancestry, unreadable row, capacity loss, or incomplete returns cannot be silently counted as selected proof. Do not keep a raw row pointer for use after native return. The receipt must continue to state ownership/generation, material source, native tick, concurrent-writer exclusion and proxy/render/GPU completion as **unproven**.

The retained sidecar [for this selected interval](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3d0a33dc9d253dd471bac71cf438897854557128470eca0a5e9435f825ccb396.json) has four complete getter/setter calls but no dispatch child. A production-boundary local regression can start with that same synthetic native sequence, make the lower setter take first the changed path and then the equal path, and require the receipt to distinguish them before a second bounded live run. Existing tooling and build directory should be reused. Normal rendering/complete-B qualification is outside this diagnostic.

Ghidra plates at `141F1E420` and `141F05150` were updated, saved and read back. An EOL comment was set at `141F1E523`. The setter's decompiler local was renamed from misleading `pFieldMap` to `pVectorParameterArray` and verified in the decompilation. The existing `FVectorParameterValue_Partial` reflected type was reused; no speculative struct edit was needed.
