# Deterministic Simulation Rewrite — Outstanding Implementation Plan

Date: 2026-08-25  
Repository: `E:\myMods`  
Branch: `codex/rollback-rewrite`  
Required starting tip: `5d36624a8b5e0767898cb61e6bb41c0df3dd776b`  
Remote: `origin/codex/rollback-rewrite`

This document is the continuation plan for the unfinished portions of
`docs/deterministic-simulation-goal.md`. That goal document remains the
authoritative definition of done. This plan does not waive or replace any of its
acceptance criteria.

## Assignment contract

This is a continuous implementation assignment. Continue through reverse
engineering, implementation, builds, runtime diagnosis, replay qualification,
offline correction qualification, networking, Sandboxie qualification, and
release evidence. A build, test pass, reverse-engineering finding, or partial
runtime milestone is a continuation point rather than an endpoint.

Before editing, read these files completely:

1. `E:\myMods\AGENTS.md`
2. `E:\myMods\docs\deterministic-simulation-goal.md`
3. `E:\myMods\docs\investigations\deterministic-simulation-takeover-handoff-2026-08-24.md`
4. This plan

Fetch `origin/codex/rollback-rewrite`, verify that the local branch contains the
required starting tip, and preserve the following unrelated dirty files exactly:

- `tools/moveset_parser/hgmotion_reference.py`
- `tools/moveset_parser/luxformats.py`
- `tools/moveset_parser/motion_decode.py`
- `tools/moveset_parser/tests/test_hgmotion_reference.py`
- `tools/moveset_parser/tests/test_motion_decode.py`
- `tools/moveset_parser/tests/test_uassetparse.py`
- `tools/moveset_parser/uassetparse.py`

Never stage, discard, stash, clean, overwrite, or commit those seven files.
Check free disk space before artifact-heavy runs. Reuse
`build_cmake_LessEqual421__Shipping__Win64` and the existing evidence directories;
do not clone the repository or create duplicate build trees.

Keep production rollback disabled and the production allowlist empty until all
qualification gates pass. The deployed safe configuration at handoff is:

```ini
config_version=1
enabled=false
rollback_window=12
input_delay=1
trace=true
correction_probe=false
forced_depth7_qualification=false
```

Always return diagnostic flags to `false` immediately after a run, including
failed or interrupted runs.

## Proven foundation to preserve

Do not replace or regress these completed foundations:

- Opaque SC6 native reconstruction images are bounded, checksum-validated,
  generation-bound, local-only `Snapshot` attachments. They are not transmitted
  and do not enter canonical peer hashes.
- Native bulk restore is transactional: preflight, undo capture, native readers,
  typed supplements, derived repair, canonical recapture, and exact undo after
  any post-write failure.
- Snapshot/history storage is bounded and reuses owned buffers. Replay capture
  retains the 512 MiB limit.
- `HorseDeterministicCore` is explicitly optimized with `/O2 /Ob2` outside Debug.
- Qualification-only forced depth-7 correction uses the production correction
  path on each active normal-render gameplay tick. Character intros are excluded;
  qualification starts only after `round_state_frame > 16` and
  `unpause_countdown == 0`.
- Before the stricter presentation identity gate, one workload completed 600/600
  exact canonical depth-7 corrections with correction p99 3.8 ms, capture p99
  0.190 ms, maximum capture 0.384 ms, and a bounded approximately 7.57 MiB forced
  history ring.
- Stage/audio suppression counters are now propagated through owned batches and
  qualification reports. One run observed 5,018 suppressed battle-audio calls
  and zero hook failures. That result is not full presentation qualification.
- The ordered battle-audio identity gate deliberately fails closed on an actual
  omitted selector state. Do not remove or weaken it.

## Current failing evidence

The first forced correction at gameplay frame 980 currently fails with
`presentation_failed`; transactional undo succeeds. For the failed native batch:

- authoritative audio calls: 10;
- resimulated/suppressed audio calls: 10;
- event-type/route hash: exact (`31ae5da0`);
- positional payload hash: exact (`83fad042`);
- authored payload-ID hash: different (`44c34e1e` versus `b2c29812`).

Current-executable Ghidra evidence identifies one direct cause:

- `HandleContactSoundEventForBattleSound @ 0x1403C63C0` derives the authored cue
  before terminal audio dispatch from handler voice-cue tables, player/material/
  setup state, alternate-contact state, and a remapping helper.
- `LuxMove_RemapAttackType_WithCounter @ 0x1403BA080` mutates and consumes a
  two-state counter at battle-sound handler `+0x3E0` for contact types 8–11.
- That counter is absent from the current checkpoint, so gameplay can converge
  canonically while resimulation chooses different sounds.

This is a typed-state omission, not permission to ignore authored payload IDs.

## Phase 1 — Close deterministic audio selection

1. Through Ghidra MCP, fully type and document
   `LuxMove_RemapAttackType_WithCounter @ 0x1403BA080`, the owning battle-sound
   handler field at `+0x3E0`, its initialization/reset/destruction writers, and
   every reader. Correct the function prototype and relevant handler structure,
   follow the required type/comment workflow, run the completeness audit, and
   save the program.
2. Inventory every input that can alter the payload passed from all direct callers
   of `LuxBattleManager_DispatchBattleEventByClass @ 0x140519480`. At minimum
   resolve voice-cue table values, player modes, stage-material selection,
   player setup/preset state, style ID, alternate-contact state, phase/move cue
   remapping, and any RNG or alternating counters.
3. Classify each input as immutable content, canonical gameplay state, persistent
   presentation selector state, derived state, or terminal-only state. Record its
   owner, bounded size, generation, writers/readers, capture phase, restore order,
   and invalidation boundary.
4. Add the smallest evidence-backed typed checkpoint exception for selector state
   omitted by native images. Restore it before resimulated semantic listeners run.
   Do not snapshot active voice IDs, audio player pointers, UE objects, locks,
   queues, or allocator internals as portable state.
5. If selector state cannot safely be restored, implement a bounded source-frame
   semantic journal/shadow boundary only after proving the exact source value,
   stable identity, downstream consumers, confirmation behavior, and failure
   semantics. Do not defer cue selection to confirmation-time state.
6. Keep the ordered per-batch audio check. Require exact count, order, route,
   authored payload ID, and position on unchanged-input forced corrections.
7. Add restrained tests for generation drift, invalid handler identity, selector
   restore/undo, sequence mismatch, capacity exhaustion, and lifecycle reset.
8. Rerun the normal-render forced workload. Phase 1 passes only after 600
   consecutive corrections have zero audio sequence mismatches, zero audio
   terminal leakage, exact canonical convergence, and the existing timing limits.

## Phase 2 — Complete presentation ownership and exactly-once reconciliation

1. Replace any allocation-growing production journal containers with bounded,
   preallocated storage. Prove stable capacity and memory usage after warm-up.
2. Finish audio presentation coverage beyond the current play terminal:
   stop/replace operations, character cues, phase/subsystem sounds, stage sounds,
   scheduled/BGM lanes if reachable, active-voice tracking semantics, and
   Blueprint publication. Unknown owned-iteration routes fail closed.
3. Exercise and verify the existing stage wall/barrier suppression paths. Required
   workloads must produce nonzero wall/barrier and semantic dispatch counters;
   zero observations are a coverage failure, not a pass.
4. Complete VFX/particle ownership using the verified typed shadow design. Never
   return null from `SpawnLuxParticleSystemComponent @ 0x1408A3920`; callers
   immediately configure and publish the returned identity. Shadow slot/component
   state must accept semantic writes without constructing rollback-doomed UE
   objects, and confirmed commit must materialize it exactly once.
5. Resolve camera publication routes reachable from the owned simulation corridor,
   including the unresolved interface slots near `+0xA0/+0xA8/+0xB0`. Separate
   canonical camera inputs, persistent camera state, ephemeral shake/vibration,
   and render-only output. Implement suppression/reconciliation only at verified
   boundaries.
6. Give each presentation event a pointer-free stable identity and source-frame
   value payload. On correction: discard invalid speculative events, record the
   corrected sequence, commit confirmed events once, and retain a retry cursor so
   a partial terminal failure cannot replay an already committed prefix.
7. Instrument attempted, suppressed, committed, discarded, duplicate, missing,
   failed, and leaked events by family without production fault injection.
8. Pass unchanged-input and deliberately corrected-input tests proving no missing
   or duplicate audio, camera, particle, stage, Blueprint, or journal event and
   exact final persistent presentation state.

## Phase 3 — Prove bounded allocation and lifecycle behavior

1. Measure snapshot store, batch timeline, forced history, journal/shadow storage,
   hook objects, native allocation generations, and relevant UE object counts at
   warm-up and throughout qualification.
2. Remove per-correction map nodes, vector growth, heap churn, or lifecycle-local
   objects that grow after activation. Preallocate rings and queues to fixed
   capacities and fail closed on exhaustion.
3. Validate enter, active use, correction, correction failure, exact undo, round
   change, replay exit, scene change, restart, clean exit, and re-entry.
4. Any fighter, stage, camera, SubVM, container, handler, or heap allocation
   replacement must atomically invalidate every dependent opaque image, typed
   exception, journal identity, and pending event.
5. Demonstrate no allocation/lifecycle growth across at least 600 forced
   corrections, multi-round transitions, repeated exit/re-entry, and the required
   soak windows.

## Phase 4 — Finish strict replay seek/resume qualification

1. Implement or finish batch-aware restore/resimulation/landing using the hybrid
   adapter and completed presentation journal.
2. Use the normal renderer for all certifying evidence. `lux-no-render` remains a
   diagnostic only.
3. Pass seeks at 10%, 25%, 50%, and 75%, plus backward, repeated, failed-cleanup,
   restart, capacity-limit, and cross-round cases.
4. Each seek must validate within 0.5 seconds, resimulate at most 29 frames from
   the nearest compatible checkpoint, and resume for 600 frames at at least 58
   ticks/second with zero canonical divergence.
5. Verify presentation suppression during resimulation, exactly-once landing
   reconciliation, normal presentation resumption, and clean teardown/re-entry.
6. Prove capture stops cleanly at 512 MiB and leaves a usable partial timeline.

Use the repository-required strict replay command after every relevant change:

```powershell
python E:\myMods\tools\replay_seek_test_run.py --kill-game --launch-game `
  --allow-unknown-presence `
  --start-replay E:\myMods\ReplayExample\REPLAY_12744704008398858106.bin `
  --timeline-generation-mode normal --case-preset watch --watch-frames 600 `
  --wait --analyze --strict --min-resume-tick-rate 58 `
  --resume-tick-window 120 --max-seek-validation-seconds 0.5
```

## Phase 5 — Complete the immutable offline correction matrix

On one immutable DLL/config/schema/runner/source combination, capture fresh
normal-render baselines and qualify:

- Astaroth versus Raphael, stage 273/map 17;
- Raphael versus Maxi, stage 9/map 9;
- Siegfried versus Cervantes, stage 23/map 23.

For every matchup, run corrections near round start, active combat,
hit/presentation activity, and round end:

1. 600-frame no-correction baseline;
2. correction depths 1, 6, and 11 through the production correction path;
3. at least 600 consecutive forced depth-7 corrections at every required
   correction location;
4. exit/re-entry and multi-round cases.

Require after every correction: exact canonical convergence, exact presentation
event reconciliation, no audio/camera/particle/journal leakage, no stale native
identity, bounded preallocated memory, no allocation/lifecycle growth, and exact
undo on injected qualification failures. Require correction-cycle p99 below
16.67 ms, capture p99 at most 0.5 ms, no capture above 1 ms, and resumed replay
at least 58 ticks/second.

Networking remains blocked until this complete matrix is green.

## Phase 6 — Finish the real rollback coordinator and Steam transport

1. Pin and review an immutable `FottenSC/GekkoNet` fork commit containing the
   required save/load/confirmed-frame APIs. Remove runtime patch application.
2. Complete the real simulation/online coordinator and first pass it through an
   in-memory transport pair using production state transitions.
3. Support exactly two explicitly enabled casual Player Match peers. Before
   ownership, agree on executable, HorseMod build, schema/protocol, Steam lobby
   identities, slots, content, input delay, rollback window, and frozen baseline.
4. Use authenticated production Steam P2P only: reliable bootstrap/control,
   unreliable gameplay input on a dedicated Horse channel, ephemeral peer keys,
   and confirmed session keys.
5. Exchange confirmed canonical hashes every 30 frames and at round boundaries.
   Any confirmed mismatch is terminal and emits bounded diagnostics.
6. Before ownership, mismatch/timeout leaves stock behavior untouched. After
   ownership, transport, authentication, lifecycle, restore, hash, or peer
   failure terminates rollback and returns to the lobby; never resume stock
   simulation mid-match.
7. Opaque local images and native pointers never enter messages, peer hashes, or
   protocol state.

Do not add direct UDP, loopback fallback, shared-memory gameplay transport,
sidecars, route switching, a standalone launcher, website launch flow, webserver,
or legacy rollback compatibility.

## Phase 7 — Sandboxie/Steam paired qualification

Keep Sandboxie and network impairment outside production HorseMod. Extend the
existing external runner to prove:

- one normal Steam SC6 host and one named-box Sandboxie SC6 client;
- distinct process IDs, box identity, Steam identities, writable roots, logs,
  traces, and reports;
- identical executable, DLL, configuration, schema, runner, manifest, source,
  and recursive submodule identities;
- gameplay messages still use production authenticated Steam P2P.

Run clean, latency, jitter, loss, burst loss, reorder, duplicate, corruption,
disconnect, full-match, multi-round, lobby return/re-entry, one-hour repeated
lobby/match cycling, and one-hour continuous-session cases. The final clean run
uses a fresh Sandboxie box and must contain real prediction corrections, multiple
rounds, exact confirmed hashes, zero canonical divergence, and clean process,
module, hook, journal, coordinator, and transport teardown.

## Phase 8 — Allowlisting and release audit

1. Keep the allowlist empty until each content case passes offline and paired
   Steam/Sandboxie qualification on the exact release build.
2. Reduce the supported public configuration to:

   ```ini
   config_version=1
   enabled=false
   rollback_window=12
   input_delay=1
   trace=false
   ```

3. Remove qualification-only flags and instrumentation from public configuration
   or keep them inaccessible to production activation as required by the goal.
4. Audit function/file size, lifecycle ownership, typed failures, schema
   generation, bounded queues/stores, ignored artifacts, legacy-path absence, and
   reverse-order hook teardown.
5. Bind every release report to source and recursive submodules, executable,
   DLL, configuration, generated schema, runner, replay/content, case manifest,
   normal-render traces, performance percentiles, and canonical hashes.
6. Expected fail-closed reports remain separate and cannot make a functional gate
   green.

## Build and verification baseline

Use the existing build tree:

```powershell
cmd /d /s /c "call E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat >NUL && cmake -S E:\myMods -B E:\myMods\build_cmake_LessEqual421__Shipping__Win64"

cmd /d /s /c "call E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat >NUL && cmake --build E:\myMods\build_cmake_LessEqual421__Shipping__Win64 --target HorseMod ReplayQualificationMod DeterministicCoreSelfTest NativeCandidateRegionsSelfTest DeterministicSchema"

ctest --test-dir E:\myMods\build_cmake_LessEqual421__Shipping__Win64 --output-on-failure
python -m pytest tools\deterministic_qualification\tests -q
```

Reconfigure after a commit when embedded source stamps must change. Build and
deploy both `HorseMod` and `ReplayQualificationMod` before exact-source runtime
runs. Use the live log at:

`E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\UE4SS.log`

At the current tip, the last verified local suites passed 3/3 CTest and 16/16
Python tests. These are structural checks, not runtime acceptance.

## Evidence and stopping rules

Maintain a matrix for every goal criterion and case with one of:
`not started`, `implemented/unproven`, `failed`, or `passed`. Record exact source,
artifact hashes, workload, renderer, correction location, counts, percentiles,
canonical hashes, presentation counters, memory high-water marks, and lifecycle
result. Never promote old evidence to a changed build.

If native knowledge is missing, use Ghidra MCP, improve relevant names/types/
comments, save the verified contract, implement it, and continue. If a test or
runtime gate fails, diagnose and fix the responsible contract. Only pause for a
genuine external-authority blocker after exhausting safe alternatives and state
the single exact user action required.

The assignment finishes only when strict replay seek/resume and qualified
multi-round Sandboxie/Steam rollback both pass on the same reviewed system with
real corrections, clean teardown/re-entry, and zero confirmed canonical
divergence.

## Ready-to-send prompt

```text
Take over and finish HorseMod's deterministic simulation rewrite in E:\myMods.

Read E:\myMods\AGENTS.md,
E:\myMods\docs\deterministic-simulation-goal.md,
E:\myMods\docs\investigations\deterministic-simulation-takeover-handoff-2026-08-24.md,
and E:\myMods\docs\investigations\deterministic-simulation-outstanding-plan-2026-08-25.md
completely. Fetch origin/codex/rollback-rewrite and continue from tip 5d36624a.

This is a continuous implementation assignment, not a request for another plan
or status report. Start with Phase 1's battle-audio selector omission and keep
working through every phase until the full goal passes with current-build
evidence. Preserve the seven dirty moveset-parser files listed in the plan and
never stage, discard, stash, clean, overwrite, or commit them. Reuse the existing
build/evidence directories because drive space is limited.

Use Ghidra MCP whenever native ownership, writers/readers, types, lifecycle, or
restore order are not proven. Save and document verified contracts. Do not weaken
the ordered audio identity gate: it currently exposes a real missing selector
counter at battle-sound handler +0x3E0. Keep rollback disabled and the allowlist
empty until qualification passes.

The final paired gate is one normal Steam SC6 host and one Sandboxie-isolated SC6
client with distinct Steam identities using production authenticated Steam P2P.
Sandboxie and impairment remain external tooling. Do not add a launcher EXE,
website launch flow, webserver, direct UDP, sidecar, fallback transport, shared
memory gameplay path, or legacy rollback system.

Only finish after strict normal-render replay seek/resume and qualified
multi-round Sandboxie/Steam rollback pass with real corrections, exact
presentation reconciliation, bounded memory, clean teardown/re-entry, and zero
confirmed canonical divergence.
```
