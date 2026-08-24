# Deterministic native contract: reverse-engineering agent plan

Date: 2026-08-23  
Binary: `SoulcaliburVI.exe`, image base `0x140000000`  
Primary consumer: the replacement replay-seek and rollback `IGameStateAdapter`  
Primary evidence ledger: `docs/investigations/deterministic-simulation-contract-2026-08-23.md`

## Mission

Close the native-contract gaps that currently keep
`Schema::production_regions` empty. Work in the existing `SoulcaliburVI.exe`
Ghidra program through native Ghidra MCP tools and leave two synchronized
outputs:

1. a saved, improved Ghidra database containing verified names, prototypes,
   types, structures, globals, and comments; and
2. a new Markdown report at
   `docs/investigations/deterministic-native-contract-results-2026-08-23.md`
   containing the evidence and implementation contract needed by the next
   HorseMod agent.

This is a reverse-engineering task, not authorization to implement the native
adapter, reintroduce old rollback source, activate networking, or add a state
region to production. The result must distinguish proven facts, bounded
inferences, runtime-validation needs, and unresolved blockers.

## Required starting context

Read these files before changing Ghidra:

1. `AGENTS.md` for the mandatory MCP, naming, type, comment, completeness, and
   save workflow.
2. `docs/investigations/deterministic-simulation-contract-2026-08-23.md` for
   the current authoritative ledger and already-established boundaries.
3. `HorseMod/horselib/deterministic/Interfaces.hpp`, `Types.hpp`, and
   `Schema.hpp` to understand exactly what the adapter must supply.
4. Historical investigation documents only as search indexes. In particular,
   `rollback-effects-event-hub-investigation.md` and
   `rollback-beta-remaining-native-boundaries-re-investigation-2026-08-05.md`
   are not trusted contracts. Re-prove every reused conclusion from the open
   binary.

Do not import another Soulcalibur executable. Do not edit `.gpr` files or use
Ghidra scripts for database edits. Do not use Ghidra snapshot endpoints. Use
the program named exactly `SoulcaliburVI.exe`.

## Definition of an admissible state region

A state region is complete only when the report establishes all of the
following:

- native owner and a stable resolver from an already-validated root;
- exact type and byte extent, including dynamic bounds where applicable;
- complete direct and indirect writer set within the admitted lifecycle;
- gameplay readers and ordering relative to `LuxBattle_PerFrameTick`;
- constructor, reset, replacement, destruction, round, and scene lifetime;
- pointer fields classified as identities, owners, derived references, or
  forbidden raw restore data;
- capture phase, preflight predicates, ordered restore action, derived repair,
  post-restore verification, and exact undo requirements;
- canonical hashing rule, including padding and uninitialized-byte exclusions;
- classification as canonical gameplay, derived gameplay, client-local,
  persistent presentation, or ephemeral presentation;
- reachable unsupported cases and the exact fail-closed condition;
- supporting function/global/struct names, addresses, and Ghidra comments.

Unknown writers, unresolved ownership, unbounded containers, raw heap pointer
restoration, or unexplained shared RNG consumption block admission. A high
completeness score is necessary documentation hygiene, not proof that a state
region is complete.

## Work order

Follow the phases in order. Later phases depend on identities and ordering
proved earlier. At the end of every phase, save the Ghidra program and update
the Markdown results file before moving on.

### Phase 0: freeze scope and build the open-question ledger

- Reconfirm the per-coordinate call chain around
  `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`
  and `LuxBattle_PerFrameTick @ 0x1402DBC60`.
- Reconfirm the native round/frame identity and the exact point after input
  filtering but before simulation consumption.
- Inventory every direct callee and relevant virtual/indirect dispatch reached
  by one admitted traversal. Mark each as closed, out of scope with proof, or
  open.
- Copy the six current gates from the primary ledger into the results file as
  a live checklist. Do not silently omit a difficult branch.

Deliverable: one call-order table and one open-question table. This phase may
correct the current ledger if new binary evidence disproves it.

### Phase 1: input production, filtering, and round sequencing

Start from:

- `GetCurrentInputForFrameInputLogSlot @ 0x1403F0680`
- `GetCachedInputForFrameInputLogSlot @ 0x1403F0720`
- `InitializeFrameInputLogCacheForRound @ 0x1403F7C60`
- `ResetFrameInputSyncRoundState @ 0x1403F7D20`
- `LuxBattleChara_UpdatePlayerInputData_FromRoundCache @ 0x1403FCD10`
- `ProcessFrameInputLogCurrentInputRefresh @ 0x1403FDF30`
- `FilterALuxBattleMoveDispatchInputPairByFrameSlot @ 0x140427940`
- `LuxBattleManager_Tick_ProcessRoundStateSequence @ 0x1403FCE80`

Required closure:

- enumerate all FrameInputSync send/receive/cache writers, including virtual
  and callback writers, duplicate/stale packet behavior, and exact tag checks;
- recover manager-owned previous-input words, suppression masks, action-window
  state, callback collection ownership, and reset/destructor paths;
- resolve the producer and consumers of the manager callback collection at
  `+0x8E0` and revalidate the known `+0xB80` and `+0x1210` collections;
- prove which transport bookkeeping can be excluded without affecting later
  pair production;
- produce the exact input-state capture extent and restore order without
  restoring collection owners or UObject pointers.

Exit gate: every value that can change the post-filter input pair for a replayed
coordinate has a proven owner, writer set, lifetime, and restore rule.

### Phase 2: fighter, MoveVM, HgCpu, hit, reaction, animation, and motion

Start from:

- `LuxMoveSystem_BeginVMPump @ 0x14031C950`
- `LuxMoveSystem_EndVMPump @ 0x14031CAC0`
- `LuxMoveVM_TickCharaCommandScheduler @ 0x1402E52D0`
- `g_abLuxMoveSystemVMPumpState @ 0x144100C70`
- `g_abLuxMoveCommandPlayers @ 0x14470F390`
- `g_abLuxMoveVMSlotParamArray @ 0x14470E0C0`
- `g_abLuxBattleCpuCommandStatePerPlayer @ 0x144715400`

Trace both player lanes and every reachable state of the VMPump. Recover:

- fighter and opponent gameplay fields read or written during one traversal;
- command-player arena, normal SubVM, CPU-direct SubVM, HgCpu, scheduler, and
  slot-param semantics;
- all allocation classes, sizes, factories, replacements, destructors, vtable
  identities, back-pointers, and generation boundaries;
- hit creation, collision/contact results, damage, guard, reaction, stun,
  knockback, ring-out, and their ordered callbacks;
- animation clip/section/time state that feeds collision or motion, including
  clip replacement and derived cache rebuilds;
- root motion, position, velocity, facing, floor/contact, and coordinate
  conversion state used by later gameplay;
- which native rebuild routines are safe and complete after a semantic restore.

Apply `ghidra-investigate-type` whenever a generic pointer is used as a shared
state object across functions. Apply `ghidra-doc-function` to important single
functions once their surrounding ownership is understood. Never create a
struct merely to make raw offsets look tidy.

Exit gate: produce a pointer-free semantic snapshot design for both fighters
and their reachable execution graphs, or list exact routes/content that must
remain disallowed. No numeric heap address may appear in a restore-write list.

### Phase 3: RNG and floating-point environment

Revalidate all known RNG state and close the remaining consumers:

- `g_dwLuxBattleLcgRngState @ 0x14485EB28`
- `g_adLuxBattleLfsrState @ 0x14485EB30` and index `0x14485EB94`
- `g_stLuxBattleXorshift96State @ 0x14470E2C8`
- `g_stLuxBattleWindCombinedRngState @ 0x14470E2B0`
- `g_stLuxBattleMtState @ 0x144100EA0`
- `LuxBattle_InitRngAndHashPrimes @ 0x14034F610`

Required closure:

- enumerate all direct, inlined, virtual, and imported consumers reachable
  during owned simulation and presentation reconciliation;
- identify every UCRT `rand`/`srand` call site, its calling thread, and whether
  the current-thread CRT state has a usable native get/set boundary;
- if no safe CRT contract exists, define a scoped broker interception design
  with exact call-site allowlist, thread identity, nesting, and restoration on
  all exits;
- inspect traversal entry/callees for MXCSR and x87 reads or writes, exception
  behavior, rounding/denormal assumptions, and thread migration;
- specify capture/restore of caller FP state and a versioned normalization
  policy only where binary evidence supports it;
- prove RNG draw ordering when ephemeral presentation is suppressed.

Exit gate: every admitted RNG draw has one restored stream and stable ordering;
the caller's CRT and FP environment can be returned exactly on success and
failure. Otherwise specify the terminal blocker.

### Phase 4: stage, barriers, wind, camera, and dynamic world objects

Trace the state reached by the admitted traversal for:

- stage/map controller and round-bound geometry identity;
- walls, ring edges, barriers, floor queries, destructibles, hazards, and
  collision proxies;
- wind object graphs, oscillators, and their shared RNG use;
- camera or attention-camera state only where it feeds shared RNG, gameplay,
  target selection, or persistent presentation reconciliation;
- spawned/destroyed dynamic objects, arrays, sparse tables, delegates, and
  allocator generations.

For each dynamic graph, identify the stable owner, bounded count/capacity,
element type, allocation/reallocation/destruction sites, and pointer validation
strategy. Prefer semantic reconstruction using verified factories over raw
heap capture. Prove exclusions for systems dormant in an allowlisted
character/stage case; do not infer dormancy from names.

Exit gate: a stage/content contract can name exactly which native identities
and topologies are supported, and any allocation-generation change invalidates
the checkpoint atomically.

### Phase 5: irreversible presentation boundaries

Trace gameplay events through their last reversible state mutation and first
irreversible terminal for:

- audio voice creation/stop/update and BGM state;
- VFX spawn, update, destroy, material, color-fade, and persistent actor state;
- camera shake/cinematic triggers;
- listener/delegate broadcasts and callback collection mutation;
- UI, round announcement, result, replay, and other game-flow events.

For each event family, determine:

- whether later gameplay reads any resulting state;
- whether it consumes shared gameplay RNG before the terminal;
- the minimal value-only journal record and stable logical identity;
- speculative discard behavior;
- confirmed exactly-once commit key;
- persistent-state reconciliation required at a seek landing frame;
- cancellation or cleanup required after a failed restore.

Do not label an entire function presentation-only if it first mutates gameplay
state or consumes a shared stream. The suppression boundary must be at the
irreversible terminal.

Exit gate: every presentation-producing path is either deterministically
executed with only its terminal journaled, proven unreachable for qualified
content, or an explicit blocker.

### Phase 6: lifecycle, identity generations, and teardown

Build the complete event sequence for:

- match/lobby entry and baseline binding;
- fighter, stage, round, and heap-object creation;
- round exit and re-entry;
- scene/world change;
- disconnect and return to lobby;
- replay start, stop, restart, and cross-round seek;
- module/process cleanup and hook teardown.

Identify the authoritative native signal for each transition and the objects
that become invalid. Define one monotonic adapter generation policy and the
exact event that invalidates all snapshots, pending presentation, and cached
resolvers. Record callback registration order and reverse-order removal, plus
what can still call Horse during teardown.

Exit gate: there is no path where a checkpoint, pointer, callback, hook, or
pending event survives its native generation, including partial initialization
and failure paths.

### Phase 7: synthesize the implementation contract

After the subsystem work, produce these final artifacts in the results file:

1. **Production candidate region table** in restore order. Each row must name
   owner/resolver, semantic fields or bounded extent, classification, capture
   phase, preflight, restore, repair, verify, hash, lifetime, and evidence.
2. **Identity-generation table** mapping every native transition to affected
   resolvers and invalidation behavior.
3. **Presentation journal table** containing value-only event schemas and
   commit/reconcile rules.
4. **Hook table** with target, phase, owner, install prerequisite, teardown
   ordering, recursion/thread rule, and failure behavior.
5. **Qualification allowlist prerequisites** for the three candidate cases,
   explicitly separating statically proven scope from runtime proof still
   required.
6. **Unresolved blocker table** with the next exact function, xref, runtime
   observation, or design decision needed—never a vague “needs more RE.”
7. **Recommended adapter sequence**: the smallest order in which HorseMod can
   implement resolvers and state adapters without temporarily admitting an
   incomplete region.

Only rows with complete proof belong in the candidate table. Do not edit
`Schema::production_regions`; the implementation agent will independently
review the report and admit regions.

## Required Markdown results format

Create `docs/investigations/deterministic-native-contract-results-2026-08-23.md`
at the start of work and update it continuously. Use this structure:

```markdown
# Deterministic native contract results

## Build and Ghidra identity
- Executable identity/hash: ...
- Ghidra program: SoulcaliburVI.exe
- Analysis date and agent: ...
- Runtime evidence used: none / exact paths and hashes

## Executive status
- Closed gates: ...
- Open blockers: ...
- Safe implementation work now unlocked: ...
- Work still forbidden: ...

## Ghidra change ledger
| Kind | Address | Old | New | Evidence/reason |

## Per-frame call order
| Order | Function/address | Thread | Reads | Writes | External effects |

## State-region ledger
| Region | Owner/resolver | Type/extent | Writers | Readers | Lifetime |
| Capture phase | Restore order | Repair | Verify | Hash/classification | Status/evidence |

## Pointer and allocation generations
| Object | Factory | Replacement/destructor | Stable identity check | Restore policy |

## RNG and FP contract
...

## Presentation journal contract
...

## Lifecycle and teardown contract
...

## Production candidate regions in restore order
...

## Unsupported routes and qualification prerequisites
...

## Contradictions with historical documents
| Prior claim/source | Current binary evidence | Resolution |

## Unresolved blockers and exact next actions
...

## Ghidra saves and completeness audit
| Phase | Saved | Functions audited | Remaining fixable deductions |
```

If a table becomes unreadable, split it by subsystem, but retain every required
column. Link to source documents using repository-relative links. Record exact
addresses and names together so later renames do not destroy provenance.

## Ghidra quality procedure

For each bounded function cleanup, follow this ordering:

1. rename and set prototype;
2. audit variables, set types, then rename variables;
3. add plate, PRE, and EOL comments together after structural changes;
4. run `analyze_function_completeness`, fix deductions above ten points, and
   record genuine phantoms/register-only exceptions;
5. re-decompile callers and callees to ensure the type did not create false
   casts or propagate into unverified uses.

Use xrefs in both directions. Search for inlined writers by constants, adjacent
field accesses, imports, vtable slots, constructors, and destructors; a direct
xref list alone is not writer closure. For virtual dispatch, identify the
concrete classes reachable in the admitted content, not only the base slot.

Save through MCP after each phase. If an MCP endpoint is reported available
but cannot be invoked by the client, record the tool-exposure failure and stop
before claiming that Ghidra was updated.

## Evidence and confidence rules

Every material conclusion must carry one of these labels in prose or tables:

- **Proven-static:** established by decompile/disassembly, xrefs, types, and
  lifecycle closure in the current binary.
- **Proven-runtime:** established by a named, hashed trace or observation in
  addition to static evidence.
- **Bounded-inference:** strongest current interpretation, with alternatives
  and the exact evidence needed to promote it.
- **Unknown/blocker:** unsafe for production admission.

Absence of an observed writer is not proof of no writer. Explain the search
surface used to claim closure. Record negative results because they prevent the
implementation agent from repeating the same searches.

## Runtime work allowed during this task

Static RE is the default. Read-only runtime traces or debugger observations may
be proposed where static evidence cannot establish thread identity, dynamic
class reachability, allocation generation, or indirect targets. Do not patch
the game or add production hooks as part of this task. Any temporary diagnostic
instrumentation requires a separate, explicit scope and must record DLL/source
hashes and normal-render conditions.

The following can never be certified by Ghidra alone and must remain listed as
later qualification work:

- correction convergence in the three replay workloads;
- seek/resume timing and cross-round behavior;
- two-process Steam message behavior;
- a normal Steam SC6 host and Sandboxie-isolated Steam SC6 client using distinct
  Steam identities, production Steam P2P, real corrections, and multi-round
  matches;
- disconnect recovery and one-hour soak behavior.

## Completion criteria

The RE handoff is complete when:

- all six primary ledger gates are either closed or reduced to precise,
  explicitly unsupported routes;
- the results file contains the implementation tables listed in Phase 7;
- Ghidra contains the relevant verified names, types, structures, variables,
  globals, comments, and completeness results;
- the Ghidra program has been saved through MCP after every completed phase;
- contradictions with historical rollback research are explicitly resolved;
- no candidate restore operation writes a raw pointer, unbounded container
  header, allocator metadata, or unknown byte range;
- the final report states exactly what the adapter agent may implement next and
  what must remain fail-closed.

If one blocker cannot be closed, finish and save all independent phases rather
than weakening the admission rule. The useful result is a precise boundary,
not a declaration that rollback is ready.
