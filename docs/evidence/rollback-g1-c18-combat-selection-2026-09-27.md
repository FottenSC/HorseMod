# G1 C18 combat-window selection — 2026-09-27

Implemented and selected-local tested; ready for root's project build, two
CTests, full ordinary local suite and preflight. No build, game, deployment,
Ghidra edit, status edit or commit was performed by this code-writer slice.
This is diagnostic selection, not G1 completion or an ownership capability.

## Why this experiment changed

Root located the preceding selected return in setup at frame23, round-frame0,
between the actor tail and next input publication, in
[immutable live log, line1515](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/64308ada0370894dcedd43a5ffdc65ff6b400a6225cddb7045e2de88d79035ed.log).
See [root's inline/live evidence](rollback-g1-c18-inline-live-2026-09-27.md).
That run established early observer behavior only. Neither combat callback
presence nor inline storage was established. The prior six listener failure
subreasons are preserved exactly; no listener identity rule is relaxed here.

## Production boundary and scope

- Observer installation/startup remains before replay/level loading. A dedicated
  start export selects combat gating atomically before enabling observation.
  All existing registration/writer observation continues before the arm;
  arming never clears its history or counters. Generic observation retains its
  existing first-occurrence behavior.
- Strict request protocol19 requires `c18_diagnostic=true` and
  `c18_selection=combat_170_220`, trajectory mode, watch360, skip-intros and
  include-setup. Protocol18 cannot silently become a combat request. The
  existing stage, startup flags, opt-in sidecar parking and cleanup are reused.
  No scheduler preparation, executor, seek, mutation or new hook is requested.
- `ObserveReplayTrajectory` calls `ObserveC18CombatSample` after the actual
  payload/initial-round verification and successful native trajectory read,
  before duplicate-sample suppression. Only exact frame170 in active round0,
  manager phase2, round state2, world mode2 and positive round-frame can arm.
  Missing that boundary fails; setup callbacks cannot consume selection.
- The native arm records run, thread, sequence, frame/epoch, indexed manager and
  player observations and the reflected player-property slot. Arm and C18
  entry independently take two guarded samples of the same small input set
  already used by the trajectory observer. They check indexed pointer/table/
  serial, player binding/tracker, active round/phase, frame170..220 and epoch.
  Entry requires the arm thread and an epoch no earlier than the arm. Input
  disagreement is sticky rejection. Indexed serial0 here is an observation,
  not a generation lease. No native serial is allocated.
- The bridge polls only owned return metadata, after a fresh validated combat
  sample. It checks arm identity and ordering, thread, manager/player, selected
  frame and epoch. Frame221 is a one-frame return-observation allowance for an
  entry no later than220; no occurrence by then fails `c18_combat_window_missed`.
  Scene/replay/sample failure invalidates the native selection. The existing
  wall-clock timeout still bounds a stalled or never-armed run.
- The new `C18 combat diagnostic return` marker contains entry/return sequence,
  thread, collection/descriptor, arm sequence/frame/epoch, manager/player,
  selected frame/epoch/round-frame and observed frame/epoch. The live parser
  accepts only this marker for the exact run. After clean owned-process exit,
  it cross-checks these coordinates against `collection18_witness` and the
  separate `collection18_combat_selection` version1 sidecar object. Old setup
  markers cannot stop the new stage. Legacy sidecars remain readable offline.
- Native return bookkeeping and the metadata reader do not revisit borrowed
  C18 pages. Callback forwarding and Win32 LastError remain intact. Native
  callbacks are never delayed or suppressed. Matching observations prove no
  lifetime, synchronization, material publication or resource/GPU completion.

The fixed combat selection state adds144 bytes to the existing C18 reservation:
195614 bytes total, below256KiB, including the existing double capture and
scratch allowance. Detail remains16 rows and total observer reservation4MiB;
production ceiling remains1GiB. The fixture maps one additional MiB of its
synthetic executable to cover the existing world-mode RVA; this is test input,
not retained production native memory. Retained native memory remains unknown.
Complete B, HistoricalRestoreSupported=false, ProducerUncovered C-only veto,
all six material hooks and all ordinary C18 eligibility rules are unchanged.

## RED and GREEN receipts

The pre-fix test drove the real callback executor before any combat arm. Its
legacy selected-return reader returned `ready=1`, callback count1, exit89.
This is a behavioral RED, not a missing export or build failure. The fixed
fixture additionally executes the real bridge sample method at setup frame23,
arms at170, and invokes another native callback at177. It records `ready=0`
after setup, then arm sequence2, entry3, return4, frame177, epoch707, forwarding
count2. Ordinary provider census remains invalid; separate zero-serial shape
rows carry no ownership/completion claim.

| Run | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
| Pre-fix setup selection | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ec5cb90fd7230ba25c188a026049b6317f8caf624b1173c50757f542eb0e4fd7.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3538b0954da22aaa044d20aa87a608e481182013fce5cb6ceb4f7bee4b7073c8.log) |
| First implementation attempt | fixture failure, stopped | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/7b143af6194b428401f9e9160734c132949acbcaac64a9c131d28ff5a4da41ae.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6bf40ea49f81ec419cf103c57f2eb86c8d516f5b0bbb1e943aee2260d2246e07.log) |
| Combat/zero-serial native cases | GREEN, 56 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/918f0253a94f6c8ee0c6a3e62cb4d802ea95ee5695b95ea4f8c8e3ff45c65427.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2eebf6dd4eafe23f56fdc04501bce3ae8de81c53bf5f429521e14974e1cf4628.log) |
| Protocol, marker, cleanup workflow | GREEN, 30 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/4cf896a0dddbdf0da4af3e455ea4ea740bb47b3b0519314e915d5ad06caeb57a.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/bd60dc26417626b9c8f98600a4253fd9b7fb1ee67f74c33c8e967204d025e2f0.log) |
| Entry sample race and wrong thread | GREEN, 2 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/14747ddc8f1489f36166a259299cff7680f9caa25d372b5142c45911040870e8.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b46ef4c2b7ee4c7514b30f4c1be3ed6330cd7590f47f9ddf5d43cdc3c7646bae.log) |
| Generic C18 compatibility | GREEN, 12 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f346a2a0623d8ae1c0c1ddffd62f67cc7a4d824699a9565e90f11b7728421a21.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/374a7cee9e3b8940beedf9a66a7309b46732049f574302f88ab1a962f771f469.log) |
| Final reader missing-coordinate hardening | GREEN, 1 native + 20 workflow | [native](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c73728377dc5d5908dcf7a62224ec59fc813ca6f11f07bd6f8ec196e577b7192.json), [workflow](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/2538dccc3a4b3c24b88f0fab71928bc340c6b5371678f114d2f2e209f713ca0d.json) | [native log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/8ff85f0ad03bc8765b9fe804616c7b6dabb8209ee926d5578f983134ce6203fe.log), [workflow log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b858f677732a810f35a5a75fcd0be14cca719af8e0f244d6ffe9b06eb5976a7d.log) |

The implementation-attempt failure was the fixture resolver's `bool`-only
export extraction omitting the new uint64 arm and void invalidation exports.
That extraction was repaired specifically, with unbuffered failure output;
the identical single node then passed. No native behavior was bypassed.

Retained native output: [pre-fix RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/8a97e30ddabd633a94b240d05a958daabb13df13c935914693ad10f3a352fa30.log),
[final GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/2d1c374b5d8841db75ddde66fbeabef12605c88aef69fa0801d77e64aec91710.log).
Retained publications: [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c0a953d30216a6479d76824680a59eebb0ffd7f155c9e312b4f08f9ee07deb76.json),
[GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3dd3631ea5aa2daea259bad6942eecfdd4980333947afb81d441a5728c263a74.json).
The manifests retain exact pytest commands and reconstructible source,
including untracked production/test files. Pre-fix source manifest:
`099b9e8f3a4f15a9cdb61eee3720e80128a96dc86e3d66f18de6308ccfac78d3`.
Final source manifest:
[ff78db67fa8aa14981ec8c6888fc52a38f17aadb872e87b954aa78e888c599d0](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-ff78db67fa8aa14981ec8c6888fc52a38f17aadb872e87b954aa78e888c599d0.json).

Exact runner selections below. The first command was used for RED, the stopped
fixture attempt, its repair, and the final native reader check. The final
workflow reader check repeated only `poll_does_not_open_native_publication`,
`combat_marker_rejects_wrong_coordinate` and `rejected_live_receipt_stays_diagnostic_only`
from the workflow command. These are selected passes, never G3 qualification.

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context[double-sample] --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context[thread]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_uses_ordinary_forward_capture --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_cli_rejects_execution_options --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_control_parks_and_stops_at_separate_boundary --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_rejected_live_receipt_stays_diagnostic_only --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_close_requires_opt_in_and_clean_exit --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_diagnostic_poll_does_not_open_native_publication --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_combat_marker_rejects_wrong_coordinate --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_combat_arm_follows_verified_trajectory_handoff
```

70 distinct native-contract and30 workflow cases passed across these selections.
The final repeated reader checks are not counted twice. Native negative cases
change actual phase/player slot/indexed serial/frame/epoch input, including an
epoch change between entry samples and a wrong-thread callback. Scene/replay
failure uses the extracted production bridge and invalidation export. No
fixture supplies a native observation receipt or grants the selection arm.

## Changed files and next bounded live question

Production: `HorseMod/dllmain.cpp`,
`HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.hpp`,
`HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`,
`tools/replay_qualification_mod/ReplayQualificationMod.cpp`,
`tools/replay_qualification_mod/ReplayFailureProtocol.hpp`,
`tools/deterministic_qualification/replay_control.py`.
Fixtures: `tools/replay_vfx_completion_observation_selftest.cpp`,
`tools/deterministic_qualification/tests/test_vfx_completion_observation.py`,
`tools/deterministic_qualification/tests/test_replay_workflow.py`.
Documentation: this note. The source-retention comparison against RED found
only those nine source/test files changed. No runner harness, hook owner or
unrelated source was changed.

After root's build/full-local/preflight and exact binary/replay identity checks,
one existing `c18-diagnostic` run with the same startup flags is justified by
this changed selection. It can show whether a relevant C18 callback enters
after the verified frame170 arm through220. A bounded miss falsifies callback
presence in this window under these exact conditions; it says nothing about
later frames or global C18 absence. A selected rejection identifies the current
entry/listener subreason in combat. A successful separate shape sample proves
only that entry-visible diagnostic shape, not complete providers, live
membership, writer coverage, retained memory bounds, lease safety, material
completion, GPU completion, coherent recovery or a G1 pass. No unchanged setup
retry and no historical preparation are warranted.

## Project-build namespace correction

Root's subsequent project build stopped at `ReplayQualificationMod.cpp:1689`:
`C2065: 'UObject': undeclared identifier`; subsequent parser errors followed
that failure. Retained [build RED manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3e2cb164e5a4bd08052d5eaa1745a4327ae5000230d91a837b23c7dc9bf101db.json)
and [raw build log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/efe8f58bb52b73744e688b631283fae603ed17646d944bd7d77e7f71135d53d5.log).

The exact production fix is the template argument
`GetValuePtrByPropertyNameInChain<RC::Unreal::UObject*>`, matching existing
same-file calls; `auto** slot` and all selection behavior remain unchanged.
The fixture's `CombatBridge::UObject` alias had masked the missing production
qualification. It was removed, with the external test type now provided only
as `RC::Unreal::UObject`. Before the production fix, this corrected fixture
reproduced the same C2065 in the extracted production method:
[local RED manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/2134c575022b9a5c24254d2e3f8bec22bfb4fcce17426f1673b1801b6873d684.json),
[raw compiler/test log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3a0ca80c85233bece132aa48841f1d4268e942c7107bb4c63da929df491cd45b.log).

After the one-line production fix: **12 selected native-contract PASS**
([manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/187139be7e1860f814b7736d4a1cc8a802d6d51e98f94a2d06353d7b1095eec5.json),
[raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e675193b6823fe52f4c53232561528cab376e48f1f486298c3ebf6c52b204b3b.log))
and **1 selected workflow PASS**
([manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/d6040fd9e491c4de8e8768763ec6f04cfbe2fbeb5761fdbd3ec655ee450d5bb0.json),
[raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e8c2ebdecc6c68bd31c948de0b0c1dd4c57dd47d093194a6c1d241c65727824d.log)).
Both GREEN manifests retain reconstructible corrected source
`1e6deddf41da1a31a67f0aefe5b56797bf6f008086428d5a4cb3c6afc2a16b64`.

Exact commands, in order (first RED, then two GREEN selections):

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_combat_arm_follows_verified_trajectory_handoff
```

This correction changes only `ReplayQualificationMod.cpp`,
`replay_vfx_completion_observation_selftest.cpp` and this evidence note. No
project build, full suite, game, deployment or status edit was performed here.
The project rebuild remains root's next gate; local fixture GREEN does not
establish that the complete project builds.

## Held-owner failure fixture compatibility correction

Root reported the rebuild and both CTests passed, then full ordinary local
stopped at its first failure after259 passes: the isolated `Fail` prefix fixture
lacked `c18_diagnostic`, `ResolveHorseModExport`, and a string-backed `run_id`.
This was a fixture-source mismatch, not a production compile or native crash.
Retained [full-suite RED manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/63d6d51c4f748e9511b5450e4241861c68a1a0dccb0ccec119207fd58f842fa0.json)
and [raw pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/46972e32661bfbb2ba2958b3149b7e4643903fc21d249b08f9d5d1931ad1decc.log).

The exact selected node reproduced all three compiler errors before the fixture
fix: [selected RED manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0b09b17f58a0b9dee8820f267cb05b626c45c9926d0cb912f8764b15fc1c04bf.json),
[raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/338eaf6dae2407442a8392a60108654607fb2d3475174e57d998767b2b36fce8.log).
Only `test_particle_copy_failed_hold_survives_repeated_failure` in
`tools/deterministic_qualification/tests/test_replay_run.py` was changed.
Its request now models the production string and opt-in flag. A checked external
export stub records exact export name, lookup/call count and forwarded run ID;
it supplies no observer state or receipt. The production `Fail` prefix is still
extracted unchanged. The original preserved-hold, repeated-failure and normal
cleanup assertions remain, with coverage for opt-in/out, missing export,
invalidation before the repeated held-owner early return, and ordinary
diagnostic cleanup. Production invalidation was neither removed nor reordered.

The exact node is **GREEN, 1 passed**
([manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c1f6a950ff09e65506d9fe97df634907ca3a4283386e7024be1015723a3c23cb.json),
[raw pytest log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/fb9f4833ec108937050465e52b6621ab8e602a363b84195e69739696eeb7595e.log)).
Retained [native fixture output](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/40f31633759ef27d18f5b37af2316d32114b0cf8089a60b5f4798d93370bb02e.log)
and [generated C++ fixture](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5d64544599b3dab6f129c5e9aa60ca889a936f14b93d0e874d0af7956b3c4e8d.cpp)
show all six assertion groups passed.
Adjacent actual native observer/export cases for scene, replay and post-return
scene failure are **GREEN, 3 passed**
([manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/b059a315871bcb3217924a826e3f4efdc261d12e50b6b9faec283ed4ae8878b7.json),
[raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5d5bebbd6f056f73a5e4069b6c53d59e52d6d76fb93e09ddd770b405e2693b0f.log)).
Both GREEN manifests retain reconstructible source
`99cde2bd0773c67562b76119066b74daed583ddba91640e5279f1fa4265f51b7`.

Exact commands: the first ran once RED and once GREEN; the second ran GREEN.

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_replay_run.py::test_particle_copy_failed_hold_survives_repeated_failure
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context[scene] --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context[replay] --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context[post-scene]
```

Only the existing fixture test and this evidence note changed in this correction.
No project build, full suite, live run, deployment, status update or production
edit was performed. Root owns the remaining full-suite gate; these selected
results do not qualify G1/G3 or establish any new ownership capability.
