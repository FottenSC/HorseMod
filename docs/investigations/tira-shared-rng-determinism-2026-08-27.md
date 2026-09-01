# Tira shared-RNG determinism contract — 2026-08-27

## Question

Tira has authored moves whose outcome can switch her between distinct behavior
and move sets. A rollback implementation is invalid if it restores only the
global random state while omitting the selected character state, or if
resimulation skips a non-gameplay consumer of the same random stream and shifts
the later gameplay draw sequence.

## Native RNG contract

Ghidra program `/SoulcaliburVI.exe` proves that
`LuxMoveVM_GetRandXorshift96Gameplay @ 0x14034F1F0` advances the single
three-word state at `g_stLuxBattleXorshift96State @ 0x14470E2C8`.
Its 26 direct callers include gameplay hit/block/damage and MoveVM probability
routes, plus camera, effect-variant, intro, round-pose, and CPU routes. The
stream is therefore shared. Camera and effect calls cannot be removed from
resimulation merely because their immediate output is presentation: omitting a
draw changes later gameplay decisions.

The two relevant authored MoveVM probability contracts are independently
verified:

- `LuxMoveVM_GetRandWeightedIndex @ 0x1402E58B0` implements CALLCOND `0x23`,
  consumes exactly one xorshift96 draw, and maps it through authored weights.
- `LuxMoveVM_EvaluateIfOpcode @ 0x1403732F0`, IF `0x007F` at
  `0x140374451`, calls `LuxMoveVM_GetRandFloat01` before reading/comparing the
  authored percentage threshold. It consumes exactly one draw even when the
  threshold is outside the ordinary 0..100 range.

HorseMod now detours the shared xorshift function under an exact executable
signature and records every draw as the ordered pair `(direct caller return
RVA, returned uint)`. The 61 direct call sites recovered in Ghidra form the
closed admission set. Authoritative capture fails on any unknown caller, and
owned verification requires the exact draw count, route/result sequence,
caller mask, weighted/IF counts, and native source-frame masks. Camera, effect,
intro, CPU, and gameplay consumers therefore remain in one ordered contract.

`LuxMoveVM_GetRandU32 @ 0x14034F130` is not the same generator. It owns a
25-word LFSR plus cursor. The deterministic candidate already captures and
restores all 25 words and the between-draw cursor, in addition to the LCG,
xorshift96, and wind state.

## Tira authored-data evidence

`dump/Battle/hdr/hdr023.khd` is the Tira movement program. A complete decoded
instruction scan found:

- 104 CALLCOND `0x23` instances across 46 move slots;
- 16 IF `0x007F` instances across slots 338, 356, 357, 358, 359, 2005, 2427,
  2683, 2946, and 2947;
- 92 direct reads of MoveVM state-short index 25 across 72 slots;
- 182 direct writes of state-short index 25 across 68 slots.

Slots 338 and 359 write an authored value to state-short index 25 immediately
before IF `0x007F` consumes it as the percentage threshold. Slots 356 through
358 use the same probability opcode with threshold 5. This establishes that
Tira actively couples character-specific MoveVM state and the shared gameplay
xorshift stream. Subsequent native writer/consumer analysis closes index 25
(`0x19`) as Tira's live stance word at `ALuxBattleChara+0x19AE`: Tira values
`0` and `1` are Gloomy and Jolly respectively. The globally shared enum keeps
neutral numeric names because other character banks author values `2` and `3`.

The original transition detector incorrectly treated an IF `0x007F` result and
a later `TransitionAuthor_07` target as the stance writer. A complete writer
scan and decompilation of the nested-script boundary disproved that model:

- `LuxMoveVM_CallCond_WriteCharaStateShort_14 @ 0x1402FDA30` is the direct
  writer. It stores argument 1 to `pChara->stateShorts[argument 0]`; index
  `0x19` is therefore written at `ALuxBattleChara+0x19AE`.
- Tira slots 551, 553, 575, 2407, 2517, 2667, and 2946 contain direct
  state-`0x19` writes. Most are deterministic authored routes and must not be
  credited as random transitions merely because another RNG predicate or
  transition target occurs nearby.
- packed move `0x321B` resolves Tira slot 2946. That helper evaluates IF
  `0x007F` from its local chance argument and, on success, directly toggles
  state `0<->1` through CALLCOND `0x14`.
- `LuxMoveVM_ExecuteBankSlotScript @ 0x1402FCC30` synchronously runs that
  helper. While nested execution is active it publishes packed helper ID
  `0x321B` at `pChara+0x1C68`, then restores the enclosing move on return.
  The enclosing move, not a later transition target, is the route identity.
- for example packed move `0x0165` invokes helper `0x321B` with chance `0x14`
  before its own later IF/transition work. Thus `0x0165` can be a valid
  enclosing random-stance route; it is not evidence that target `0x0165`
  itself wrote state `0x19`.

HorseMod signature-guards and observes that typed wrapper. Every call records
the ordered `(argument count, target move ID)` sequence. Target move IDs are
local to the owning movement program, so Tira qualification first binds the
callback's `pChara` to the verified native fighter/resource ID `0x23` in
`wCharaIdA` at `+0x24C`. This is distinct from the zero-based reflected
`ELuxCharacter::ELC_TIRA` value `16` carried by replay metadata. The native
identity is independently established by
`LuxMoveVM_ClassifyCharaAIMode @ 0x1402FA1F0`, which selects Tira's state-`0x19`
branch only when `wCharaIdA == 0x23`, and by both round initializers. The
separate `+0x250` move-table index also holds `0x23` for this resource, but the
observer uses the stock classifier's exact `+0x24C` identity boundary.
Qualification now detours the typed `LuxMoveVM_ExecuteBankSlotScript` and
CALLCOND-`0x14` writer boundaries. It admits a random Tira stance transition
only when the same Tira owner is executing nested helper `0x321B`, exactly one
new gameplay-xorshift draw has occurred in the same native frame, and the
direct writer changes state `0x19` between `0` and `1`. The saved enclosing
move (including `0x0165`) is recorded as the route identity. Direct state writes
outside helper `0x321B` remain visible as writer provenance but do not increment
the random-transition count. The full general transition sequence is still
compared during ordinary owned verification. Its aggregate may legitimately be
zero and is not a prerequisite for a helper-`0x321B` qualification event.

The observer API exposes deterministic writer provenance separately from the
random route: writer count, ordered writer sequence, fighter-slot mask, and
exact last live writer move. Random target/slot fields are populated only by a
helper-qualified transition. Consequently a deterministic `0x306F` write can
be reported without making `tira_random_transitions` or `tira_targets`
nonzero. The current 46-value API remains compatible with 42-value callers;
the four writer fields are append-only.

UE reflection metadata gives two additional, distinct inputs:

- `FLuxBattlePlayerParam.TiraSide` is an `int` at `+0x4C` in setup state.
- `FLuxBattlePlayerResetParam.TiraMode` is an `int` at `+0x14` in the 0x38-byte
  round/reset input.

The symbol `TriggerChangeMood` at `0x140928510` was a misleading anonymous
function during this audit. Ghidra now names it
`GetLuxBattleDramaticVoiceTriggerChangeMoodUFunction`; decompilation proves it
only constructs UE reflection metadata for a dramatic-voice event and consumes
no battle RNG. It is not the gameplay writer.

## Implementation consequence

The native HgCpuDirect fighter archive remains the sole writer during restore,
but opaque local archive bytes are deliberately excluded from peer canonical
hashes. That previously left the complete MoveVM state-short bank without an
independent typed peer-canonical projection.

HorseMod now captures all 240 `ushort` state shorts for both fighter roots from
`ALuxBattleChara+0x197C`, appends them to canonical checkpoint bytes, and gives
them dedicated native fingerprint slot 30. Restore does not perform a second
write; the post-HgCpu typed comparison verifies that the authoritative archive
restored the exact bank before simulation resumes. Snapshot schema 45 and
checkpoint format 18 prevent older images from being accepted.

The native self-test mutates fighter 0 state-short index 25 and requires both
the aggregate canonical hash and native fingerprint slot 30 to change. This
closes the prior silent-state hole, but runtime qualification still must use a
genuine Tira workload that observes a real probability-driven transition and
must retain exact xorshift state plus exact canonical fighter state after every
correction.

## Remaining runtime gate

A Tira workload is certifying only when it records an actual authored
probability-success transition, performs real corrections across that route,
and reports:

- exact xorshift96 landing state;
- exact MoveVM state-short bank and canonical hash;
- no omitted camera/effect shared-stream draws;
- exact subsequent move availability/selection;
- exact peer-confirmed hashes in the paired Steam run.

The runtime evidence is therefore an ordered contract, not a count-only gate.
It binds each helper execution to the verified Tira fighter root, records its
enclosing move, chance, exact state-`0x19` before/after values and fighter slot,
and reports the final three-word xorshift96 landing state. A certifying
transition requires nested helper `0x321B`, exactly one IF-`0x007F` draw, and a
same-frame Gloomy/Jolly `0<->1` direct write for that exact fighter. Repeated
runs must match the full RNG, helper-route, stance-sequence, landing-state, and
canonical hash evidence.

Non-certifying detector-re-audit runs on Snow-Capped Showdown recordings 34,
35, and 37 all observed Tira P1 state `0->1`, and the supplied
`REPLAY_11775433596982945207.bin` on Astral Chaos: Tide of the Damned observed
the same landing for both Tira roots. The authoritative writer in all four runs
was packed move `0x306F`, outside helper `0x321B`; each is a real deterministic
stance swap and correctly contributes zero random transitions. Earlier target
`0x0165` correlation came from the broader transition stream and was not the
causative writer. All of these runs remain diagnostic, not certification. The supplied
`REPLAY_10919796003596567142.bin` contains character IDs 13/11 rather than
Tira; it remains an Astral Chaos: Tide of the Damned stock-control workload,
not Tira transition coverage.

The four retained base-game recordings were swept with the new gate. They
contained up to 11 IF draws, 12 weighted draws, and 226 transition-author calls
in one replay, with zero unknown xorshift callers, but none proved a nested
helper-`0x321B` state toggle. They remain negative diagnostic evidence, not a
substitute for a targeted current-build recording. The modular runner's
`--require-tira-probability-transition` option fails closed when the authored
success route is absent.

Base-game Tira recordings retained under
`ReplayExample/baseGameTiraReplays` are useful input workloads, but older Horse
documentation explicitly rejects them as state oracles for its then-modified
RNG contract. Fresh current-build evidence is required.
