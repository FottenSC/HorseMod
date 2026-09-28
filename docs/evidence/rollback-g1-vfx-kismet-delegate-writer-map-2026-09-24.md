# G1 VFX Kismet delegate writer map — read-only native audit

Queried the existing `SoulcaliburVI.exe` Ghidra program (image base `140000000`, SHA-256 `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`). No database or production/test source edit, build, or live run. This narrows the generic reflected writer gap in the preceding [property and retirement audit](rollback-g1-vfx-reflected-property-retirement-2026-09-24.md).

The manager class registrar `FUN_140CBDC10` constructs the reflected `OnVFxFinished` multicast property at manager offset `+0x388`. Its generated declaration is `BlueprintAssignable`, `BlueprintReadWrite`, and `EditAnywhere`. A Kismet property expression can therefore resolve that exact collection, although this static evidence does **not** show a particular cooked script doing so in the selected match.

| Registered Kismet native | Registrar and body | Effect after operand evaluation |
| --- | --- | --- |
| `execAddMulticastDelegate` | `1401C4BA0` → `140F74770` | `GNatives` evaluates collection then weak receiver/name. Body scans exact 16 byte weak/name rows, appends and possibly grows allocation, then prunes expired rows. |
| `execRemoveMulticastDelegate` | `1401C78B0` → `140F77500` | Evaluates collection and weak receiver/name, removes the matching row, compacts/decrements count, then prunes expired rows. |
| `execClearMulticastDelegate` | `1401C53E0` → `140F756F0` | Evaluates collection, sets count zero and, if capacity is nonzero, calls `ResizeTArrayAllocationExact16B` to release or resize backing. |
| `execLetMulticastDelegate` | `1401C6980` → `140F76750` | Evaluates destination/source expressions, calls `FUN_1409481E0` to copy count and 16 byte rows with possible allocation resize, then frees temporary source backing. |
| `execCallMulticastDelegate` | `1401C5260` → thunk `140F75310` → `140F6B400` | Evaluates the collection, builds parameter storage and calls shared `DispatchMulticastScriptDelegateAndPruneInvalidEntries` `1403D74D0`. The existing forwarding hook at that dispatcher can observe this call if the resolved collection is the manager's `+0x388`. |

These exact registrar links come from xrefs to the `exec*` strings in the game binary, not assumed engine source names. `execBindDelegate` `140F75210` creates/updates a weak/name delegate value; it does not itself append to the multicast collection. The four mutation bodies have no visible common lock or game-thread assertion in their decompiled bodies. An enclosing ProcessEvent/thread contract may exist and is not established here.

`FUN_1409481E0` has four direct inspected callers: `140948270` (property helper), `140F76750` (Kismet assignment), `1417549D0` and `14176BE40` (copies into other structure offsets). This direct-call census does not exclude virtual property copying, serialization, or raw writes. Native `AddUniqueWeakNamedCallback` `1408C9120`, native dispatcher pruning `1403D74D0`, manager constructor/destructor and mod `PreparedManager::Write`/`Clear` are separate writer/retirement routes.

The existing game's 48 byte entry prefixes read through Ghidra are:

| Body | Hex bytes |
| --- | --- |
| `140F74770` | `48895c240848896c24104889742418574883ec40488b4220488d2d81b5320333ff4533c048897a38488bda48897a300f` |
| `140F77500` | `48895c240848896c24104889742418574883ec40488b4220488d2df187320333ff4533c048897a38488bda48897a300f` |
| `140F756F0` | `40534883ec20488b42204c8d0d0fa6320348c74238000000004533c048c7423000000000488bda0fb60848ffc048894214` |
| `140F76750` | `48895c240848896c24104889742418574883ec40488b4220488d2da195320333f64533c048897238488bda488972300f` |

## Destination pointer across operand evaluation

Disassembly of the actual game bodies resolves the uncertainty in an entry/return-only Kismet hook. Add (`140F74770`) saves the destination from frame `+0x38` in `RSI` at `140F747B3`, **before** it evaluates the second operand at `140F747E4`. Remove (`140F77500`) does the same at `140F77543`, before the second operand at `140F77574`. Both use `RSI` for writes and unconditionally call `TArray_PruneExpiredWeakPtrs_Stride16` `1403EA210` with that collection in `RCX` on their non-null path (`140F74892`, `140F77634`). The frame slot itself is not proven stable after the second operand, so a Kismet return wrapper must not attribute the write using frame `+0x38`.

Clear (`140F756F0`) evaluates only one property operand. It loads the destination directly from frame `+0x38` at `140F7572B`, clears count at `140F75738`, then either returns or tailcalls `ResizeTArrayAllocationExact16B` `142025710`. Assignment (`140F76750`) saves the first evaluated destination in `RDI` at `140F767A0`, then evaluates the source at `140F767BD`; it calls `FUN_1409481E0` at `140F767CE` with destination in `RCX`. The shared copy helper receives the destination explicitly and can resize/copy the 16 byte rows.

These are instruction-level destination contracts for the named Kismet bodies. A bounded observer at the **shared prune** `1403EA210`, **shared copy** `1409481E0`, and **clear entry/return** `140F756F0` could see the actual collection pointer without assuming frame-slot survival through nested expressions. Prune is reached after add/remove's direct write and may itself remove expired rows. Copy is reached before assignment's copy. Clear can be read at return only after validating the frame and live indexed manager; its single operand path is distinct from add/remove. A shared helper can have unrelated callers, so each hit still needs manager-vtable/index/generation filtering. These points identify reachability and observed overlap only; they do not synchronise the native writes or lease copied receivers. They also do not cover arbitrary raw writes or all serialization.

Additional verified 48 byte prefixes: `1403EA210` = `4056574883ec2833f6488bf93971080f8e9100000048895c24488bde48896c24508d6e014c89742420448bf648897424`; `1409481E0` = `40574883ec20488bf9483bca7476448b410c48895c24308b5a084889742438488b3289590885db751b4585c07516488b`; clear prefix is in the table above.

## Forwarding ABI verified from native instructions

`1403EA210` consumes only `RCX` as a pointer to the 16 byte stride collection, preserves the Windows x64 nonvolatile registers that it uses, and returns with `RET` at `1403EA2BC`. Its callers do not inspect a return value. The body may compact rows and call allocation resize at `142185090`. A forwarding observer must call its original trampoline exactly once with the unmodified pointer before collecting a post state; the original has no contractually consumed return value.

`1409481E0` consumes `RCX=destination collection` and `RDX=source collection`; it reads source count/pointer and destination capacity, possibly calls allocation helper `141ACFF20`, then copies 16 byte weak/name rows. Every return path places the destination in `RAX` (`140948213` or `140948264`). A forwarding wrapper must return the original `RAX` rather than synthesize one. The Kismet assignment caller at `140F767CE` does not use that return but other direct callers can.

`140F756F0` is a Kismet native body with `RDX=FFrame*` (it reads frame `+0x20`, `+0x18`, `+0x30`, `+0x38`); the disassembly does not consume the incoming `RCX`. It calls one GNatives operand at `140F75727`, reads its destination from the frame at `140F7572B`, writes count zero at `140F75738`, and either returns at `140F75752` or tailcalls `142025710` at `140F75748` after restoring its frame. The tailcalled resize consumes `RCX=collection`, `EDX=0`, returns with `RET` at `142025772`, and its return register is incidental to this void Kismet exec path. An entry/return hook for clear cannot assume the frame's `+0x38` slot retains its destination if GNatives itself mutates it, so clear needs either a proven post operand site or an explicit collection boundary. A generic entry/return wrapper should not claim a clear destination without that proof.

## First bounded shared-writer live prefix

After the forwarding-only prune/copy observer passed local/native checks, one A210/B217 selected consumer-mutation live run reached complete B recovery and 120 independent continuation ticks, but its VFX diagnostic **overflowed** at 128 records with 13 dropped calls before the tick483 phase close. This invalidates whole-phase observer completeness. The first 64 complete entry/return pairs comprise 36 dispatch, 14 native registration, and 14 shared-prune calls; there are zero shared-copy calls in this retained prefix. All retained records are on one observed thread with zero unstable/truncated records. All 14 prune caller return addresses resolve to RVA `0x8C9232`. Independent disassembly of `AddUniqueWeakNamedCallback` `1408C9120` confirms its call to prune at `1408C922D` and return address `1408C9232`, after its direct add/unique path. Thus those hits establish an actual native registration/prune route and do not show Kismet add/remove reachability. Zero Kismet or copy hits in an incomplete prefix cannot exclude those routes.

Immutable raw sidecar: `build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6830a3732c446fa3a2cbd98651e7863c8bc28605e9200f3aab0a683ec7e9209e.json` (SHA-256 is the filename). Candidate report: `.../evidence/objects/df4fdea0c32f8111bbcd0e607ddfa3420facca1387b4c670b3768e509d7699fb.json`; complete run manifest: `.../evidence/manifests/ad61a9fd61a2b22488ae769c1d8ed3f4ea1f5329b27c819f829ab02471a7625c.json`. The next diagnostic action is a bounded capacity/serialization fix with production-boundary RED/GREEN and required build/native checks before exactly one new shortest live run.

**Next resolving test:** Astra high should assess a bounded forwarding diagnostic at these explicit destination boundaries, using verified signatures and existing hook ownership, to identify whether they reach this indexed manager's `+0x388`, on which thread, and whether they overlap observed dispatch. Preserve operand evaluation and callbacks, treat unobserved routes as unresolved, and retain a local production-boundary RED/GREEN before one shortest live diagnostic. A zero-hit observation does not prove these generic routes can never execute. Even complete writer reachability leaves receiver lifetime through weak resolution and `ProcessEvent` to prove before enabling the existing mutation-enforcement RED.
