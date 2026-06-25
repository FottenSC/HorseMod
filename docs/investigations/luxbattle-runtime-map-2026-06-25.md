# SC6 LuxBattle Runtime Map - 2026-06-25

Static Ghidra pass focused on `LuxBattleSetup`, `LuxBattleRule`,
`LuxBattle`, `ALuxBattleChara`, and directly adjacent runtime systems.

This note records modding-useful findings that should survive beyond the
current Ghidra naming pass. A separate charge-state subsystem is intentionally
out of scope for now; its current recovery still needs a deeper provider/mesh
class pass before it should be treated as a stable modding reference.

## ALuxBattleChara Context Overlays

`ALuxBattleChara_Partial` is a working reverse-engineering overlay, not a
single proven source-level C++ class layout. Some offsets are stable within a
function family, but a field name recovered in one family must not be blindly
propagated into another family.

Practical rule: before using a recovered `ALuxBattleChara_Partial` field, check
which function family established it. If the decompile crosses from actor glue
to hit math, input, frame actions, trace, or replay, treat repeated offsets as
context overlays until the struct is proven by constructors or reflected
properties.

Useful family-specific anchors:

| Function family | Stable local interpretation |
|---|---|
| Actor/component glue | `+0x388` primary mesh, `+0x390` weapon mesh/component path, `+0x448` collision component, `+0x458` trace manager, `+0x470` attached-entity set, `+0x520` weak component array, `+0x532..+0x537` visibility/state gates, `+0x1438` move-provider cache |
| Input/runtime | `+0x2150/+0x2158` current stick/button words, `+0x215C..+0x2180` decoded held-frame derivatives |
| Hit/damage math | `+0x3F0..+0x420` behaves as float damage-rate and queued-rate state, even though older actor-array recovery gave some overlapping names |
| Provider/action runtime | `+0x398` provider-state enum, `+0x39C` active provider slot/mode, `+0x470` frame-slot table, `+0x480` action-mode/pending-window union, `+0x490/+0x491` dirty/gate bytes |
| Skill-check lanes | `+0x1FF8` skill-check lane state, `+0x96F80` four runtime entries, `+0x97100/+0x9713C` lane float defaults |

The `+0x3F0..+0x420` warning is especially important for damage mods: in the
hit-damage path, these fields should be read as battle-rate lanes, not as the
older actor-array names that may still appear in some decompiles.

## Runtime Damage Factors

KHD section A gives authored base damage. Final runtime damage is produced by
`ALuxBattleChara` factor math after hit classification, defense queues, stat
checks, style state, and extra-skill rates are applied.

Key functions:

| Address | Name | Role |
|---:|---|---|
| `0x140343630` | `LuxBattleChara_ComputeHitDamageFactors` | Builds final multiplier and writes a 0x64-byte factor vector |
| `0x140344D10` | `LuxBattleChara_AccumulateDefenseRates` | Consumes queued defender rates and seeds the factor vector |
| `0x1403810E0` | `LuxBattleChara_QueryStatTableField` | Queries runtime stat-table entries used by threshold/guard/damage helpers |

`FLuxHitDamageRateFactors_Partial` is the useful live trace target:

| Offset | Meaning |
|---:|---|
| `+0x00` | base defense rate |
| `+0x04..+0x10` | queued/conditional defender rates |
| `+0x1C` | move-class defense rate |
| `+0x20` | guard-impact rate |
| `+0x24..+0x3C` | posture/combo/state rates |
| `+0x40..+0x44` | attacker stored rates |
| `+0x4C..+0x58` | threshold/style/frame proc rates |
| `+0x5C..+0x60` | skill-rate sum and final skill multiplier |

Runtime path summary:

1. Resolve attacker from `pDefender->pOpponentChara`.
2. Reset the defender final damage-rate lane to `1.0`.
3. Accumulate queued defender rates and calculate move/posture/guard-impact
   defense rates.
4. Multiply attacker stored rates from `attacker+0x3F4` and
   `attacker+0x2B4B4`.
5. Let selected hit-classifier outcomes force `1.0`, `0.0`, or `0.5`.
6. Apply style mismatch, threshold/stat procs, and extra-skill attack damage
   rates.
7. Copy the final factor vector to `attacker+0x2B440`.

Parser/modding implication: static tools should label KHD damage as base
damage. Exact damage previews need either a live trace of
`FLuxHitDamageRateFactors_Partial` or a model of the runtime queues/stat keys
that feed these functions.

## Move Providers And Action Slots

Move-provider state is the best current map for diagnosing replay/freeze,
stuck-action, and MoveVM scheduling bugs. The recovered names show a clean
split between provider slot control and frame-action subelement dispatch.

Important anchors:

| Address | Name |
|---:|---|
| `0x14040E8E0` | `LuxBattleChara_StopMoveProvider_AtSlot` |
| `0x14040EEC0` | `LuxBattleChara_StartMoveProvider_AtSlot` |
| `0x14040EDC0` | `LuxBattleChara_CallProviderMethod0x198_AtSlot` |
| `0x140435E90` | `LuxBattleChara_Tick_UpdateFrameSlots_ProcessEventWindows` |
| `0x140438370` | `LuxBattleChara_Tick_ActionSlotSubElements_DispatchAndComplete` |
| `0x140438780` | `LuxBattleChara_SetActionMode` |

Recovered type landmarks:

- `LuxBattleCharaProviderStateRuntime_Partial`
- `LuxBattleCharaStyleRuntime_Partial`
- `LuxBattleCharaFrameActionRuntimeV2_Partial`
- `FLuxFrameSlotTable_Partial`
- `FLuxActionSubElement_Partial`
- `FLuxPendingActionWindowEntry_Partial`
- `FLuxEventAsyncTaskPool_Partial`

Useful runtime fields:

| Offset | Meaning |
|---:|---|
| `+0x398` | provider state enum |
| `+0x39C` | active provider slot/mode |
| `+0x3A8` | provider slot table/overlay |
| `+0x470` | frame-slot table |
| `+0x480` | action-mode / pending-window union |
| `+0x490/+0x491` | action dirty/gate bytes |
| `+0x494` | completion-delay frames |
| `+0x498` | subelement runtime table |
| `+0x4A0` | subelement runtime count |
| `+0x560` | byte-key dispatch table for subelements |

Modding implication: if a freeze, replay seek, or no-render step leaves a
character alive but inert, inspect provider clear gates, pending action windows,
completion-delay frames, and subelement completion before blaming input. These
functions are also good hook/log points for a future "why is this move stuck"
debug overlay.

## Training Battle Setup Tables

Training setup is mostly fixed table construction, not opaque special casing.
`LuxBattleRule_BuildTrainingModeDataTablePath @ 0x1405D6F40` constructs the
training battle table path and writes concrete leaves:

| Table path | Value |
|---|---|
| `CommonParam.BattleTime` | `-1` |
| `CommonParam.BattleType.BattleType` | `BATTLE_RULE_BATTLETYPE_TRAINING` |
| `CommonParam.Demo.IntroType` | `BATTLE_RULE_INTROTYPE_BATTLECALL` |
| `CommonParam.Demo.CharacterIntroType` | `BATTLE_RULE_CHARACTER_INTROTYPE_NOTHING` |
| `CommonParam.Demo.OutroType` | `BATTLE_RULE_OUTROTYPE_NOTHING` |
| `BattleRule.Endless` | `true` |
| `BattleRule.Rounds` | `0` |
| `PlayerRight[0].PlayerParam.CPU.CPUType` | `CPU_TYPE_STAND` |

`ULuxUIBattleLauncher_GetBattleStageCode_Impl @ 0x1405B0C60` resolves stage
code from `StageSetting.StageCode` and falls back to `STG001`.

Modding implication: tools that synthesize or compare battle setup should treat
these as the stock training-mode defaults. Training-mode hacks should patch the
table leaf or its consumer, not assume the values are scattered constants across
the battle loop.

## Battle Camera Synthesis

`LuxBattle_UpdateBattleCameraSynthesis @ 0x14031EA50` is the compact map for the
per-frame battle camera output. The camera state is not just an actor transform;
the function blends effect-camera components, applies optional adjustment passes,
then publishes a global output block.

Runtime path:

1. Update active effect-camera component weights for mode `0`.
2. Blend weighted components into `g_BattleCameraScratchState`.
3. Tick the slow-motion camera state machine.
4. Copy scratch state through its vtable into `g_BattleCameraTransformOutput`.
5. Copy scratch state into a stack `FLuxBattleSynthesisCameraState_Partial`.
6. If `g_dwBattleCameraAltVelocityGate` is set, run the velocity/yaw adjustment.
7. If `g_dwBattleCameraAltLookAtGate` is set, run the look-at offset adjustment.
8. Build the final rotation/position block into
   `g_BattleCameraPublishedOutput`.
9. Publish normal vector and per-frame delta.

Important globals:

| Global | Type/role |
|---|---|
| `g_BattleCameraScratchState` | `FLuxBattleSynthesisCameraState_Partial` blended camera state |
| `g_BattleCameraTransformOutput` | transform output copied from scratch state |
| `g_BattleCameraPublishedOutput` | final matrix/normal/delta block |
| `g_dwBattleCameraAltVelocityGate` | enables velocity/yaw adjustment pass |
| `g_dwBattleCameraAltLookAtGate` | enables look-at offset pass |

Replay/rollback implication: if a hash or visual validation includes camera,
capture or reset these globals explicitly. If no-render generation suppresses
camera, compare against an oracle that also treats camera as out of scope.
Camera-lock patches should also consider the final published delta, not only the
visible transform.

## Follow-Up Targets

- Split the `ALuxBattleChara_Partial` overlay into smaller function-family
  structs where evidence is strong enough.
- Build a live trace around `FLuxHitDamageRateFactors_Partial` to correlate
  static KHD base damage with real damage on hit.
- Add provider/action-slot logging to replay seek validation when a seek resumes
  with an inert character.
- Keep the separate charge-state subsystem out of this map until its
  provider/mesh internals are recovered enough to avoid misleading field names.
