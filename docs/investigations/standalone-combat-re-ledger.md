# Standalone combat RE ledger

Scope frozen 2026-08-16: Soulcalibur VI v2.31 executable SHA-256
`f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`,
ActiveBattle, open plane, two human roles, P1 Raphael (14) and P2 Maxi (3).
Reversed roles, other characters, creations, stage geometry, indirect targets,
and unproved asset/RNG variants fail closed.

## Milestone 0 native oracle

| Field | Evidence |
|---|---|
| Native root | `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`, `void __fastcall(ALuxBattleManager_Partial *)` |
| Reachability | Direct ActiveBattle state-machine caller `LuxBattleManager_Tick_MainStateMachine_At1461 @ 0x1403FBF30`; existing subordinate producer hooks remain in `NativeReplayTraceHook.hpp` |
| State read | Two current-input snapshots; MoveVM state shorts and scheduler/SubVM state; pose/matrix/clip sources; KHit list; damage/vital state; position, facing, root step, and velocity; LFSR and xorshift96 histograms |
| State written | Oracle is read-only. It records pre/post samples and ordered trace events; it never feeds a later native result into the portable step |
| External effects | Trace emission only. QPC, PID, thread ID, image base, absolute pointers, and return address are process metadata, not canonical gameplay state |
| Portable owner | `tools/moveset_parser/native_combat_oracle.py` |
| Oracle | Artifact-bound `native_battle_tick_root_transaction` JSONL; canonical repeat comparison and first-divergence partitions have focused synthetic tests |
| Unsupported | Any root record without exact executable/KHD hashes or the admitted P1/P2 IDs; missing readable sources; topology outside the frozen pair |

Runtime byte-identical qualification is pending because the configured
GekkoNet FetchContent cache is partially patched and prevents HorseMod from
configuring. The source change itself passes an MSVC syntax-only compile up to
the pre-existing GekkoNet API errors.

## CALLCOND 0x01 — EvaluateIfOpcode

| Field | Evidence |
|---|---|
| Native root | `LuxMoveVM_EvaluateIfOpcode @ 0x1403732F0`, `ulonglong __fastcall(ALuxBattleChara_MoveSystemPartial *, int, ushort *)` |
| Reachability | 23,288 unique sites in hdr014/hdr003. Argument counts: 1=9,825; 2=9,505; 3=3,638; 4=196; 5=124 |
| Predicate domain | 137 first-word IDs. 23,286 literal sites. Raphael slot 2476 pc `0xB52E3` and Maxi slot 2501 pc `0xB62A3` are helper-parameterized and resolve through all three concrete callers in each bank to only `0x138D` or `0x139C` |
| State read | Typed current/opponent character fields, active lane, input snapshot/history, state banks, relation/stat tables, terrain probes, frame context, battle globals, and gameplay RNG as documented on the native root and its 20 direct helpers |
| State written | Always caches the IF ID at character `+0x1C6A`; selected guard/stance helpers may write their documented state. Most predicates are read-only |
| External effects | IF `0x007F` consumes one gameplay xorshift96 draw even for guaranteed thresholds. Helper calls and table lookups retain native order |
| Portable owner | Existing partial routes in `native_input_routes.py` and `lux_movement_vm.py`; complete production handler is not yet registered |
| Oracle | Authored-domain generator `lux_callcond_01_domain.py`; native predicate boundary capture still required for boundary-value differential fixtures |
| Unsupported | The first-word domain is closed. The stricter operand scan currently reports 78 unresolved caller-local/VM-state producer instances, so the complete handler remains `static-incomplete` and absent from `IMPLEMENTED_CALLCOND_HANDLERS` |

The generated evidence is
`docs/investigations/standalone-combat-callcond-01-domain-v1.json`. It includes
the exact 3,092 authored operand patterns, source annotations, asset hashes,
dynamic sites, and fail-closed blockers; regenerate it rather than editing it.
