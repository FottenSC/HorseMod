# Deterministic simulation rewrite — revised final takeover plan

Date: 2026-08-28

This is an execution plan for the agent taking over implementation. Continue
until the same immutable release DLL passes the complete offline matrix, strict
normal-render replay seek/resume, and authenticated normal-Steam/Sandboxie
paired rollback matrix. A build, unit-test pass, reverse-engineering result,
replay-only milestone, apparently playable match, or zero-correction online
match is not completion.

## 1. Read first and preserve the workspace

Read completely before editing:

1. `E:\myMods\AGENTS.md`
2. `E:\myMods\docs\deterministic-simulation-goal.md`
3. `E:\myMods\docs\investigations\deterministic-simulation-takeover-handoff-2026-08-24.md`
4. `E:\myMods\docs\investigations\deterministic-simulation-outstanding-plan-2026-08-25.md`
5. This file.

Fetch `origin/codex/rollback-rewrite`, verify that its tip contains
`5d36624a` and `b5f063d3`, and continue from the current tip without replacing
the dirty implementation. At revision time `HEAD` is
`c70b60ffbca1ab53b176d0d658f7c487e2d378a9`.

Never stage, discard, stash, clean, overwrite, format, commit, or touch:

- `tools/moveset_parser/hgmotion_reference.py`
- `tools/moveset_parser/luxformats.py`
- `tools/moveset_parser/motion_decode.py`
- `tools/moveset_parser/tests/test_hgmotion_reference.py`
- `tools/moveset_parser/tests/test_motion_decode.py`
- `tools/moveset_parser/tests/test_uassetparse.py`
- `tools/moveset_parser/uassetparse.py`

Untracked deterministic files and evidence are active work. Reuse only
`E:\myMods\build_cmake_LessEqual421__Shipping__Win64` and existing evidence
trees. Do not clone or create another build tree. Check free space before
artifact-heavy runs.

Before every evidence build, generate a bounded source-identity manifest with
superproject `HEAD`, recursive submodule commits, `git status --porcelain=v2`,
SHA-256 and size for every deterministic source/header/CMake/runner/schema input,
a binary patch for tracked deterministic changes, hashes for untracked
deterministic files, compiler/CMake identity, and build command. Exclude the
seven protected files. Dirty-tree evidence is usable only when this identity is
bound to the exact DLL.

## 2. Current operational baseline is not qualification

At revision time no SC6 process is running. Both deployed configs have
`enabled=false` and every diagnostic flag false. Production rollback stays
disabled and production allowlists stay empty until section 13.

| Artifact | SHA-256 |
| --- | --- |
| built, host-deployed, and `sc67` HorseMod DLL | `2A96C1315909CAAB8D23967677EAF41E85470C6CAA25F64B409BB25BD34E91C2` |
| ReplayQualificationMod DLL | `BD1A0BC60BBF89741B59CD073D394C30E49F45107A0AB1F826AA3A2FAEDACCE2` |

There is **zero qualifying evidence** for `2A96…E91C2`. A read-only audit found
93 JSON reports under `docs\investigations\evidence`; none mentions that DLL and
none contains `"certifying": true`. Reports named `current` use older DLLs,
commonly `C92E…E4E`.

Treat every existing JSON as non-certifying regression evidence. It may provide
workloads and expected behavior, but satisfies no current-build or release gate.
Do not claim the current DLL has passed audio identity, stage presentation,
strict seek, matrix, Tira, or authored-winner qualification.

The phase-1 invariant remains mandatory: the typed two-state selector at the
battle-sound handler `+0x3E0`, mutated by
`LuxMove_RemapAttackType_WithCounter @ 0x1403BA080`, must be captured/restored at
its independently verified source-frame boundary. Never weaken ordered audio
payload-ID identity.

## 3. Correct status semantics and protocol evidence

| Status | Exact meaning |
| --- | --- |
| 1 | Request armed; observing locally |
| 2 | Local contract accepted; transport and local `Hello` initiated |
| 3 | Local baseline proposed |
| 4 | Bilateral contract and baseline accepted; prefix catch-up underway |
| 5 | HorseMod deterministic tick ownership active |
| 6 | Terminal failure/lobby return requested; cleanup unproven |
| 7 | Scene-exit cleanup verified and request disarmed |

Status 2 proves neither `Hello` receipt nor `HelloAck`; status 3 proves neither
remote-baseline equality nor acknowledgment. Status 4 is the first bilateral
point. Preserve this mapping or replace it with a typed state and compatible
report mapping.

Add bounded structured events carrying run/session IDs, slot, peer SteamID64,
generation/coordinate where relevant, and result: local contract/Hello sent;
peer Hello received/HelloAck sent; peer HelloAck received; Steam authentication
and session-key completion; local baseline; remote baseline; baseline Ack sent
and received; bilateral activation; prefix start/end; ownership start; round
barrier start/end; each terminal cleanup step; final cleanup verification. The
runner evaluates these in order and never infers agreement from status alone.

## 4. Phase A — genuinely observer-only accessor probe

Do not validate the fixed accessor using `online_request.txt`; that arms takeover
and may reach status 5 despite public `enabled=false`.

Implement a separate trace-only observer probe that:

- invokes only read-only methods of `Sc6OnlineSessionObserver`,
  `SteamLobbyObserver`, and `Sc6BattleSyncObserver`;
- cannot mutate an allowlist, construct/enable a coordinator, start Steam
  transport, send packets, freeze a baseline, configure Gekko, set predicted
  player, suppress presentation, or select inputs;
- uses a separate request/schema and compile-time type unavailable to production
  and takeover code;
- expires after 180 seconds or one observed battle contract;
- emits a bounded report and disarms in `finally`.

Add a fake-backed test that fails if any forbidden subsystem is reached. Do not
implement this as a late boolean inside the armed takeover path.

Run it in the manually proven casual Player Match with normal Steam
Fotten/fotten333 (`76561198070521860`) and sandbox ulvunge1
(`76561198201141039`). Launch the sandbox game executable directly:

```powershell
Start-Process 'C:\Program Files\Sandboxie-Plus\Start.exe' `
  -ArgumentList '/box:sc67', `
    'E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe', `
    '-QueryPort=27012'
```

Verify exactly two SC6 processes, only the sandbox PID has `-QueryPort=27012`,
and `/box:sc67 /listpids` identifies it. The user controls visible lobby and
character selection; do not drive normal character select as replay automation.

Both reports must prove role, virtual session state, online session/name/info
pointers, lobby ID, two distinct members, local slot, fighter codes, authored
stage package/display name, and received-content flags. Use display map names in
all reports and updates, never IDs alone. If this fails, use Ghidra MCP to close
owner/type/writer/reader/lifetime contracts, improve relevant names/types/
comments, save, implement the authoritative fix, and repeat observer-only.

## 5. Phase B — repair ownership, barriers, and cleanup

Complete this phase before any request can reach status 5.

### 5.1 Monotonic ownership

```text
StockBeforeOwnership -> PrefixCatchup -> Owned -> OwnedRoundBarrier
                                           |             |
                                           +-> FailClosed+
```

There is no in-battle edge from `Owned`, `OwnedRoundBarrier`, or `FailClosed`
back to stock. Stock runs only before first ownership or after verified scene
exit and complete cleanup.

Replace the current round-generation path that clears takeover readiness and
returns `Stock` while the coordinator owns simulation. Add a typed
`OwnedRoundBarrier` tick/input source. HorseMod continues the same owned native
outer-tick executor; Gekko supplies paired inputs where transition code consumes
them, and explicit neutral pairs are allowed only where Ghidra/runtime evidence
proves inputs are ignored. Never invoke the original stock selector.

At the first safe new-generation fencepost, peers stop advancing, exchange the
round barrier and new baseline coordinate/hash, reset generation-bound Gekko
state, then resume `Owned`. A late peer remains in owned fail-closed hold. Test
both arrival orders, duplicate/stale barriers, timeout, mismatch, exit, and a
second barrier.

### 5.2 Baseline coordinate and timeouts

Extend reliable control messages:

1. Both send `BaselineReady(generation, earliest_safe_coordinate)` after
   authenticated Hello agreement.
2. Slot 0 selects `target=max(proposals)+120` normal ticks and sends
   `BaselineCommit(generation,target)`.
3. Both remain stock before first ownership and capture post-tick canonical
   state exactly at `target`.
4. Both exchange `Baseline(generation,target,hash)` and acknowledgments.
5. Only identical acknowledged values enter status 4/prefix catch-up; status 5
   is emitted only with the first `NotifyOwnedTick`.

A generation change, missed target, coordinate/hash disagreement is terminal.
Use monotonic deadlines: 10 seconds authenticated Hello, 10 ready/commit, 10 to
target, 10 baseline/Ack, 5 prefix catch-up; round-barrier negotiation gets 10
seconds and remains fail-closed. Cover all with fake-clock tests.

### 5.3 Terminal cleanup state machine

Centralize cleanup as lifecycle-owned state, not more flags in `dllmain.cpp`.

Pre-ownership failure: reject callbacks; stop transport/authentication; reset
Gekko if constructed; clear temporary qualification allowlist and request/run;
leave stock inputs and presentation untouched. Status 7 requires all probes
clear, not merely status 6.

Post-ownership failure: atomically select `FailClosed`; reject gameplay packets;
prevent stock inputs; suppress uncommitted speculative presentation; stop Gekko
advancement; send best-effort authenticated disconnect; request lobby once; wait
for verified scene exit. Only scene-exit cleanup may reset transport/Gekko,
clear predicted player, presentation ownership/journals, checkpoints/timelines/
prefix, temporary allowlist/request, hooks, and session identities. Never resume
a partially owned match.

Add the missing failed-coordinator scene-exit branch. The same cleanup must be
idempotent for normal exit, both failure classes, disconnect, scene change,
module unload, and shutdown. Each resource exposes `IsClearForStock()`; status 7
requires their conjunction. Fault every cleanup step and test re-entry.

Have Hermes adversarially review ownership, barrier advancement, and cleanup
against source, Ghidra contracts, and tests. Resolve findings before live
takeover.

## 6. Phase C — implement production activation

Production currently does not exist: the coordinator uses the qualification
allowlist, `m_online_allowlist` is unused, `Schema::production_regions` is
empty, and `enabled=true` is rejected. This is a full milestone.

- Construct a dedicated production coordinator with an immutable
  evidence-bound production allowlist.
- Keep qualification in separately typed coordinator/request APIs. Production
  builds do not parse qualification files/flags unless the qualification module
  is loaded.
- Public `enabled=true` only arms observation. Ownership still requires exact
  two-member casual Player Match, executable/build/protocol/schema, authenticated
  Steam identities, allowlisted content, and bilateral baseline.
- Keep UI read-only: lifecycle, peer/build agreement, named content, confirmed
  frame, rollback count, terminal failure.

Create a reviewed candidate manifest plus generated `constexpr` representation.
Each content entry binds exact fighter ordering, authored stage code/package/
display name/map identity/RNG policy, executable/protocol/schema/region/source
hashes, and IDs/hashes of qualifying offline and paired reports. No wildcard.

The initial production set is exactly the three goal cases:

1. Astaroth versus Raphael on the authored map of
   `REPLAY_10224262924037108963.bin`;
2. Raphael versus Maxi on the authored map of
   `REPLAY_12744704008398858106.bin`;
3. Siegfried versus Cervantes on the authored map of
   `REPLAY_10655830802443135226.bin`.

Before manifest generation, resolve every authored package to its native display
map name from the content contract/game data and replace the descriptions above
with those names. This is blocking: do not publish or run a case using IDs alone,
and never play a replay on another map. If any case fails, the production
allowlist remains empty; do not silently shrink the goal's three cases.

Promote only native regions with verified owner, address/resolver, type,
writers/readers, lifetime, restore order, class, size, and failure behavior.
Generate `Schema::production_regions` from the reviewed region manifest. Fail
the build for an uncontracted region or missing/mismatched evidence binding.

Qualify startup, disabled behavior, activation, mismatch, pre/post-ownership
failure, round transition, teardown, re-entry, scene change, unload, and shutdown.
Remove superseded experimental/legacy rollback paths. Add no launcher, website,
server, direct UDP, sidecar, shared-memory gameplay, or fallback transport.

## 7. Phase D — certifying offline matrix

Implement a manifest/evaluator with one row per:

```text
matchup × correction location × depth/mode × renderer × exact DLL
```

- Matchups: the exact three fighter/authored-map cases above.
- Locations: near round start; active combat; confirmed hit with nonzero
  presentation; round end with terminal activity.
- Modes: same-build no-correction baseline; depths 11, 1, 6 in fail-fast order; continuous forced
  depth 7. The single correction canary also uses depth 11 and must not retain
  the old depth-1 hard-code.
- Renderer: normal only for certification.
- Artifact: one immutable candidate/release DLL plus matching schema/runner.

The canary ladder is fast tests, the automatic 60-120-frame normal-render
smoke, one depth-11 active-combat correction row, strict seeks, then the full
matrix. Interrupted rows may resume only when the DLL, schema, capture harness,
replay bridge, config contract, replay, executable, native metadata, workload,
and bounded-log evidence hashes are unchanged. Capture-producing code lives in
`offline_capture.py` and has an identity independent of the matrix evaluator,
so a policy-only evaluator repair may re-evaluate immutable raw captures while
any producer change invalidates them. The row/config specification is isolated
in producer-hashed `offline_spec.py`; evaluator code must not be a transitive
source of capture semantics.

This is 3 baseline rows plus 3 × 4 × 4 = 48 correction rows. Each correction
row executes at least 600 consecutive corrections at that location. Zero
required presentation activity or terminal coverage other than `complete`
fails. `lux-no-render` is diagnostic only.

Require exact same-build baseline canonical equality, ordered audio payload IDs,
exactly-once ephemeral events, final persistent presentation, no camera/particle/
wall/barrier/stage leak, no stale identity/allocation, zero capacity failure or
growth, clean exit/re-entry. Depth-7 p99 <16.67 ms; checkpoint capture p99
<=0.5 ms and max <=1 ms.

On the same DLL, strict normal-render seeks at 10/25/50/75 percent must validate
within 0.5 seconds and resume 600 frames at >=58 ticks/s with zero canonical or
presentation mismatch. Compare all simulated round/match winners with authored
metadata and perform a same-replay, same-map vanilla control because prior
HorseMod playback looked wrong.

Tira is an additional RNG gate. Use the supplied Tira replays
`REPLAY\_13510506239876751347.bin` and
`REPLAY\_11775433596982945207.bin`, plus Astral Chaos: Tide of the Damned
`REPLAY\_10919796003596567142.bin`, always on each authored map. At least one
certifying run must execute the random mood/moveset transition (`IF 0x007F`).
Require exact RNG caller/consumption sequence, target, mood/moveset state,
canonical/presentation, authored round winners, and final winner. Unknown RNG
callers or zero Tira transitions leaves the gate red.

## 8. Phase E — paired-online runner before evidence

Extend `tools\deterministic_qualification.py` with:

```powershell
python E:\myMods\tools\deterministic_qualification.py paired-online `
  --case-manifest <absolute-json> --case <name> `
  --host-steamid64 76561198070521860 `
  --client-steamid64 76561198201141039 `
  --sandbox-box sc67 --sandbox-query-port 27012 `
  --impairment-profile clean --impairment-tool <absolute-external-tool> `
  --battle-timeout 300 --phase-timeout 10 --match-timeout 1800 `
  --output-dir <existing-evidence-directory>
```

Runner requirements:

1. Preflight free space, source manifest, all hashes, zero prior SC6 processes,
   Steam identities, distinct writable roots, box, and exact launch command.
2. Deploy only with games closed; require built/host/sandbox DLL hash equality.
3. Record distinct log byte offsets and one parent plus two side run IDs.
4. Write both requests to temp files, flush, and publish with atomic replace only
   after preflight. Include a common not-before timestamp. If either publication
   fails, disarm both before launch.
5. Launch normal Steam SC6 and sandbox game executable directly; never use the
   current helper's sandbox-Steam launch.
6. Enforce phase timeouts and preserve offset-bounded raw logs on pass/failure.
7. Evaluate identity/authentication/transport, ordered protocol, baseline,
   corrections, hashes, presentation, round outcomes, memory/timing, teardown,
   and re-entry. Fail-closed reports never count as functional passes.
8. Unconditional `finally`: stop impairment and remove rules, stop watchers,
   close/kill games, verify zero PIDs, disarm both requests, restore all
   diagnostic flags false, and verify expected production state.

Schema-v2 reports bind source/DLL/schema/config/runner/tool/game hashes, display
map name/package, fighters, both SteamID64s, PIDs, box, command lines, writable
roots, run/log offsets, ordered events, impairment rules/seeds, authenticated
Steam P2P proof, correction/hash/presentation/round/memory metrics, cleanup
probes, and evaluator reasons. Missing fields fail.

Use an external reviewed WinDivert-compatible tool such as Clumsy through an
adapter. Hash it and record exact filters/rates/seeds. Target only the tested SC6
traffic, never substitute gameplay messages, and enumerate afterward to prove
rules removed. Packet/transport logs must prove authenticated Steam P2P remained
the gameplay channel. Implement clean, latency, jitter, independent loss, burst
loss, reorder, duplicate, corruption, and disconnect. Corruption must fail
authentication and return to lobby. Test disconnect before and after ownership.

For release create a unique fresh Sandboxie box with no copied Steam/UE4SS
writable state except required separately authenticated Steam configuration.
Verify roots/logs initially absent, then deploy the unchanged release. Do not
reuse `sc67` or delete an existing box to simulate freshness.

## 9. Phase F — live paired qualification

Only after A–E and reviews pass may a request reach status 5. The user performs
visible lobby/character selection; the runner observes and evaluates.

For every allowlisted content case, run clean plus all impairment profiles with
the numeric identities above. Functional cases require real corrections, exact
confirmed hashes every 30 frames and barriers, complete presentation, multiple
rounds, lobby return, and a second match from the same lobby. Zero corrections
fails.

Also run pre-ownership mismatch/timeout/disconnect; post-ownership auth/hash/
restore/peer failures; one-hour same-process lobby/match cycling; one-hour
continuous corrected play; shutdown; and fresh-process re-entry. Always restore
diagnostic flags false, even after failure/interruption. Never end a turn with a
request armed or unattended SC6 ownership.

## 10. Explicit ceilings

Instrument allocator-accounted deterministic memory. Per process:

- replay timeline stores: existing 512 MiB hard limit;
- forced snapshot store: 16 MiB;
- online presentation payload: 2 MiB, 8,192 journal events, 8,192 correction
  events;
- aggregate HorseMod deterministic owned storage including scratch/metadata:
  <=576 MiB;
- zero store growth after status 4 and zero capacity failures;
- after 10-minute warm-up, one-hour-soak ending private bytes <=64 MiB above
  baseline, and HorseMod-owned bytes return to pre-match value after lobby exit.

Report correction p50/p95/p99/max. Raising a ceiling requires design review and
rerunning affected matrices.

## 11. Structure, RE, documentation, review

The size gate is violated: `dllmain.cpp` ~11,173 lines,
`DeterministicHookSet.cpp` ~6,182, `Sc6ReplayRuntime.cpp` ~3,142. Extract online
lifecycle/ownership/cleanup/qualification from `dllmain.cpp`; split hooks/runtime
by native owner. Keep functions <=150 lines. Any file still >1,500 needs a
narrow reviewed exception explaining why splitting obscures a verified native
transaction; legacy/schedule is insufficient.

For missing native contracts use Ghidra MCP, improve relevant names/prototypes/
variables/types/structs/comments in mandated order, run completeness, and save.
Never edit `.gpr`, run unapproved scripts, or import a second executable.

Transfer stable session accessors, audio selector, round-barrier owner, and new
cleanup/presentation findings to
`E:\DevShitPosts\SC6Mods\SC6ModdingDocs` following its `AGENTS.md`. Copy only
used dump inputs into `E:\myMods\dump`.

Have Hermes adversarially review production activation and runner/evaluator
before release freeze. Resolve findings against primary evidence.

## 12. Evidence retention

Keep the 93 old JSONs as local regression inputs; do not indiscriminately commit
them. Generate a compact index with filename/hash/source/DLL/certifying/case/
result. Commit only that index plus final schema-v2 summaries/manifests required
for reproduction. Keep raw offset-bounded logs and large artifacts in the
existing evidence bundle with a SHA-256 manifest. Do not duplicate builds,
dumps, or identical logs.

## 13. Immutable release ordering

1. Finish observer, lifecycle, production, evaluator, runner, RE docs, and
   reviews.
2. Generate candidate region and exact three-case content manifests.
3. Build the **final release DLL** with those manifests; freeze complete source,
   DLL/schema/config/runner/tool/game hashes. Any semantic/artifact change
   invalidates qualification and returns here.
4. On that unchanged hash run all 51 offline rows, strict seeks,
   authored-winner/vanilla controls, Tira transition, and every paired clean/
   impairment/failure/re-entry/soak case for all three content contracts.
5. On the same hash run the fresh-box clean release gate with real corrections,
   multiple rounds, exact presentation, teardown/re-entry, bounded memory, and
   clean process/module shutdown.
6. Publish/populate production allowlists only if every report binds the exact
   unchanged hash and passes. A rebuilt DLL inherits nothing; smoke is not
   certification.

Commit only deterministic-rewrite files; exclude all seven protected files.

## 14. Finished means beta-ready evidence exists

Only say beta-ready when one unchanged production-enabled release DLL has passed
the full exact-map normal-render offline/seek matrix, actual Tira random
transition, all three authenticated Steam P2P online cases with real corrections
under clean/impairment, zero confirmed divergence, exact ordered presentation,
explicit timing/storage/soak ceilings, multiple rounds, fail-closed faults,
lobby teardown, same/fresh-process re-entry, module cleanup, fresh-box release,
schema-v2 reproducibility, and both Hermes reviews.

If any row/artifact is missing, it is not finished. Continue implementation.
Pause only for a genuine external-authority blocker after exhausting safe
alternatives.
