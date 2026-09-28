> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Independent startup compatibility

- Signature: matching native callback prefixes, but different stage-water particle age/shared RNG at native tick 3, before rollback at tick 417.
- Suspected producer: variable count of native stage updates during asynchronous loading. Retained native manager chronology shows phase 1 then phase 2 with adjacent RNG states; historical pairs show the variation in both runtime and stock processes.
- Production change: none. Never reset RNG, adjust particle age, omit lifecycle observations, or install expected state.
- Testing change: permit the existing verified candidate-resume path with named profiles. Require identical profile hash and diagnostic settings in addition to existing protocol, binary identity, log integrity, and cleanup validation.
- Local regression: profile resume reaches admission; mismatched profile or diagnostics rejects; legacy CLI remains intact. All 122 runner tests passed.
- First experiment: resume the completed candidate and capture one new independent control. Outcome: control has the other startup signature; reject comparison and retain failure. Both captures cleaned up.
- Next distinguishing experiment: catalog that completed control under its own original-forward signature, before capturing another candidate. Run `combat-restore --profile root-transform-417`. Reuse is permitted only for exact startup, identities, setup and coverage. Regenerated observations are never selection criteria.
- Short live profile: `root-transform-417`, seven regenerated ticks plus 120 independent continuation ticks.
- Outcome: passed. Candidate `replay-9affe1fc7cf043dbb1dc6547187efa64` matched the pre-existing independent control, including all seven regenerated ticks and 120 continuation ticks. Cached-control reuse and both cleanups verified. Rolling30 then exposed a separate production capacity rejection before its first correction.

Primary chronology and prior direction reversal are retained in [historical status](rollback-status-history-2026-09-20.md). Current evidence and deployment state are in [current status](../../../rollback-status.md).
