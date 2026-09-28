# G1 C18 rejected type: bounded entry sample — 2026-09-27

The selected zero-serial diagnostic now publishes one bounded type-mismatch
sample without admitting the receiver or selected fighter. Production-boundary
RED is retained; **12 new native cases,90 adjacent native cases and8 legacy
workflow cases pass**. No project build, full suite, preflight, game, deployment,
Ghidra/status edit or commit ran here. Root owns those gates.

## Live context and native facts

Root's latest run `replay-9791060079be451782d60359f7fc5b0a`, with runtime
SHA256 `56b1327ad60329156e0c3f3cbd4a658380063182f9e0da1a863c7f635b4b5dea`,
selected C18 at frame172 after arm170. Its separate diagnostic again rejected
`listener_receiver_vtable`, ordinary census rejected `entry_invalidated`, and
there were no material rows. Bounded run/cleanup passed; root reports no game
remaining and Steam kept. This is not material observation or recovery success.

Immutable [report](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/da22409c7b84f928968f4e2d66c76d11065f804078bd414e8270c880390b70f2.json),
[raw game log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b432e5f5ae21dfd9726e66d4ca64cc0e44e84b3f86627726d15867818e3b0479.log),
[sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e7991eafa718e56067c65cd8ef5464ab451cdaf497865d3fbf9dba8b592ce661.json),
[manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/15eedd348826ff5d47122e0ac3ae187132048ab82c59225274512cbcdc2a8cf8.json).

Root rechecked the existing Soulcalibur Ghidra program: the named registration
of HandleLuxBattleCharaTraceActivationEvent1403C5360 is the BeginPlay_G call
at1403B54D8, passing the character receiver. The ALuxBattleChara constructor
installs143268078; ALuxBattleVFxEventHandler installs14326B8D8. The prior
[exact-class correction](rollback-g1-c18-battle-chara-identity-2026-09-27.md)
did not resolve the live predicate. Derived/concrete class identity remains a
hypothesis. **The latest actual live receiver table remains unmeasured.** This
slice adds discrimination, not another guessed allowlisted class.

## Receipt and safety boundary

Only `zero_serial_diagnostic` advances to version2; its scope and existing
permission fields remain unchanged. Version2 adds required `rejected_type`,
normally null. At `listener_receiver_vtable` or `selected_fighter_vtable`, it is:

```json
{
  "scope": "single_entry_sample",
  "subject": "listener_receiver",
  "listener_row_index": 1,
  "object_index": 8,
  "object_serial": 108,
  "vtable_rva": 52869336,
  "image_membership_proven": false
}
```

This example is fixture output for326b8d8, not live data. The selected-fighter
subject uses that sampled object's index/serial; its serial can be zero. The
listener still must pass the existing positive weak-serial and indexed-object
checks before any detail is captured. No address, `valid` flag, reference or
lease is exposed in this scalar receipt. Parent entry sequence/thread/collection/
descriptor retain occurrence association; `entry_samples_match` stays false on
rejection. This is one entry sample, not matching captures or a lifetime claim.

`vtable_rva` means the sampled table minus the observer's known image base when
that subtraction is nonnegative and fits32 bits. It is a candidate base-relative
RVA, **not proof the table belongs to the image**. Below-base or wider values
serialize as null; a measured offset zero stays zero. If earlier identity reads
fail, the whole detail is null. Both forms of unavailability differ from zero.

The selecting call copies only the already sampled listener row/object fields to
fixed local scratch. It publishes the scalars under the existing metadata lock
after capture stops. Pending publications expose null. Serialization uses only
owned scalars, and emits detail only while the effective rejection matches that
subject; reentry, closure and other interruption/failure overrides suppress it.
No native lock spans a call, and no new native reads, UObject scans, serial
allocations or hooks were added. Native input pages are revoked before callback
return in the fixtures, which verify zero subsequent read attempts and exact
forwarding/LastError66c-to77c.

The reader accepts genuine version1 receipts without adding metadata and rejects
attempts to attach version2 detail to them. Version2 requires the field and
checks exact keys/scope/subject, bounded row/index/serial/RVA types, false image
membership, failure association and interruption flags. It rejects ownership
fields or contradictory known-admitted tables in a mismatch receipt. Ordinary
census, existing type predicates and all admission decisions are unchanged.

Accounted C18 storage/scratch rises64 bytes, from195614 to195678, within256KiB;
serialization remains inside its existing allowance. The16-row shape limit,
4MiB observer reservation and1GiB production ceiling remain. Complete B, six
material hooks, writer/lifetime/GPU/deferred guards, ProducerUncovered and
HistoricalRestoreSupported=false are untouched. Native retained memory remains
unknown, and ownership/completion/admission permissions stay false.

## Retained tests and source

First, the existing real callback owner received a wrong but indexed receiver
table326b8d8. It correctly rejected and forwarded, but the production sidecar
lacked the requested detail: behavioral RED, not a compile failure. After the
implementation,9 cases passed before a test assertion failed on reentry: the
fixture's fixed outer-owner message says `collection18=1`, while its actual native
executor counter correctly says2. The assertion was corrected to check that
counter; no production change followed that failure.

| Selection | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
| Indexed wrong receiver, before production fix | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/00de271f58e93affc3c36405f8f2b37fe4eedd9b29b3774df5f824fccbb7c72b.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/70917f6a2eddcf3d955189a6ef023164cc2737510456ea5f79153f03a5ea1216.log) |
| First selected attempt after implementation | 9 passed, stopped at reentry assertion RED | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/4445069b0274e438d539e8abf8c9ec9e8817148af48797bf0262253e32b53b0c.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f2b0bbd4b16b667ae21ea32f89a27e90c6197c5c8af5b25216ee3705f49bedc8.log) |
| Type detail/bounds/unavailability/interruption/reader cases | GREEN, 12 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/945996408c9c4e9d3ad20c493584487c387143e02d5cca7516432cb6fa272402.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/beb735d40b987526f1b8f6a7693309d053fb79e212bbaa8905e9e2b0840e84a5.log) |
| Adjacent native cases | GREEN, 90 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/4bbc0587e3e0c4f90959b4bc41aac3ddba350e9abfddcb881c9bb1e244d5ad9b.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/33b321471fe29ad218c231b4e322b5fbb1c2db8590fcee19838d3c2584ad9ab1.log) |
| Legacy real rejected publication workflow | GREEN, 8 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/07350cae3a55eb96d7cd6fc181e1e929c0e304d43ec5fcbb850cac8fb37053f0.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7d033b6114e3b7e226dd134ddf87559ce2573e4749f27f5567211d071d888ecc.log) |

Additional raw evidence: [RED fixture stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b937bda5be128abad590dc326ccf50e9eab3158ac98f7760fbd94d35efb37030.log),
[RED sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5f9847f05424b553729cb2696adcf8c9c2e7773413815f65dd7803f60f08945a.json),
[all12 GREEN stdout/sidecar receipts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7ebcb6ea2cb26e12a668359b43dafea9ae8a6e3b5e29f459e69ee9e791f6882f.json).
Their ordered cases are receiver-handler, fighter-handler, receiver-unknown,
zero-RVA, below-base, wider-than32-bit, null table, stale weak serial, unreadable
object, reentry after mismatch, close after mismatch and successful shape capture.

Reconstructible dirty/untracked source snapshots:
[RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-df90c2dbb1f017b2d0fda7bcd0b32879e417dc0108fa50e2a9490cb2491e2a01.json),
[before assertion repair](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-3e95b6decef2e2ac029e6aa0f8ff7f79e22cddbcadc4510ae2551f186db434cd.json),
[final GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-abc259acaeab7a8cce80d929f94b0587a92ce8314bba728c3fa4120dacc73fd8.json).
Comparison with preceding sourceabceddbf finds exactly the four code/test paths
below changed; unrelated retained source is unchanged.

Exact commands, in order:

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_rejected_type_sample[chara-receiver-handler]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_rejected_type_sample
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_rejected_type_sample
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_receiver_and_fighter_require_battle_chara --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_rejected_live_receipt_stays_diagnostic_only
```

Changed paths:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`
- This evidence note.

A new root-owned run after the required gates can measure the selected mismatch's
candidate table offset. It cannot prove a derived class, admission, lease, native
resource/GPU completion or recovery. Unknown/out-of-range detail remains explicit;
no unchanged live retry or eligibility relaxation is justified by these passes.
