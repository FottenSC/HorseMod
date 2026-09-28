# G1 C18 TraceEventHandler receiver correction — 2026-09-27

C18 now requires the native TraceEventHandler receiver table `14326B2E0`
separately from the descriptor-selected Chara fighter table `143268078`.
The production-boundary regression first failed with the actual receiver class;
**104 selected native-contract cases and 12 workflow cases now pass**. This is
an exact diagnostic class correction, not ownership or G1 recovery. Root owns
the project build, full suite and any subsequent bounded live trial.

## Measured live sample and native proof

Root's run `replay-9d8009d431bf49f4811e21e9dc6863ba`, runtime SHA256
`6b157fc82de0fa234cfe114b329b10487b569ef5e45313ddcb61e32900d53a0d`,
armed at frame170 and selected/returned C18 at combat frame172. The version2
zero-serial diagnostic rejected `listener_receiver_vtable` and retained row0,
indexed object310145/serial8261, candidate vtable RVA `0x326B2E0`, with
`image_membership_proven=false`. Ordinary census stayed `entry_invalidated`;
there were no material rows or ownership/completion claims. Root reports build,
two CTests and full ordinary local1127 passed before this run; cleanup completed,
no game remained and Steam was kept.

Immutable [live manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/47310991ecde96942e551a7aad2ae54f3ffbc01fee86d5b2212a3e51a06a0eb0.json),
[raw sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c42d6b6ff000cc68dfa15a60d0b8fb986ccb579f44dc05bffa154d8628168179.json),
and [stage report retained before edits](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e23c3f12c6f811052b6d5362cbe4a814e1c47420fe2118664a39184bd5cdc2aa.json).
The [preceding discriminator](rollback-g1-c18-rejected-type-sample-2026-09-27.md)
made this measurement possible. Earlier receiver-name assumptions in historical
notes are superseded by the native proof below, not silently rewritten.

Root independently resolved the measured table in the existing Soulcalibur
Ghidra program and supplied these facts; this code task did not edit Ghidra:

- `FUN_1403ABFF0` constructs the AActor-derived receiver and installs
  `14326B2E0` at `1403AC002`.
- Registration `FUN_1409A8680` calls `UE4_RegisterClassEx` with the exact name
  `LuxBattleTraceEventHandler`, size `0x388`, and adapter `1409A5C60` calling
  that constructor.
- The table's `+0x138` getter is `141C204E0`; `+0x2E8` is `1403B54A0`.
  That BeginPlay function registers `this` in collection `0x12` with callback
  `1403C5360`. Its previous `ALuxBattleChara_BeginPlay_G` label was incorrect.
- The callback separately selects the fighter in BattleManager `+390/+398`.
  Chara's constructor installs `143268078`. VFX handler `14326B8D8` is a third,
  distinct type, not an equivalent receiver despite the shared world getter.

After the code-writer slice, root corrected the directly relevant names,
prototypes and plate/EOL comments in that same Ghidra program:
`InitializeLuxBattleTraceEventHandlerObject` at `1403ABFF0`,
`BindLuxBattleTraceEventHandlerCallbacksOnBeginPlay` at `1403B54A0`, and
`HandleLuxBattleTraceActivationEvent` at `1403C5360`. The constructor and
BeginPlay prototypes use the verified AActor base pointer; the activation
callback retains the typed 0x1C-byte descriptor and now types its receiver as
an AActor base. Comments distinguish the weak receiver from the selected
fighter and retain the decompiler's omitted RCX argument limitation. Root
saved `SoulcaliburVI.exe` through native MCP and read back all three signatures
and the callback plate comment. No new Ghidra program, database script or
struct type was used.

## Production scope and receipt compatibility

Only the receiver exact-type predicate in `C18CaptureMaterials` changes from
`base+0x3268078` to `base+0x326b2e0`. Selected fighter remains `base+0x3268078`;
BattleManager remains `base+0x327aa20`. Genuine VFX manager checks at
`base+0x3356f68` in the main observer are unchanged. Fixture native memory now
models a separate indexed TraceEventHandler receiver and Chara fighter; it
does not import an expected receipt into the production capture.

Material census, its outer material observation version, and the separate
zero-serial diagnostic advance to **version3**. Fields are otherwise unchanged.
This version separates the corrected exact-class contract from prior receipts:
the measured version2 rejection of `326b2e0` must remain readable as a historical
rejection, while a version3 rejection naming its accepted receiver table is a
contradiction. The reader keeps versions1/2 under their original class policy,
checks version3 consistency, and applies role-specific rejection validation.
Version3 rejects Chara/VFX-handler/unknown receivers rather than silently treating
same-world or same-getter classes as interchangeable. All these are observation
readers; no historical version grants an ownership or admission permission.

No hook, native read, callback route, serial allocation, reference acquisition,
or post-return borrowing is added. Native forwarding and LastError remain exact.
Weak indexed identity, world/registry, provider, writer and double-sample checks
remain. Rejected observations still have no material rows. C18 accounted storage
is still195678 bytes within256KiB; shape rows remain limited to16, observer
reservation4MiB and production ceiling1GiB. Total retained native memory remains
unknown. Complete B, six material hooks, ProducerUncovered C-only veto,
HistoricalRestoreSupported=false, and lifetime/GPU/deferred guards are untouched.

## RED then selected GREEN

Before the production edit, the real selected callback received native receiver
`326b2e0` and fighter `3268078`. It forwarded once, preserved LastError
`66c -> 77c`, and emitted `listener_receiver_vtable`. Its actual rejected-type
sample was row1/object8/serial108/RVA `326b2e0`. The expected successful shape
receipt was absent: **behavioral RED**, with the fixture process exiting0, not
a compiler or forwarding failure. The first test failure stopped the selection.

| Selection | Result | Immutable manifest | Raw pytest log |
| --- | --- | --- | --- |
| Actual native receiver, before production fix | RED, 1 failed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/51864dd53e53b728a3af7fcfd1162085e083c710dbdb1baa6854dd53219f336d.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/d8d5b2898b6e0e1dda4417128238d18b1e6ef638f0ee21cb605f79409ffc03ab.log) |
| Receiver/fighter production class matrix | GREEN, 12 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/039fb15808bb62ae5dace6ff7105f4e8345819022789bb425226f0925b49af99.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/93862b0a462586114a60267fa5445b6907ee6f5e3f1c433740101f423e506337.log) |
| Rejected-type detail, bounds, interruption and reader | GREEN, 12 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e02af7d3c559d6def2f5965453cba4a0d10bcf560354484eaa7d8b1e62d53e39.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/ffe30d270f7ee036702135321c9efeb10e7af196a5d13f0c607931173e0a38bd.log) |
| Adjacent production guards/census/combat selection | GREEN, 80 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/847974f4600c684686e0ef34965a7a89a6655f3fc4ba69bad8a475fb962a3768.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/a026224d1f3ab569ec9431112560746fc2a573e91b755c6978c8da269ed2e44d.log) |
| Measured version2 rejection and legacy rejected publication | GREEN, 12 passed | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/5bb6cca9c58062f8a2766b9ce6da3277d7681395911f1d56a080423464895ecc.json) | [log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/565ade205a987af445e884d655d9c669f93f009dce77f5ad13b9baafe7bec268.log) |

Additional immutable [RED stdout](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c96fc5b4b623d92b2cd453ce1759d273e4899ae9cd94b289fd657f12ced642d1.log),
[RED sidecar](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/506adabee9d1badea3dfebd11bb619430ee3cf6eb12d5de5567a4ecbf5226788.json),
and [all12 GREEN class fixture stdout/sidecar receipts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9f60309a791f85bc55b16293b6fd16d7ae18e640f05efc7ea8f743c6c46241c1.json).
The latter also identifies intentionally corrupted offline reader inputs;
`vfx_completion_observation.json` is each fixture's restored raw publication.
In case order: good, Chara receiver, VFX-handler receiver, unknown receiver,
VFX-handler fighter, unknown fighter; each has ordinary then zero-serial paths.
The successful zero-serial path yields only diagnostic rows; ordinary census
remains invalid. Pages are revoked before return and post-return read attempts
remain0. Reader negatives also reject TraceEventHandler as the selected fighter.

Reconstructible dirty/untracked source:
[RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-ade484fdc4a382c42ce2935ff1e90ebe28fc55b543638bcfdd0c516e33d66183.json),
[all GREEN](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/source-archives/source-9d46764043c9458664f90144c5d9e9d453703f25243ed4ecabb322b471d61787.json).
Comparison with preceding source `abc259acaeab7a8cce80d929f94b0587a92ce8314bba728c3fa4120dacc73fd8`
finds exactly these four code/test paths changed; current contents match GREEN:

- `HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl`
- `tools/deterministic_qualification/replay_control.py`
- `tools/replay_vfx_completion_observation_selftest.cpp`
- `tools/deterministic_qualification/tests/test_vfx_completion_observation.py`

This evidence note is the only additional authored file. Exact test commands:

```powershell
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_trace_receiver_and_chara_fighter_types[good-zero_serial]
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_trace_receiver_and_chara_fighter_types
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_rejected_type_sample
python tools/replay_test.py local --layer native-contract --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_zero_serial_diagnostic --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_memory_shape --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_selected_material_provider_census --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_registry_target_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_arm_requires_battle_manager --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_excludes_setup --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_combat_selection_rejects_changed_native_context --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_first_occurrence_and_guarded_read_attempts --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls
python tools/replay_test.py local --layer workflow --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_measured_trace_receiver_legacy_rejection --test tools/deterministic_qualification/tests/test_replay_workflow.py::test_c18_rejected_live_receipt_stays_diagnostic_only
```

No project build, broad suite, preflight, game/deployment, Ghidra/status/AGENTS
edit or commit ran here. A later root-owned run after its gates can falsify
whether this exact receiver correction clears the measured live predicate and
exposes the next bounded route result. It cannot establish MID ownership,
completion or recovery. No post-fix live material observation is claimed.
