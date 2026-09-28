# G1 C18 combat arm: exact BattleManager identity — 2026-09-27

The arm's exact class check is corrected and selected-local tested. Root owns
the project build, full suite, preflight and any next live experiment. This
slice performed none of those, no Ghidra/status edit and no commit. It adds no
ownership, completion or rollback admission. See the preceding
[combat-selection evidence](rollback-g1-c18-combat-selection-2026-09-27.md).

## Live failure and native evidence

Root's run `replay-268992ef0a1544d4b72bdf21f1d42127` failed at the frame170 arm:
[raw game log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/aefb3631c4251b3e3189e007f6746eb0e8ae66429e94e9e8a32ed47c8adff964.log),
[capture report](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/1f8712ad652ed7a17554c4ffbc32d1e85b6b3234c0322ae18621c7295cadefe4.json),
[raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2ac9ee3574bc3360da433e124487cc821923cec96e3528608450a789fa432495.json).
The causal report is `native observer failed: c18_combat_arm_rejected` and the
sidecar says `arm_identity_or_phase`, armed=false, prearm_callbacks=1, with no
selected C18 witness. Frame170 boundary samples show source active, round0,
manager phase2, round state2, world mode2 and round-frame7. The runner's unrelated
early GameImGui line is not the causal failure used for this correction.

Root verified through the existing Soulcalibur Ghidra program that
`InitializeALuxBattleManagerObject` at1403DC7F0 installs vtable14327AA20 via
the LEA at1403DC817. Table143356F68 identifies ALuxVFxInstanceManager, the
separate object at BattleManager+508. The bridge passes its `FindBattleManager`
object to `C18ArmCombat`; both that production check and `SetupCombatContext`
incorrectly used the VFX table. The local regression reproduces this specific
defect. The grouped live failure does not prove it was the only live blocker.

## Narrow change and audit of other uses

The only production behavior change is
`g.manager.table == base_ + 0x327aa20` in `C18ArmCombat`, replacing the VFX
table constant. Indexed identity, player binding/tracker, guarded double
samples, exact frame/epoch, phase, thread, pre-arm exclusion and all other
rejection checks remain. The grouped `arm_identity_or_phase` label is unchanged;
no additional reads, fields, hooks, permissions or memory are needed for this fix.

The fixture independently seeds the native BattleManager table, and two negative
cases seed VFX/unknown tables with otherwise valid indexed combat input. All
three execute the actual extracted bridge, native arm and callback owner. The
negative cases still forward the native callback once and expose no selected
receipt. The positive case forwards setup then combat callbacks, preserves
LastError, keeps setup unselected and records arm170/selected177. The ordinary
provider census remains invalid and the separate shape output grants no
ownership or completion.

The source audit distinguished these other3356f68 uses:

- `NativeReplayVfxCompletionObservation.hpp::Before` and `Snapshot` check the
  separate VFX delegate container at manager+388 (and the independent attachment
  fallback). Their three checks and generic fixture manager are unchanged.
  This agrees with the retained [VFX lifetime native evidence](rollback-g1-vfx-listener-lifetime-boundaries-2026-09-24.md)
  and [BattleManager+508 route audit](rollback-g1-vfx-complete-b-route-audit-2026-09-24.md).
- `C18CaptureMaterials` separately checks `l.battle.table` against3356f68 after
  the world-registry lookup; `SetupMaterialCensus` and its multibucket registry
  seed that same table. These were inspected and left unchanged in this
  arm-only correction. **They are a likely additional mismatch, not verified
  VFX checks:** the retained [material native contract](rollback-g1-finish-material-native-contract-2026-09-27.md)
  describes `1403EF7A0`/`142018870` returning the registry's battle-manager
  pointer, followed by character-array reads at+390/+398. Root should bind that
  selected registry target to its exact native class/vtable before the next
  census correction. The falsifiable local experiment is to seed that registry
  target with327aa20 and observe whether `target_unresolved` results before a
  separately justified fix. Compatibility GREEN below preserves the old
  synthetic registry contract; it does not establish its real-world identity.

The16-row,256KiB C18 and4MiB observer bounds are unchanged, as is the1GiB
production ceiling. Complete B, all six material hooks, callback forwarding,
ProducerUncovered and HistoricalRestoreSupported=false are preserved. Native
retained memory remains unknown. No live recovery or G1 pass is shown.

## Retained RED/GREEN

First, only the fixture/test changed. With327aa20 input, the real native arm
rejected; the fixture exited90 with `c18_combat_arm_rejected`, sidecar
`arm_identity_or_phase`, armed=false and prearm_callbacks=1. This is a native
production-boundary rejection, not a fixture compile failure or native crash.

| Selection | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
| BattleManager class, before production fix | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/6a71d19dae2147315acf225c82d18dcec96c124178aab5f874aa50e187ca7b9f.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/d5df9d3c5cf2450c71613a77c5e1d4b219381414b936dda6045bd25204f43ba9.log) |
| BattleManager/VFX/unknown classes, after fix | GREEN, 3 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a8211889fc1126fb053f04e277708531a0ec5cdbbd25eb02edc21d7dbe2f7c7f.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/042bd4d1066e0cde28a83860567ff637658a1a8fcdf2a803c47260aaa1033fa3.log) |
| Affected combat/zero-serial/census/VFX cases | GREEN, 63 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/770b377431d49846108c32b519d9dc86250a17c30fede894b6dfedadd11486dd.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/78ba000ba91c2f16c6bd84fd9e551c0e2f701419b2fa5bf205e2a21a93ffb10b.log) |

Retained native fixture output/publication:
[RED stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/517c2643b5ad926e0ef7f1c0bbd753ebc0ef92a62ffa744fe0bace4d28f19183.log),
[RED sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/dbf8d618347db1e845801cf8b67e7a17e8a8ef6c27abd9643b404adc6fb89da3.json),
[GREEN stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f9f4ce7991dfc83831ef731d575042d9b68c33fd06f1c60d0b63afd909fcc2dc.log),
[GREEN sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/430c72f9fce8a3b7eee3d08926a440993b3d0260d5715ac2f24b593419186c20.json),
[rejected VFX sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6f93d6529ad6b616c87d7e9f4473aef06c5dcbf94abfc8e6e73c7ba90c6ee3ff.json),
[rejected unknown sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/128edd8fc8496b8b312b005893ac90387ad047029324416c81f4a3d5bb8cd05b.json).

Reconstructible source manifests include dirty/untracked production and tests:
[RED217777135c718c54aa0594e4b64d8fd57cb4a3239f7cb027f71ea6a23db0069d](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-217777135c718c54aa0594e4b64d8fd57cb4a3239f7cb027f71ea6a23db0069d.json),
[GREEN94b08313f89ad6cf681c38bb178353cdf4be93b20d37736c200e564a426ce5e1](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-94b08313f89ad6cf681c38bb178353cdf4be93b20d37736c200e564a426ce5e1.json).

Exact commands, in order:

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager[battle]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls
```

Changed paths: `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`,
`tools/replay_vfx_completion_observation_selftest.cpp`,
`tools/deterministic_qualification/tests/test_vfx_completion_observation.py`,
and this note. The source-retention comparison against the previous checkpoint
found exactly these three code/test paths changed. No diagnostic run was retried.
