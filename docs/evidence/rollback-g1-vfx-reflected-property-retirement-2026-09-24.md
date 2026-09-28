# G1 VFX reflected property and mod retirement — bounded read-only audit

Existing `SoulcaliburVI.exe` Ghidra program, image base `140000000`, game SHA-256 `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`. No database, production source, test source, build, or deployment changes were made.

## Reflected route

The manager class registration `FUN_140cbdc10` constructs a reflected dynamic multicast property named `OnVFxFinished` and passes offset `0x388` to `FUN_140947ff0`. The checked-in generated declaration `HorseMod/include/SoulCaliburVI/LuxorGame/Public/LuxVFxInstanceManager.h` marks this delegate `BlueprintAssignable`, `BlueprintReadWrite`, and `EditAnywhere`; `OnVFxFinishedDelegate.h` declares its one `int32 InstanceID` parameter. These declarations support a generic reflected binding or assignment route in addition to direct native `AddUniqueWeakNamedCallback` calls. They do not prove that a particular cooked script exercises it. The direct helper caller census alone cannot close the writer set.

`ULuxTraceComponent` class registration `FUN_140c0c8e0` registers its own named `OnVFxFinished` function through `FUN_140c23ae0`; native binder `1408CDBB0` references `&ULuxTraceComponent::OnVFxFinished`. That receiver function is separate from the manager's reflected delegate property. The `OnVFxFinished` string xrefs are `14345ca60` to manager registration, `143460720` and `14345e010` to trace function registration, and `14335bf30` to the native binder.

## Mod ownership transitions

`Sc6ReplayVfxState::PreparedManager::Write` at `Sc6ReplayVfxState.cpp:2878` writes every manager array header, including listener offset `+0x388`. `ValidateBinding` at line2734 checks current thread, live manager generation, object leases, battle-manager identity, and epoch; it does not prove native quiescence while the header is written or through subsequent dispatch.

`PreparedManager::Clear` at line2368 frees either previous or target array allocations at lines2383-2385. `Commit` at line3308 validates then calls `Clear(true)`, retiring previous backing. `BeginExecution` at line3112 validates publication, then nulls each target allocation address and clears target array metadata at lines3122-3128 because native code owns the published target during execution. A diagnostic cannot keep treating that target backing as mod-owned after execution admission. This adds mod retirement and ownership transfer to native registration, dispatch pruning, manager EndPlay, and manager destruction as exact boundaries needing a common execution/lifetime contract.

The native dispatcher `1403D74D0` copies 16-byte weak receiver/name rows, resolves the copied receiver twice around named function lookup, and invokes virtual `ProcessEvent` at vtable offset `+0x1F8`. The trace callback `140C40570` → `1408D3F20` changes slot identity, attachment state, and weak membership. Its attachment deactivation path may broadcast another delegate at component offset `+0x1B8` (`141D41870`); no proof excludes callback reentrancy that retires a later copied receiver.

## Disposition and next resolving test

The retained listener mutation RED still shows same-count serial replacement reaching the native snapshot after admission. Astra high's independent code/architecture assessment found no safe GREEN implementation yet. The required hypothesis is that all accesses to this manager's listener storage share an enforced owner or barrier, and receiver lifetime stays valid through callback invocation, including reflected mutation, mod publication/retirement, callback reentrancy, and teardown. Resolve the concrete generic reflected writer instructions and the enclosing thread/barrier for one nonempty finish path. An off-owner write, unprotected free/reallocation, or receiver retirement between resolution and invocation falsifies that hypothesis. If static evidence leaves one precise uncertainty, use one shortest compatible nonempty finish observation with the existing hook owners and stop on first failure. Do not retry an unchanged live campaign or green the RED with a pre-copy comparison alone.

Observer evidence remains limited to the valid first122 in-run records; final process sidecar overflow remains invalid. Selected prerequisite mutation complete-B recovery and120 matching continuation ticks remain a separate passing result. VFX listener-mutation recovery, visual coherence, and rollback-update cost remain unproven. No source change justified tests/build at this checkpoint.
