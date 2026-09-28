# G1 C18 zero-serial diagnostic — 2026-09-27

Implemented and locally tested; ready for root's build/full-suite checkpoint.
No game, deployment, project build, broad suite, Ghidra edit, status-file edit or
commit was performed by this writer. G1 remains open. This is a pointer/shape
diagnostic, not material ownership, completion, recovery or rollback admission.

## Boundary and schema

The existing selected `C18Before` owner has a separate zero-serial branch.
Ordinary `SameIdentity`, `C18ObjectAt`, `header_valid`, provider census and weak
delegate eligibility remain strict. The ordinary census stays invalid and empty
when the selected hub serial is zero. No serial allocator or reference owner is
called, and no hook is added.

`collection18_witness.zero_serial_diagnostic` is either null or version 1,
scope `zero_serial_entry_pointer_samples`, status `pending`, `rejected` or
`sampled`. Every receipt explicitly has `generation_unknown=true`,
`provider_census_valid=false`, `ownership_permission=false`,
`resource_completion_proven=false`, `rollback_admission=false` and
`total_retained_native_bytes=null`. Matching samples are not generations or an
atomic snapshot. The reader stores this separately as
`vfx_collection18_zero_serial_diagnostic`; it cannot promote it into the ordinary
census. Diagnostic object records use `indexed_pointer_checked`, not `valid`.

The receipt binds entry sequence, thread, collection and descriptor to the
existing occurrence. Guarded indexed pointer/flag/table checks and two bounded
captures follow only the already verified listener → world registry → selected
fighter/trace root → actor/provider → effective override slot route. Weak
delegate receivers still require a positive matching stored/indexed serial.
Raw route objects may have serial zero. An entry-local set of at most 256 object
samples rejects observed address/index/serial/table/flag changes, including
inconsistent aliases; it is never a persistent identity registry. Collection,
descriptor, sparse registry, provider headers/topology and MID shape changes
also reject. Writers, reentry, overlap and closure during the selected call
invalidate the diagnostic independently of ordinary `invalidated`.

Only exact override MID vtable `base+0x391ee70` receives shape detail: vector
header `+B8`, capacity times `0x28`, three proxy occupancies and twelve cache
occupancies. No vector backing, proxy or cache target is traversed. At most 16
detailed rows are retained. Detail overflow reports matched/omitted counts and
rejects all diagnostic rows. Other read/association failures suppress rows and
leave unknown counts null. Unsupported inline collection storage with nonzero
capacity and no backing is conservatively rejected by this diagnostic branch.

The ordinary and diagnostic branches exclusively reuse the existing two capture
buffers. The compiled C18 charge, including the alias set and scratch allowance,
is **195,470 bytes**, below its existing 256 KiB allowance. The existing overall
static assertion passes under the unchanged **4 MiB observer reservation**.
No production ceiling, native/GPU reservation or MID retention allowance was
increased; the 1 GiB production ceiling remains unchanged.

## Ordinary-forward runner

`python tools/replay_test.py c18-diagnostic` uses the existing capture, deployment
journal and cleanup path. Dedicated request protocol 18 whitelists a 360-sample
ordinary trajectory window, intro/setup observation and optional startup-loading
flush. The stage uses the retained single-game-thread/no-async startup setup.
Executor, consumer-task, historical, mutation and seek options are rejected.
The bridge starts the existing VFX observer before replay loading.

The saved selected callback return is exposed by the existing observer owner
through `ReadCollection18Return` and one DLL export. It reads five owned metadata
values only, with no borrowed native reads. The bridge emits one run-bound marker
from its ordinary trajectory observer. The runner reuses the bounded incremental
log reader, stops at that marker, verifies mapped identities, requests existing
owned-game `WM_CLOSE` cleanup, requires clean exit, then retains the sidecar and
checks its five occurrence coordinates against the marker. It never opens the
sidecar while native publication is active. Missing occurrence exhausts the one
bounded window and fails; it does not fall back to historical preparation.
Normal native execution can continue between the marker and graceful cleanup;
this is not an exact-tick stop or a quiescence receipt.

The capture result is `diagnostic_observed`, with phase-completion false. A
rejected diagnostic is still an informative observed occurrence. It does not
claim that a trajectory, material resource, GPU operation or recovery completed.

## Retained RED/GREEN

All commands ran from `E:\myMods` through the existing runner, which used pytest
`-x` and stopped at each first failure. Manifests contain exact commands and
reconstructible dirty/untracked source receipts.

| Boundary | Result | Immutable evidence |
|---|---|---|
| Zero-serial hub, real callback owner, before production change | RED: missing separate diagnostic; forwarding/LastError already pass; zero header/world reads | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/68af30c333f8561bdd49a0153c6851f9767bc6c1fef9dc806395efa2c18de704.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/de33075c37a722c04f8a89a37cb879fd5582a118cd5cff1bbe73f072bae10989.log) |
| Ordinary-forward CLI before runner extension | RED: stage absent | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/dfa4cb837fe509095d97a3fd28a72175b383ab8a0db99a06026ed3b590320de8.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/37e004c8e4a6661f5eecbd353ff78ccc9a730cc7cc4e302a216c07ad68318e0c.log) |
| Diagnostic, ordinary census/shape, historical rejection, Start/helper and first-occurrence controls | GREEN: 31 selected native-contract cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/d8ae03286863a709e8a5bf7a2556b5470fa3fcfc2b0461ee62d1518238abdfc6.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b3a5df53ec0730975fe30fdbb982ad82f9c986c7bc0442bea2388dd0fabad143.log) |
| Final runner, protocol separation, burst/CRLF marker parsing, no live sidecar reads and retained startup settings | GREEN: 9 selected workflow cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c806ae08a213435d940ca9f88c883b9ecf5693ff2781a9ce6711d52d33eb5c99.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/47150831b8aff85930d88a893091ffb4810a1dcf7f23b1d3052f1d9e5e14e60c.log) |
| Final diagnostic rerun retaining each native stdout and JSON | GREEN: 16 cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/1bd9452dcb807230c9dcd3247a01ff5073bd1c43d4339283371fd13b24420e05.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/45d989bc55fe27c0e9f3231efe0d82d362c41f183d98063ec5222a5df5ee95f2.log), [all 16 stdout/JSON receipts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/155128640ef6f930ad06fced456d8f811d61310f3b6f0b9daa2031de35d6bd59.json) |

The successful [native stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/cc7c06f649ed4d3a4c000a8578193f97bbb455ef63b526072d509aacce4f4a26.log)
and [raw JSON](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/85235482584ad2843b755892b02c785d25a4bc818eb40091ffb29b69be658b34.json)
show four ordered diagnostic occurrences, count 1/capacity 8/320 vector bytes,
secondary proxy present, ordinary census invalid/empty, exact forwarding,
incoming `0x66c`/outgoing `0x77c`, zero resource-target reads and zero reads after
native input revocation. The metadata export is exercised after revocation and
rejects premature reads during the active callback. Negative cases cover hub
pointer replacement, hub/MID serial changes, MID index/flags, collection/vector
headers, provider topology, unreadability, zero weak serial, overflow, reentry,
cross-thread overlap, closure and writer entry.

Initial RED / final 16-case GREEN command:

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic
```

31-case command:

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_material_builder_between_clear_and_native_retirement --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_selected_trace_start_and_mid_helper_observed_before_effects --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts
```

Runner RED command:

```powershell
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_uses_ordinary_forward_capture
```

Final workflow command:

```powershell
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_poll_does_not_open_native_publication --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_control_parks_and_stops_at_separate_boundary --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_uses_ordinary_forward_capture --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_rejects_execution_options --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_consumer_capture_preserves_no_async_launch_setup
```

Two implementation experiments rejected live sidecar polling: replacement with
an open read handle failed with Win32 error 5 using both
[Python replacement](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/6c73d89452937683dbd8e59e3e6bd480e7d28e1483d3dd3a13475f477477a5cf.json)
and the observer's exact
[MoveFileExW flags](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f0f8bac21f477b23a47aabb67a4bb65a7ab38b332653329112215214c750c424.json).
Those experiments are superseded by the tested no-live-read path, not represented
as successful publication. The incremental-reader integration then exposed a
[CRLF marker RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/faff6f664ba65d7413c4302093788c80caabf40eeb2095c75890c4cbd37a6c07.json):
the existing reader retains a terminal carriage return after removing newline.
The marker parser now accepts that exact termination; the final workflow GREEN
includes a marker surrounded by more than 64 KiB of unrelated logging.

## Source and next experiment

[Pre-fix source](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-96171edbcd9567c8bdd957897170c0d2292434c56e629994c57e0c47a091699c.json)
and [final source](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-c42fd00abc6641c0f868d483294eb94c48b8e8d22def66115baa34e41a5414cb.json)
include reconstructible dirty/untracked contents through the existing source
object store. Relative to the retained initial source, only these code/test
files changed:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.hpp`
- `HorseMod/dllmain.cpp`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- `tools/deterministic_qualification/tests/test_replay_workflow.py`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_test.py`
- `tools/replay_qualification_mod/ReplayFailureProtocol.hpp`
- `tools/replay_qualification_mod/ReplayQualificationMod.cpp`

After root's build, CTests, full ordinary local suite and identity/preflight
checks, one `c18-diagnostic` run with the same retained replay/startup settings
is justified. Preserve any previously selected startup flush option. The live
question is whether lazy hub serial zero was the only obstacle to reading the
selected entry-visible material route. A new independent layout/topology/read
rejection falsifies that hypothesis and identifies the next native dependency;
do not repeat unchanged. No result can prove complete native memory bounds or
resource ownership. There is no live result for this source yet.

All six material hooks, `ProducerUncovered` C-only veto,
`HistoricalRestoreSupported()==false`, complete B and lifetime/membership/GPU/
deferred-retirement guards remain. New Start-created MIDs, zero-serial ABA,
provider replacement exclusion, actor-death survival, raw GetMaterial-to-builder
leases, MID reset/reinit/destruction, secondary proxy ownership and resource/GPU
completion remain unresolved as described in the
[native evidence](rollback-g1-material-task-cpu-guard-2026-09-27.md) and
[preceding shape slice](rollback-g1-material-memory-shape-census-2026-09-27.md).

## Ordinary-capture compatibility repair

Root reported a successful project build and two CTests, followed by an ordinary
`python tools/replay_test.py local` failure after 597 passes. The retained log
shows `test_capture_rejects_abnormal_exit_after_observation[3221225477]` correctly
rejecting the abnormal exit, then failing its clean-exit control with
`KeyError: 'c18_diagnostic'`. Its production close-game block runs with
`report={}`. This is a Python optional-field compatibility regression, not a
native crash or a reason to weaken the abnormal-exit assertion.

The repair changes only the two post-exit diagnostic flag reads in
`tools/deterministic_qualification/replay_control.py` to default an absent flag
to false. Exit-code validation still precedes sidecar association and success.
An absent or disabled flag produces ordinary `captured` without a diagnostic
receipt read; explicit opt-in produces `diagnostic_observed` only after clean
exit and successful association. There is no default diagnostic run.

The original failing test is unchanged. A new three-case workflow regression
executes the entire production close-game suffix with absent, false and true
flags. Each rejects `0xc0000005`, `259` and `None` before receipt collection;
clean exits preserve ordinary/diagnostic results, close/exit/receipt order, and
diagnostic association failure cannot set success. The existing post-exit AST
selector was updated to select the changed conditional.

| Check | Result | Immutable evidence |
|---|---|---|
| Root ordinary full suite, before repair | RED: 1 failed, 597 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/8dc8d6f7b2208abebf2ffe5292cda2140d764087869521c1621a7d0720d6d395.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c94b5a46e2995b38d018ce095e608ebc74a35440db6df6c54731b8f54dbd1c58.log) |
| New production-boundary regression before repair | RED: absent-flag clean exit raises the same KeyError; stopped at first failure | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f891fbbd91e99290e8c69957d2a7264b32d1e9b4ab4fc448b9e5ee648f4d4427.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/99441ff8f624bd9c4dcf019181d9975aea0799f744194bb119b9bded9b0dd0ec.log) |
| Original failing node and all its parameters, bootstrap crash rejection, adjacent extracted guards | GREEN: 7 selected unit cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/53413be6e9bac6a82091e375e712660466ec245f9b8d84761b789036537c4b43.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/18671fe2901da052a0234e02d371c831a75f36dcebf54028c7512d03c45926c4.log) |
| New close-game regression and affected diagnostic workflow | GREEN: 12 selected workflow cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/91f0260b30d361d1465db29ac2ef7a802aab30a559cff3820e2d2af8b06b0865.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2d20cd9d667e24753fb843a626c438bb02e14ce054a1291944fcf0e773228ff3.log) |

Exact writer commands, in order (the first was intentionally RED):

```powershell
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_close_requires_opt_in_and_clean_exit
python tools/replay_test.py local --layer unit --test tools/deterministic_qualification/tests/test_replay_run.py::test_capture_rejects_abnormal_exit_after_observation --test tools/deterministic_qualification/tests/test_replay_run.py::test_retained_bootstrap_rejects_completed_observation_with_crash --test tools/deterministic_qualification/tests/test_replay_run.py::test_native_failed_receipt_stops_current_run_immediately --test tools/deterministic_qualification/tests/test_replay_run.py::test_rolling_runner_stops_on_unrecoverable_native_undo --test tools/deterministic_qualification/tests/test_replay_run.py::test_ground_admission_failure_stops_existing_control_guard
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_close_requires_opt_in_and_clean_exit --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_poll_does_not_open_native_publication --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_control_parks_and_stops_at_separate_boundary --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_uses_ordinary_forward_capture --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_rejects_execution_options --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_consumer_capture_preserves_no_async_launch_setup
```

[Selected RED source](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-c72555d80dbbd254663f901870ce2ca8ea08b208fa4ebd90f31077e37b7dff59.json)
retains the new regression against unfixed production. Both GREEN runs retain
the same [repaired source](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-e78dade8da909cb71dbe5f5bac1939e9a2a840c02d39a3f31228975d44c5b1ea.json).
Comparing source manifests against root's full-suite RED confirms that only
`tools/deterministic_qualification/replay_control.py` and
`tools/deterministic_qualification/tests/test_replay_workflow.py` changed. This
evidence note is the only additional authored file changed for the repair.

Ready for root's next build/full-suite checkpoint. This writer ran no project
build, broad suite, deployment or game. The selected passes do not qualify G3.
No native code, observer budget, hook, complete-B handling, material guard or
historical admission changed; G1 remains open and all diagnostic limitations
above remain.

## Inline collection diagnostic and entry discrimination

Root's subsequent build/two CTests and full ordinary 1,034-test suite passed.
The one [live diagnostic](rollback-g1-c18-zero-serial-live-2026-09-27.md) selected
and returned C18 but rejected the separate receipt at `entry_pointer_or_header`.
The [raw live sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a974e55ddaa5a4a3b6d692a1f9b7a399bb342cac22c44a0ef7108c229ea0db7d.json)
does not publish a measured header. Null heap with capacity one was therefore a
native-supported hypothesis, not an established explanation of that live run.

Root verified `SetWeakCallbackEntryCollectionCapacity` `142050940`, storage
helper `142050570`, and executor `141D38300` in the existing Ghidra program:
first insertion permits count/capacity one with null heap `+0x40`, and the
executor uses the inline row at the collection base. The production change
adds precisely that case to the separate zero-serial diagnostic. It preserves
the existing empty count/capacity-zero form. Other null-backed shapes reject,
including count two, excess capacity, and empty count with capacity one.
Heap-backed shapes keep the existing bounded-array check; the general
`C18Array` and ordinary provider census are unchanged.

The inline row address is checked against the 0x40-byte range. Existing guarded
reads validate the listener and the independently verified route. Each of the
two complete captures rechecks hub identity, dispatcher, heap pointer, count,
capacity and recursion at its start/end; both semantic capture buffers and
both material shape buffers must compare equal. Pointer/serial/header or
listener replacement suppresses all detail rows. Matching samples still do
not establish a generation, atomic snapshot, lifetime or writer exclusion.

Entry failure strings now discriminate these checks; each has a production
callback-boundary regression:

| Failure | Meaning |
|---|---|
| `entry_hub_identity` | Independent indexed hub sample no longer matches the selecting zero-serial identity |
| `entry_hub_vtable` | Indexed hub has an unsupported vtable |
| `entry_dispatcher_vtable` | Dispatcher vtable read or exact table check failed |
| `entry_header_unreadable` | Heap pointer, count, capacity or recursion could not be read |
| `entry_header_shape` | Collection shape is outside the supported bounded heap/inline forms |
| `occurrence_invalidated` | Nonzero entry recursion or existing interruption/reentry/overlap/writer/closure invalidation |

The schema stays version 1. Only a sampled diagnostic may publish
`collection_header={data:0,count:1,capacity:1}` and rows associated with the
collection base/index zero. The reader permits this exception only for the
collection header, never vector/material arrays. Rejected receipts retain
null hub/header/descriptor fields and empty rows. The old live rejection is
still readable, and attempts to insert rows/header/identity, mark samples as
matching, or promote its status/provider validity reject. Ordinary
`header_valid`/provider census remain false, every ownership/completion/admission
claim remains false, `generation_unknown` stays true, and total native memory
remains unknown.

No retained structures or hooks were added. Native fixture receipts still
charge **195,470 bytes**, including both captures and existing scratch, under
the 256 KiB C18 cap and 4 MiB observer reservation. Six material hooks,
`HistoricalRestoreSupported()==false`, `ProducerUncovered`, complete B and the
1 GiB production ceiling are unchanged.

| Boundary | Result | Immutable evidence |
|---|---|---|
| Valid count 1/capacity 1/null heap, before fix | RED: `entry_pointer_or_header`, zero listener/material reads; exact native forwarding and LastError already pass | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e4fe3f592221614d6d4c1f51ba2cd01589a285184052be928e0c68f8d9a6c011.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/23d7e684b7adab9a7d55f3b6e8245f1fd728591c229a941e35e90592b9182819.log), [native stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3c8d07aa28d0509a5efe55fe75e701b91d25c5092d1eb4ac8459eed4d55f6561.log), [raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/689a3adad928fba42ed96adb0f88205c5608085a6706f3be835c2ebee53f7906.json) |
| Separate diagnostic including inline/entry failures and prior negative controls | GREEN: 35 native-contract cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/168aca3c2ff3dc8015d175c41a3123c1a7c833e16b896f6c0b463a3a14e0fb5a.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9281c1d6c268d8ce48cacd56024a4adf8d569eb95a41f77a3818b57fffc138a6.log), [all 35 raw stdout/sidecar receipts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b07bf67f9c76d49e1b2814eea44e6d594e7fcc0dea36836e85a41ab0c47476bb.json) |
| Ordinary provider/shape census, selected occurrence and guarded reads | GREEN: 12 native-contract cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/438bba7b1952f816916f56089db4831ff110d590408c4d08f90b50c6fc7bb491.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7a3cb24430b2a14146419741360f5a9f40c47f4b97287ab1c662a7b5599f4b46.log) |
| Rejected live receipt reader and affected diagnostic runner workflow | GREEN: 18 workflow cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e9ff2abee406e84634b2e9be5dc2467cdaaf4c49384b69ad72543c7735f3a2a0.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/bcf90c38b8f06841524d35285acb47f7aed3489961bfce3db8a78803833308e3.log) |

The inline GREEN [stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/47b4c50a0a69c66061a975c29b8a636ae5770941874d2533b8437682907875e4.log)
and [raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f25fa7100b39f617e6b19538895c97ab7f7c4c79daf06028b4cbbfd492c8f32c.json)
show one inline listener and two shape rows, each with 320 vector payload
capacity bytes and secondary proxy occupied. They retain ordinary census
invalidity, forwarding once, incoming `0x66c`/outgoing `0x77c`, zero resource
target reads and zero native reads after return. No fixture supplies a receipt
or reference. Negative cases alter native input memory/index entries; the
unreadable-header case temporarily protects the real input page, and the
entry-recursion case exposes a nonzero native recursion field during its read.

Exact commands, in order (first RED, then all GREEN):

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic[-inline]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_rejected_live_receipt_stays_diagnostic_only --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_close_requires_opt_in_and_clean_exit --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_poll_does_not_open_native_publication --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_control_parks_and_stops_at_separate_boundary --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_uses_ordinary_forward_capture --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_rejects_execution_options
```

[Pre-fix source with inline regression](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-a0e1a7569f4c191f7caf7638d1ada9b7eba34196b06761612f14c49403552640.json)
and [final source shared by all three GREEN runs](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-b160e1c3a4e5dbc7d419dfa168def053662dc66462a951bb238dfabf2e635c6b.json)
retain dirty/untracked contents. Compared with root's 1,034-test source, only
these code/test files changed:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- `tools/deterministic_qualification/tests/test_replay_workflow.py`

This note is the only additional authored file. Ready for root's project build,
CTests and full local checkpoint; this writer ran only the selections above.
After those gates and identity/preflight checks, one bounded live diagnostic
with the same retained replay/startup settings can test the inline hypothesis.
A sampled `{data:0,count:1,capacity:1}` would support that header explanation;
another entry failure or downstream route failure would identify the next
blocker and falsify inline support as a sufficient fix. No unchanged live retry
was performed. No result here proves material ownership, native memory bounds,
resource/GPU completion, recovery or G1 success; no status file was edited.

## Listener identity discriminator

Root reports that the inline slice passed project build/two CTests and the full
1,061-test ordinary suite. The subsequent run
`replay-88afec7f229c4df5a594ad54bc4c1ab3`, with mapped runtime
`abd072c55086c3a9cf837fb849af5ca7ef26938c3dd118711a45bc2b68f64072`,
selected/returned C18 and rejected at `listener_identity`; root reports a clean
journal afterward. The verified [raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/1ef05c575e77e3d709666a30a4b9164733950ed443283a4273f9a59842ea28c4.json)
binds entry/return 5,071,540/5,071,541 and has no diagnostic header or rows.
Its ordinary count/capacity zeros are short-circuit defaults from the positive
serial check, **not measured diagnostic header values**. The listener loop was
reached, but the live collection's inline/heap form remains unproven.

Root's native investigation found that `AddWeakUObjectCallbackToCollection`
`14043D210` constructs finalized vtable `14374B760` with a serial-bearing weak
target. Slot `+0x68`, `TryExecuteWeakUObjectCallbackOneArg` `1403EF060`, can reject
a stale target; executor `141D38300` may compact stale entries. A rejected weak
listener is therefore not sufficient evidence of a layout bug.

The smallest change splits the existing short-circuit predicates into named
failures **only for the separate diagnostic**, with identical read order and
no extra reads, native calls or retained metadata. Ordinary census still uses
`listener_identity`. Existing alias-change and occurrence-invalidation priority
remain unchanged; rejected receipts still publish no raw identity/header/rows.

| Diagnostic failure | First failed check |
|---|---|
| `listener_weak_serial_nonpositive` | Stored weak serial is zero or negative |
| `listener_indexed_lookup` | Guarded indexed lookup failed |
| `listener_weak_serial_mismatch` | Indexed serial differs from stored weak serial |
| `listener_object_sample` | Independent indexed object/flags sample failed |
| `listener_sample_identity_mismatch` | Successful sample's index or serial differs from the weak reference |
| `listener_receiver_vtable` | Sampled receiver vtable is unsupported |

Lookup/sample failure does not itself distinguish collection, unreadability or
a concurrent change. No label authorizes stale-row skipping, weak resolution,
serial allocation, revival, ownership or recovery. Reader code/tests needed no
extension: existing production reader paths accept diagnostic failure strings
while enforcing rejected-receipt emptiness and false permission claims.

| Check | Result | Immutable evidence |
|---|---|---|
| Changed indexed serial, before production split | RED: generic `listener_identity` instead of `listener_weak_serial_mismatch`; native forwarding/LastError already pass | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/dd69720c8d155dcc795535c76214d4bc252279a1a216d835c99b42abd9ad629a.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/56468de3516621b69b4a5a0cd424a7f03d4283f3e7d828a47f80a0435201e0ce.log), [stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c460bc8512d6ddf6dd9bd8060acef0c3708833f89e450fe80edd7bf28a761722.log), [sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/94011036d32b19e429e6f5b910eae8294d1a4818b1f905de44072e036d3de1de.json) |
| Diagnostic and adjacent ordinary census/shape/occurrence controls | GREEN: 58 native-contract cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cf79bd0acef49782ea96417ec3d0d2df1023123e1e448fc33f386ecacb3d873f.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/429305225ac2dfa7253874af5ac445216f1b64f987f668f5a30ffecb59afc034.log), [all 46 diagnostic stdout/sidecar pairs](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/02b97f3772b10e69896fc03d2aea32529fa640cb32a0760706c394c108a19afb.json) |
| Existing rejected-live-receipt reader workflow | GREEN: 8 cases | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/4df367fde77dc2a363224bf2b46842c432497625b868cb8f6b395ba3682d6392.json), [pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7d033b6114e3b7e226dd134ddf87559ce2573e4749f27f5567211d071d888ecc.log) |

Eleven new negative fixture cases alter native weak serial/index, indexed slot,
object flags, readable page protection or receiver vtable. Serial/index changes
between the weak lookup and independent sample exercise the mismatch branch.
They use two heap rows, reject before any world/material read, preserve native
forwarding once and incoming `0x66c`/outgoing `0x77c`, and retain no rejected
rows. The stale-serial GREEN [stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/66bf1495de0e1d54f08f8f0fd75f3b55b6d6ad8b784bfd45a5088275d18cfa5d.log)
and [sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6b5ec3db040256ec223023df10d6cf75e8cfdae6fa03d1dca28cfc09dbd6538d.json)
show the precise new label. Each diagnostic case also exercises both existing
production receipt readers; no fixture grants a good receipt.

Exact commands, in order (RED, then GREEN):

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic[-listener-stale]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_rejected_live_receipt_stays_diagnostic_only
```

[RED source](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-64bc36ee21a6c149b2fd5d26fa098283ca5c08675bf80385040d2ed70aac791c.json)
and [GREEN source](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-af09c3beb3ec44e63cb88d53820b3a51a52f544585c7d6fa26c891f0388705cc.json)
retain exact dirty/untracked contents. Only the C18 `.inl`, existing native
selftest and `test_vfx_completion_observation.py` changed since the inline
slice, plus this note. C18 accounting remains **195,470 bytes**; 16-row,
256 KiB/4 MiB limits, six material hooks, `ProducerUncovered`, complete B,
1 GiB ceiling and `HistoricalRestoreSupported()==false` remain unchanged.

Ready for root's build/full local checkpoint. After those gates and preflight,
one bounded live run has a real instrumentation change: a serial mismatch
would support weak-generation mismatch as the failing predicate, while an
unsupported receiver table would falsify that explanation and direct root to
the concrete receiver class. Neither proves why an object expired or grants
permission to skip it. No project build, broad suite, deployment, game or status
edit was performed by this writer; G1 remains open.
