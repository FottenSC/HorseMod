# Niagara reflected spawn to registration: bounded native map

Read-only Ghidra MCP inspection of the existing `SoulcaliburVI.exe` program, image base `140000000`. Every query passed the program name explicitly; `EBOOT.elf` was open but not selected. No database edits, new imports, scripts, build or live process were used for this map. Function addresses and offsets below are native observations, not a safe rollback disposition.

| Boundary | Verified behavior |
| --- | --- |
| `141BEE900`, `141BEEA90` | Reflected UFunction parameter thunks unpack object/value parameters and call `141BCC5A0` and `141BCC730`, respectively. They provide a script-reachable route; no manager-task continuation is identified in these thunks. |
| `141BCC5A0`, `141BCC730` | Each calls `141BA5A20` to create a component, writes its Niagara asset at `+0x808`, can weak-bind and dispatch instance callbacks if already registered, then calls `141D58BA0` to register it with the world. The attached form also resolves its source component/world. |
| `141BA5A20` → `141BD3F90` | The class accessor registers the literal `NiagaraComponent`, size `0x860`, then `StaticConstructObject_Internal` creates that class. This distinguishes these routes from `SpawnLuxParticleSystemComponent` at `1408A3920`, which constructs a Cascade particle component despite sharing registration code. |
| `141D58BA0` | Checks indexed UObject flags and owner, may call component virtual `+0x368`, writes the world at component `+0x1c8`, then calls `141D42EF0` and later registers tick functions. These are side effects before a later initializer-only check. |
| `141D42EF0` | Calls component virtual `+0x280` when its `+0x188` bit 0 is clear; then may call virtual render/physics registration slots and dispatch/compact callbacks. Niagara's table at `1437FA218` has `141BC65E0` in slot `+0x280` (`1437FA498`). Dynamic type still needs a live identity receipt for a particular call. |
| `141BC65E0` → `141DA5F50` | The initializer first invokes a prefix that writes component `+0x188` bit `0x100000`, may call virtual `+0x3a0` and `141C4F720` when bit `0x200000` is set, then allocates/publishes instance `+0x810/+0x818`, binds asset, rebuilds and dispatches instance callbacks. |

Queries: `decompile_function` on `141BC65E0`, `141DA5F50`, `141C4F720`, `141D42EF0`, `141D58BA0`, `141BCC5A0`, `141BCC730`, `141BA5A20`, `141BD3F90`, `141BEE900`, and `141BEEA90`; `get_function_xrefs` on `141D42EF0`, `141D58BA0`, `141BCC5A0`, `141BCC730`; bounded `search_instructions` for `CALL` with operand `+ 0x280`. The generic slot search returned many unrelated virtual calls; the class-accessor and direct-call chain above supply the relevant type evidence.

The [selected-window diagnostic](rollback-g1-niagara-zero-hit-selected-window-2026-09-24.json) installed healthy and recorded zero calls at the two monitored helper entries. That result covers only the selected A210/B217 candidate. G1 still needs a before-construction version/owner witness and a valid disposition for an actual supported Niagara spawn, or a proved eligibility exclusion before application. The existing queued-tick guard does not supply that continuation.
