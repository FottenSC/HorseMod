> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Rollback test workflow checkpoint — 2026-09-27

Development can now select exact test files/nodes, affected source groups, and
unit, workflow or compiled native-contract layers through the existing runner.
[Usage](../../../rollback-tooling.md#validation-scopes) and AGENTS.md direct routine edits
toward affected tests and reserve broad runs for coherent checkpoints. Unknown
dependencies remain conservative. Filtered passes cannot produce G3 evidence.

## Removed work and preserved coverage

Two nested baseline test calls were removed. Trace retirement/storage now share
one compiled executable; scheduler admission/dispatch share another, with the
real Windows fail-fast configuration compiled separately. Programs still execute
in separate processes and test working directories. Only compilation is shared.
Group-map validation also parses each test module once instead of reparsing the
largest module 125 times. Redundant broad mappings for ordinary test-file edits
were removed; the imported source-retention helper keeps its dependent tests.

Comparison with the retained previous source snapshot found all 558 existing test
functions and their parameterized cases preserved. The two C++ fixtures only add
baseline command switches and their string header. Complete-B undo, retirement
fault boundaries, terminal diagnostics and distinct native service cases remain.
Four new test functions cover selection, invalid paths, dependency mapping and
the prohibition on qualifying a filtered pass (seven collected cases total).

## Measurements

These are individual retained runs, not statistical benchmarks. The same four
test cases and both dispatcher modes (intercepted and native fail-fast) were
exercised before and after sharing.

| Fixture-group run | Compile/cache lookups | Actual compiles | Test execution | Total wall |
| --- | ---: | ---: | ---: | ---: |
| [Before sharing, warm executables](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f2015cf5cbb23648144a6c42ab105b941da941d11312f488cda53f20166897b4.json) | 7 | 0 | 18.20 s | 31.08 s |
| [After sharing, first compile](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cfff38a37dffe48f8b22ffa6d653928bba502d8e4b1fdf718303382e8f9a2986.json) | 3 | 3 | 15.77 s | 22.54 s |
| [After sharing, unchanged warm run](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f0ed8a0f8dc053d54f81a02d7cadae3eca0dddd1bec089c5bbf07976248589b0.json) | 3 | 0 | 9.54 s | 16.08 s |

All four tests passed in each run. Warm compile/dependency lookup time fell from
14.70 to 6.54 seconds. Total wall time also includes source retention: the first
row published a snapshot, whereas the later two verified an existing snapshot.
Use test-execution and lookup spans for the closer comparison. Cold and warm
post-change rows use identical sources and test inputs. No executable results
were reused, and the unchanged warm run performed zero recompilation.

The [unit layer](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f2bf3c19290f76d59798c721461a88687f1976e017d9ea7c8c85a6e22ef8022f.json)
passed 378 cases in 6.67 seconds of test execution, 19.47 seconds total.
The [workflow layer](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/2a265ff25508f56fc2fd7ef62dd22ae15e22baaa24ee05992ccfca6114545222.json)
passed 216 cases without fixture compilation; both reports have no G3 evidence.

The [final broad checkpoint](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/639efff35afd037abd74f81730728bad2f494affdfa70db595a4db516a3f92ee.json)
passed 858 cases: 378 unit, 216 workflow and 264 compiled native-contract cases.
It took 469.08 seconds, essentially unchanged from the earlier 468.37 seconds.
Its 151 compile/cache invocations include 18 compilations, including deliberate
invalidation/bypass cases. The savings are in narrower development runs and the
shared fixture group; there is no demonstrated full-suite wall-time improvement.

## Evidence and disposition

- [Exact-selection RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/45f54d134674373d7ac76787b179f0aefcc375c0ed48e59b450a0779b35c8bf8.json): the runner lacked the requested selector; the final checkpoint passes the regression.
- [Shared-baseline RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/395820350121932b1c1945e4d7b71e9080fbf3b8982307fa8b44e5bd5ad6bc91.json): the storage executable did not execute retirement assertions; the shared group now passes both paths.
- [Final build/native checks](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ad66ef08f96a692afeb65a3f4fe77474823ee56a9f06e161021df4c31064a76b.json): two CTests and mandatory shipped checks pass, with unchanged runtime/observer/framework DLL hashes. Source retention reused all 127,852,580 payload bytes and stored zero new payload bytes.

Workspace identity is `f441246ddeb1f8f4cb696a0f61f6e5163438dab379b4b2a5b7fd78480ee3b971`.
Readiness verifies both final build and local evidence as current. The broad
selection still excludes the previously retained G1 same-count VFX completion
replacement regression; it cannot qualify G3 or authorize live deployment.
G0–G10 remain open and the next live experiment remains undefined.

No runtime behavior, native hooks, game deployment or configuration changed in
this follow-up. The recorded deployment journal is clean; no Soulcalibur process
was observed, and existing Steam PID 9240 remains running. Historical evidence
and the byte-for-byte status archive remain intact.
