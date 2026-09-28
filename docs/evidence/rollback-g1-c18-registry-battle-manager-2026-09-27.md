# G1 C18 registry target: exact BattleManager class — 2026-09-27

The registry-target class check and its two receipt readers now require
ALuxBattleManager. Selected local regressions pass: **6 new class cases and
73 adjacent cases**. This is an observation correctness fix, not ownership,
completion, historical admission or G1 recovery. Root still owns project build,
full suite, preflight and live testing; none ran in this slice.

## Native evidence and scope

Root independently verified these identities in the existing SoulcaliburVI.exe
Ghidra program:

- `GetBattleManagerFromWorldContext`1403EF7A0 calls
  `FindWorldContextRegistryIndexByWorld`142018870 with the exact UWorld key and
  returns the matched registry row's+08 ALuxBattleManager pointer.
- Callback1403C5360 reads that object's selected-character array at+390/+398.
- `InitializeALuxBattleManagerObject`1403DC7F0 installs14327AA20 at1403DC817.
  `InitializeLuxVfxInstanceManagerDefaults` installs the distinct143356F68
  table at14085EDAE.

See the retained [native material contract](rollback-g1-finish-material-native-contract-2026-09-27.md)
and [preceding arm correction](rollback-g1-c18-battle-manager-arm-2026-09-27.md).
No Ghidra edits or new native investigation were performed by this code slice.

The preceding combat run `replay-268992ef0a1544d4b72bdf21f1d42127` failed at
frame170 with `c18_combat_arm_rejected`/`arm_identity_or_phase`, before selecting
a C18 witness: [raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2ac9ee3574bc3360da433e124487cc821923cec96e3528608450a789fa432495.json).
It did **not** reach this census defect. The present RED is local production-boundary
evidence, not a claim about a measured live registry target or recovery.

## Exact implementation and audit

`C18CaptureMaterials<Diagnostic>` now requires `l.battle.table == base_+0x327aa20`
after the exact world-registry lookup. Its indexed sample, array bounds,
descriptor, selected fighter, trace root/world binding, guarded double capture
and all other predicates are unchanged. The rejection remains `target_unresolved`.
Both ordinary and zero-serial readers in `replay_control.py` require the same
exact table; neither accepts an alternative class or skips the check.

`SetupMaterialCensus` now seeds the verified BattleManager table for its selected
registry row and other multibucket registry targets. Negative cases change only
the selected target's native vtable to VFX or unknown, retaining otherwise valid
indexed input. The actual callback owner and census produce the receipt; the
fixture supplies no replacement census or validity decision. Separate reader
negatives consistently change all published BattleManager tables and verify
rejection clears stale receipts.

The directly relevant audit found no other registry-target class checks. The
three genuine3356f68 checks in `NativeReplayVfxCompletionObservation.hpp::Before`
and `Snapshot`, the generic VFX fixture manager, and the existing arm's wrong-VFX
negative case are unchanged. The previously corrected combat arm still requires
327aa20. No hook, field, allocation, schema version or limit changed.

The tests preserve exact native callback forwarding, incoming/outgoing LastError
66c/77c, unchanged native input and zero reads after callback-return revocation.
BattleManager input yields four ordered MID shape occurrences, each recording
320 vector capacity bytes and primary/secondary proxy occupancy. These are not
owned allocation totals. Zero-serial input keeps ordinary census invalid and
labels only the separate diagnostic sampled, generation_unknown=true. VFX and
unknown classes reject with no rows in both paths. Ownership/resource completion
remain false, and total retained native bytes remain unknown.

Complete B, six material hooks, ProducerUncovered C-only veto and
HistoricalRestoreSupported=false are unchanged. The16-row/256KiB C18 and4MiB
observer limits and1GiB production ceiling remain unchanged. No serial allocation,
native material call, lease, GPU completion or lifetime protection was added.

## Retained RED/GREEN

The first run changed only fixture/test input: native327aa20 reached the real
census and failed `target_unresolved` with no MID header reads, after forwarding.
After the native one-constant fix, the next run stopped at its first failure:
the real census emitted four rows, but the reader still expected3356f68 and
rejected the identity. That specific failure justified correcting the two reader
constants. No unchanged failing experiment was repeated.

| Selection | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
| Native BattleManager registry, before production fix | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/853db14b4e57434c6eede2f2b50dcb1989a9be7697fd026e9bcc76bc0bc1242e.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6c5c8e9828c73880fc2fdf327e619c5a5fa013869e71abea0a70cc4e9ed1ed63.log) |
| Class cases, native fix with old reader | RED, stopped at 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0e51f7ea701c1b0432205e654357acc92d263e6a8c19d7597128c1699c9a9bc9.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/dc07ef782b801f8d85840b9c40b0729da216fc957606b44842f9a390b50aec00.log) |
| BattleManager/VFX/unknown, ordinary and zero-serial | GREEN, 6 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/767367e51e6a3732fb82dd0565f339eb82bf026c5ebb3588a8b12f267150699e.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5adbce96ed8d7d4b80556c4f081c7e8c11cb2d3b8f533ea7b5af2f3c4b519e91.log) |
| Adjacent census, shape, zero-serial, combat and VFX forwarding | GREEN, 73 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/fc502568ccf6eb33d7c327c093c843fd7dbb08e0b701666786224f7d8fc77290.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2ec857148355a5e38652f2a7ba1f6b41f441707223b8dab53bfa56319bf79861.log) |

The initial native RED also retains [fixture stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6437533f9dcc35ce80508228aaa477278c91fc31474441f3c92b1e781a130c8b.log)
and [raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e4f7795a86667efa15b159b47123eac57d4cc70865c96336549e7ed2f8ddcc76.json).
The [fixture receipt index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9102bdbee57440fa5c7712a407c3ee7882e5cd968c4f90ef53a6f483dcf3ef9a.json)
links immutable raw stdout and sidecars for the reader RED and all six GREEN cases.
In its GREEN session3308, case0/1 are BattleManager ordinary/zero-serial,
case2/3 VFX, and case4/5 unknown.

Reconstructible dirty/untracked source snapshots:
[native RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-91c6eb91580c259b99f204ba1129818d4a4daa26a0c6dca3584dab8c59690acb.json),
[reader RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-82d676b2f2e2d679c00e535ddb30028daf9bf6043a90aeb5cbbf16c73d0e8415.json),
[final GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-b4176eaf5e4b55a36a16e46c2f9d6acff0c87cccacee1de2a62c7b3da54c299a.json).
Comparison against the preceding arm source94b08313 found exactly the four
code/test paths below changed; unrelated retained source is unchanged.

Exact commands in execution order (the second command stopped at reader RED;
the third followed that specific correction):

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager[battle-ordinary]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls
```

Changed paths:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- This evidence note.

No project build, full suite, preflight, live run, deployment, AGENTS/status edit
or commit. Selected passes do not qualify G3. G1 remains open; no live census
result, retained-memory bound, ownership/completion or recovery is demonstrated.
