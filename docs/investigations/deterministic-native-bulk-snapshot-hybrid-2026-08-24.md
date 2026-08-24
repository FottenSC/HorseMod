# Deterministic Native Bulk Snapshot Hybrid

## Decision

Local checkpoint restoration uses a bounded hybrid image. Native paired
serializers reconstruct opaque same-generation state; typed regions remain the
pointer-free canonical hash, generation/identity preflight, native omission
layer, and post-restore verifier. Opaque images are never protocol payloads or
peer-canonical bytes.

The initial and only admitted local serializer is
`LuxBattle_HgCpuDirect_ExecMoveChangeAndPost @ 0x1403841E0` paired with
`LuxBattle_HgCpuDirect_ExecFinalizeAndPost @ 0x140384540`. Ghidra MCP confirms
that the pair uses the exact bounded `0x28018` stream ABI, builds a temporary
two-fighter relocation table, and covers fighter, xorshift96, five global
ranges, a `0xBF0` MoveVM bank, optional camera, timer, motion, physics, terrain,
and VFX sections in matching order. The reader rebuilds current-generation
pointers and relinks opponents.

`LuxBattle_SaveRoundRestoreSnapshot @ 0x1402FBA90` and
`LuxBattle_RestoreRoundSnapshot @ 0x1402FBCB0` are not admitted as a bulk
rollback primitive. Their `0x240` payload is a narrow round-transition image
that omits vital, inputs, MoveVM lanes, LFSR, animation playback, wind, and the
full terrain graph.

## Schema 6 implementation

- `CandidateCheckpointImage` owns a bounded local reconstruction image set.
- Each image records serializer ID/version, exact cursor/size, integrity
  checksum, and build/schema/session/round/fighter/stage/camera/allocation
  generations.
- Runtime currently accepts exactly one unique `HgCpuDirect` image. Unknown,
  duplicate, version-mismatched, corrupt, or cross-generation images fail
  before the native reader.
- The canonical SHA-256 domain remains pointer-free and excludes all opaque
  local bytes.
- Restore remains transactional: capture full undo, run the native reader,
  restore typed omissions, rebuild derived state, recapture/compare, and undo
  exactly on failure.

## Current-build evidence

Source base: `70b2f6fb368fc057962629f115ce6fdcff3550b0` plus the dirty schema-6
implementation described here. The protected moveset-parser edits remained
untouched.

- CTest: 3/3 passed.
- Python deterministic qualification tests: 14/14 passed.
- Normal-render replay-entry: passed 600 frames with 597 batches, two
  multi-coordinate batches, maximum width 3, zero cursor/accounting mismatch,
  zero uncovered batch entries, and maximum checkpoint/resimulation gap 19.
- DLL SHA-256: `7839F566D60D4C68D0FA4CEA1B7EA730BE435B506AEB2C5F645E5A766B002D38`.
- Report: `tools/deterministic_qualification/output/replay-entry-report.json`.

The first performance measurement fails the proposed hybrid admission gate:
landing capture reached approximately 0.88 ms p99/max and the first batch-entry
capture reached 1.301 ms. Snapshot-store insertion remained below the 10 us
histogram bucket. The next implementation step is phase-level timing followed
by removal of the dominant bounded allocations/copies; the threshold is not
relaxed.

### Capture-path optimization evidence

Dirty implementation work after `6412e08e` added fixed-size timing histograms
for typed capture, native local capture, UCRT, wind, encoding, total capture,
and store insertion. It then removed three measured costs without changing the
captured contract:

- packed the 1,024 InputLog rows into one pre-sized canonical buffer instead of
  thousands of tiny vector insertions;
- replaced per-checkpoint SHA provider opening and the second contiguous hash
  input copy with direct fragment hashing through the Windows SHA-256
  pseudo-provider;
- advanced `HgCpuDirect` local-image format to serializer version 2 and replaced
  byte-serial FNV with a word-at-a-time 64-bit local integrity checksum. This
  checksum is local-only and remains excluded from peer canonical truth.

Normal-render 600-frame replay-entry continued to pass after every step. On DLL
SHA-256 `70A0CFF5890954090DCF8A13BDE7BCE97574D3380FF181974533F30E58106039`,
landing capture reported 0.44 ms p99/max and every observed capture remained
below 1 ms. Batch-entry reported 0.74 ms because its first and only cold CNG
initialization sample took 0.735 ms; its encode phase fell from 0.54 ms on that
first sample to 0.22 ms after initialization. A longer exact-commit workload is
still required to establish steady p99 with enough samples; the cold maximum is
retained and the 0.5 ms threshold is not relaxed.

An exact-commit 1,800-frame run at `ee0f7d3a` then produced 61 landing and 103
batch-entry samples across a native generation change. It retained a 0.658 ms
maximum but narrowly missed the steady gate at 0.52 ms landing p99 and 0.54 ms
batch-entry p99. Phase evidence showed encoding still revalidated the checksum
of the same freshly captured opaque image. The capture adapter now uses a
dedicated same-stack `EncodeCaptured` entry point that validates image metadata
without recomputing that checksum; general encoding, decoding, restoration, and
all stored-image paths still verify the full checksum. A 600-frame normal-render
regression reduced landing capture to 0.37 ms p99/max and encode to 0.09 ms p99;
batch-entry retained one 0.671 ms cold sample and no capture exceeded 1 ms. The
next exact-commit long run remains the admission measurement.

### Exact-commit capture admission evidence

Commit `3e1e22b360` removed the redundant checksum of a fresh, same-stack native
image while retaining checksum verification for stored, decoded, general encode,
and restore paths. Its exact-source normal-render 1,800-frame replay-entry run
produced 61 landing samples and 102 batch-entry samples across a native generation
change. Landing capture was 0.37 ms p99 with a 0.387 ms maximum; batch-entry
capture was 0.35 ms p99. DLL SHA-256 was
`75B610BE3AD37DF82E16AD3D64993CF5B1CB691E8684FB9630F334861CAD8986`.
This passes the capture-time gate for this workload only. All three qualification
matchups and the depth-11 restore/resimulation gate remain required.

### Schema 7 input-boundary closure

Current-executable Ghidra evidence closes the native input boundary used by the
outer simulation worker. `LuxBattleManager_Tick_MainStateMachine_At1461 @
0x1403FBF30` and
`LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`
show that the complete worker owns input production, the manager `+0x1210`
callback collection, world/simulation callbacks, round sequencing, repairs, and
the once-per-batch tail. `FilterALuxBattleMoveDispatchInputPairByFrameSlot @
0x140427940` proves those callbacks may change the pair before the per-frame
consumer. `ProcessAndCompactCallbackEntries @ 0x141D38300` is therefore hooked
only when its collection identity equals the current manager plus `0x1210`.

Schema 7 records both the pre-filter pair and the post-filter verification pair.
Only the pre-filter pair is authoritative for later injection; the post-filter
pair proves that the native callbacks ran exactly once and produced the same
consumer input. Malformed callback arguments, a count other than two, a missing
observation, or disagreement with the manager fencepost fails closed. A
normal-render 600-frame run observed this boundary at all 600 coordinates, across
597 native batches, a maximum batch width/filter ordinal of three, and one repeat,
with zero disagreement. This workload produced zero changed filter values, so a
qualification workload that exercises an actual filter mutation remains required.

### Bounded native-batch reconstruction implementation

The inactive schema-7 adapter now contains the state-only half of the required
batch-aware seek transaction. A fixed-capacity request replays only a recorded
outer batch through the original `0x1403FE520` trampoline. At each verified
manager `+0x1210` callback it publishes the recorded pre-filter pair, lets the
native collection run once, and compares its output with the recorded post-filter
pair. The executor validates owner thread, manager identity, entry/exit clocks,
frame counter, manager cursors, main/round state, batch width, coordinate order,
and stable InputLog generation. It rejects allocation/input-generation crossing.
Ordinary timeline observation is suppressed only for this owned transaction.

For a mid-batch target, the runtime restores the batch-entry image, replays every
complete envelope, captures a transient landing image at the target fencepost,
finishes the enclosing batch tail, and restores/verifies the landing. A complete
pre-seek image is retained and restored on any failure. Exact batch-entry targets
restore directly. Opaque images remain local and fixed-capacity arrays bound the
per-batch input working set. The implementation has deliberately no production
caller yet: native presentation terminals are not suppressed/reconciled, so
exposing this state-only primitive would violate the seek contract.

Restore instrumentation now separates native local reader, typed supplements,
stage/wind, UCRT, derived repair, and total restore. Exact undo uses the same
verified consumer order as ordinary restore and continues through every undo lane.

The first normal-render regression exposed callback-topology compaction between
the frame-0 batch entry and frame 1. Retaining that frame-0 image under the later
generation would be unsafe. The runtime now finishes the enclosing native batch,
atomically clears dependent images/input/batch history, and rebaselines on the
next batch under a new native generation without incrementing the replay session.
A subsequent 600-frame normal-render run passed with no checkpoint or fencepost
failure, captured new generation-2 batch-entry and landing images, and reached
native frame 601. This is dirty-tree regression evidence (DLL SHA-256
`C24F8FE79B55808470BC3696F0A9442F6BBDB5C598527DA118A0D4EF22488781`),
not release qualification.

## Remaining admission gate

### Schema 9 owned-correction and rotating-pose findings

Current-executable decompilation of `AdvanceCMatrixBankRingBuffer @
0x14030B630` verifies the primary and auxiliary matrix-bank controller contract:
`+0x08/+0x10/+0x18` are immutable slot pointers, `+0x20` is the current-slot
index, `+0x28` is current, and `+0x30` is previous. The transition is
`(current + 2) % 3`; it publishes old current as previous and never clears or
copies the selected destination. `LuxBattle_TickCharaMainSimulation @
0x14034DA70` consumes and produces this state synchronously around MoveVM,
physics, terrain, stochastic block state, pose finalization, and KHit pose
evaluation.

Schema 9 therefore adds a second ordered local serializer. It captures no raw
pointers: it stores the current/previous slot indices, all three live primary
and auxiliary buffers for both fighters, and the bounded fighter `+0x96490`
0x1000-byte motion tail whose extra-bone matrices are future KHit input. Restore
validates unchanged vtables, slot pointers, authored matrix counts, generation
context, exact size, and checksum; derives controller pointers from live
topology; verifies exact recapture; and restores a complete undo image after a
partial write. Unit tests cover pointer exclusion, allocation replacement,
checksum rejection, partial failure, exact undo, ordered serializer decoding,
and canonical exclusion.

The first normal-render depth-11 runs reject this candidate rather than admit
it. With a deliberately diagnostic checkpoint at every batch entry, all 11
batches replay and outer undo is exact, but canonical recapture differs in LCG,
LFSR value/index, wind RNG, wind output, and wind node state (`diff_mask=0xc800`,
`rng_mask=0x17`, `wind_mask=0x30`). Adding the motion tail did not change that
classification. The first matrix-image disagreement is inside the current
primary slot and corresponds to the same native-writer source span previously
mapped to fighter `+0x3E10`; it is evidence of another missing consumer input,
not permission to hash or transmit opaque bytes.

The diagnostic every-entry workload also fails the performance gate: observed
capture p99 was 1.00--1.06 ms (maximum 1.14 ms), local-image capture p99 was
0.90--0.94 ms, and depth-11 correction took 20.96--23.87 ms. These figures are
not the sparse production policy, but the serializer is not admitted until both
correctness and the stated normal-render limits pass. Production rollback
remains disabled and the allowlist remains empty.

Repeat native-source coverage for every required matchup and correction phase.
For each admitted subsystem, the native stream must cover at least 90% of
measured changing bytes and every remaining byte must be classified as typed
canonical state, derived state, generation identity, or presentation. Runtime
restore remains disabled until coverage, performance, transactional restore,
replay seek/resume, and offline correction proofs pass.
