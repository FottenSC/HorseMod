# Deterministic Simulation Rewrite Takeover Handoff

Date: 2026-08-24  
Workspace: `E:\myMods`  
Active branch: `codex/rollback-rewrite`  
Takeover base: `50d0382658187f96b7d74be22d591ac08c49bb27`

## Mission and stopping rule

Finish the complete deterministic-simulation rewrite defined in
[`docs/deterministic-simulation-goal.md`](../deterministic-simulation-goal.md).
The final paired-machine gate has deliberately been replaced with a normal Steam
SC6 host plus a Sandboxie-isolated Steam SC6 client. Do not reinstate the physical
two-PC requirement.

This is one continuous implementation assignment. A passing build, a unit-test
milestone, a replay capture, a reverse-engineering discovery, or an offline demo
is a checkpoint from which to continue; none is permission to end the task. Keep
the goal active across turns and continue until every completion criterion in the
goal document is green and backed by current-build evidence. Do not post a final
"still incomplete" inventory and stop. When a test fails, diagnose and fix it.
When the native contract is incomplete, use Ghidra MCP, document the subsystem,
implement it, and resume qualification.

Only an actual external-authority blocker may interrupt forward progress, such as
missing Steam credentials/identity access, a required user-controlled interactive
action that cannot safely be automated, or unavailable infrastructure after all
safe alternatives are exhausted. If that happens, report one exact action needed
from the user, preserve the active state, and continue every independent workstream.
Do not treat difficulty, elapsed time, a context boundary, missing Hermes service,
or a failed test as blockers.

## Source-control and workspace safety

- `origin/codex/rollback-rewrite` currently resolves to
  `50d0382658187f96b7d74be22d591ac08c49bb27`.
- The remotely retrievable superproject safety snapshot is
  `origin/codex/pre-rollback-rewrite-snapshot-20260823` at
  `37bf49bbf223882193a70a62e19b9857f7b9410e`.
- `tools/BlueprintToCpp` is pinned at
  `6b3fd8a175f1e164eb1bd1be6f766f48b980f7c0`, on its matching remote safety
  branch `codex/pre-rollback-rewrite-snapshot-20260823`.
- The safety snapshot stores the 145,340,404-byte
  `artifacts/frame-meter-static-v1/frame-meter-static-v1.json` losslessly as
  `frame-meter-static-v1.json.gz`. Restoration and hashes are recorded in the
  safety branch's `artifacts/frame-meter-static-v1/SAFETY_SNAPSHOT.md`. The
  original SHA-256 is
  `CF67BB6E281060539FB908158023D05DA62AD70B47B7A062DD2185C1BF59EE0F`.
- Do not create more repository copies. Use the existing build directory and
  bounded generated artifacts.

The working tree intentionally contains unrelated user moveset-parser work. Do
not modify, stage, discard, clean, stash, or commit these paths as part of the
deterministic rewrite:

```text
tools/moveset_parser/hgmotion_reference.py
tools/moveset_parser/luxformats.py
tools/moveset_parser/motion_decode.py
tools/moveset_parser/tests/test_hgmotion_reference.py
tools/moveset_parser/tests/test_motion_decode.py
tools/moveset_parser/tests/test_uassetparse.py
tools/moveset_parser/uassetparse.py
```

Before every commit, inspect `git diff --cached` and stage only explicit rewrite
paths. Reconfigure after each commit before producing evidence so the DLL embeds
the exact source commit. Push accepted milestones to `origin/codex/rollback-rewrite`.

## What has actually been completed

### Preservation and retirement

The failed Rollback/ReplayScrub implementation was preserved remotely and removed
from the active product beginning at commit `7476c6d6`. The old giant rollback
runtime, ReplayScrub, lab/P2P/controller harnesses, UDP and route-switching paths,
Gekko patch stack, obsolete profiles, generated qualification machinery, and old
large acceptance runner are historical evidence only. Do not restore them as the
new implementation. Investigation documents remain available for facts that are
independently rechecked against the current executable.

The replacement is organized as ordinary `.hpp/.cpp` deterministic modules with
typed status/failure values, bounded stores/timelines, replay/simulation/online
coordinators, candidate native adapters, generated schema metadata, and modular
qualification tooling. Production rollback remains deliberately disabled and the
content allowlist remains empty.

### Current deterministic evidence

The most recent commit, `50d03826`, advances the candidate checkpoint format to
schema 5 and adds the exact native input boundary:

- global Lux frame counter;
- InputLog and manager round/time cursors;
- round-state frame and unpause countdown;
- repeat/pending move state;
- previous input words and current/prior post-filter input pairs;
- the InputLog scalar bank at `+0x390..+0x3BF`;
- all 1,024 semantic InputLog cache rows at `+0x3C0`, preserving each row's three
  reserved bytes instead of canonicalizing or overwriting them.

Binding validates the InputLog pointer/class, all three manager-owned input array
identities, and the active two-player count. Canonical serialization contains
values only—no pointer bytes or padding. Preflight rejects clock disagreement,
invalid player counts, and non-Boolean cache fill flags. Transactional native
candidate tests cover semantic restore, reserved-byte preservation, failure, and
undo.

Earlier current-rewrite evidence closed or implemented important pieces of the
outer batch/input observation contract, FP caller restoration, UCRT observation,
callback topology, stage break/listener state, particle value qualification,
stage wind topology, transactional wind graph rebuild, and candidate checkpoint
capture. The authoritative details and remaining blockers are in:

- [`deterministic-native-contract-results-2026-08-23.md`](deterministic-native-contract-results-2026-08-23.md)
- [`deterministic-simulation-contract-2026-08-23.md`](deterministic-simulation-contract-2026-08-23.md)
- [`deterministic-online-coordinator-2026-08-24.md`](deterministic-online-coordinator-2026-08-24.md)
- [`deterministic-native-contract-agent-plan-2026-08-23.md`](deterministic-native-contract-agent-plan-2026-08-23.md)

`Sc6ReplayRuntime` currently captures authoritative post-filter inputs, exact
native outer-batch envelopes, batch-entry checkpoints, and landing checkpoints.
`PlanSeek` is batch-aware. It does **not** yet execute a live restore/resimulation
or resume. `CandidateGameStateAdapter` has transactional restore and verification,
but the live SC6 binding does not yet supply the complete advance, derived rebuild,
and presentation reconciliation callbacks. `Schema::production_regions` is still
empty; keep it empty until the enclosing contract and qualification gates pass.

### Build and test state at handoff

The latest build and deployed DLL are exact-source commit `50d03826`:

```text
Build DLL:  E:\myMods\build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll
Deploy DLL: E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\dlls\main.dll
SHA-256:    0AE439A75F840BCE7F44E410F4111A32401D9EB3A8A5C20E5E4D3B13C6648D15
```

The build graph embeds the full `50d03826...` source ID in both HorseMod and the
ReplayQualificationMod. Before this handoff, these tests passed against the same
source changes:

- `DeterministicCoreSelfTest`
- `NativeCandidateRegionsSelfTest`
- `OnlineCoordinatorSelfTest`
- 14 Python deterministic-qualification tests

Re-run them before further mutation. The schema-5 DLL has **not** yet completed a
new 600-frame normal-render replay-entry run. The last successful live checkpoint
proof was schema 4 at `fd7a7710`; it captured 21 landing checkpoints and 35
batch-entry checkpoints through approximately frame 600, and observed stage-wind
growth from two to four nodes without capture, identity, checksum, or capacity
failure. That is useful regression evidence, not schema-5 certification.

## Native execution invariants already established

- The authoritative outer transaction is
  `LuxBattleManager_Tick_SimulationLoop_UpdateInputAndRoundState @ 0x1403FE520`.
  Do not call `LuxBattle_PerFrameTick` directly; it omits the manager transaction
  tail.
- Input callback execution is
  `ProcessAndCompactCallbackEntries @ 0x141D38300`.
- The post-production filter is
  `FilterALuxBattleMoveDispatchInputPairByFrameSlot @ 0x140427940`.
- `FLuxBattleInputPair` is exactly eight bytes: current/held plus rising edges.
  `FLuxTArrayHeader` is exactly 16 bytes.
- Online authoritative input must be published before the `+0x1210` filter
  callback, exactly once; the native filter then runs exactly once and simulation
  consumes its post-filter result. Do not force the pre-filter pair to equal the
  final pair.
- A seek target inside a native outer batch cannot jump into a reconstructed
  stack/register continuation. Restore the batch-entry checkpoint, replay the
  complete recorded batch with presentation suppression through the landing
  coordinate, capture the landing image, finish the batch, and restore the landing
  image for the paused view. Resume by restoring that batch entry and replaying the
  enclosing batch again, suppressing through the landing offset and publishing
  only later coordinates. An exact batch-entry target resumes with the following
  batch.
- Every checkpoint and timeline record is generation-scoped. Never restore across
  fighter, round, stage, manager, InputLog, or heap-object generation changes.
- Raw UObject/native heap pointers are validation identities only. They never enter
  canonical bytes, peer messages, or restorable values.
- The game thread alone owns native capture/restore, advancement, Gekko, and
  presentation reconciliation. Worker threads exchange bounded value-only
  messages.

## Required execution plan

Work through this order without treating a phase boundary as task completion.

### Phase 0: Re-establish the exact current baseline

1. Confirm branch, remote commit, recursive submodules, and the protected dirty
   paths above.
2. Reconfigure, build, run CTest and Python tests, deploy, and run the schema-5
   normal-render replay-entry workload for 600 frames.
3. Bind the report to commit, recursive submodules, DLL/config/schema/runner
   hashes, replay/content identity, and normal-render trace. Because unrelated
   user edits keep the tree dirty, `--allow-dirty` evidence is non-certifying; do
   not mistake it for a release gate.
4. Diagnose and fix any schema-5 capture regression before adding more state.

### Phase 1: Close the remaining native contract

Use Ghidra MCP before implementing each unknown adapter. Complete writer/reader,
lifetime/generation, validation, capture, restore order, derived repair,
verification, and presentation classification for:

1. fighter/HgCpu/MoveVM/command-player supplemental object graph;
2. hit, pending-hit, reaction, and body-collision state;
3. camera action state and historical camera input;
4. round-transition dynamic queues and allocation generations;
5. persistent and ephemeral presentation lanes needed by seek and rollback;
6. any RNG or allocation boundary reached by the exact outer traversal that is
   not already closed.

Use only the existing `SoulcaliburVI.exe` Ghidra program through ghidra-mcp. Never
import a second copy, edit `.gpr` files, use database-editing scripts, or use broken
snapshot endpoints. Opportunistically correct directly relevant function names,
prototypes, variables, types, structs, and comments. Perform structural edits
before comments because prototypes erase plate comments. Run completeness checks,
save the Ghidra program after each verified subsystem, update the native-contract
ledger, and transfer reusable facts to `E:\DevShitPosts\SC6Mods\SC6ModdingDocs`
under that repository's instructions.

Do not admit a region with unknown writers, unresolved ownership, unchecked
capacity, or pointer resurrection. Add typed, transactional candidate coverage
first; populate `Schema::production_regions` only after the complete enclosing
contract and independent review pass.

### Phase 2: Make replay seek/resume real

1. Implement the live SC6 `advance`, derived-cache rebuild, and presentation
   reconciliation bridge at the exact outer-batch boundary.
2. Connect an operator/UI seek request to the typed replay lifecycle. Keep replay
   and online ownership mutually exclusive.
3. Implement batch-aware restore/resimulation/paused landing and correct resume
   exactly as described above, including exact batch-entry targets.
4. Suppress ephemeral presentation during resimulation, discard rolled-back
   speculative events, commit confirmed events exactly once, and reconcile
   persistent visual state once at landing.
5. Make every failure path transactional and lifecycle-complete: failed seek,
   replay exit/re-entry, round change, scene change, module unload, restart, and
   memory-bound exhaustion. The 512 MiB limit must stop capture cleanly while
   preserving a usable partial timeline.
6. Build a new modular strict replay runner. Do not restore the deleted 2,920-line
   `replay_seek_test_run.py` or the giant old acceptance runner. Keep process
   control, trace parsing, case manifests, evidence validation, and reporting in
   separate modules.
7. With the normal renderer, validate seeks at 10%, 25%, 50%, and 75%, each
   landing within 0.5 seconds and resuming for 600 frames with zero canonical
   mismatch. Also pass backward, repeated, cross-round, failed-cleanup, replay
   restart, and capture-limit tests.

### Phase 3: Mandatory offline correction proof

Networking stays blocked until one immutable new DLL passes all of this:

```text
Astaroth vs Raphael  stage 273 / map 17
Raphael vs Maxi      stage   9 / map  9
Siegfried vs Cervantes stage 23 / map 23
```

For every case, generate a fresh 600-frame normal-render no-correction baseline
and forced correction depths 1, 6, and 11 near round start, active combat,
hit/presentation activity, and round end. Old goldens are workloads only. Every
confirmed canonical frame must equal the new baseline; final persistent state
must match; ephemeral events must commit once; no stale identity/allocation may
survive; and the replay qualification from Phase 2 must pass on the same DLL.

### Phase 4: Implement authenticated Steam rollback

1. Pin a reviewed immutable `FottenSC/GekkoNet` fork commit containing the required
   save/load/confirmed-frame APIs. Remove all runtime patch application. The
   current CMake integration is only a disabled scaffold, not qualification.
2. Finish the real online coordinator and Steam transport for exactly two members
   in casual Player Match. Observe stock lobby/selection; do not automate menus,
   create sessions, inject invites, or own general game flow.
3. Require bilateral enablement and agreement on executable, HorseMod build,
   schema/protocol, Steam lobby identities, slots, content, input delay, rollback
   window, and frozen baseline hash before ownership.
4. Use reliable Steam delivery for authenticated bootstrap/control and unreliable
   Steam delivery for gameplay inputs on a dedicated Horse channel. Establish
   ephemeral peer keys and confirmed session keys. Stop without tearing down
   SC6's shared Steam session.
5. Before ownership, mismatch/timeout leaves stock behavior untouched. After
   ownership, lifecycle, transport, authentication, hash, restore, or peer failure
   is terminal and returns to lobby; never resume stock simulation mid-match.
6. Exchange confirmed canonical hashes every 30 frames and at round boundaries.
   A mismatch is terminal and emits a bounded evidence report.
7. Pass the in-memory transport pair using the real coordinator and pinned Gekko
   before starting process-pair qualification.

No direct UDP, sidecars, route switching, fallback transport, production fault
injection, or Sandboxie-specific code may enter HorseMod.

### Phase 5: Sandboxie paired qualification

Sandboxie replaces the original physical two-PC gate. It does not replace Steam
P2P or allow the runner to carry gameplay data.

Extend the modular runner already containing `sandboxie_pair.py` so it starts and
validates:

- one normal Steam SC6 host;
- one client in a named Sandboxie box;
- distinct process IDs and box identity;
- distinct Steam peer identities;
- distinct isolated Steam/UE4SS writable roots, logs, traces, and reports;
- identical game executable, DLL, config, schema, runner, manifest, source commit,
  and recursive submodule commits.

Run clean, latency, jitter, loss, burst loss, reorder, duplicate, corruption, and
disconnect cases through production Steam P2P. Then pass full matches and
multi-round lobby return/re-entry for each candidate, a one-hour lobby/match cycle
soak, a one-hour continuous-session soak, and a clean release run with a fresh
Sandboxie box. The release evidence must contain real corrections, multiple
rounds, exact confirmed hashes, zero canonical divergence, and clean process,
module, transport, and hook teardown.

Shared memory, loopback substitution, direct UDP, or runner-side gameplay message
forwarding cannot satisfy this gate. Test-only impairment belongs outside
production HorseMod and must be visibly excluded from release configuration.

### Phase 6: Allowlisting and release audit

The initial production allowlist stays empty. Add a content case only after its
offline, local-pair, and Sandboxie evidence all pass. Finalize the single public
configuration:

```ini
config_version=1
enabled=false
rollback_window=12
input_delay=1
trace=false
```

Old profiles and command-line rollback flags are unsupported and produce one
clear diagnostic. The UI is read-only and exposes lifecycle, peer/build agreement,
active content contract, confirmed frame, rollback count, and terminal failure.
There is no standalone launcher executable and no website game-launch flow.
Qualification scripts may start processes, but do not replace the removed product
launcher with another shipped EXE or an unnecessary webserver.

Audit every production file for the engineering constraints in the goal document,
run all structural and runtime gates, and bind each release report to source,
recursive submodules, DLL/config/schema/runner hashes, executable/content identity,
case manifest, and normal-render traces. Expected fail-closed cases are reported
separately and never make a functional gate green.

## Build, deploy, and initial runtime commands

Run from `E:\myMods` in PowerShell:

```powershell
cmd /d /s /c "call E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat >NUL && cmake -S E:\myMods -B E:\myMods\build_cmake_LessEqual421__Shipping__Win64"

cmd /d /s /c "call E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat >NUL && cmake --build E:\myMods\build_cmake_LessEqual421__Shipping__Win64 --config Shipping --target HorseMod ReplayQualificationMod DeterministicSchema -j 4"

ctest --test-dir E:\myMods\build_cmake_LessEqual421__Shipping__Win64 -C Shipping --output-on-failure

python -m pytest tools\deterministic_qualification\tests -q

Copy-Item E:\myMods\build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\HorseMod\dlls\main.dll -Force

python tools\deterministic_qualification.py replay-entry --allow-dirty --replay E:\myMods\ReplayExample\REPLAY_12744704008398858106.bin --watch-frames 600 --timeout 240
```

Use
`E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\UE4SS.log`
for the live UE4SS/HorseMod log. `lux-no-render` is diagnostic only; all accepted
replay, golden, rollback, and release evidence uses the normal renderer.

## Review and progress discipline

After each milestone, request the required Hermes adversarial review against
primary code, Ghidra evidence, traces, and reports. The local Hermes CLI previously
reported `No inference provider configured`. If that remains true, record Hermes
as unavailable, perform and document an adversarial primary-evidence review
yourself, and continue; missing Hermes service is not a reason to stop the goal.

Maintain a concise evidence matrix in this document or a linked report with each
goal criterion marked `not started`, `implemented/unproven`, `failed`, or `passed`,
plus the exact commit and artifact hashes. Never upgrade a status based only on
compilation, synthetic tests, an old DLL, stale goldens, or expected fail-closed
results.

The final response is allowed only after all ten goal criteria pass. It must link
the replay and Sandboxie release reports and state the exact source/submodule,
DLL, config, schema, runner, executable, and content identities. Until then,
continue the implementation rather than summarizing incompleteness as an endpoint.

## Ready-to-send takeover prompt

```text
Take over and finish HorseMod's deterministic simulation rewrite in E:\myMods.
Start by reading E:\myMods\AGENTS.md,
E:\myMods\docs\deterministic-simulation-goal.md, and
E:\myMods\docs\investigations\deterministic-simulation-takeover-handoff-2026-08-24.md
in full. Fetch and continue from the current tip of
origin/codex/rollback-rewrite. The implementation base immediately before this
handoff document is 50d0382658187f96b7d74be22d591ac08c49bb27.

This is a continuous implementation assignment, not a request for another plan
or progress report. Do not stop at builds, tests, reverse-engineering findings,
capture milestones, replay-only progress, or offline qualification. Keep the goal
active across turns and continue until every completion criterion in the goal
document passes with current-build evidence. If a test fails, diagnose and fix it.
If native knowledge is missing, use Ghidra MCP, document and save the verified
contract, implement it, and continue. If Hermes is unavailable, document that,
perform a primary-evidence adversarial review, and continue.

The final two-instance qualification uses one normal Steam SC6 host and one
Sandboxie-isolated Steam SC6 client with distinct Steam identities, not two
physical PCs. Both instances must still use production authenticated Steam P2P.
Sandboxie and impairment control stay in external qualification tooling and must
not enter production HorseMod. Do not recreate or ship a standalone launcher EXE,
website launch flow, unnecessary webserver, direct UDP, sidecar, fallback
transport, or old rollback compatibility layer.

Preserve the unrelated dirty moveset-parser files listed in the handoff exactly;
never stage, discard, clean, stash, or commit them. Keep production rollback
disabled and the allowlist empty until all required evidence passes. The first
action is to re-run the exact-source schema-5 baseline and its 600-frame
normal-render replay-entry proof, then proceed through every phase in the handoff.

Only pause for a genuine external-authority blocker after exhausting safe
alternatives. In that case ask for the single exact user action required while
continuing every independent workstream. Do not end with a statement that the
original plan remains incomplete. Your final answer is due only when the complete
definition of done is satisfied and linked evidence proves strict replay
seek/resume plus qualified multi-round Sandboxie/Steam rollback with real
corrections, clean teardown/re-entry, and zero canonical divergence.
```
