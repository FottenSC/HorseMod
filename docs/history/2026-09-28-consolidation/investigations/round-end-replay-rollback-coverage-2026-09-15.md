> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Round-end replay versus current rollback coverage

Source audit, 2026-09-15. No production changes, deployment or new runtime test. Native reference: [mechanism and retained Ghidra evidence](round-end-replay-2026-09-15.md).

## Conclusion

We cannot claim all factors are accounted for. The current code implements the native serializer plus substantial enclosing rollback state, but this audit found no explicit checkpoint participant for the embedded cinematic recorder and no proof that it remains inactive in every supported rollback traversal. This is an unresolved coverage item, not a newly reproduced runtime failure.

## Comparison

| Native replay factor | Current implementation evidence | Assessment |
|---|---|---|
| Native battle snapshot writer/reader | `Sc6CandidateCheckpointCapture.cpp:53`, binding at 682/684 | Same 0x3841E0/0x384540 pair is used |
| Independent saved-current state | `Sc6ReplayHost.Restore.inl`, operation-owned original/undo; checkpoint capture explicitly distinguishes A GPU image from B | Implemented transaction structure; complete native recovery qualification remains open |
| Restore then advance exact ticks | `Sc6ReplayHost.Rolling.inl:14`, 109 onward, 129 onward | Eight checkpoint slots, seven-tick target, completion/retirement checks implemented |
| Input history and corrected input source | `Sc6ReplayInputSource.hpp:41`, `Sc6ReplayHost.Checkpoint.inl:358`, `NativeCandidateRegions.hpp:356` | Saved-replay source revision and input-log state handled; this does not capture the separate cinematic cyclic recorder |
| Camera, timing, RNG, MoveVM | `NativeCandidateRegions.hpp:356`, camera source image at 317, `Sc6CandidateCheckpointCapture.cpp` | Explicit components exist; this does not prove the cinematic owner's separate camera-control state is covered |
| Engine actors/effects/HUD/physics/scheduling | `Sc6ReplayHost.hpp:103`, `Sc6ReplayHost.Checkpoint.inl:391` onward | Explicit participants exist; lifecycle and coherence qualification incomplete |
| Five cinematic snapshots and saved-current buffer | Native embedded owner +0x488 and +0xC8500 | No explicit participant found in checkpoint definitions, capture/restore integration or deterministic source searches |
| Cinematic cyclic inputs, retained window and cursors | Native +0xF0518 controls and +0xF0538 buffers | No explicit production capture/restore path found; source revision above is a different owner |
| Trigger, cleanup, auxiliary state, input-frame tags and camera controls | Native owner +0, +0x468, +0xF0598 onward | No explicit checkpoint coverage or inactive-state admission found |
| Start/finish presentation callbacks and cleanup | Native state-machine dispatcher +0x60/+0x68, per-character publications and input clearing | Broad VFX/audio ownership exists, but no one-to-one proof for these replay lifecycle events was established |

Code references are relative to `HorseMod/horselib/deterministic` and describe the inspected working checkout. The generic snapshot stream's existence is not evidence that every owner outside that stream is included.

## Why active-combat scope is insufficient by itself

The native state machine initializes its recording state in world mode 2 and records input and conditional snapshots during combat. It does not start all its work only when the round ends. The cyclic input transform is registered directly into the character input pipeline.

If that recorder is active during a rollback traversal, restoring the battle core without its input cursor/window/control state can append another copy of resimulated inputs, advance its tags or overwrite its retained snapshots. A later round-end replay could then consume inconsistent history. If its playback mode is active, the encoded-input transform can also substitute historical inputs after an upstream input source supplies them.

These are consequences to test, not claims that the retained 128-cycle experiment exhibited them. The supported saved-replay session may configure some of these controls inactive; this audit did not establish that condition.

The inspected rolling admission checks session, source recording, revision, checkpoint shape and completed application state. It does not explicitly check this embedded owner's trigger/mode/cyclic controls. Qualification entry checks active-combat world mode, but that alone does not prove recorder inactivity.

## Required closure

1. Observe the actual embedded recorder controls, input windows/cursors and snapshot tags at A, B and resimulated C in the shortest existing supported combat case.
2. If inactive, establish why and enforce that supported-state invariant before publication and throughout execution.
3. If active, investigate its ownership and add transactional preservation of the consumed history/control state, including original-B recovery and corrected-history handling. Do not fix it by blindly suppressing required callbacks or copying pointers.
4. Add a production-boundary regression for the demonstrated state mutation. Verify future native round-end replay and boundary handling with a bounded native continuation where necessary.
5. Independently close start/finish callback and input-cleanup semantics for any supported transition. The replay's deliberate input clearing is not an operation to copy into every combat rollback.

The sparse five-slot capture policy, 378-frame cinematic start preference and replay camera selection need not become the seven-tick rollback algorithm. What matters is preserving or explicitly excluding their live mutable owners so rollback cannot corrupt later native behavior.

## Qualification remains separate

The status file reports retained 128-cycle prefixes and a historical trace-child reconstruction dependency. This audit does not advance unchanged-input, corrected-input, B-recovery, coherence or performance gates. It adds a specific cinematic-recorder coverage question to resolve alongside the existing ownership work.
