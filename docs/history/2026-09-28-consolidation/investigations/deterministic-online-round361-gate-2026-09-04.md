> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Deterministic online round-361 development gate — 2026-09-04

This ledger records non-certifying development evidence. It does not freeze a
candidate or authorize the offline matrix, Tira campaign, or release
qualification.

## Gate result

- Result: pass.
- Mode: authenticated Steam/Sandboxie, normal renderer, two same-process match
  cycles.
- Exact authored location: Silver Wolves' Haven,
  `/Game/DLC/07/Stage/STG011_R`.
- Report:
  `evidence/dev-round361-two-match-post-reownership-final.json`.
- Report SHA-256:
  `41BFA2D1B87C6E6270D16831A17CB2E72E6A24D20451F371965CED28D7852C57`.
- Tested HorseMod SHA-256:
  `A70967A30B843D77EE9F15D62A7E05007426F015EDC0605AB67E6177F7516DC1`.
  The build output and both runtime-loaded `ue4ss/Mods/HorseMod/dlls/main.dll`
  files matched this hash after cleanup. Older DLL copies outside that loader
  path did not match and are not evidence for this run.

## Root failure and repair

The bounded diagnostic run failed at generation 2, frame 450 with:

`presentation_commit_failure status=illegal_transition confirmed=2:450 pending_events=4 payload_bytes=100 guard_mask=0x2 publish_failures=1 failed_event=2:442:3:2:0x6cdbd6c2ed4c5c02 payload_size=24 payload_head=01/00/03/00`

`guard_mask=0x2` proved that the frame-fencepost callback attempted a confirmed
presentation commit while an outer-tick capture was still active. The repair
queues/coalesces the confirmed coordinate at the fencepost and publishes it
after `ObserveOuterTick` has closed the active capture. Cross-generation or zero
coordinates are rejected. Failure remains terminal.

The evaluator now waits for the exact coordinate's bounded
`confirmed_presentation` drain record when the earlier confirmed-hash record
contains pending events. It requires zero pending events, payload bytes,
duplicates, publish failures, and guard mask. It does not accept a later or
different coordinate.

## Focused verification before the live canary

- Native deterministic tests: 9/9 passed.
- Python qualification tests: 147/147 passed.
- The new native unit test covers deferred-coordinate queueing, coalescing, and
  zero/cross-generation rejection.
- The new evaluator test rejects transient pending presentation by itself,
  accepts only the exact post-outer drain, and rejects a nonzero guard mask.

## Two-match round barrier and correction evidence

Both cycles independently established equal peer baseline identities at
generation 1 frame 123 and generation 2 frame 390. Each crossed the frame-361
round barrier, first regained ownership at generation 2 frames 393/394, and then
ran the depth order 11 -> 1 -> 6 in owned round 2.

Cycle 1 bilateral correction hashes:

- `2:450` — `67ffb918c5214d773bbc8e6ade562a3adedd4de58bb8e5b721dd5927c68bd784`
- `2:480` — `63bf20dc1526e9ec90b571517a37715619d1a1d8f1c99551d011ac45d1af89e5`
- `2:510` — `13d010d8ebfad06331b521a265a7a50e405a57171b820142cba08831d9de2ce2`

Cycle 2 bilateral correction hashes:

- `2:450` — `b83484242f03dd82f70562029779fee3acc9a65faee262ca830c903b9e79e8d9`
- `2:480` — `eaf7b167bfea68f7353b9006ae18997dbffd4466a48e25ba167f2eb0ccb38a4f`
- `2:510` — `c04eddb9f222346290ac59a699453ccf0cc747de5e6a557d9a22ee6a5a1b9e4f`

Cycle 2 exercised changed presentation data: both peers reported post-outer
drains at frames 450, 480, and 510 with four committed events and zero pending
events, payload bytes, duplicates, publish failures, and guard mask.

## Fail-closed crossing proof

`tools/online_coordinator_selftest.cpp` now exercises the exact invariant:
after both peers begin the 1:360 -> generation-2 round barrier and independently
capture 2:390, unequal baseline hashes make both coordinators return
`StateHashMismatch`, enter `Failed`, reject `NotifyOwnedTick(2:391)`, record
`FreezingBaseline` as the failure origin, and select match termination to lobby.
An independent test also rejects equal hashes attached to unequal coordinates.
Thus an incorrect or unconfirmed crossing cannot silently regain ownership.

## Cleanup, timing, and storage

- Both cycles returned deterministic owned storage exactly to 297,957,728 bytes
  on both peers.
- Maximum correction time was 6.3805 ms on the host and 5.9377 ms in the
  sandbox for cycle 1, and 5.4045/5.5234 ms for cycle 2.
- Both peers automatically returned to the Player Match lobby after both
  cycles.
- Runner cleanup was graceful; emergency cleanup was not used; zero game
  processes remained; requests were disarmed; all four diagnostic flags were
  false.
- Cycle-2 process-private growth was 72,261,632 bytes on the host and 25,329,664
  bytes in the sandbox. This is recorded for the later explicit memory ceiling
  and soak gates. Exact owned-storage return means this rehearsal does not by
  itself prove a deterministic-storage leak, but it also does not certify the
  broader process-memory ceiling.

## Ghidra persistence

The Ghidra workflow was completed against the existing `SoulcaliburVI.exe`
program for
`LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`.
After structural and variable audit, EOL comments were persisted for its 0xE0
scratch allocation and release. Completeness verification scored 95.0%
effective completeness; the remaining five fixable points were comment-density
only, with separate structural deductions for register-merged decompiler
locals. `save_program` succeeded for `SoulcaliburVI.exe`. This was not merely
read-only inspection.

## Qualification state

No immutable build was created and no broad campaign was restarted. The next
ordered development gate is the production-depth-7 timing ceiling, followed by
observing and verifying the actual Tira RNG transition. Certification remains
blocked until those and the remaining release-plan gates pass on a later
unchanged-hash candidate.
