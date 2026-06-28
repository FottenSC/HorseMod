# SC6 Move Editor Ghidra Map - 2026-06-28

Goal: identify the native and static-data map needed for a real move editor,
using Scuffle as an external format reference and Ghidra as the authority for
runtime behavior.

## Working Model

A useful editor has to keep three sources separate:

| Layer | Source | Editor use |
|---|---|---|
| Movelist UI metadata | `DA_MovePlayData_<cid>`, `DA_MoveListTable_<cid>` | Category/order, displayed command, localized name/text, UI tags |
| Authored battle data | KHD `hdr<cid>.khd`, `KH11` records | Move rows, attack cells, throws, modifier records, script/cancel bytecode |
| Runtime truth | `LuxMoveVM_*`, `Lux_KHitChk_*`, `ALuxBattleChara` fields | Validate transitions, active frames, effect opcodes, KHit geometry, playback |

The static editor should write KHD-style data. Runtime hooks should be treated
as validation and preview targets, not as the primary storage format.

## Scuffle Evidence

Snapshot inspected: `FottenSC/Scuffle` at commit
`b8e8129b58417b8b71189f05ea3e789b09493f10`.

Key format facts from `MovelistParser.py`:

| Record | Size | Notes |
|---|---:|---|
| Header | `0x30` | Magic `KH11`; offsets/counts for move, attack, throw, modifier, and script sections |
| Move | `0x48` | Animation id/speed, total animation frames, cancel/script address, up to six attack or throw indices |
| Attack | `0x70` | Hitbox masks, physics vectors, active window, damage, stun, effect ids, combo/guard fields |
| Throw | `0x06` | Damage, unknown word, scaling |
| Attack modifier | `0x30` | Move id, attack index, hitbox-mask override, xyz offsets, scale multiplier |

Important Scuffle attack offsets to reconcile with native structs:

| Offset | Scuffle meaning |
|---:|---|
| `0x00..0x07` | Core/limb/misc/weapon/additional/general hitbox mask bits |
| `0x08..0x31` | Packed physics and launch vectors plus hit-level metadata |
| `0x36` | Active window start / startup |
| `0x38` | Active window end |
| `0x3A` | Base damage |
| `0x40` | Combo scaling |
| `0x42` | Guard-break scaling |
| `0x44..0x4E` | Block/hit/counter stun fields |
| `0x50..0x58` | Hit/block effect ids |
| `0x5A` | Guard damage override |
| `0x5C..0x62` | Combo condition and attack type/strength-ish fields |
| `0x68..0x6F` | VFX additions on block/hit |

Scuffle script tables give a good first naming pass for effect opcodes:

| Script table | Editor relevance |
|---|---|
| `25/03.json` | Movement, facing, animation speed, move speed, throw, hit properties, damage scaling, sound/voice, VFX |
| `25/0d.json` | State/switch/attack-state predicates and move-switch helpers |
| `A5/01.json` | Conditions: input, opponent distance, current frame, current move, hitbox index/damage |

## Ghidra Updates Applied

These changes were applied to the active `SoulcaliburVI.exe` Ghidra program and
saved separately from this note:

| Address | Name | Change |
|---:|---|---|
| `0x1402FC930` | `LuxMoveVM_DecodeVariadicStreamArgs` | Typed `pChara`, `pwArgStream`, `nLaneIdx`; restored/updated plate comment |
| `0x1402FC400` | `LuxMoveVM_ResolveBankSlot` | Plate comment now documents packed bank/slot decoding and proves the `0x48` `FLuxMoveBankSlotView` stride |
| `0x1402E5A30` | `LuxMoveVM_ExecuteBytecode` | Typed `pChara` and local-var frame, named durable VM locals, and documented opcode families/CALLCOND dispatch |
| `0x1402FDEA0` | `LuxMoveVM_ExecuteOpStream` | Typed `pChara`, `qwDispatchFlags`, `pwCmdStream`; restored plate comment |
| `0x140376B20` | `LuxMoveVM_DispatchEffectOp` | Typed `pChara`, `pnCmdStream`; plate now links Scuffle `0x25` scripts to the native dispatcher |
| struct | `LuxBattleAttackCell` | Marked `+0x08` and `+0x40` byte runs as Scuffle-correlated candidates so they are no longer described as unused padding or native-confirmed semantics |
| struct | `FLuxMoveBankSlotView` | Normalized first three 16-bit field types to `ushort`; renamed `+0x30` to `flPlaybackSpeed60ths_30` after native transition evidence showed it seeds lane playback speed, not animation length |
| `0x1409FA0E0` | `InitializeUEnumLuxorGameELuxCharacterAssetType` | Named the `ELuxCharacterAssetType` enum-registration xref for `ECA_AttackHitData`; this is a loader breadcrumb, not the KHD parser |

Verified signatures after edit:

```c
ulonglong __fastcall LuxMoveVM_DecodeVariadicStreamArgs(
    ALuxBattleChara_Partial *pChara,
    int nArgCount,
    ushort *pwArgStream,
    int nLaneIdx);

void __fastcall LuxMoveVM_ExecuteOpStream(
    ALuxBattleChara_Partial *pChara,
    int nLaneIdx,
    ulonglong qwDispatchFlags,
    ushort *pwCmdStream);

void __fastcall LuxMoveVM_DispatchEffectOp(
    ALuxBattleChara_Partial *pChara,
    int nArgCount,
    short *pnCmdStream);

short __fastcall LuxMoveVM_ExecuteBytecode(
    ALuxBattleChara_Partial *pChara,
    byte *pBytecode,
    uint dwUnused,
    short *pnLocalVarFrame);
```

## Native Function Map

| Address | Name | Move-editor role |
|---:|---|---|
| `0x1402FC930` | `LuxMoveVM_DecodeVariadicStreamArgs` | Decodes transition authoring args and writes lane transition targets |
| `0x1402FC400` | `LuxMoveVM_ResolveBankSlot` | Resolves packed `(bank<<12)|slot` ids into `FLuxMoveBankSlotView *` |
| `0x1402FDD70` | `LuxMoveVM_CheckMoveTransitionTiming` | Runtime check for when queued transitions fire |
| `0x1402FDEA0` | `LuxMoveVM_ExecuteOpStream` | Per-lane MoveVM tick: transition checks, bytecode, effect entries, frame advance |
| `0x1402FE350` | `LuxMoveVM_TransitionToMove` | Enters a new move / slot and re-classifies active cell state |
| `0x1402FCC30` | `LuxMoveVM_ExecuteBankSlotScript` | Executes bank-slot script helpers referenced by cancel bytecode |
| `0x1402E67B0` | `LuxMoveVM_RunBytecodeScript` | Runs bytecode stream; next target for opcode-level documentation |
| `0x1402E5A30` | `LuxMoveVM_ExecuteBytecode` | Stack-VM opcode interpreter and CALLCOND dispatch choke point |
| `0x140376B20` | `LuxMoveVM_DispatchEffectOp` | Native dispatcher for Scuffle `0x25` effect op payloads |
| `0x140300620` | `LuxMoveVM_ClassifyHitboxFrameState` | Runtime startup/active/recovery classifier for the current attack cell |
| `0x14030C940` | `Lux_KHitChk_DeserializeLinkedList` | Deserializes KHit linked-list data into runtime hitbox lists |
| `0x14030D6A0` | `LuxBattleChara_UpdateAllKHitWorldCenters` | Updates runtime hitbox world centers |
| `0x14030E2F0` | `KHitSphere_UpdateFromAnimCell` | Applies animation-cell transform to KHit sphere data |
| `0x140423440` | `LuxObject_LookupFName_MovePlayData` | UI metadata lookup anchor for `DA_MovePlayData` |
| `0x1404235C0` | `ALuxBattleMoveCommandPlayer_GetMovePlayParam_Impl` | Native accessor for move-play UI/demo params |

Existing type anchors:

| Type | Size | Move-editor role |
|---|---:|---|
| `FLuxMoveBankSlotView` | `0x48` | Native match for Scuffle move record size; has total frames, cancel bytecode offset, and six cell/throw refs |
| `LuxBattleAttackCell` | `0x70` | Native/Scuffle attack-cell size match |
| `FLuxBattleMoveListTableRow` | `0x88` | UI movelist row schema for names, descriptions, tags, and command metadata |

## `FLuxMoveBankSlotView` Status

Current struct size is `0x48`, matching Scuffle's move record size. Native proof
comes from `LuxMoveVM_ResolveBankSlot`: packed move-slot ids use bank bits
`15..12`, slot bits `10..0`, bucket start/count pairs in `FLuxMoveBank`, and a
`0x48` stride from `pBank+0x30`.

Native-confirmed/high-confidence fields:

| Offset | Field | Evidence |
|---:|---|---|
| `0x00` | `wAnimationIndex_00` | Scuffle and native layout agree this begins the slot record; type normalized to `ushort` |
| `0x02` | `wMotionPlaybackParam_02` | Native 16-bit slot header field; exact semantic still needs reader proof |
| `0x06` | `wMotionFlags_06` | Native 16-bit slot header field; exact flag bits still need reader proof |
| `0x30` | `flPlaybackSpeed60ths_30` | `LuxMoveVM_TransitionToMove` divides this by the native 60-frame divisor and writes lane playback-speed current/target fields |
| `0x34` | `wTotalFrames` | Scuffle move record offset and existing native field name agree |
| `0x38` | `dwCancelBytecodeOffset` | `LuxMoveVM_ExecuteBankSlotScript` adds this offset to the move-bank base to get bytecode |
| `0x3C..0x46` | `nCellOrThrowRefVariant0_3C..5_46` | `ExecuteBankSlotScript` and prior comments treat these as six attack-cell/throw refs |

Scuffle-correlated candidate fields:

| Offset | Field | Evidence |
|---:|---|---|
| `0x08` | `qwField_08` | Scuffle maps `0x08/0x0C` to speed scalar floats; native subfield readers still need proof |

Unknown or unresolved:

| Offset | Field | Current handling |
|---:|---|---|
| `0x04` | `nField_04` | Leave unchanged until native readers prove whether this is start frame or another slot header value |
| `0x10/0x14` | `dwSubTableOffset_10/14` | Offset-like fields; need loader/parser xrefs |
| `0x18` | `qwField_18` | Unknown qword |
| `0x20/0x28` | `qwInputMask_20/28` | Likely input masks by name and placement; still needs native reader/writer confirmation |
| `0x36` | `nHitWindowStart_36` | Name is suspicious because attack cells own active windows; keep until native use is audited |

`wTotalFrames @ +0x34`, not `flPlaybackSpeed60ths_30 @ +0x30`, is the
slot-level animation-length source in the transition path. A sentinel total of
`0xFFFE` falls back to a computed motion length during
`LuxMoveVM_TransitionToMove`.

## `LuxBattleAttackCell` Status

Current struct size is `0x70`, matching Scuffle's attack record size.

Native-confirmed/high-confidence fields:

| Offset | Field | Evidence |
|---:|---|---|
| `0x00` | `u64SlotMask` | Hitbox/mask use in native paths |
| `0x36` | `wI16MasterWindowStart` | Read by `LuxMoveVM_ClassifyHitboxFrameState` as active-window first frame |
| `0x38` | `wI16MasterWindowEnd` | Read by `LuxMoveVM_ClassifyHitboxFrameState` as active-window last frame |
| `0x3A` | `wI16BaseDamage` | Matches Scuffle base damage offset |
| `0x44` | `wI16BlockstunFrames` | Stun block begins at Scuffle `0x44` |
| `0x5E` | `wU16HitboxGroupBitfield` | Native classifier reads it to select hitbox sub-window bank/group |
| `0x62..0x65` | range min/max chars | Native range gates; Scuffle has nearby attack property fields but exact names still need proof |

Scuffle-correlated candidate fields:

| Offset | Field | Evidence |
|---:|---|---|
| `0x08` | `pScuffleCorrelatedVectorHitLevelBytes_0x08` | Ghidra type is `byte[42]`, not a pointer. Scuffle maps this byte run to authored physics vectors and hit-level metadata; native subfield readers still need proof |
| `0x40` | `pScuffleCorrelatedScalingBytes_0x40` | Ghidra type is `byte[4]`, not a pointer. Scuffle maps this 4-byte run to combo and guard-break scaling; native subfield readers still need proof |

The MCP datatype layer currently preserves a `p` prefix for these two byte-array
fields even when the rename request uses an `ab` array prefix. Treat the field
types as authoritative: both entries are inline byte arrays, not pointers.

Do not blindly overwrite the remaining fields from Scuffle names yet. Several
offsets around `0x56..0x62` are interpreted differently by current native
comments versus Scuffle's editor labels, so they need a focused type pass before
being promoted to canonical Ghidra names.

## Editor Build Implications

Minimum viable static editor:

1. Parse `KH11` header and section offsets exactly as Scuffle does.
2. Preserve unknown bytes on every record.
3. Expose move records (`0x48`) and attack records (`0x70`) first.
4. Treat attack active frames as KHD fields at `attack+0x36/+0x38`.
5. Treat active-frame runtime validation as `chara+0x1980 == 2`, produced by
   `LuxMoveVM_ClassifyHitboxFrameState`.
6. Decode scripts read-only first, using Scuffle's JSON names, and validate
   effect op payloads against `LuxMoveVM_DispatchEffectOp`.
7. Keep UI movelist data (`DA_MoveListTable`, `DA_MovePlayData`) separate from
   executable battle data.

The first dangerous editor operation is script rewriting, not numeric attack
field editing. Numeric edits can preserve opaque bytes. Script edits can break
transition scheduling, lane state, or effect dispatcher arity if the VM parser
is incomplete.

## MoveVM Bytecode Status

`LuxMoveVM_ExecuteBytecode @ 0x1402E5A30` is now the native opcode anchor. It is
a 16-bit stack VM:

| Native opcode | Native behavior | Scuffle correlation |
|---:|---|---|
| `0x01` | Create stack frame with u16 local count | Function/script prologue |
| `0x02/0x06` | Return without pushing new accumulator | Return-ish one-byte op family |
| `0x03/0x04/0x2A` | Absolute jump to u16 PC | Jump family |
| `0x05/0x07/0x08` | Return/break paths; `0x08` sets `g_nMoveVM_BreakFlag` | Return/break one-byte family |
| `0x0A` | Load variable by ID | Scuffle variable load |
| `0x0C..0x11` | Arithmetic / unary negate | Scuffle math ops |
| `0x12/0x13` | Post-increment / post-decrement variable by ID | Variable mutation |
| `0x14..0x18` | Bitwise and/or/not-like and shifts | Bitwise ops |
| `0x19..0x1E` | Store/add/sub/mul/div/mod variable by ID | Variable write family |
| `0x1F..0x24` | Comparisons | Scuffle comparison ops |
| `0x25` | CALLCOND dispatch through `g_apfnMoveVM_CallCondDispatchTable` | Scuffle executor family; subkey chooses handler |
| `0x26/0x27` | Push/pop accumulator/stack helper | Stack helper |
| `0x28/0x29` | Conditional jumps on popped stack value | Conditional jump family |
| `0x2B..0x3C`, `0x00` | No-op/default-family entries in this interpreter | Unresolved/default |

Variable ID ranges are native-confirmed:

| Range | Storage |
|---|---|
| `<0xF0` | `g_pnMoveVM_GlobalVarBankForPlayer` |
| `0xF0..0xFF` | `pnLocalVarFrame` |
| `>=0x100` | Current VM stack frame relative to `g_wMoveVM_StackFrameBaseIndex` |

Residual Ghidra scorer debt remains on `LuxMoveVM_ExecuteBytecode`: effective
score is `77.51`, with `22.49` fixable deductions still reported. The remaining
debt is mostly four compiler labels, 34 repeated opcode constants, five
undefined scratch/ABI-ish temporaries, and one scorer-reported Hungarian issue.
The plate comment documents the opcode families and the stable locals are
typed/named; forcing every switch label into a custom name is not a good
tradeoff for this pass.

## Loader Breadcrumbs

No literal `KH11` string was found in the binary string table. The currently
known `ECA_AttackHitData` xref is `InitializeUEnumLuxorGameELuxCharacterAssetType
@ 0x1409FA0E0`, which registers the `ELuxCharacterAssetType` enum and includes
the `ECA_AttackHitData` entry. That is a useful asset-type breadcrumb, but it is
not the KHD/KH11 loader. The next loader pass should follow enum consumers and
asset-array users rather than searching for a magic string.

## Next Ghidra Targets

1. Find the native KHD/`KH11` loader path from `ELuxCharacterAssetType::ECA_AttackHitData`
   consumers and asset-array users; direct string search found no literal `KH11`
   and the current `ECA_AttackHitData` xref is enum registration only.
2. Audit and promote existing `FLuxMoveBankSlotView` fields for the Scuffle
   `0x48` move entry, especially unresolved `+0x04`, `+0x08`, `+0x10/+0x14`,
   `+0x18`, `+0x20/+0x28`, and `+0x36`.
3. Deep-document `LuxMoveVM_ExecuteBytecode`, `LuxMoveVM_RunBytecodeScript`, and
   `LuxMoveVM_ExecuteBankSlotScript` to anchor VM opcodes and CALLCOND families.
4. Split or rename more `LuxBattleAttackCell` fields only where native reads
   prove Scuffle's labels.
5. Map attack mask bits to the runtime KHit body/attack/hurt lists:
   `chara+0x44478`, `chara+0x44498`, and `chara+0x444B8`.
6. Trace `LuxObject_LookupFName_MovePlayData`,
   `ALuxBattleMoveCommandPlayer_GetMovePlayParam_Impl`, and
   `FLuxBattleMoveListTableRow` for UI metadata round-tripping.
7. Build a tiny round-trip parser test using one copied KHD asset before any
   write UI exists.

No replay seek test was run for this pass because only Ghidra metadata and docs
were changed; no HorseMod runtime code was modified.
