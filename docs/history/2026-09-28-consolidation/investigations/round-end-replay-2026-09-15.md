> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Round-end instant replay — native investigation

Date: 2026-09-15. Program: existing `SoulcaliburVI.exe` in Ghidra, x86-64 Windows, image base `0x140000000`.

## Finding

The automatic round-end replay uses **sparse native battle snapshots plus rolling recorded inputs**. It saves the current post-round state, restores an earlier battle snapshot, feeds recorded inputs through the normal character-input path, renders that execution with replay camera control, then restores the saved post-round state.

This was verified by native decompilation, call relationships, constructor bindings and assembly. No new live test or deployment was performed. [Retained native evidence](../../../investigations/evidence/native/round-end-replay-2026-09-15-native.json) contains 13 decompilations and the state-machine disassembly. Existing comments were treated as hypotheses and two reversed restore-site comments were corrected.

## Main functions

| Address | Role |
|---|---|
| `14037D280` | Construct snapshot storage and linear/cyclic input recorders |
| `14037D530` | Register per-player cyclic and linear encoded-input transforms |
| `140387540` | Automatic result-screen replay eligibility and trigger |
| `14037D670` | Record, select, rewind, replay and return state machine |
| `1403841E0` | Write native local battle snapshot |
| `140384540` | Read native local battle snapshot |
| `140312510` | Record or substitute encoded inputs during character input processing |
| `1402D9CD0` | Run cinematic state machine before ordinary world-mode dispatch |
| `1402D7660` | Separate explicit cinematic trigger adapter, with auxiliary byte |
| `1402D76B0` | Request cleanup |

All addresses are static virtual addresses in this Ghidra program.

## 1. What it retains during the round

The owner is the active session's embedded cinematic/serialization object at **session +0xAA120**. Offsets below are relative to that embedded object:

| Offset | Native ownership |
|---|---|
| `+0x0000` | State, trigger, auxiliary trigger and cleanup latch |
| `+0x0468` / `+0x046C` | Snapshot count / ring cursor |
| `+0x0470` | Five recorded input-frame tags |
| `+0x0484` | Current cinematic input-frame coordinate |
| `+0x0488` | Five snapshot streams, stride `0x28018` |
| `+0xC8500` | Separate saved-current snapshot stream |
| `+0xF0518` | Two 16-byte cyclic input-transform controls |
| `+0xF0538` | Two cyclic ushort input buffers, stride `0x30` |
| `+0xF05C0` | Replay camera parameters |

The constructor sets each input ring's capacity to **720 words**. Each input word encodes two seven-bit input lanes plus a side bit. The six snapshot stream objects occupy **983,184 bytes** (`0xF0090`, approximately 960 KiB); this is only their inline storage, excluding input vectors, other object fields and engine resources. Each stream has 163,840 bytes of payload capacity and 24 bytes of overhead.

The constructor also builds 18 linear fixed-key buffers, grouped as nine per player, and nine parity buffers. These are distinct from the five cinematic snapshots.

State 0 seeds the first snapshot when the world-mode getter returns 2. State 1 records subsequent snapshots into the five-slot ring. Capture requires **at least 84 input frames since the preceding snapshot**, active recording, no active MoveVM palette slot and additional fighter-state conditions. Some fighter conditions defer eligibility below 360 frames. World-mode ID 3 excludes this capture branch.

Therefore the snapshot cadence is conditional. A separate every-32-frame branch stores/checks packed move-lane parity; it is not the battle-snapshot cadence.

## 2. How round end starts it

`LuxBattle_ResultScreen_PostTick @ 140387540` waits past its phase-end coordinate and checks fighter delay/special-state gates. Its automatic cinematic branch requires:

- The cinematic-triggered latch is clear.
- `g_dwScbattleStageInfoInitialized == 0`.
- The signed-short round-result code is in 1..3.
- Special-match mode is not 2 or 4..6.
- An active world subdriver exists to receive the session trigger.

The native branch writes cinematic trigger=1, auxiliary trigger=0, cleanup=0, and initializes camera parameters. These are the verified predicates; this audit does not assign user-facing mode names to the partially named global flags.

The separate explicit trigger at `1402D7660` accepts an auxiliary byte. It is not needed to explain the automatic result-screen branch.

## 3. How it chooses the replay start

State 1 first saves the current state to `+0xC8500`, unless already inside replay. It then scans at most five snapshot tags using:

```text
delta = current recorded-input frame - snapshot frame

Ignore delta >= 600.
Prefer candidates >= 240 frames old:
    choose the one closest to 378.
If none qualifies:
    choose the greatest positive delta below 240.
Start playback only if the selected delta >= 120.
```

Strict comparisons preserve the earlier scanned choice on equal scores. The assembly at `14037E536..14037E590` verifies the 600, 240, 378 and 120 constants.

At 60 input frames per second, 378 corresponds to about **6.3 seconds**, with a 2-second minimum and a less-than-10-second candidate ceiling. This is a conversion of native input coordinates, not a measured on-screen duration; freezes and camera/time effects can change wall time.

## 4. Rewind and actual playback

The historical restore is at **`14037E5E9`**:

```text
ReadSnapshot(state + 0x488 + selectedIndex * 0x28018)
```

It sets the interactive-block flag and changes both cyclic input controls to mode 2, positioning their cursors at the selected snapshot's input-frame tag. It restores presentation flags, dispatches per-character effect state, emits the start event through VFX dispatcher slot +0x60, and enters state 2.

The input path is independently verified:

1. `14037D530` registers the cyclic control in each player's encoded-input transform list.
2. `LuxBattle_TickCharaInput @ 140312510` handles mode 1 by appending the encoded ushort.
3. Mode 2 reads a historical ushort at the cursor, increments the cursor, checks validity, and decodes it into the fighter's live input fields.
4. `LuxBattle_AdvanceWorldModePump @ 1402D9CD0` drains cinematic work, then dispatches the restored world mode's ordinary Tick/PostTick.

Thus the ring does not contain an image or pose for every displayed frame. Native battle execution advances from a restored state using the recorded inputs. The input controls at +0xF0518 retain misleading historical “palette” names in the partial Ghidra type; their constructor bindings and consumer prove their input role.

State 2 also controls replay camera blending/focus. It can switch focus on input bit 2, use the selected/winner fighter, follow its matrix position, and blend the attention camera against ordinary/special camera slots. Auxiliary replay can rearm the historical selection while preserving camera parameters.

## 5. How it returns to the finished round

Once the recorded-input control finishes or cleanup/world-mode conditions request exit, the return restore is at **`14037DE57`**:

```text
ReadSnapshot(state + 0xC8500)
```

This reinstates the saved pre-replay current state. The function then clears the cyclic playback controls, interactive-block flag and latest input words, clears both 61-entry live input rings, dispatches presentation/audio cleanup, and returns to state 0. The live input-ring cursors themselves are not reset by that clearing loop.

Two old comments labelled these restore sites backwards. They now identify the selected historical restore and saved-current return correctly. The plate was expanded with the input binding and selection evidence, read back, and the program saved.

## What this means for rollback

This is a real native rewind-and-reexecute mechanism. Its serializer is already the `1403841E0 / 140384540` pair used by the project's HgCpu snapshot work.

The native stream writes both fighters, xorshift96, selected global battle state, the MoveVM global bank, optional camera, timers, motion/physics, terrain flags and native VFX state. The reader restores these sections and relinks the opponents.

Its existence does **not** establish complete arbitrary active-combat rollback. The recorded-input owner and cinematic transaction are separate from the stream, and this investigation does not demonstrate reconstruction of all UE actors, particles, pending CPU/GPU work or scheduling state. The controlled return to a saved post-round state is narrower than repeatedly committing corrected active-combat history.

The separately named HgBattleModeReplay/ShortReplay camera modes were inspected as leads. Their shared camera Tick/PostTick is not the core automatic replay implementation; the embedded cinematic state machine above performs the rewind and input playback.

## Remaining limits

- Static mechanism established; no fresh live observer/coherence/recovery/performance result.
- Exact UI interpretation of all eligibility constants is not established.
- Detailed subscribers of the replay-start event and all engine-side effect reconstruction remain outside this bounded trace.
- Generic SSA variables and existing partial type warnings remain; no speculative struct/prototype changes were made.

