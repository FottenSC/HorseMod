**Bounded G1 handoff: C-only ownership veto, not a producer permit.**

This implements the task's fail-closed fallback. No operation-scoped producer
exclusion or positive material retirement is claimed. Historical Request and
direct Prepare still reject through `HistoricalRestoreSupported() == false`.
The existing opt-in `HORSE_TRACE_MID_BORROW_RED=1` positive-admission test is
unchanged and remains an open prerequisite; its retained
[RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/faa9237cd11a8ca76d93bed899bf30cd1c4da9b901901d66743990df9a4e0e56.json)
was not rerun without a resolving change.

The ownership inspection found `DeterministicHookSet` already owns 1403FCA60
and 141D38300. Its callback executor forwards through the current collection18
observer; that observer does not establish coverage of all native calls to
1408D5840. `NativeReplayTraceTaskGuard` owns the existing finite trace/retirement
entries (including world actor destruction and component destruction), but its
29-entry signature table does not include the selected helper or Start.
`NativeReplayMaterialTaskGuard` owns only the two task builders and two CPU
callbacks. No competing or speculative hook was installed. The selected
helper's verified 48-byte prefix and exact Win64 prototype were requested from
root; no new native entry was assumed verified in this slice.

`InspectCOnlyRetirement` now returns an explicit `ProducerUncovered` receipt
instead of converting an idle CPU snapshot into destructive permission. All
possible receipt states veto C-only retirement. The three existing host call
sites for hidden trace rendering, particle births and added trace children use
that destructive-boundary check. Missing coverage publishes the sticky failure
`material_C_only_producer_uncovered`, retains B/C/in-flight owners through the
existing failure machinery, and rejects later render-command transitions and
release. The sticky flag is atomic because the render-command thread consumes
it. There is no new native call under a lock, producer blocking, callback
suppression, lifetime transfer, memory reservation or ceiling increase.

This does **not** serialize a successful native teardown with a producer. It
prevents that teardown from being authorized at all until the missing contract
exists. It also does not guard every direct child-retirement caller outside the
three host C-only call sites. Admission remains closed for that reason too.

Changed files:

- `HorseMod/horselib/deterministic/NativeReplayMaterialTaskGuard.hpp`: destructive-boundary receipt and explicit missing-producer state.
- `HorseMod/horselib/deterministic/Sc6ReplayHost.hpp`: destructive-boundary selector on the existing check.
- `HorseMod/horselib/deterministic/Sc6ReplayHost.cpp`: atomic sticky failure flag.
- `HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl`: three C-only vetoes, retained failure reason and sticky render-command rejection.
- `tools/replay_vfx_completion_observation_selftest.cpp`: concurrent and same-thread raw-borrow cases through extracted production retirement decisions; forwarding and post-failure command/release observations.
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`: assertions and existing extractor update.
- This evidence file. No status-file edit or commit.

Exact retained receipts:

| Experiment | Result and observation | Immutable evidence |
| --- | --- | --- |
| Concurrent raw borrow before the first production change | RED: `held=1 builders=0 tasks=0 destructive_calls=3 B_retained=1 commit_decided=0 failed=0`; provider, both builders and both callbacks forward once | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/2217be618a19b9c0eb76c7c8aab0f1770db7816f82188670a08c16ef919d272c.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3399390d277cdda05690e00354c57dad64e460ae4a8497c89067eee812657603.log) |
| Same-thread raw-borrow reentry before the first production change | RED: the same three destructive calls while the producer stack retains its raw borrow | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/688820e0ab0dc2264756985196b00c4ba64287cbd05941d9ebdd026285c06927.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/12a5c93c57b3c83c16fed3f396b6435ceccb9826a044f6dd2995a18354184636.log) |
| Post-veto render-command predicate before its fix | RED: `destructive_calls=0 failed=1 command_allowed=1 release_allowed=0`; clear CPU counters still allowed a command after terminal ownership failure | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c9642f61d66a56d71c73924e9971c1daf426d4724ea39c726544702ff5703522.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/21d459021e911541cccbd5f0e7b900acf3cb1f9b6bd0311c1958c8edd59abe88.log) |
| Final affected selection after all production edits, including atomic flag | GREEN: 34 native-contract cases. Both raw-borrow schedules assert `destructive_calls=0 B_retained=1 commit_decided=0 failed=1 command_allowed=0 release_allowed=0`; forwarding remains provider=1, builders=2, callbacks=2 | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/537eb8dfe91297a17c46d1ca8ab936ebf7f18c552a323dc20aa358754c6aac46.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3dce0029c5350545dda7b10e01b95306ee7ee2de0e9fed740a1f108de0d7cf57.log) |

The first adjacent selection stopped after nine passes on its former CPU-idle
control, which expected three native retirements without a producer census:
[failure manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/bcf9009b26576025a886c61f24eb31809875d13f0dcf9770b3848f4820570474.json),
[raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/93dad7f7657ede5a47dd8d8268c4551a5da9046132ade3502e1a8ca364520e91.log).
The control still proves its isolated idle CPU/render settlement; its retirement
expectation now asserts the missing-coverage rejection and retained B. Zero task
builders are not proof of a material-free producer domain. No historical
positive-admission assertion was removed or made artificially green.

Final production/test contents are reconstructible from
[source-7ac77b15…](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-7ac77b15d3fe7ea4d18c5039355ee0e9508385dfea439d15c813ff5bd3e4b814.json).
The source fingerprint at that run is
`be2c3b76e0bcbc305fc15bc37ca60e35d038a67f9a373d6c5496c18ca806de34`.
The evidence document was added after that run; it changes no tested source.
Earlier manifests retain their own reconstructible pre-fix source contents.
No production DLL was rebuilt in this slice; recorded binary identities are
the prior build, not validation of these edits.

Only `python tools/replay_test.py local --layer native-contract --test ...`
was used. The final selection comprises these nodes in
`tools/deterministic_qualification/tests/test_vfx_completion_observation.py`:

- `test_selected_trace_mid_borrow_blocks_internal_c_only_retirement`
- `test_material_builder_between_clear_and_native_retirement`
- `test_corrected_capture_material_failure_is_terminal`
- `test_material_cpu_tasks_block_host_render_settlement`
- `test_material_task_guard_controls`
- `test_material_task_guard_startup_rejects_incomplete_coverage`

Root's remaining native questions are narrowly scoped:

1. Verify the exact Win64 prototype, entry prefix and ownership of 1408D5840 for a dedicated pre-GetMaterial permit; close its direct/indirect callers rather than assuming the broad callback executor encloses them all.
2. Identify the provider replacement, reset/reinitialization and native MID destruction entries that must participate in the same exclusion, including ownership/replacement of +F8/+100. Define same-thread reentry disposition before allowing any native teardown.
3. Establish transaction-linked completion for +F0 deletion tasks, both native fences, deferred resource queue/delayed storage and GPU consumers. Builder/callback return and the particle query remain insufficient. Complete B material/proxy reconciliation remains unimplemented.

Observer validity: 34 scoped local cases pass; G1 remains open. Simulation,
required native lifecycle, coherent complete-B recovery, normal rendering and
performance were not measured. No game, deployment, broad suite, Ghidra edit or
production build was performed. The runner reports cleanup unknown; all local
fixture commands returned and no game process was launched by this work.
