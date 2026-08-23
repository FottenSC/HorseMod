# Standalone SC6 combat simulation: reverse-engineering handoff

Date: 2026-08-16

## Objective

Recover only the native behavior required to execute a deterministic, headless,
two-character SC6 combat step outside the game. Extend the existing portable
reference model in `tools/moveset_parser`; do not start a second emulator.

The first qualification scope is deliberately narrow:

- exact v2.31 executable and hashed extracted assets;
- the two admitted player roles, initially with one fixed character pair, in a
  normal two-character battle;
- open-plane movement and combat, without walls, ring edges, terrain, Inferno,
  creations, stage geometry, rendering, UI, camera presentation, or VFX output;
- VFX or presentation work is modeled only when it changes gameplay state,
  consumes shared gameplay RNG, or changes ordering visible to later gameplay;
- unsupported topology or an unresolved indirect target fails closed.

The completion target is not “the model looks plausible.” It is an executable
transition function that reproduces native frame-to-frame gameplay state for
unseen input recordings within the declared scope.

## Existing baseline

The current tree already contains a strict portable model and an immutable
coverage gate:

- `tools/moveset_parser/lux_reference_engine.py`: instruction-faithful MoveVM
  integer/ring-stack core;
- `tools/moveset_parser/lux_input_pipeline.py` and related `lux_input_*`
  modules: raw input selection, transforms, delay rings, training playback,
  current-input publication, and input-history commit/scanning;
- `tools/moveset_parser/lux_movement_vm.py`,
  `lux_transition_author.py`, and `lux_lane_lifecycle.py`: partial transition
  authoring and lane lifecycle;
- `tools/moveset_parser/lux_attack_cell_variant.py`: partial attack-cell and
  attack-window state;
- `tools/moveset_parser/static_model_coverage.py`: fail-closed corpus and
  subsystem qualification manifest.

Run the baseline before changing anything:

```powershell
python tools\moveset_parser\static_model_coverage.py --output <scratch-path>\static-model-before.json
python -m pytest tools\moveset_parser\tests
```

The current manifest is `static-incomplete`. It reports four reachable
CALLCOND gaps:

- `0x01` — `EvaluateIfOpcode` at `0x1403732F0` (334,243 authored sites);
- `0x03` — `DispatchEffectOp` at `0x140376B20` (295,484 authored sites);
- `0x16` — `DrainPendingTransition` at `0x1402FCDE0` (171 authored sites), dependent on the complete
  `LuxMoveVM_TransitionToMove` transaction;
- `0x26` — `SetActiveMoveSlot` wrapper at `0x140300DF0` into
  `LuxMoveVM_SetActiveMoveSlot` at `0x140300C70`, including its gameplay-visible
  RNG/allocation side effects (2,591 authored sites).

These are corpus reachability counts, not expected runtime frequencies.

The required subsystem gaps are:

- all reachable CALLCOND handlers;
- move scheduling and complete lane lifecycle;
- motion root/velocity integration;
- pose, skeleton, and blending required by collision;
- body, weapon, and attack-volume construction;
- KHit intersection and contact resolution;
- all authored state/stat profiles used by reachable combat paths;
- required asset parsers and hashes;
- exact context-query engine;
- independent lifted-IR agreement;
- context explorer UI, which is last and is not part of the simulation kernel.

## Evidence and Ghidra rules

For every newly owned native function:

1. Use only the open `SoulcaliburVI.exe` program through native Ghidra MCP
   tools. Never import another executable or modify `.gpr` files directly.
2. Establish the exact caller, callee, global, virtual-slot, and structure
   relationships from decompiled code and xrefs. Existing C++ comments are
   leads, not authority.
3. Rename the function with a verb-first PascalCase name and set its exact
   prototype before comments.
4. Recover and apply structures only to independently verified uses. Name and
   type relevant parameters, locals, globals, fields, and labels. Do not invent
   types for decompiler artifacts.
5. Add plate, PRE, and EOL comments after structural edits. Record inputs,
   outputs, persistent writes, RNG use, allocations, indirect dispatch,
   ordering, and failure behavior.
6. Run `analyze_function_completeness`. Address fixable deductions above ten
   points; document genuine register-only artifacts instead of gaming scores.
7. Save the Ghidra program at the end of each completed native transaction,
   not after every speculative rename.
8. Implement the behavior in the existing Python reference model and add a
   focused test covering the complete reachable argument/state domain.
9. Update `IMPLEMENTED_CALLCOND_HANDLERS` or `IMPLEMENTED_SUBSYSTEMS` only after
   the corpus manifest and differential evidence prove the whole declared
   domain. A name or decompile alone never makes a subsystem implemented.

Maintain a small RE ledger for each transaction:

| Field | Required content |
|---|---|
| Native root | name, address, executable signature |
| Reachability | exact callers and authored input/corpus domain |
| State read | typed fields/globals and preconditions |
| State written | typed fields/globals and ordering |
| External effects | RNG, allocation, callbacks, indirect calls |
| Portable owner | module/function implementing it |
| Oracle | native trace or fixture used for comparison |
| Unsupported cases | explicit fail-closed conditions |

## Milestone 0: freeze the native oracle boundary

Before lifting more code, make the native game produce deterministic,
partitioned before/after evidence at `BattleManagerSimulationLoop @
0x1403FE520` and the subordinate boundaries already listed in
`HorseMod/horselib/NativeReplayTraceHook.hpp`.

Capture, at minimum, the two inputs, MoveVM/lane state, collision-pose sources,
KHit state, health/damage/reaction state, positions/velocities, gameplay RNG,
and ordered semantic events. Every record must carry exact executable and
asset hashes. Repeating one native script must produce byte-identical records,
and a comparison must identify the first divergent subsystem rather than only
one aggregate hash.

The checked-in `RuntimeOracle/CMakeLists.txt` currently points to external
sources under `E:/DevShitPosts/caliburRE/ue4ss/RuntimeOracle`. Do not make that
external directory a hidden dependency of the standalone model. Reuse the
existing HorseMod tracing first; if it cannot expose a required source
boundary, add a small bounded oracle capture route inside the repository.

Exit: the oracle records current-frame inputs and pre/post state but never
supplies future native outputs to the portable simulator.

## Milestone 1: close the four reachable CALLCOND gaps

Work from the authored corpus outward, not from the entire native dispatch
table.

### 1A. `CALLCOND 0x01` — predicate evaluation

- Generate the exact reachable first-word and argument-count domains with
  `static_model_coverage.py` and `trace_movevm_calltree.py`.
- Document `EvaluateIfOpcode @ 0x1403732F0` and only the predicate handlers
  reached by those domains.
- Recover the context structure and every queried character/opponent field.
- Implement exact signedness, short narrowing, float conversion, and failure
  behavior. Unknown predicate IDs remain blockers.
- Differentially compare each reachable predicate against native captures at
  boundary values.

Exit: no reachable `0x01` site can select an unimplemented predicate or an
untyped state source.

### 1B. `CALLCOND 0x03` — effect dispatch

- Enumerate the complete reachable effect-opcode and argument-shape domain.
- Document the branches in `DispatchEffectOp @ 0x140376B20` reached by `0x03`.
- Classify each effect as gameplay-state, shared-RNG/order-only, or safely
  omittable presentation. Preserve gameplay state and RNG/order even when no
  visual object is produced.
- Reject any effect that requires an unresolved callback or borrowed object
  topology.

Exit: every reachable opcode has an executable state/RNG model or remains an
explicit `static-incomplete` blocker.

### 1C. `CALLCOND 0x16` and full transition transaction

Use `LuxMoveVM_TransitionToMove` at `0x1402FE350`. The former
`0x1402FEC50` reference was an interior byte of the instruction beginning at
`0x1402FEC4B`, not a function entry.

- Recover the complete transition input package, including live register/XMM
  inputs, lane selection, bank/slot resolution, animation initialization,
  active-cell selection, scheduled-effect reset, global/character writes, and
  source-lane retirement.
- Document and implement `LuxMoveVM_AdvanceLaneFrameStep @ 0x1402FFEB0` and the
  transition timing path rooted at `LuxMoveVM_CheckTransitionTiming @
  0x1402FDD70` as one state machine.
- Bind `drain_pending_transition` to this exact executor. Never replace the
  missing transaction with a simplified lane-ID assignment.

Exit: immediate, deferred, cancel, same-lane, cross-lane, and move-end paths
match native state deltas for the complete authored corpus.

### 1D. `CALLCOND 0x26` — active attack-cell variant

- Complete the existing `lux_attack_cell_variant.py` transaction.
- Trace the inactive-to-active shock-wave path far enough to reproduce its
  shared gameplay RNG consumption and ordering. Rendering/allocation may be
  represented by a deterministic event record if no later gameplay read
  depends on object identity.
- Prove all six variant indices and sentinel/error behavior.

Exit: all reachable `0x26` sites update attack-cell, KHit gates, reaction
windows, and shared RNG exactly.

## Milestone 2: complete the per-character scheduler and lane lifecycle

Use the native order rooted at:

- `LuxBattle_TickCharaMainSimulation @ 0x14034DA70`;
- `LuxMoveVM_TickDriver @ 0x1403656B0`;
- `LuxMoveVM_ExecuteOpStream @ 0x1402FDEA0`;
- `LuxMoveVM_RunBytecodeScript @ 0x1402E67B0`;
- `LuxMoveVM_ExecuteBankSlotScript @ 0x1402FCC30`.

Recover the exact per-frame order for input-history consumption, transition
evaluation, lane advancement, scheduled effects, secondary lanes, move end,
and state publication. Convert the current partial lifecycle types into one
canonical `CharacterSimulationState` only after all field aliases are proven.

Exit: from a captured pre-frame state and current inputs, the portable model
selects the same moves, lanes, animation frame counters, active cells, and
scheduled events as native code for unseen non-contact sequences.

## Milestone 3: close required asset formats

The current parser validates 147/147 available files, but validation is not
semantic completion. Finish only the formats read by the scoped runtime:

- KHD section-C typed/variable records and loader fixups;
- MOT/HgMotion keyframes, interpolation flags, root tracks, and skeleton
  references;
- VTB and related frame-event records consumed by the scheduler;
- hit streams and any weapon/body profile indirection;
- character state/stat profile sources used by reachable predicates, damage,
  defense, and movement.

Each parser must use checked offsets, bounded counts, exact file hashes, and
round-trip fixtures where the native loader exposes a canonical in-memory
projection.

Exit: the simulator initializes every required state table from hashed assets
without copying opaque native memory images.

## Milestone 4: motion and collision-pose pipeline

Recover in native execution order:

- `AdvanceCharaAnimClipPlayer @ 0x14037C2F0`;
- `FinalizeTickPoseAndState @ 0x140305B50`;
- `DecodeHuffmanKeyframeData @ 0x1402E71E0`;
- `BlendKeyframeTransforms @ 0x1402E79C0`;
- `EvaluateBonePose @ 0x1402F0F20`;
- `SolveBonePose @ 0x1402EDB90`;
- `SampleKeyframeTransforms @ 0x1402E7780`;
- `WritebackScaledBoneTransforms @ 0x1402F3690`;
- `UpdateRootMotionDeltasFromBone1 @ 0x1403043D0`;
- `IntegratePhysicsPerTick @ 0x140306BB0`.

Do not implement the renderer or full UE animation graph. Implement the
minimum collision-pose skeleton, blend inputs, root motion, facing transform,
velocity, and position integration consumed by later gameplay.

Exit: native and portable models agree on root transform, velocity, relevant
bone transforms, and frame cursors for idle, movement, attack, recovery, and
transition blends in open space.

## Milestone 5: body, weapon, and attack-volume construction

Start with the existing layout evidence in `HorseMod/horselib/KHitWalker.hpp`,
but re-prove every used field in Ghidra. Root native work at:

- `Lux_KHitChk_DeserializeLinkedList @ 0x14030C940`;
- `LuxBattleChara_UpdateAllKHitWorldCenters @ 0x14030D6A0`;
- `KHitSphere_UpdateWorldCenter @ 0x14030E1A0`;
- `KHitSphere_UpdateFromAnimCell @ 0x14030E2F0`;
- `KHitArea` construction/update functions around `0x14030E3A0` and
  `0x14030E480`.

Recover sphere/area/fix-area construction, bone attachment, local-to-world
conversion, activation gates, weapon transforms, and attack-cell variant
selection. Exclude unsupported geometry tags rather than approximating them.

Exit: every active scoped volume matches native kind, count, attachment,
world-space coordinates, radius/extents, and active frame.

## Milestone 6: KHit intersection, response, and two-character physics

Root the call tree at:

- `TickHitResolutionAndBodyCollision @ 0x14033CCA0`;
- `ResolveAttackVsHurtboxMask22 @ 0x14033C100`;
- `SolvePhysBodyCollision @ 0x14030CCF0`;
- `TickBothCharaCollisionPhysics @ 0x140317800`;
- `TickHitStateStateMachine @ 0x140308EC0`;
- `LuxBattleChara_EvaluateHitContactMode @ 0x14034EA60`;
- `UpdateBlockStateStochastic @ 0x14034E820`;
- `TickDamageAndBehaviorLock @ 0x14034E900`;
- `ComputeHitDamageFactors @ 0x140343630`;
- `AccumulateDefenseRates @ 0x140344D10`.

Recover exact broad-phase ordering, shape-pair tests, contact filtering,
priority/deduplication, throw/guard/hit/counter-hit selection, damage/stun,
pushback, body separation, behavior locks, and RNG use. Preserve native
floating-point environment and iteration order; do not replace geometry with
an epsilon-based approximation.

Exit: scripted two-character contact cases reproduce native contact identity,
damage, guard state, stun/reaction selection, position/velocity, RNG state,
and resulting MoveVM transitions.

## Milestone 7: battle and round rules

After the active-frame kernel is stable, recover the non-presentation state
transitions rooted at:

- `PreTickStateSnapshotAndRoundDecision @ 0x14034FCE0`;
- `BattleManagerSimulationLoop @ 0x1403FE520`;
- `LuxBattle_ResetRoundCountersAndCommandSlots @ 0x140302930`;
- the VM-pump round-end/cleanup paths around `0x14031D530` and `0x14031D5B0`.

Model timer, KO, round result, input gating, character reset, new-round
initialization, winner/counter publication, match completion, and re-entry.
Do not model menus or scene actors when native code shows that a compact battle
state is sufficient.

Exit: a scripted multi-round match reproduces native frame coordinates,
health, winner, round counters, reset state, and RNG without UI or UE actor
ownership.

## Milestone 8: canonical headless frame step

Create one portable API with no game pointers:

```text
StepBattleFrame(BattleState, PlayerInput[2], StaticAssets) -> FrameResult
```

The ordered kernel should contain only verified operations:

1. raw input transform and delayed publication;
2. input-history commit and condition evaluation;
3. scheduler/MoveVM/lane transition work;
4. motion sampling, collision pose, root/velocity integration;
5. volume update and hit/body collision;
6. hit/reaction/damage state-machine publication;
7. RNG/event ledger update and canonical state hash.

Every container must have a fixed or validated bound. Every pointer-like
relationship must become a stable ID or checked asset index. Presentation
events are output records, never host object allocations.

Exit: the kernel can start from a portable snapshot and advance at least 600
frames without consulting the game process.

## Milestone 9: differential qualification

Build a native oracle capture mode that records pre-state, two inputs,
post-state, ordered RNG calls, transition/contact events, and hashes at the
same source boundaries used by the portable kernel. Use the exact executable
and asset hashes in every corpus record.

Qualification sequence:

1. deterministic unit fixtures for each lifted native transaction;
2. one-character non-contact motion traces;
3. two-character approach/body-collision traces;
4. guard, normal hit, counter hit, whiff, throw, knockdown, recovery;
5. unseen moves and unseen recordings from the admitted roster/scope;
6. long runs with rollback: save at frame N, advance, restore, resimulate, and
   require byte-identical canonical state/event hashes.

Do not tune the model to recorded outputs. On divergence, identify the first
different field and return to its native writer/call order.

Exit: zero unexplained divergence over the frozen qualification corpus and a
separate unseen holdout corpus.

## Milestone 10: independent agreement and final tooling

- Independently lift the small arithmetic/branch-heavy kernels to a second IR
  or implementation and compare them across exhaustive bounded domains.
- Implement the exact context-query engine over canonical state.
- Add the context explorer only after the simulation kernel qualifies.
- Regenerate the immutable coverage manifest. `--require-complete` must pass
  without removing blockers or weakening the declared scope.

## Work ordering and stop conditions

One agent should work one native transaction at a time. The preferred order is:

1. native oracle and first-divergence partitions;
2. four CALLCOND gaps;
3. transition/scheduler/lane lifecycle;
4. asset semantics needed by the next runtime stage;
5. motion and collision pose;
6. volume construction;
7. intersection/contact response;
8. battle/round rules;
9. canonical frame kernel;
10. differential qualification;
11. independent agreement and explorer.

Stop and report a blocker when any of these occurs:

- an indirect function target cannot be enumerated for the reachable corpus;
- a borrowed object or allocation identity is read by later gameplay and no
  portable ownership rule is proven;
- shared RNG consumption or floating-point ordering is unresolved;
- a required asset structure has an unbounded or ambiguous variant;
- open-plane behavior unexpectedly depends on stage geometry, UE physics, or
  another excluded subsystem;
- native traces cannot establish the same source boundary as the portable
  step.

Do not broaden scope silently. Record the dependency, keep qualification
closed, and ask for a scope decision only when no narrower verified route can
make progress.

## Definition of done

This RE program is complete only when all of the following hold:

- Ghidra names, prototypes, variables, globals, structures, and comments cover
  every native function owned by the scoped frame kernel;
- `static_model_coverage.py --require-complete` passes for the frozen scope;
- no reachable CALLCOND, indirect target, asset variant, or gameplay RNG
  consumer is unresolved;
- the portable frame API runs without game memory, UE objects, hooks, or DLLs;
- native differential and rollback-resimulation corpora have zero unexplained
  divergence;
- unsupported topology fails closed and is named in the manifest;
- HorseMod ownership gates remain unrelated and closed until their separate
  runtime qualification succeeds. A standalone static model does not, by
  itself, authorize in-game rollback ownership.
