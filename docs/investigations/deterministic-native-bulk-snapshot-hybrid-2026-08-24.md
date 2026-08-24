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

## Remaining admission gate

Repeat native-source coverage for every required matchup and correction phase.
For each admitted subsystem, the native stream must cover at least 90% of
measured changing bytes and every remaining byte must be classified as typed
canonical state, derived state, generation identity, or presentation. Runtime
restore remains disabled until coverage, performance, transactional restore,
replay seek/resume, and offline correction proofs pass.
