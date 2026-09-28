# G1 PhysX callback lifetime: bounded native audit (2026-09-24)

This is a read-only audit of the existing `SoulcaliburVI.exe` Ghidra program and current production admission code. It does not establish a recovery protocol or a general exclusion for debris motion.

## Concrete vehicle callback lifecycle

- `1430F4C70` registers the scene-start callback `1430FBFC0` in global collection `144095F50` and scene-shutdown callback `1430FBFF0` in `144095FC0` through `1421377C0`.
- `142022930` creates the native physics scene, then broadcasts `144095F50` through `1429B4530`. On scene type zero, `1430FBFC0` allocates an 0x80-byte owner and calls `1430F5C20`.
- `1430F5C20` registers `UpdateVehicleComponentsBeforePhysics` in scene `+0x10` and `UpdateVehiclesForPhysicsStep` in scene `+0x80` through `1421377C0`, storing returned handles in owner `+0x60/+0x68`.
- `1420297F0` broadcasts scene `+0x10` and, outside substep mode, `+0x80` through `1420113E0`. `142055C40` broadcasts `+0x80` for a repeat substep.
- World scene replacement `1421C5950` clears the old scene's world binding `+0xF0`, destroys the old scene through `14200D940`, frees it, then binds the replacement. `UWorld_FinishDestroy` also calls `14200D940`.
- `14200D940` first calls `14202DE80`, which gathers scene `+0x160` and each `+0x130/+0x148` event and invokes a task-graph object's virtual `+0x48` on the gathered list. Only after this wait does the destructor call `142028E90` for each scene type.
- `142028E90` broadcasts `144095FC0` through `1429B4530`. On type zero, `1430FBFF0` looks up the vehicle owner, unregisters both scene callbacks through `141F605A0`, removes the owner map row, calls `1430F6870`, and frees the owner.

This proves an order within this destructor route: recorded event wait precedes shutdown callback removal. It does not prove that the event set covers every active substep, prevents new work after collection, or precludes reentrant scene replacement. A substep context retains a scene pointer at `+0xD0`; clearing scene `+0xF0` does not clear it.

## Shared collection behavior and verified entry signatures

`1421377C0` compacts, increases the count, may grow backing storage, and appends a callback. `141F605A0` finds a handle, invokes callable virtual `+0x48` to destroy it, frees its backing storage, disables the row, then compacts. `1420113E0` increments collection recursion `+0x64`, traverses entries backwards and invokes callable virtual `+0x68`, then decrements recursion. `140399DF0` defers structural compaction while recursion is nonzero; it does not prevent `141F605A0` from destroying a callable. No lock is visible in those three bodies. This does not establish actual concurrent or nested removal.

Native Ghidra memory and disassembly at the two selected observation entries:

| Entry | RVA | Win64 arguments | Verified first bytes |
| --- | --- | --- | --- |
| `1429B4530` lifecycle broadcast | `0x29B4530` | RCX collection, RDX scene, R8D scene type; void | `40564883ec30ff4164488bf18b415048895c244048896c24484032ed4c8974242883e8014c897c24` |
| `1420113E0` scene callback dispatch | `0x20113E0` | RCX scene collection, RDX scene, R8D scene type, XMM3 delta; void | `40564883ec40ff4164488bf18b415048895c245048896c24584032ed4c8974243883e8014c897c2430458bf00f297424` |

The current source inventory has no direct hook at either RVA. `NativeReplayTraceTaskGuard.Signatures.inl` owns world scene replacement `0x21C5950`; any new observation must coordinate with that owner and verify live binary bytes before installation.

## Production implication

`ReplayPhysicsStepConsumer.hpp::Admit` checks current callback handles, owner-map membership and zero vehicle count, but has no generation-stable lease from validation through callback consumption. `Sc6ReplayTaskGroup.cpp` site11 rejects before the substep wrapper and then terminally contains the already-published scene/event chain. This is neither suspension nor complete-B recovery. The existing component-task resume path does not own the scene's event chain.

The next native proof is the transitive dependency closure of `14202DE80`'s virtual wait: initial/repeat substep delegates, `+0xC8/+0xB0` completion ownership, both scene types, and callback return. Check reentrancy and whether startup/removal can bypass the lifecycle broadcasts. A bounded forwarding observation can test selected-phase overlap, but zero overlap cannot alone prove general writer exclusion or a recoverable enclosing task disposition.
