# Deterministic Simulation Rewrite Goal

## Goal

Replace HorseMod's retired Rollback and ReplayScrub experiments with one
production-quality deterministic simulation system shared by replay seeking and
online rollback.

The replacement must be based on verified Soulcalibur VI native behavior, own a
single authoritative simulation timeline, and fail closed whenever its native
contract, content qualification, transport agreement, or restored state cannot
be proven valid.

This work is not complete when the code compiles, a synthetic test passes, or a
rollback-looking effect appears on screen. It is complete only when normal-render
replay seek/resume is deterministic and real Steam P2P rollback completes
qualified multi-round matches between two physical PCs with corrections, clean
teardown, and zero canonical divergence.

## Product outcome

The finished HorseMod provides:

- Replay capture and seeking through one authoritative input and checkpoint
  timeline, including backward, repeated, failed, and cross-round seeks.
- Correct resumed replay playback after a seek, with speculative presentation
  suppressed during resimulation and persistent presentation reconciled once.
- Bilaterally enabled rollback for exactly two players in casual Player Match,
  using Steam P2P only.
- Runtime activation only for character, stage, executable, schema, build, and
  baseline combinations supported by qualification evidence.
- Terminal fail-closed behavior after HorseMod takes simulation ownership. Stock
  simulation is never resumed in the middle of an owned match.
- No standalone HorseMod launcher executable or web-based game-launch flow.
  Qualification tooling may start the normal Steam game process, but production
  functionality lives in the injected mod and uses value-only files/messages at
  the tooling boundary.

## Scope boundaries

In scope:

- The currently supported Soulcalibur VI executable.
- Normal-render replay capture, seek, resimulation, resume, and cleanup.
- Two-member casual Player Match over authenticated Steam P2P.
- A qualification-driven content allowlist that is empty until evidence passes.
- Test-only runners and temporary qualification adapters that cannot enter the
  production activation path.

Out of scope:

- Ranked play, spectators, more than two players, and unqualified content.
- Direct UDP, sidecars, route switching, or alternate fallback transports.
- Menu automation, session creation, invite injection, or ownership of general
  game flow in production HorseMod.
- Compatibility with old rollback profiles, command-line activation flags,
  report schemas, patch stacks, and experimental interfaces.

## Completion criteria

All criteria below are mandatory. A partial milestone does not satisfy the goal.

### 1. Retired implementation is absent from production

- The old Rollback and ReplayScrub implementations, lab/controller/P2P harnesses,
  runtime Gekko patch machinery, obsolete profiles, generated qualification
  machinery, and legacy activation paths are removed from the active product.
- The old standalone launcher executable and website-based launch path are not
  required or shipped.
- Investigation records and Ghidra-derived facts remain available as historical
  evidence.
- Generated reports, traces, audit exports, and large runtime artifacts are
  ignored and cannot silently return to production source control.

### 2. Native simulation contract is closed

Every state region admitted to capture or restoration has evidence that records:

- Native owner, resolver/address, type and bounded size.
- Complete known writers and readers relevant to simulation.
- Lifetime, generation boundary, identity checks, and pointer validation rules.
- Capture phase, ordered restore phase, derived-state repair, and verification.
- Classification as canonical gameplay, derived state, local diagnostics,
  persistent presentation, or ephemeral presentation.
- Supporting Ghidra function, variable, type, and structure names and comments.

No production adapter contains unknown writers, unresolved ownership, or raw heap
pointers treated as restorable values. Relevant discoveries are saved in Ghidra
and transferred to reusable SC6 documentation.

### 3. One deterministic engine owns both features

- Normal `.hpp/.cpp` modules implement explicit contracts for game-state
  adaptation, input timelines, snapshot storage, presentation journaling, and
  rollback transport.
- Simulation, online, and replay lifecycles use typed states and typed events.
  Illegal transitions fail before mutation.
- The game thread exclusively owns native state, Gekko, simulation advancement,
  snapshots, and presentation journals. Worker threads exchange only bounded,
  value-only messages through preallocated queues.
- Restore is transactional: full preflight, undo capture, ordered writes, derived
  repair, canonical recapture/compare, and exact undo on every post-write failure.
- Snapshot storage is bounded and generation-aware; invalidating a native
  generation is atomic.
- Hooks have one lifecycle owner, install atomically, and tear down completely in
  reverse order.
- Failure handling uses typed codes with one string mapping.
- C++ contains the canonical state/protocol schema and generates tool-readable
  metadata. Python does not duplicate schema or version constants.

### 4. Replay qualification passes

- Normal replay playback records exact authoritative inputs and canonical
  checkpoints every 30 confirmed frames and at every round/native-generation
  boundary.
- Seek restores the nearest valid checkpoint at or before the target and
  resimulates no more than 29 frames.
- No checkpoint is restored across incompatible fighter, round, stage, or native
  heap-object generations.
- During resimulation, ephemeral presentation is suppressed. At the landing frame,
  persistent presentation is reconciled once and normal presentation resumes.
- Replay and online ownership are mutually exclusive.
- Timeline capture stops cleanly at the default 512 MiB limit and leaves a usable
  partial timeline.
- Backward, repeated, failed-cleanup, restart, and cross-round seeks pass.
- Seeks at 10%, 25%, 50%, and 75% validate within 0.5 seconds and then resume for
  600 normal-render frames with zero canonical mismatch.

### 5. Offline deterministic proof passes before networking

Using one immutable newly built DLL, each initial candidate is tested:

- Astaroth versus Raphael, stage 273/map 17.
- Raphael versus Maxi, stage 9/map 9.
- Siegfried versus Cervantes, stage 23/map 23.

Each case runs a 600-frame normal-render baseline plus correction depths 1, 6,
and 11 near round start, active combat, hit/presentation activity, and round end.
Acceptance requires:

- Every confirmed canonical frame equals its no-correction baseline.
- Final persistent presentation state matches the baseline.
- Every ephemeral event commits exactly once.
- No stale native identity or allocation survives restoration.
- Replay seek/resume satisfies criterion 4 on the same build.

Networking implementation and qualification remain blocked until this gate is
green. Old goldens may supply workloads but are never trusted expected results.

### 6. Online rollback contract passes

- Both peers explicitly enable the same profile and agree before ownership on
  executable identity, HorseMod build, protocol/schema, Steam lobby identities,
  player slots, content selection, input delay, rollback window, and frozen
  baseline hash.
- Bootstrap/control messages use reliable Steam delivery; gameplay inputs use
  unreliable Steam delivery on a dedicated authenticated Horse channel with
  ephemeral peer key agreement and confirmed session keys.
- Confirmed canonical hashes are exchanged every 30 frames and at round
  boundaries. Any mismatch is terminal and emits a bounded diagnostic report.
- Before ownership, timeout or mismatch leaves stock behavior untouched. After
  ownership, any lifecycle, transport, authentication, hash, restore, or peer
  failure terminates rollback and returns to the lobby without mid-match fallback.
- GekkoNet is a pinned, reviewed immutable fork commit with the required
  save/load/confirmed-frame APIs. Runtime patch application is absent.

### 7. Online qualification passes in order

The same real coordinator and pinned Gekko fork pass:

1. An in-memory transport pair.
2. Two local SC6 processes under clean, latency, jitter, loss, burst loss,
   reorder, duplicate, corruption, and disconnect cases.
3. Full-match and multi-round re-entry for every allowlisted content case.
4. One-hour same-process lobby/match cycling and one-hour continuous-session
   soaks.
5. Two physical PCs on separate consumer connections using identical DLL,
   configuration, manifest, runner, schema, and source commits.

The physical-PC run must contain real prediction corrections, multiple rounds,
successful lobby return/re-entry, clean process/module cleanup, and zero confirmed
canonical divergence.

### 8. Test and engineering quality gates pass

- Unit tests exercise the real simulation and online coordinators through fake
  adapters/transports rather than duplicate policy helpers.
- Lifecycle coverage includes enter, active use, correction, pre-ownership
  failure, post-ownership failure, exit, re-entry, round change, scene change,
  disconnect, and module cleanup.
- Transactional restore is faulted at every preflight, write, repair, and
  verification phase and proves either zero mutation or exact undo.
- Generation invalidation, bounded-store exhaustion, input replacement,
  presentation discard/commit, and duplicate/reordered/stale packets are covered.
- Production modules are not implementation-heavy header-only code.
- No function exceeds 150 lines and no source file exceeds 1,500 lines without a
  documented reviewed exception.
- Stateful data is grouped by lifecycle phase and subsystem rather than stored as
  an unstructured flag collection.
- Each milestone receives an adversarial review against primary code, Ghidra
  evidence, traces, and reports before the next milestone is accepted.

### 9. Configuration and observable behavior are final

The only supported public configuration is:

```ini
config_version=1
enabled=false
rollback_window=12
input_delay=1
trace=false
```

Transport and lifecycle modes are implicit. Old profiles and command-line
rollback flags are ignored with one clear diagnostic. UI is read-only and exposes
only lifecycle state, peer/build agreement, active content contract, confirmed
frame, rollback count, and terminal failure.

### 10. Release evidence is reproducible

Every acceptance report binds:

- Superproject source commit and recursive submodule commits.
- DLL, configuration, generated schema, and runner SHA-256 values.
- Executable/content identity and exact case manifest.
- Normal-render traces and bounded diagnostic artifacts.

Expected fail-closed cases are reported separately and can never make a
functional gate green. A content case enters the runtime allowlist only after its
offline, local two-client, and physical qualification evidence all pass.

## Milestone gates

Progress is evaluated in this order:

1. Historical preservation and removal of production legacy paths.
2. Closed native contract and reviewed deterministic architecture.
3. Transactional deterministic core and structural tests.
4. Production replay capture, seek, resume, and offline correction proof.
5. Authenticated Steam transport and online coordinator.
6. Local impairment, re-entry, and soak qualification.
7. Physical two-PC qualification and evidence-bound allowlisting.

A later milestone cannot waive a failed earlier gate. Discovery of an incomplete
native contract sends the affected adapter back to reverse engineering rather
than being papered over with a compatibility or fallback path.

## Definition of done

The project goal may be marked complete only when all ten completion criteria are
green and the final evidence demonstrates both of these outcomes on the same
reviewed system:

1. Replay seek/resume passes strict normal-render qualification, including
   cross-round and repeated seeks, within the validation budget and with no
   canonical mismatch.
2. Authenticated Steam rollback completes qualified multi-round matches on two
   physical PCs over separate consumer connections, with real corrections, clean
   teardown/re-entry, exact confirmed canonical hashes, and no fallback to stock
   simulation after ownership.

Until both outcomes exist, the goal remains active regardless of implementation
progress, synthetic tests, or local demonstrations.
