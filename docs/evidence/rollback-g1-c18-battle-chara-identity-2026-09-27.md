# G1 C18 receiver and selected fighter: ALuxBattleChara — 2026-09-27

The exact C18 receiver and selected-fighter class checks, fixture input and
receipt readers now use ALuxBattleChara table143268078. Two production-boundary
REDs are retained, followed by **10 new and80 adjacent selected GREEN cases**.
Root owns the next project build, full suite and any live trial. No such gates,
Ghidra edits, deployment, status edit or commit ran in this slice.

## Latest live result and native basis

Root's bounded run `replay-d0f5b0785ebc45b0a604768b2672c374` passed its diagnostic
run/cleanup scope. It armed at frame170 and selected/returned C18 at frame172,
round-frame10. The separate diagnostic rejected `listener_receiver_vtable`;
ordinary census rejected `entry_invalidated`, with no material rows. This is
neither material observation success nor ownership/recovery. The receiver's
actual live table was **not retained**: the result only proves it failed the old
326b8d8 check after passing the preceding identity predicates.

The working stage report was copied into immutable evidence before editing:
[report](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/92beb380634fbd6487966ae6437d4ae73a5c1c9fa42cd1c09f431a3483a2cd6e.json),
[raw game log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9169b36d3965bc2f822c74b8b6485cd33b57223ecc93d533eeaa8eb29d4dc15a.log),
[raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/32efc8b5d131f1a45a9866a126f46c1dd163373228845edac12116a9a13e9488.json).
The report records cleanup complete and zero games remaining; root reports
Steam was kept. No new cleanup or live experiment was performed here.

Root independently verified in the existing SoulcaliburVI.exe Ghidra program:

- `ALuxBattleChara_BeginPlay_G`1403B54A0 binds `param_1`, the character itself,
  through AddWeakUObjectCallbackToCollection for collection0x12 and callback
  `HandleLuxBattleCharaTraceActivationEvent`1403C5360.
- That callback resolves BattleManager through the receiver's world, selects
  the character from BattleManager+390/+398 by descriptor index, then reads the
  selected character+458. Receiver and selected fighter need not be identical.
- `ALuxBattleChara_Constructor`1403AB8D0 installs143268078 via LEA1403AB8F1.
  Distinct14326B8D8 belongs to ALuxBattleVFxEventHandler, installed by its
  VTableHelperCtor at1403AC032. Both tables' slot+138 points to141C204E0.

These registration/constructor facts justify the narrow3268078 class hypothesis.
They do not measure the latest live receiver table. This supersedes the former
character-type assumption in the C18 fixture/reader while preserving the prior
[BattleManager correction](rollback-g1-c18-registry-battle-manager-2026-09-27.md).

## Change and regression boundary

`C18CaptureMaterials` now requires `base_+0x3268078` for both `l.receiver.table`
and `l.fighter.table`. Both ordinary and separate zero-serial readers enforce the
same exact identities. VFX-handler and unknown classes are not alternatives.
The receiver's existing failure reason is retained. The selected-fighter class
predicate now emits diagnostic-only `selected_fighter_vtable` using the existing
failure-string field; ordinary census still returns `target_unresolved` there.
The predicate order and reads are unchanged. No raw rejected object fields,
new buffers or post-return native-page accesses were added.

The existing fixture seeds distinct indexed character receivers and a selected
fighter, positive weak receiver generations, the real-shaped world registry and
verified BattleManager table. Both native-known tables receive their shared
world-getter slot, so VFX-handler rejection cannot be explained by a missing
fixture getter. Negative cases replace only a role's vtable in native input.
The real CallbackExecutorDetour/C18Before owns capture, serialization and return;
the fixture does not provide a census or permission. Reader negatives separately
corrupt each role's table consistently in actual production publications and
verify rejection clears prior receipts.

All10 cases forward the selected callback once, preserve LastError66c/77c,
leave native input bytes unchanged and attempt zero native reads after callback
return/page revocation. Valid input yields four shape occurrences; zero-serial
input keeps ordinary census invalid and its separate receipt generation_unknown.
Incorrect receiver/fighter classes emit no rows. Adjacent tests cover weak/indexed
identity failures, changed samples, bounded capacity, unreadable input, world
registry lookup, prior BattleManager class corrections, combat selection,
first-occurrence/return-read guards and genuine VFX forwarding.

The BattleManager327aa20 and actual VFX-manager3356f68 predicates, native getter
identity, weak/indexed generations, world/registry/provider/writer checks, all six
material hooks, complete B, GPU/deferred-retirement guards, ProducerUncovered
veto and HistoricalRestoreSupported=false are unchanged. No lease, serial
allocation, callback suppression, native material call or ownership was added.
The16-row/256KiB/4MiB bounds and1GiB ceiling remain unchanged. Native retained
memory is unknown; ownership/completion/admission claims remain false.

## Immutable RED/GREEN receipts

First, only fixture/test changes seeded3268078 for both roles: the actual
diagnostic rejected `listener_receiver_vtable` before any world lookup. Correcting
only that receiver predicate produced the second RED, `target_unresolved`, after
one world lookup and before any MID header read. Then the selected-fighter check
and both readers were corrected. Each RED stopped at its first failure and was
followed by a specific source fix; no unchanged live retry occurred.

| Selection | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
| Both roles3268078, old production checks | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ef349c02406f8d6d9e5e952d6721704477f342252cba89522b8a73732f69d690.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/cfef3f66a96c3ae55924bcb383e3a36ee3bf2725185c2b5b1df99f9d688221b1.log) |
| Receiver fixed, old selected-fighter check | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/29f82769b5ae4064ecd81dbfb9e385cf5d466a270d9f91e1ba5f370550b07cb6.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/27f53ff77ae0bc161d2ddc9fc3b9a00c9ecacd2f49bab79a30fbbeb7ec9c57c5.log) |
| Character/handler/unknown role cases, both receipt paths | GREEN, 10 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/68a35df08e30a3e8f08935fad33e33620de54f2f5ec0b252287ac576445ca028.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4467a85875cac2e3384ed0a150a60df2cb60cc32e0b80b1a4192f9fe2cb75354.log) |
| Adjacent affected cases | GREEN, 80 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/8dc1e48d3a315a59c9610b3cc1a00ecfd68a82b1b3a519b62a0ed8e3afd3a529.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6c03ccd27399b7cdb9bbdde510749693cf60c8a387d38e9843145423315da8d3.log) |

Raw fixture receipts:
[first RED stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/32604087791b11f08ce6b133f77950420f1cd79fd35d757ebe1c56ceaa292ff8.log),
[first RED sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/94668622ca7a2bd05ac0deec136b239cac708af62b48241448d6893854f8837d.json),
[second RED stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/bca4992f7c491647326bc16e9a28a6986c567c0c39987c28e59c3cd9622a896d.log),
[second RED sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/cb146f808ac23166c37a37e0cd4e92d72853acd1195067bc3a4960cd77c6a10b.json).
The [GREEN fixture receipt index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/04546e6f802888d13846340b41fb252fcaa09cb4271e3df771d9c7cf5a21bd7e.json)
links raw stdout/sidecars for all10 cases. Pairs0/1,2/3,4/5,6/7,8/9 are respectively
valid characters, receiver-handler, receiver-unknown, fighter-handler and
fighter-unknown; even cases are ordinary and odd cases zero-serial.

Reconstructible source, including dirty/untracked code and tests:
[first RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-abb171a328e6ca5f23ed2c6bd1211ee3cedad9be49adcd73271869bc4f46ec23.json),
[second RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-06c2877eade24410a99578084d6f833fa6d7581c894de1183aaec59c4507a8b0.json),
[final GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-abceddbfc43048544df9fb310a1003be5dad53d90b5c1518a43730cb1b1d8bd7.json).
Compared with preceding sourceb4176eaf, exactly the four code/test paths below
changed. Source for the other retained implementation is unchanged.

Exact commands in order (the repeated first selection follows the receiver-only
fix and deliberately exposes the still-incorrect selected-fighter predicate):

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_receiver_and_fighter_require_battle_chara[good-zero_serial]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_receiver_and_fighter_require_battle_chara[good-zero_serial]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_receiver_and_fighter_require_battle_chara
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls
```

Changed paths:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- This evidence note.

After root's required gates, a later `listener_receiver_vtable` rejection would
falsify3268078 as the exact class of that selected live receiver;
`selected_fighter_vtable` would isolate the analogous target-class mismatch.
Success would establish only entry-visible shape observations in that occurrence.
No current live shape result, resource-memory bound, ownership, completion or G1
recovery is established. Selected local passes do not qualify the full suite/G3.
