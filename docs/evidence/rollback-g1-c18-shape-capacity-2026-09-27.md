# G1 C18 bounded shape capacity — 2026-09-27

The existing selected C18 owner now samples up to **192 ordered override-MID
occurrences**, using compact references to its already captured slot topology.
The real production-boundary145-occurrence case was RED before the change and
is GREEN afterward;192 completes and193 rejects. **115 selected native-contract
and16 workflow cases pass.** No project build, full suite, preflight, live game,
deployment, Ghidra/status/AGENTS edit or commit ran here. Root owns those gates.

## Latest live scope

Root's `replay-be3a2e90707142e6bf9d4d77b3e4e4c5`, runtime SHA256
`f4e9b23fe7f133eb79ace361395e72bf8433c16e4dc8b266cba3b72082fe18fb`,
armed at frame170 and selected/returned C18 at combat frame172. The prior
[TraceEventHandler correction](rollback-g1-c18-trace-event-handler-2026-09-27.md)
passed the formerly failing receiver predicate. The version3 zero-serial
diagnostic then rejected `detail_capacity`:145 matched occurrences,129 omitted
at limit16, no rows. Ordinary census remained `entry_invalidated`. These counts
do not establish145 unique MIDs, owned bytes, exclusion, completion or recovery.

Immutable [report](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b301acb031f1c33a3bf9f4fdab68b6d12d5083c51cff990fd67db518e92a133e.json),
[game log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7e693642a25ae890476cc32c83337543f804224caab039fc810970d70f9741b4.log),
[raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/81d518b53a1f2c8ece8c3291766634e9c7494c32af64d3f1ccb3627ca06e0f10.json),
[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/55f0c49b7f717bdd28fbfe7f18ed6c3f3f39b15eaf469983c92735b6b99d676f.json).
The retained sidecar is66,200 bytes with44 events,6 producers and0 dropped
records. Root reports prior build/two CTests/full ordinary local1133 passed;
the bounded stage/cleanup passed, zero games remained and Steam PID9240 stayed.
No unchanged live retry was performed here.

## Representation, memory and rejection semantics

`C18MemoryShape` retains the vector header, all3 raw proxy pointers and all12
raw cache pointers, plus an ordered material-slot occurrence index. The matching
`C18MaterialCapture` already holds the actor/provider/material indexed identities
and provider/slot association. Serialization resolves that index exclusively
through owned captured data; it performs no native reads after return. Repeated
MID pointers remain separate ordered occurrences, never deduplicated allocations.

`C18MemoryAt` still rechecks actor/provider/material identity and vector header
immediately after its guarded shape reads. Both complete topology captures and
both complete shape captures still compare for equality. Raw proxy/cache
pointer values are compared even when their occupancy bits are unchanged.
Only occupancy is published. No proxy/cache/backing is traversed, no virtual
material call or native serial allocation is added, and no hook changes.

Win64 accounting, verified by compile-time bounds and production fixture output:

| Retained storage | Before | After |
| --- | ---: | ---: |
| One shape row |240 bytes|144 bytes|
| Detail limit |16|192|
| Two shape captures, including24-byte metadata each |7,728 bytes|55,344 bytes|
| Other C18 storage, both topology captures and existing scratch allowance |187,950 bytes|187,950 bytes|
| Total C18 owned/scratch charge |195,678 bytes|243,294 bytes|
| Fixed serialization scratch |2,134,016 bytes|2,068,480 bytes|
| Process observer reservation |4,194,304 bytes|4,194,304 bytes|

The initial compact-buffer implementation passed the256KiB C18 assertion but
failed the aggregate4MiB assertion during the selected fixture compile. It was
stopped and retained. The specific repair transferred64KiB from the existing
fixed serialization envelope to cover the47,616-byte C18 increase; no assertion
was removed or weakened. Aggregate charged storage decreases17,920 bytes.
C18 retains18,850 bytes of its256KiB allowance. The existing aggregate assertion
still charges records, both buffers, serialization, scratch and pinned detours
simultaneously. The1GiB production ceiling and4MiB reservation do not increase.

The final145- and192-row native fixture publications are340,853 and449,868 bytes;
the separate existing512-record/four-row serialization fixture publishes644,397
bytes. These are measured local publications, not a universal serialization
upper bound or a combined live run. Exhaustion of the fixed2,068,480-byte buffer
still fails persistence closed. No partial JSON is published as a complete
receipt. The fixture's larger native-input graph and mirror also remain within
its own existing256KiB assertion; they are test inputs, not production retention.

The zero-serial receipt advances from version3 to **version4**, with
`row_limit=192`. Ordinary `memory_shape` advances from version1 to **version2**
with the same capacity; the outer material census/class contract remains
version3. Existing versions retain their original limits and class meanings.
The reader checks192 only for version4, validates omission arithmetic and
ordered provider/slot occurrence keys, and rejects row reordering, duplication,
changed alias identities and version/limit substitution. The actual live
version3 capacity rejection remains readable as rejected evidence.

Overflow stays explicit. The zero-serial193-occurrence case reports
`matched_count=193`, `omitted_count=1`, `detail_capacity`, and no rows. The
ordinary census retains its existing explicit incomplete-prefix behavior on
shape overflow; its shape `complete` is false. Unsupported/non-MID/asset slots,
1024-slot/128-provider topology limits,256-object diagnostic alias limit,
indexed/weak serial checks, reentry/overlap/closure checks and writer guards
remain unchanged. Native memory totals remain unknown and every diagnostic
ownership, resource/GPU completion and rollback-admission permission stays false.
Complete B, six material hooks, ProducerUncovered and
HistoricalRestoreSupported=false are untouched.

## Production-boundary evidence

The existing callback fixture supplies one native heap listener, two providers,
and145 ordered override occurrences alternating two indexed MID identities.
It does not fabricate a receipt. The initial RED reproduced the actual
`detail_capacity`/145/129 failure at16; callback forwarding was exact and
LastError stayed `66c -> 77c`. No resource/backing reads occurred and input pages
were revoked before return, with0 post-return read attempts.

The new eight-case matrix covers145,192,193 and five native-input changes
between captures: same-occupancy proxy replacement, same-occupancy cache
replacement, vector header, indexed serial, and a final-slot MID replacement
with counts unchanged. All negatives reject. Per-row assertions check exact
order, actor/provider/material alias topology, vector count/capacity payload,
all3+12 occupancy bits and unchanged false permissions. Reader corruptions are
offline negative comparison data, never input to native capture. Existing
overflow fixtures now supply196 occurrences so they continue exercising actual
capacity rejection, including the ordinary incomplete prefix.

| Selection | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
|145 native occurrences before fix|RED,1 failed|[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/90b50453c4259228a5c12113825a32083f6e60b3ad4e86f0f81167a051ff0289.json)|[log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2fb2cd67ac07d963a6a5a5a2298ef65b4024994f9edb6605812abd68680f65b1.log)|
|Initial compact storage, before serialization rebalance|RED, fixture compile aggregate assertion|[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/9cb79c1aeaad19873b920408dcb0310d28c50974b58e7ed23b56fc66ffae9a03.json)|[log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b156fcefa4251026e50f2c89d40b0f9d72773a4b1091381730b874ff63f4da9d.log)|
|Capacity/native mutations/reader checks|GREEN,8 passed|[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/069dcbf3d289cefc76f5cda505f85062d832b65cfcf493d3abb1d758f6a16857.json)|[log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c41a8038362b9139668cbfa4f60d3470820b7ddb5a49ddd7d029a5c5728387a8.log)|
|Adjacent C18 guards and existing serialization bounds|GREEN,107 passed|[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/12cc748f86a92d464adea5a16de878229affa2d6c50f676b604d1e1b1974c9bf.json)|[log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5b028159bb4995742d5367075630239ee9127eab9d4197ee792575c4733fe80f.log)|
|Actual live capacity and legacy rejected receipts|GREEN,16 passed|[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3f897c5603733fc6c39cffa0e75daa04cfa393ae3ae7775cc9b754ec2c84f410.json)|[log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/ac188113d7aa29b09977570c3113c7911fdac14c7be067a828e6dd3b2138d6c9.log)|

Additional immutable [RED native stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/759f2838bdcace35a352731ae27d4cf22c8721562785c2f2ba10c5e10740ecb9.log),
[RED sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3e7c0fa38eee576a436c61d1c97c72fad99372dff6543e086d72a78116872271.json),
[eight GREEN raw fixture stdout/sidecar pairs](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/10288a2594a9c23bee0fe42b0b574389273a837fad7b1debcc677227849b7b2c.json),
and [512-record serialization publication](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/06e824ca3869a87c268022b9d018485baa82d0c051f2aee17379d538c548f347.json).

Reconstructible source snapshots:
[behavioral RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-65ed91396cb576baf7b653efdbcee677e9de9905c2a654a69304f964bdced7dd.json),
[aggregate-assertion RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-27b8bd675733f696dba4ce00213203c72097cafa9e125bdd65e7b77edacff0d7.json),
[final GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-4cdb7940eed6ea218693bd5d67261fde8a73a7470554bb7300fca2df9ebb90fe.json).
Compared with preceding source9d467640, exactly these five code/test files differ:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.hpp`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`

This note is the only additional authored file. Exact test commands, in order:

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_material_shape_capacity[observed-145]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_material_shape_capacity
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_material_shape_capacity
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_rejected_type_sample --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_trace_receiver_and_chara_fighter_types --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_shared_capacity_full_phase_and_forwarding --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_measured_window_capacity_and_durable_sidecar
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_live_shape_capacity_rejection_keeps_legacy_limit --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_measured_trace_receiver_legacy_rejection --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_rejected_live_receipt_stays_diagnostic_only
```

The source/tests are ready for root's build and full suite. After those gates,
one changed bounded combat diagnostic can determine whether the actual native
occurrence now completes both shape samples or exposes another rejection.
Neither matching samples nor additional capacity establishes a lease, native
memory upper bound, callback exclusion, material publication, C-only retirement,
GPU/deferred completion or G1 recovery. No post-change live result is claimed.
