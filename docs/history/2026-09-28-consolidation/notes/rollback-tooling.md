> Archived pre-consolidation document; see [current status](../../../rollback-status.md) and the [documentation map](../../../README.md) for active work.

# Rollback tooling contracts

All commands use `python tools/replay_test.py` and the existing build directory.
Tooling checks do not qualify rollback. The ordered G0–G10 requirements remain in
`rollback-netcode-local-readiness-plan-2026-09-22.md`.

## Results and readiness

Reports carry outcome schema 1: stable check ID, category, outcome, scope,
coordinates, evidence locator and, when established, comparison/execution order.
Unobserved coordinates and evidence are null. Expected interventions are separate
from failures. The named legacy adapter recognizes only known report fields; it
does not recursively search for a key named `failure`. Historical manifests remain
unchanged. Compact/JSON schema 2 and detailed evidence use the same checks.

`status` is read-only. It separates build, local and live evidence, G0–G10
requirements, compatible qualification evidence, scoped attempts, blockers and
recorded deployment ownership. Missing identities cannot qualify a gate. Live
compatibility also verifies the selected replay, game, PhysX and UCRT identities;
it cannot be inferred from the mod DLLs alone. Current candidates verify retained
payloads before supporting a gate; a newer relevant failure remains visible beside
an earlier successful receipt.
`status --report PATH` inspects historical evidence; `status --since PATH` accepts
compact versions 1 and 2. The checked-in readiness definition supplies the next
experiment; no next command is inferred from logs.

`evidence --rebuild-index` rebuilds the index offline. Ordinary finalization adds
immutable index entries and a final timing receipt referencing the original stage
manifest. Earlier evidence is not rewritten or deleted.

## Time measurements

Orchestration spans use a monotonic clock and carry parent IDs, outcomes and
durations. Compiler recipe spans are imported from the fixture subprocess using
the shared Windows monotonic clock; they include dependency validation and sit
inside fixture execution. Cache hits are labeled separately. Nested phase
durations must not be summed as wall time. Failed and
interrupted spans remain recorded. Final timing excludes publication of the final
timing receipt itself and console rendering. Existing `elapsed_seconds` and native
`correction_us`/`preceding_forward_us` fields remain legacy scoped measurements.

Native rolling timing protocol 1 starts at B before correction admission and ends
after the ordinary forward update, next checkpoint and required retirement.
Fixed storage for 600 samples is included in the host's admission accounting.
Records include preparation, publication, resimulation, application, capture,
forward and retirement time. Storage/output adds observer cost; these measurements
do not subtract it. The backlog is a count of pending owner categories, **not** a
count of tasks or bytes. Ownership amounts remain separate existing receipts.

The final transaction has no following checkpoint in the existing driver. It is
recorded as `terminal_no_next_checkpoint`, outside the complete-sample
distribution and visible as incomplete coverage. Final window teardown and initial
warm-up are separate. Aborted and missing-end intervals remain visible and cannot
produce a passing full-update measurement. Median, p95, maximum and every interval
over 16.7 ms are retained. Resumed TPS and viewport cadence remain separate.

`evidence --observer-overhead CORE_MANIFEST WITNESS_MANIFEST` requires matching
identities, workload, setup, complete intervals, independent comparison and cleanup.
It reports the optional witness increment with run-to-run variation. It does not
measure the common observer cost. `tooling-timing-core` and
`tooling-timing-witnesses` keep safety enforcement and common comparisons enabled.

## Experiments and coverage

Profile schema 1 and experiment contract version 1 are independently versioned.
Contracts declare a hypothesis, decision, required event/phase/window, setup,
identities, success/falsification, injections/recovery, stop conditions, permitted
claims, named validator and required execution coverage. No JSON expressions run.
Legacy profiles remain readable but cannot claim new coverage. A complete zero-hit
capture can be valid while its ownership hypothesis is inconclusive; task
correlation cannot establish a lease or before-effects ownership.

Rolling coverage distinguishes requested/scheduled/admitted/committed corrections,
attempted/committed cycles, unique forward/regenerated ticks, actual consumption
ages, revision transitions and unchanged-revision cycles. Lifecycle differences,
visual coherence and unmeasured ownership stay unknown unless supported separately.

`short-boundary617-624` uses request protocol 17 to author the frozen final input
prefix in both independent control and candidate before native forward execution.
It explicitly omits preceding rolling transactions and starts with that authored
baseline revision. It requires compatible G1 proof before launch. A negative result
cannot replace the 408-cycle accumulated-history regression.

To freeze a distributed profile from a retained independent native report:

```
python tools/replay_test.py evidence --generate-distributed NATIVE_REPORT --distributed-cycles 30 --destination tools/deterministic_qualification/replay_profiles/changed-rolling30-distributed.json
```

The generator requires native consumption protocol 1, immutable identities and
complete cleanup. It selects the earliest eligible non-neutral P2 sample in each
third, uses neutral corrections with ages 1/4/7, and retains the exact source rows.
It fails when a third has no eligible sample or consumption proof is unavailable.
The profile is frozen before launch and its evidence is revalidated on loading.
These schedules are diagnostics until independent controls demonstrate the required
birth/death difference. Older three-early-arrival profiles retain that scope.

## Retention and compilation

Source format `objects-v2` stores zlib-compressed payloads keyed by uncompressed
SHA-256 in a transactional SQLite database. Immutable snapshot manifests preserve
paths, deletions, hashes, sizes, repository identity, build inputs and dependency
roots. Publication verifies content and membership a second time and publishes the
manifest atomically. An interrupted publication cannot produce a valid snapshot.
ZIP and object readers share hash verification. Historical archives are retained.

```
python tools/replay_test.py evidence --seed-source SOURCE_RECEIPT
python tools/replay_test.py evidence --export-source SOURCE_RECEIPT --destination E:/myMods/build_cmake_LessEqual421__Shipping__Win64/replay-tests/portable-source.zip
```

Seeding is explicit for one verified snapshot. Export is deterministic and portable.
Broad source retention remains independent from compatibility: Python reporting
changes are excluded from native reuse only when Ninja does not consume them.
Unknown native dependencies retain conservative invalidation.

The fixture cache preprocesses actual generated fragments on every lookup, keys
their bytes, recipe, include search order, compiler backends and linker identity,
and compiler/linker environment options (CL, _CL_, LINK and _LINK_),
and verifies resolved link inputs and executable hashes. RED-header overrides and
unclassified recipes bypass shared caching. Corruption retains a diagnostic before
rebuilding. Only compilation is reused; fixture programs execute on every requested
local run, in isolated test directories. Shipped native checks are never replaced
by fixture-cache hits. Full-suite receipt reuse also binds build provenance.

Cache and retention receipts report hits/misses, reasons, bytes reused/newly stored
and measured time. Deliberate cache invalidation tests are not warm-cache benchmark
workloads; use an unchanged production-fixture group to measure reuse.

## Validation scopes

During development, select the affected test file/node or an affected group:

```powershell
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_replay_reporting.py
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_replay_run.py::test_production_trace_retirement_method_fault_boundaries
python tools/replay_test.py local --changed-path tools/deterministic_qualification/replay_outcomes.py
python tools/replay_test.py local --group fixture-sharing
```

Repeat `--test` or `--changed-path` for several targets; do not combine the two.
`--test` accepts existing qualification test files or pytest node IDs. `--list-tests
--output full` previews the command and file/node selection without running tests;
layer filtering happens during pytest collection. Named groups come from
`replay_test_groups.json`, including `reporting`, `fixture-cache`, `retention`,
`experiments` and `timing`. Unknown production dependencies still select `all`.
A changed test file selects itself and any mapped helper dependents. Bare `local`
examines all dirty/untracked paths, so a busy checkout can still select `all`;
use explicit changed paths for the task under development.

Layers are independent of groups:

| Local layer | Scope |
| --- | --- |
| `--layer unit` | Python predicates, parsers and data checks |
| `--layer workflow` | CLI, filesystem and orchestration boundaries |
| `--layer native-contract` | Compiled production fragments with controlled native service boundaries |
| `--layer all` (default) | Every selected local layer |

Explicit pytest markers distinguish workflow and compiled tests; remaining tests
are unit tests. The compile helper rejects compilation from a Python layer, so a
missing compiled-test marker fails visibly. Reports record collected counts by
layer alongside actual pass/fail counts. These are all local checks; live game
experiments still use the existing profiles/stages.

Run affected groups while editing and one broad check at a coherent checkpoint.
`local --group all` remains the complete local suite required by live admission
and stops at its first failure. Exact selections, filtered layers and other groups
cannot emit the full-suite G3 receipt, even when all their tests pass. Mandatory
shipped native checks remain separate requirements.

The trace retirement/storage and scheduler/dispatcher tests share generated
fragments and session-scoped compilation, once per compiler configuration. Each
test still executes in a fresh process and its own working directory. The real
Windows fail-fast variant is a separate compilation. Two nested baseline test
calls and their redundant build paths were removed; the baseline assertions,
complete-B undo, retirement fault cases and terminal diagnostics remain.

The temporary `tooling-integration` selection explicitly deselects the retained G1
same-count VFX completion replacement regression. Its checked-in blocker record
names the test and immutable failure evidence. This allows the remaining tooling
changes to be checked without altering or satisfying that regression. It cannot
qualify G3 or authorize live deployment. Remove the exclusion only after the
separate native defect is fixed and the full suite passes.
