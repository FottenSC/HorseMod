# G1 Niagara reflected spawn UFunction bindings

Read-only native Ghidra MCP queries against the existing `SoulcaliburVI.exe` program, image base `140000000`; the selected live report identifies the game SHA-256 as `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`. Every query selected that program explicitly. No Ghidra database edit or new game run was made.

`141BD4BD0` registers the class literal `NiagaraFunctionLibrary` through `UE4_RegisterClassEx`, passing `141BD5B00` as its native-function registrar. `141BD5B00` calls `UClass_AddNativeFunction` with the table beginning at `143805BE8` and count 2. Raw bytes at the table resolve to these name/function pairs:

| Table entry | Function name pointer | Native thunk pointer | Thunk's direct call |
| --- | --- | --- | --- |
| `143805BE8` | `143803BB8`: `SpawnEffectAtLocation` | `141BEE900` | `141BCC5A0` |
| `143805BF8` | `143803C58`: `SpawnEffectAttached` | `141BEEA90` | `141BCC730` |

Independent string xrefs link `SpawnEffectAtLocation` to `141BE47A0` and `SpawnEffectAttached` to `141BE4F20`. Those functions construct the corresponding reflected UFunction objects at globals `144390628` and `144390630`. The native table, not those string xrefs alone, establishes the precise thunk binding. `get_xrefs_to` on `141BEE900` and `141BEEA90` identifies the table pointers at `143805BF0` and `143805C00`, respectively. The helper-to-constructor and registration chain is retained in [the native spawn map](rollback-g1-niagara-reflected-spawn-native-map-2026-09-24.md).

This proves the two exact registered reflected entry names. It does not establish which selected match callbacks could call them, exclude other Niagara construction or reset paths, or provide a resumable synchronous script continuation. The selected A210/B217 observer recorded zero helper calls, which remains a bounded observation rather than a G1 admission proof.
