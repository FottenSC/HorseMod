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

The first detector audit used a stale 2019 copy of `hdr023.khd` (SHA-256
`b061e7786ae98890355ed999c6a6b68c672f3ee7501092aadee968dfa08e6743`).
That file does not describe the current installed Tira movement program. The
authoritative current dump is 1,109,735 bytes with SHA-256
`1538128d8417deb6e917722697b902164cc0b6ae050a3e6783373a1b67db6fe`.
All route conclusions below come from that bank. The earlier `0x321B` helper
claim is invalid for the current executable/data pairing.

Decompilation and a complete instruction scan of the current bank establish:

- `LuxMoveVM_CallCond_WriteCharaStateShort_14 @ 0x1402FDA30` stores argument 1
  to `pChara->stateShorts[argument 0]`; index `0x19` is the live Tira stance
  word at `ALuxBattleChara+0x19AE`.
- packed moves `0x0244`, `0x0246`, `0x1014`, `0x3000`, `0x306F`, `0x3114`,
  `0x3250`, and `0x3251` directly write state `0x19` in the current bank.
- `0x306F` contains direct paths writing both 0 and 1. Therefore its observed
  `0->1` writes are real Tira stance changes; they are not false telemetry.
  They do not use IF `0x007F`, so they are not probability-helper events.
- `0x3250` and `0x3251` each evaluate IF `0x007F` from a local chance argument
  and directly toggle state `0<->1` on success. These are the current bank's
  two RNG-owned stance helpers. `0x3252` evaluates IF `0x007F` but does not
  directly write state `0x19`.
- `LuxMoveVM_ExecuteBankSlotScript @ 0x1402FCC30` synchronously publishes a
  nested packed move at `pChara+0x1C68`, executes it, and restores the enclosing
  move. This is the authoritative ownership boundary for `0x3250/0x3251`.

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
Qualification detours the typed `LuxMoveVM_ExecuteBankSlotScript` and
CALLCOND-`0x14` writer boundaries. It records every exact Tira state-`0x19`
value change as a stance change. It additionally classifies the narrower
RNG-caused subset only when the same owner is executing nested helper `0x3250`
or `0x3251`, exactly one gameplay-xorshift draw occurred in the same native
frame, and the direct writer changed state between `0` and `1`. The saved
enclosing move remains separate route provenance.

The observer API exposes all stance-writer provenance separately from the RNG
route: writer count, ordered writer sequence, fighter-slot mask, and exact last
live writer move. Reports name these meanings explicitly as
`tira_stance_changes` and `tira_rng_stance_changes`, while retaining the old
fields as compatibility aliases. Thus a `0x306F` write correctly increments
the former without incrementing the latter.

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
transition requires nested helper `0x3250` or `0x3251`, exactly one
IF-`0x007F` draw, and a
same-frame Gloomy/Jolly `0<->1` direct write for that exact fighter. Repeated
runs must match the full RNG, helper-route, stance-sequence, landing-state, and
canonical hash evidence.

Non-certifying detector-re-audit runs on Snow-Capped Showdown recordings 34,
35, and 37 all observed Tira P1 state `0->1`, and the supplied
`REPLAY_11775433596982945207.bin` on Astral Chaos: Tide of the Damned observed
the same Tira P1 landing. The authoritative writer in all four runs
was packed move `0x306F`, outside helpers `0x3250/0x3251`; each is a real
stance swap and correctly contributes a stance change but zero RNG-owned
stance changes. Earlier target `0x0165` correlation came from the broader
transition stream and was not the causative writer. All of these runs remain
diagnostic, not certification. The supplied
`REPLAY_10919796003596567142.bin` contains character IDs 13/11 rather than
Tira; it remains an Astral Chaos: Tide of the Damned stock-control workload,
not Tira transition coverage.

The four retained base-game recordings were screened before the current-bank
correction. They
contained up to 11 IF draws, 12 weighted draws, and 226 transition-author calls
in one replay, with zero unknown xorshift callers, but none proved a nested
helper-`0x3250/0x3251` state toggle. They remain negative diagnostic evidence, not a
substitute for a targeted current-build recording. The modular runner's
`--require-tira-probability-transition` option fails closed when the authored
success route is absent.

The corrected detector was then exercised on supplied replay
`REPLAY_11775433596982945207.bin` on **Astral Chaos: Tide of the Damned**. Its
full authored-outcome run recorded seven exact stance changes: six owned by
the `0x3250/0x3251` probability helpers (target mask `0x3`) and a later seventh
change under `0x306F`. It recorded 14 exact helper attempts/draws, six writes,
eight legitimate no-write outcomes, zero helper signature failures, and zero
unknown RNG callers. This directly demonstrates why all stance changes and the
RNG-caused subset must remain separate report fields. The dirty diagnostic run
matched the authored round/match outcome and sustained 60.004 normal-render
FPS/TPS; immutable certification is still required.

Base-game Tira recordings retained under
`ReplayExample/baseGameTiraReplays` are useful input workloads, but older Horse
documentation explicitly rejects them as state oracles for its then-modified
RNG contract. Fresh current-build evidence is required.
