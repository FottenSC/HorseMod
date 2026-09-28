> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Replay checkpoint static audit — 2026-09-06

## Scope and conclusion

Additional downtime findings are in [the static follow-up](replay-checkpoint-static-followup-2026-09-06.md): animation worker/publication ownership, component binding and teardown, URO interpolation, and Blueprint timer callback identity/self-rearm.

This audit follows the history of **Rebuild replay seeking system** (task `01a07378-5e21-7d12-b8f6-1e21626e687f`), its current implementation, and [replay-world-ownership.md](replay-world-ownership.md). It uses native Ghidra MCP decompilation/disassembly and local source inspection during the live-testing downtime. No game launch, replay test, runtime implementation, or qualification claim was made.

The same-task repeat restoration is deliberately narrow. The main weakness is the transition from retaining one live task/epoch to reconstructing state after engine updates, callbacks, and object retirement. Three useful results emerged:

1. A specific native write explains how HgCpu restoration can invalidate the auxiliary animation runtime before the supplemental lane repairs it.
2. UE animation update-rate scheduling owns state beyond an epoch stamp, including shared membership and allocated configuration. Its lifecycle is now substantially mapped.
3. Native timer replacement has destructive callback and handle-identity semantics; public setters are not historical restoration primitives.

All addresses below refer to the existing `SoulcaliburVI.exe` Ghidra program, image base `0x140000000`, executable SHA-256 `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`. These are static facts about that executable, not validation of later HorseMod binaries.

## Priority of remaining weaknesses

| Area | Existing evidence / limit | What this audit adds | Next bounded question |
|---|---|---|---|
| Native restoration side effects | Same-task restore passes after preflight/enclosing undo repair; original intermediate `RuntimeSection` failure was not attributed to an exact write | Overlay-slot-5 alias and unconditional native pointer overwrite | Observe this one writer and the completed supplemental repair if further diagnosis is needed |
| Animation across engine epochs | Engine/application ownership is ported; same pending task and epoch remain required | Owner-shared URO tracker layout, cadence equations, allocation and last-member destruction | Which actual replay components enable URO, share trackers, and change scheduling state across one completed world? |
| Timers and scene callbacks | Manager admission, six observed callbacks, pause/finish Kismet paths already audited | Setter ordering, retained-handle reuse, global serial and executing-record cancellation | Can one classified callback transaction be reconstructed with its owner fields and handle references? |
| Retired task/event ownership | Current checkpoint requires the identical incomplete event and native task | No claim of task resurrection | Specify a fresh-task reconstruction boundary before removing the lease checks |
| Native RNG handoff | Broker restores its modeled stream; host rejects changed UCRT images in the small experiment | Source review confirms this guard remains necessary; native palette reset also consumes shared LCG | Prove original CRT alignment after a historical restore before allowing changed-draw checkpoints and native handoff |
| Lifecycle/presentation | Rendered held frames and same-task continuation have evidence; general exit/re-entry and controls do not | No new live evidence | Keep independent control, post-stop continuation, scene change and cleanup experiments separate |

The missing move-state-3 equivalence and interactive-control evidence are test gaps. More broad reverse engineering will not itself turn the existing diagnostic into a correctness gate.

## 1. Exact overlap in the native fighter reader

### Verified write chain

`LuxBattle_HgCpuDirect_ReadCharaStateFromSnapshot @ 14030AE80` restores the palette block at fighter `+0x971E8`, then:

| Instruction | Verified operation |
|---|---|
| `14030B080` | Compute fighter `+0x29120`, the bone-data bank |
| `14030B08E` | Store that address at palette `+0x10` |
| `14030B0A0` | Call `LuxMoveVM_ResetAISlotStateArray @ 1402F8FA0` |
| `1402F8FCE` | Initialize sample-frame cursor to bone bank `+0x1DEC` |
| `1402F8FE0..FE3` | Load palette active motion-bank pointer and store it at cursor `-0xC` |
| `1402F9024` | Advance cursor by `0xB0`, for seven slots |

At slot 5 the store destination is exactly:

```text
fighter + 0x29120 + 0x1DEC + 5 * 0xB0 - 0xC
= fighter + 0x2B270
```

The surrounding slot begins at fighter `+0x2B268`. Its sample pointer at `+8` is the same storage that `LuxMoveVM_InitializeCharaAnimClipPlayer` writes at `14037C255` as the auxiliary clip runtime's table. The reset also clears the index/frame at `+0x2B278/+0x2B27C`. This pointer store is unconditional for each slot; it occurs even if the later conditional palette-mode application is skipped.

`CharaAnimationState::capture_unchecked` checks the runtime pointer against the player's packed-section dictionary when the clip is active. A palette motion-bank pointer need not be such a section. Thus the reader can create an intermediate state that the supplemental capture correctly rejects. A null pointer and inactive clip follow separate validation paths; this is not a claim that every reset must fail.

This is a proven static overlapping writer and a concrete explanation consistent with the recorded failure. The failed run's first offending instruction was not captured with a live watchpoint during this audit, so retrospective attribution is not a new runtime result.

### Consequence for the current fix

The current order in [CandidateGameStateAdapter.cpp](../../../../HorseMod/horselib/deterministic/CandidateGameStateAdapter.cpp) is native HgCpu restore, motion banks, native regions, move dispatch, secondary events, supplemental animation, wind, then UCRT. Both restore and enclosing undo repair the animation after the native reader. `CharaAnimationState::RestoreUnderEnclosingTransaction` avoids taking a new undo image from that intermediate state. The recovered overlap supports this transaction boundary.

Do not remove the native reset or weaken the packed-section validator based on this finding. Do not move supplemental animation before the native reader. If future failures require instrumentation, record only the before/after pointer at the reader's reset call, its palette source pointer, and the final reconstructed identity, together with the existing transaction result.

The same reset conditionally calls `ApplyAIPaletteMode(..., 1)` at `1402F9090` when both active motion bank and key buffer exist. Mode 1 consumes one shared LCG draw through `1402FA080`. Native-region restoration writes the historical LCG later (`NativeCandidateRegions.cpp`). This is another reason to preserve the verified write order and complete undo.

### Corrected stale Ghidra claims

- The reader's old plate incorrectly claimed relocation-index validation and a guarded invalid-entry fallback. Its body directly indexes the region table and conditionally adds the selected base; a zero base leaves the encoded offset unchanged. Valid stream/table identities are preconditions, not checks supplied by this function. This is a restoration-contract correction, not a vulnerability assessment.
- The clip initializer's old plate said the outer clip-table writer was unknown. `ApplyCharaAnimSlotEntry @ 1402F77C0` already proves packed-section publication at fighter `+0x95ED8`; the stale statement was corrected.
- The advance path loads the owner runtime's own table. It does not test equality with the outer clip player's table. Both identities must remain independently represented. Two unrelated old stack-release comments were also corrected against assembly.

## 2. Owner-shared UE animation scheduling

### Recovered layout and lifetime

The new `FAnimUpdateRateParameters_VerifiedPartial` describes a `0x88` prefix. `FAnimUpdateRateTracker_VerifiedPartial` describes the enclosing allocation of exactly `0xA0` bytes. Ghidra's alignment metadata is not an independently established native ABI alignment.

| Offset | Type / meaning | Primary evidence |
|---|---|---|
| `00` | int mode: trail 0, look-ahead 1 | `141DD8F90`, `141DD6D50` |
| `04/08` | int update/evaluation rates | `141DD8F90` |
| `0C` | flags; interpolation bit 0, skip-update bit 3, skip-evaluation bit 4 | both scheduling bodies |
| `10/14/18` | float accumulated pose offset, additional time, this tick's delta | both scheduling bodies |
| `1C` | int non-rendered update rate | constructor and `141DBB7D0` |
| `20/28/2C` | owning float threshold array, count/capacity | `141DB5270`, `141DBFD60` |
| `30..7F` | owning LOD-map/set storage | constructor, lookup in `141DBB7D0`, destructor |
| `80` | int maximum interpolation rate | constructor / trail body |
| `84` | byte shift bucket | constructor / selector |
| `88` | uint last compressed update epoch | `141DDAB80` |
| `8C` | byte assigned shift tag | registration / selector |
| `90/98/9C` | component pointer array, count/capacity | registration / removal |

`RegisterComponentAnimRateTracker @ 141DCE3F0` keys the global map by component `+0x190`, falling back to the component address. Multiple components with the same owner share a tracker. It allocates/constructs the tracker when absent, appends the component, and invokes the component delegate at `+0xA50` with the tracker. Registration can allocate and execute a callback; it is not a pure resolver.

The constructor defaults update/evaluation to 1, non-rendered rate and interpolation limit to 4, and thresholds to `0.24f/0.12f`. Unknown bytes `+85..87` and `+8D..8F` are not declared deterministic padding.

`UnregisterComponentAnimRateTracker @ 141DBFD60` removes membership. When count reaches zero it erases the map entry, frees the component array, destroys map storage, frees thresholds, then frees the tracker. Saving its raw address does not preserve that lifetime. The map entry itself has stride `0x18` and tracker value at `+8`; it must not be typed as a tracker.

### Cadence is more than the last epoch

`UpdateOwnerAnimRateTrackerForEpoch @ 141DDAB80` computes the engine epoch modulo `0xFFFFFFFF`, compares tracker `+0x88`, and publishes the new stamp **before** aggregating component state. This is modulo `2^32-1`, not simple 32-bit truncation. Native `DIV` is the source of the decompiler's synthetic 128-bit dividend.

`UpdateAnimRateTrailMode @ 141DD8F90`:

1. Uses positive `a.URO.ForceAnimRate` to override both requested rates.
2. Clamps update to at least 1 and evaluation to `max(1, (requestedEvaluation / update) * update)` with signed integer division.
3. Selects interpolation from caller eligibility and the strict `evaluation < maxInterpolationRate` condition, or the force-interpolation CVar.
4. Computes `(engineEpoch + byteShiftTag) % 0xFFFFFFFF`, then takes modulo update/evaluation rate to publish skip flags.
5. Clears additional time. Skipping an update subtracts delta from the accumulated offset. An admitted update transfers a negative offset into additional time and clears the offset.

`UpdateAnimRateLookAheadMode @ 141DD6D50` retains a different time-offset algorithm. Switching from trail clears the old offset; delta is subtracted, and a negative result is advanced by `max(-offset, lookAheadSeconds)`. It publishes additional time and clears skip flags on that path, otherwise skips both. This function does not directly read the engine epoch, so counter-xref searches alone miss a dependent state machine.

The selector at `141DBB7D0` lazily assigns a zero shift tag by incrementing a byte in the bucket array at `144392F84`. A wrap to zero permits another assignment. Registration order can therefore influence phase; restoring per-tracker values without accounting for later assignments is incomplete. It selects threshold/LOD-derived rates and trail/look-ahead branches. `141DBBA30` aggregates the member components' flags, maximum screen-size value, two virtual predicates and minimum LOD-related value. The exact virtual predicates were intentionally not given speculative root-motion names.

The enable wrapper `141DDAB50` checks component `+0xA41`, the enable CVar at `144392FB8`, and owner presence before this update. Static reachability does **not** prove that every replay mesh enables the optimization. No URO snapshot implementation is justified until actual replay membership and branch use are established.

### Design implication

For the next cross-world experiment, observe the relevant registered trackers, their member identities and logical scheduling fields at two adjacent completed worlds. If the set is stable, first define restoration within that stable registration generation. Do not assume tracker buffers survive scene changes, copy owning pointers, rewind the global engine counter, or invoke registration as a silent repair. UE pose/montage/appendix state remains a separate domain from both this scheduler and the Lux clip supplement.

## 3. Timer replacement, identity and cancellation

This extends the existing timer audit rather than repeating its callback inventory.

`14217E8E0` takes manager RCX, caller-handle pointer RDX, unified callback storage R8, rate XMM3, then loop/first delay on the stack. Assembly proves:

- At `14217E91E..E934`, a nonzero handle is looked up and its old record removed or unbound **before** the rate test.
- At `14217E93C..E93F`, a nonpositive or NaN rate exits after that removal. This body does not zero the caller's handle.
- At `14217E953..E964`, only a zero caller handle causes increment/publication of process-global serial `1443B3060`. Nonzero handles are reused even when no old record was found.
- It constructs/copies owning callback alternatives into a temporary record, admits by current engine epoch, then releases the temporary callback storage.

`142176E00` searches the embedded executing record first, then active, paused and pending arrays by 64-bit handle. Executing returns index `-1`. Container indices and record addresses are transient; they are not a timer's persistent identity.

`RemoveWorldTimerByState @ 14217E5A0` removes from pending (0), active heap (1), or paused (2), or unbinds the embedded executing record (3). `UnbindWorldTimerRecord @ 14216FC80` clears native delegate storage, weak-object binding, function name, function storage and handle. It has no direct store to state/rate/deadline. Executing cancellation must not be described as simply setting a new state byte.

Consequently, recreating historical timers through the setter can change callback ownership, admission, serial allocation and owner-held handle references. A logical timer image must preserve the relationships used by callbacks and the scene, and a restoration boundary must account for callback side effects already committed. Do not rewind the global serial for one manager. These findings do not yet define a complete timer reconstruction algorithm.

## Ghidra work and validation

Saved native MCP changes include eight named/typed functions (six animation lifecycle/scheduling bodies plus timer removal/unbind), two new animation structs, the verified existing set-storage type applied to the embedded LOD map, four global labels/types, and relevant plate/PRE/EOL corrections. No Ghidra database scripts or project-file edits were used.

The completeness audit was rerun after structural changes. Trail/look-ahead/constructor scored approximately 96/92/100; tracker epoch/registration/removal retain deductions for intentionally generic dynamic owner/component pointers and the synthetic DIV operand. These are documented rather than assigned invented full object types. Timer removal/unbind scored approximately 97/98. Scores are annotation checks, not correctness evidence. The selector, aggregator, full timer setter and full timer tick remain partial investigations, with no claim of complete cleanup.

Hermes validation was attempted through the available local CLI, but it returned `No inference provider configured`. No independent Hermes review is claimed; provider configuration was not changed.

## Handoff to the main task

Preserve the current same-task lease and preflight/enclosing-undo design. The native alias supports the fix already made. Complete the outstanding bounded execution/control experiments when testing is available, then approach cross-world restore through an explicit ownership manifest: task/event generation, animation registration generation and scheduling state, classified timer/callback transactions, and RNG handoff. Reject unsupported lifetime transitions until they are reconstructed and independently compared.

Do not use a long replay campaign to discover these mechanisms. The next useful live work is a small, falsifiable boundary experiment tied to current identities. This document supplies static constraints and exact observation sites; it does not fill any execution, seek, lifecycle or performance gate.
