# SC6 Moveset Parser

Python parsers for the per-character binary data files used by
**Soulcalibur VI** (Project Lux). Format documentation derived from
reverse engineering the v2.31 `SoulcaliburVI.exe` retail binary via
the `bethington/ghidra-mcp` tools.

## Web viewer

A local Flask web viewer is included for interactive browsing of the
parsed data. Read-only — focuses on functionality.

```sh
pip install -r requirements.txt    # installs flask
python viewer.py
# then open http://localhost:5000
```

Override the data directory with `BATTLE_ROOT`:

```sh
set BATTLE_ROOT=D:\some\other\dump\Battle  &&  python viewer.py
```

Routes:

| Path | Shows |
|------|-------|
| `/` | Character list with file-presence matrix |
| `/c/<cid>` | Per-character summary (KHD sections, motion/AI/hit counts) |
| `/c/<cid>/khd` | KHD overview (3 sub-sections) |
| `/c/<cid>/khd/<sec>` | Section contents — section A as a sortable entry table, B/C as record-stride hex |
| `/c/<cid>/khd/<sec>/<idx>` | Single `FLuxMoveDataEntry` with all decoded fields + raw 0x70 hex |
| `/c/<cid>/khd/raw?offset=N&length=M` | Arbitrary-offset hex dump of the .khd |
| `/c/<cid>/mot` | Motion offset table (collapsed empty runs) |
| `/c/<cid>/mot/<idx>` | Single motion bytes (hex dump) |
| `/c/<cid>/dtp` | AI personality sections (with role hints for sections 3 / 6 / 8) |
| `/c/<cid>/dtp/<sec>` | Section bytes (hex dump) |
| `/c/<cid>/hit/atkhit` | Attack hit volumes — table with tag filter |
| `/c/<cid>/hit/bodyhit` | Body hit volumes |
| `/c/<cid>/hit/yararehit` | Reaction hit volumes |

Edit `CHARA_NAMES` in `viewer.py` to add or fix character display names
(many DLC slots in the 060-066 range aren't pre-filled).

## Validation status

Run `python validate.py` to validate against every file in
`dump/Battle/{hdr,mot,cpu,hit}/`. Current result: **147/147 files OK**
across 24 fully-shipped characters + 3 partial slots (000, 013, 0ff —
shared / DLC / training-mode placeholders).

Cross-character consistency checks confirm:

- All 24 `.khd` files have exactly 3 monotonic sub-section offsets,
  section A is always a multiple of `0x70` (150-452 entries / chara),
  section B is always a multiple of `6` (small triple-records).
- All 25 `.dtp` files have exactly **9 sections** — perfect consistency
  across characters, validating the AI personality format documented
  via `LuxBattle_InitCpuPersonalityData`.
- All 24 `.mot` files have a small "trailer" block (2 KB - 18 KB) after
  the offset table's end-sentinel — authored lookup data the engine
  reads via a separate code path.
- All 72 hit `.dat` files walk cleanly as i16-tagged streams ending with
  a `-1` sentinel. **`bodyhit` is rock-stable at 17 sphere records per
  chara**, `yararehit` at 18-19 sphere + 1 area, `atkhit` varies by
  character (30-72 records, sphere + area, no fix-area found).

## What it parses

| Extension | Source folder | Format | Coverage |
|-----------|--------------|--------|----------|
| `.khd` | `Battle/hdr/` | KH11 moveset bank — slots, attack/non-attack cells, event records | decoded core tables ✓; section-C opaque payload remains partial |
| `.mot` | `Battle/mot/` | Motion / animation offset table | header ✓ (section internals = raw frame data, not parsed) |
| `.dtp` | `Battle/cpu/` | CPU AI personality table | header ✓ (sections 3 / 6 / 8 documented) |
| `.dat` | `Battle/hit/` | KHit i16-tagged stream | record-walk ✓ (sphere/area/fixarea variant tags) |
| `.vtb` | (chara init) | Visual-tag / frame-event buffer | header ✓ (0x84-byte entries) |
| `.lpd` / `.lpb` | (chara init) | Pose-delta motion blend wrapper | both layers ✓ |

## Usage

```sh
# Parse a single file (auto-dispatched by extension)
python parse.py path\to\hdr001.khd

# Multiple files
python parse.py path\to\*.dtp

# Print more entries / records per section
python parse.py path\to\hdr001.khd -n 50

# Dump raw bytes when the section format isn't auto-identified
python parse.py path\to\hdr001.khd --raw

# Run the full validation sweep
python validate.py
python validate.py --verbose      # per-file detail
python validate.py --root <path>  # use a different dump root

# Regenerate one or more character payloads without racing shared indexes,
# then rebuild the roster and v2 lookup index after every batch completes.
python export_webui_data.py --cids 001,012
python export_webui_data.py --rebuild-index-only
```

Community frame-data spreadsheet handling is comparison-only. Keep local
downloads and generated comparison reports out of source control:

```sh
# Compare exported parser/webui JSON against a downloaded community sheet.
python compare_community_vs_parsed.py --data-dir webui\public\data --community-xlsx path\to\community_framedata.xlsx --report path\to\comparison.json

# Or compare against a pre-parsed local JSON snapshot.
python compare_community_vs_parsed.py --data-dir webui\public\data --community-json path\to\community_framedata.json --report path\to\comparison.json

# Build the experimental player-facing family calibration report.
python player_move_families.py --community-xlsx path\to\community_framedata.xlsx --parsed-data-dir webui\public\data --summary-json path\to\player_family_report.json
```

Programmatic use:

```python
from luxformats import parse_auto, parse_khd, parse_mot, parse_dtp, parse_hit_dat

khd = parse_auto("hdr001.khd")
for section in khd.sections:
    print(section.section_index, section.offset, section.size)

mot = parse_auto("chr001.mot")
print(f"{mot.count} motions, {sum(1 for s in mot.sizes if s == 0)} empty")

hit = parse_auto("atkhit001.dat")
for r in hit.records:
    if r.tag == 0:
        print(f"sphere @ bone {r.slot}: ({r.pos_x}, {r.pos_y}, {r.pos_z}) r={r.radius}")
```

### Move grouping model

The parser exports three move granularities:

| Layer | JSON field | Use |
|-------|------------|-----|
| Slot/debug moves | `khd.flatMoves` | Every attack-cell-bearing slot reachable from bytecode; useful for RE/debugging, too granular for player-facing move lists |
| Unique official moves | `movelist.moves` | One row per `MoveListID`; category-only repetitions are collapsed |
| Grouping hints | `movelist.moveGroups` / `moves[].groupIds` | Non-destructive links between rows that should often be displayed together |

`moveGroups` includes these player-facing relationships:

- `native-route-alternative`: same official name and context resolving to the
  same native slot/cell route, e.g. `B.A` and `B.6A` Bear Tamer.
- `native-timing-variant`: exact CPUAI input-mask sequence with different
  authored durations, e.g. a normal and fast input variant.
- `input-family`: heuristic; same condition and exact/extended input at a
  `.` or `~` continuation boundary, e.g. `214A` grouped with `214A.A`.

The 5,898 authored category listings collapse to 4,994 unique moves across the
28 playable characters. Each move retains `listingOrders`,
`categoryMemberships`, and category-specific MovePlay `authoredVariants` for
diagnostics; none of those memberships creates an additional player row.

The production `playerMoveFamilies` layer is native-only. Native timing and
command-prefix relationships are tagged `native-inferred`.
`player_move_families.py` remains comparison-only calibration tooling; its
external rows and inferred edges are never imported by the exporter or bundled
UIs.

### Native move evidence

Schema-v2 export decodes each unique official move's `MainIndex` through section
1 of the matching `cpuaiNNN.dtp`; it is a MoveVM command-definition ID, never a
KHD cell or slot index. The offline dispatcher evaluates the native standing
selector (`packed 0x304E`) until its first target commits to combat lane zero.
Later CPUAI button steps are input publications owned by that active lane's
transition graph; they are never replayed as independent standing moves or
treated as contacts. For proofs that need the actual lane target rather than an
observation, the full-timeline resolver executes move 0's native order:
observation coordinator `0x3048`, live
transition coordinator `0x3049`, then the `0x300B` author of globals
`0x44/0x46/0x47`. It then follows only game-authored KHD transitions. A single route is tagged
`heuristic`, multiple viable slots are `ambiguous`, and malformed or unsupported
paths fail closed. Category memberships never create rows or families.

`native_frame_analysis.py` is reusable once a KHD slot/cell route reaches the
audited common attack-setup helper. It derives the attacker endpoint from the
native `0x7600 - recoveryLead` transition convention, then combines that with
the defender's block, normal-contact, or Counter Hit stun column. The metric
inherits the route's provenance (`native-confirmed` or `native-inferred`).
Native contact mode 11 is CounterHit and selects attack-cell offset `+0x48`.
That field is a shared special-contact column (also used by other non-normal
contact modes), not a CH-exclusive or airborne column.

## File format references

### KH11 (`.khd`) on-disk layout

```
+0x00  char[4]   "KH11"
+0x04  u32       reserved (0)
+0x08  u32       reserved (in-memory: bDecoded flag, 0 on disk)
+0x0C  u32       per-file id / hash (purpose TBD, varies per chara)
+0x10  u32       off_section_A
+0x14  u32       off_section_B
+0x18  u32       off_section_C
+0x1C..+0x2F     four u16 start/count bucket pairs for packed move ids
+0x30..section_A   FLuxMoveBankSlotView table, then opaque/unused bytes
```

**Section A** is a 0x70-stride array of `LuxBattleAttackCell` records
(150-452 entries per character). Byte `+0x5F` is the high byte of the
`wU16HitboxGroupBitfield` at `+0x5E`, not an independent type tag; its
observed `0x00`/`0xFF` values distinguish ordinary group masks from cleared
sentinels.

**Section B** is a 6-byte stride table (small, 9-61 records). Each
record is three u16: `(value, marker, flags)` where `marker` is usually
`0xFFFD` (the "default reaction" sentinel) but sometimes `0` for
special-cased records.

### Engine-struct correspondences (deep RE pass 2026-05-14)

The .khd file IS the engine's `FLuxMoveBank` (48 bytes), with three
sub-arrays:

| KHD section | Engine type | Stride | What it is |
|-------------|-------------|--------|-----------|
| A (offset @ +0x10) | `LuxBattleAttackCell` | 0x70 | **Attack property cells** — the FULL list of attack-state slots with damage / frames / blockstun / hitstun / reaction IDs / ranges per slot |
| B (offset @ +0x14) | `LuxBattleNonAttackMoveDescr` | 0x06 | Non-attack move descriptors — `(damage_multiplier, passthrough_tag, duration_60ths)` triples |
| C (offset @ +0x18) | (event records, partial) | variable | Move-event records - typed header-prefix (0x30 stride, ~3-94 records) + opaque payload (~99% of section) |

**Slot table (`FLuxMoveBankSlotView`, stride 0x48):**

Packed move ids are resolved like `LuxMoveVM_ResolveBankSlot`: bits
15..12 select one of the four `FLuxMoveBank` buckets at header
`+0x1C..+0x2F`, bits 10..0 select the slot within that bucket, and bit
11 is masked out by the native slot-index calculation.

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| +0x00 | ushort | `wAnimationIndex_00` | motion id |
| +0x02 | ushort | `wMotionPlaybackParam_02` | native 16-bit slot header field, exact meaning still open |
| +0x06 | ushort | `wMotionFlags_06` | native 16-bit slot flags |
| +0x30 | float | `flPlaybackSpeed60ths_30` | playback speed seed; transition code divides by 60 |
| +0x34 | ushort | `wTotalFrames` | authored total animation frames |
| +0x38 | u32 | `dwBytecodeOffset_38` | bank-relative stack-VM bytecode/cancel offset |
| +0x3C..+0x46 | i16[6] | `nCellBoneIndexPerVariant` | attack-cell refs; refs with bit 0x1000 point at Section-B throw cells |

**Section A field list per cell** (canonical from Ghidra
`LuxBattleAttackCell` struct):

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| +0x00 | qword | `u64SlotMask` | Per-attacker bits; bits 31/55 = throw partition |
| +0x32 | ushort | `wU16AttackFlags` | High/Mid/Low/UB bitmask (see below) |
| +0x36 / +0x38 | i16 | active-frame window | start/end (60ths) |
| +0x3A | i16 | `wI16BaseDamage` | **base damage on hit**; runtime multipliers are applied later |
| +0x44 | i16 | `wI16BlockstunFrames` | block disadvantage |
| +0x46 / +0x48 | i16 | hitstun (base posture, normal / special contact) | hit / CH advantage |
| +0x4C / +0x4E | i16 | hitstun (alternate posture, normal / special contact) | |
| +0x50 / +0x52 / +0x54 | i16 | reaction IDs (normal / special contact / reaction-row selector) | indexes into chara+0x43DD8 (0x14 stride) |
| +0x5E | ushort | `wU16HitboxGroupBitfield` | **0xFFFF = cleared sentinel** |
| +0x62..+0x65 | i8 | ranges (stand-min/max, crouch-min/max) | |

**wU16AttackFlags bits** (confirmed):
```
0x001 HighBlockable    0x080 HighAttack
0x002 LowBlockable     0x100 SpecialFraming (BA candidate?)
                       0x200 Unblockable
```
Combined: `High+Low blockable → Mid`, `High only → High`, `Low only → Low`,
`neither + UB → Unblockable`, slot-mask bits 31/55 → Throw.

#### Runtime damage multiplier path (Ghidra pass 2026-06-25)

Section A gives the authored base damage and hit properties. The final
in-game damage is not just `wI16BaseDamage`; it is multiplied through the
runtime hit-damage factor path on `ALuxBattleChara`.

Key native path:

1. `LuxBattleChara_ComputeHitDamageFactors @ 0x140343630` resolves attacker
   as `pDefender->pOpponentChara`, resets the defender's final damage-rate
   lane, and fills a `FLuxHitDamageRateFactors_Partial` output vector.
2. `LuxBattleChara_AccumulateDefenseRates @ 0x140344D10` consumes queued
   defender rates at the `ALuxBattleChara` overlay around `+0x3F0..+0x420`
   and seeds the first factor lanes.
3. `LuxBattleChara_CalcDefenseDamageRate` contributes move/posture/guard
   impact defense rates.
4. Stored attacker rates at `attacker+0x3F4` and `attacker+0x2B4B4` are
   multiplied into the defender damage-rate lane and copied into the factor
   vector.
5. The hit-classifier result can force special rates: some outcomes reset to
   `1.0`, some force `0.0`, and one stance-clash case forces `0.5`.
6. Style mismatch, threshold/stat procs, and
   `PlayerExtraSkill_AccumulateAttackDamageRates` add the late skill-rate
   lanes.
7. The full 0x64-byte `FLuxHitDamageRateFactors_Partial` is copied to the
   attacker-side last-hit factor cache at `attacker+0x2B440`.

Practical parser/modding implication: the parser can safely report base
damage from KHD section A, but exact runtime damage needs either a live trace
of `FLuxHitDamageRateFactors_Partial` or a separate runtime-state model for
defense queues, stat-table keys, skill rates, style mismatch, and
the attacker stored-rate lanes. Do not treat the current
`ALuxBattleChara_Partial` field names around `+0x3F0..+0x420` as globally
stable; that range has overlapping actor-array names from older recovery, but
the hit-damage functions use it as float damage-rate state.

Broader runtime notes: `../../docs/investigations/luxbattle-runtime-map-2026-06-25.md`.

**Section C** is the bulk authored-data block (660 KB - 813 KB per
chara) split into two regions:

- **Header-record prefix**: 3-94 records (144-4512 bytes) of `0x30`-byte
  typed records. Each record:
  `[u8 type_tag][u8 subtype][u16 0x0000][u32 index][24 bytes payload]`.
  `type_tag` is from the FLuxMoveDataEntry enum (0x00..0x1E) plus the
  special `0xD6` count marker that always opens the prefix. The walker
  in `parse_khd` exposes these as `section.c_prefix_records`.
- **Opaque payload**: the remaining 99%+ of the section. Hit-cell and
  per-move data the engine accesses via in-memory pointer fixups built
  by the UE4 asset loader before the C++ loader sees it. Not yet decoded.

#### FLuxMoveDataEntry (in-memory shape, 0x70 bytes)

From `LuxMoveVM_UpdateMoveDataTable @ 0x14038F7D0`:

| Offset | Type | Purpose |
|--------|------|---------|
| +0x00 | u16[4] | Primary motion-id array (special-move types `0x0B` / `0x1E`) |
| +0x08 | u16[8] | Secondary motion-id array (same) |
| +0x30 | u64 | Name-string offset (file-relative; descrambled with `byte - 0x40`) |
| +0x38 | u16 | Motion ID — remapped via chara remap table when type=`0x06` |
| +0x3C | u16 | Motion ID — type `0x08/09/1C/1D` (raw) or `0x10` (skip if `0xFFFF`) |
| +0x3E | u16 | Motion ID — type `0x07/1B` (remapped) |
| +0x4D | u8 | Count for primary motion-id array (0..4) |
| +0x4E | u8 | Count for secondary motion-id array (0..8) |
| +0x5F | u8 | Type tag — see dispatch table in `MOVE_TYPE_NAMES` |
| +0x60 | u16 | New-value replacement for `+0x3C` when type=`0x08/09/1C/1D` |
| +0x62 | u16 | Slot index (engine-assigned at merge time) |

### Motion table (`.mot`)

```
+0x00  u32      nMotionCount      (628-2046 per chara; 1641 for chr000)
+0x04  u32      reserved_zero
+0x08  u32[N]   motion_offsets    (indexed by anim id; no count+1 sentinel)
@offset[i] : per-motion frame data (HgMotion-private layout)
```

Ghidra reference: `LuxMoveVM_InitMotionPlayback @ 0x140300400` reads
`motionBank[animIndex + 2]`, so animation `0` uses the offset at file
`+0x08`. Section size is computed from the next offset, or EOF for the final
entry. Empty motions are represented by repeated offsets.

`chr0ff.mot` is the "common motion" file (783 motions, only 4 empty —
i.e., almost fully populated, unlike per-chara files which are 34-81%
empty since the table is reserved for the full motion-id namespace).

### CPU AI (`.dtp`)

Uses the older generic offset-table layout: count at `+0x00`, offsets starting
at `+0x04`, and a count+1 end sentinel. Always **9 sections**, with
several at FIXED sizes across all characters (strong invariants):

| Index | Size (all chars)       | Role |
|-------|------------------------|------|
| 0     | 27184-73424 (varies)   | Decision data — u16 weight/value pairs |
| 1     | 4400-10064 (varies)    | Personality custom table — u16 slot count at +0x08, per-slot 18-byte rows |
| 2     | **always 2320**        | Fixed weight slot block (1 entry) |
| 3     | 560-1264 (varies)      | (additional weight block) |
| 4     | 83200 or 89600         | 8-byte reaction-weight entries, indexed as bank × 800 + move definition |
| 5     | **always 34880**       | Array of 15 × 2320-byte weight blocks (`u32 count=15 @ +0x00`, then offset table, then 15 entries) |
| 6     | **always 0**           | Personality alternate data (empty — patched in at runtime via `LuxBattle_SetCpuPersonalityAlternateData`) |
| 7     | **always 1488**        | Always starts with `"PSNL"` magic — custom personality vtable trigger |
| 8     | 2192-5024 (varies)     | (per-character override / scratch) |

### Hit data (`.dat`) — i16-tagged stream

Each record starts with an i16 tag at +0x00; the tag determines stride:

| Tag | KHit kind | Stride | Variant tail layout |
|-----|-----------|--------|---------------------|
| `0` | KHitSphere | `0x20` | `+0x08 float[4]` (x,y,z,radius); `+0x18 u32 id`, `+0x1C u32 reserved` |
| `1` | KHitArea | `0x28` | See `KHitChk_InitAreaFromStream @ 0x14030E3A0` |
| `2` | KHitFixArea | `0x30` | See inlined branch in `Lux_KHitChk_DeserializeLinkedList @ 0x14030C940` |
| `<0` | (end of stream) | — | Typical sentinel: `0xFFFF` (i16 = -1) |

Common 8-byte prefix on all three:

```
+0x00 i16   tag
+0x02 u16   slot      (defender bone slot 0..63; engine: `1 << (slot & 0x3F)`)
+0x04 u32   flags     (authored; copied to in-memory KHit+0x10)
```

Per-character invariants (verified across all 24 shipped chars):

- `bodyhit*.dat`: **always 17 sphere records**, 0 area, 0 fix-area
- `yararehit*.dat`: **18-19 sphere + exactly 1 area**, 0 fix-area
- `atkhit*.dat`: **30-72 records, mix of sphere + area**, 0 fix-area
  (FixArea tag 2 is documented in the engine but doesn't appear in any
  shipped per-character file — likely used only by special / boss moves
  in section C of the .khd payload)

### VTB (`.vtb`)

```
+0x00  char[4]   "vtb\0"
+0x04  u32       reserved
+0x08  u32       version           (must = 0x1002)
+0x0C  u32       entry_count
+0x10  u32       data_offset
+0x14  u32       reserved
+0x18..data_offset  header_data
@data_offset : entries[entry_count] x 0x84 bytes
```

### LPD + inner LPB (`.lpd`)

LPD wrapper:

```
+0x00  u32   nSections    (must = 3)
+0x04  u32   off_magic
+0x08  u32   off_data
+0x0C  u32   off_data_end
```

Inner LPB:

```
+0x00  char[4]   "lpb\0"
+0x10  u32       version          (must = 0x201)
+0x18  u32       total_len
+0x20  u32[7]    sub_section_lens
+0x40  bytes     packed sub-section data
```

## Static combo analyzer

`static_combo_analyzer.py` is the asset-only entry point for combo science.
Its scenario JSON identifies the attacker/style, opener and forced contact
class, held follow-up route, defender action/direction, spacing policy, and
optional observed labels. It consumes KHD, MOT, KHit streams, NMD/profile
references, `yarare.dat`, and MoveBank event records. The
first scenario is Astaroth lethal slot 372 into held slots 341→342 against
defender-left ukemi slot `0x8E`; Maxi and Setsuka are intentionally absent.

The recovered native sequence for that case is:

1. Opener contact is coordinate 17. The real lethal classifier selects the
   special-contact reaction row 1037; training slot 374 is evidence for the
   equivalent authored cell/event route, not the simulated input.
2. Opener coordinate 56 admits slot 341, 39 ticks after contact. A continuous
   B hold satisfies its 32-tick input-history predicate while the unit-speed
   lane independently reaches coordinate 32.
3. Slot 342 starts at coordinate 10. Cell 112 is active at coordinates
   13..16 (contact ticks 74..77 in this route). Its authored `u64SlotMask` is
   `0x1`, so KHit attack slot 0 is selected from the cell instead of being
   hardcoded by the analyzer.
4. Every tick publishes MOT root/effect movement, composes the compact NMD
   collision hierarchy, refreshes KHit world centres, and applies strict sphere
   overlap (`distance² < (r1+r2)²`). Both the row-1037 reaction entry and slot
   `0x8E` execute effect `0x0027`, which disables all BODY nodes; no later
   re-enable occurs before held slot 342's active frames, so BODY separation is
   inactive throughout this scenario window. Hurt rows above 21 are excluded
   by the damaging classifier even if geometry exists.

The row-1037 admission boundary is now static-native rather than a terminal-
coordinate probe. Lethal cell 145 publishes a 52-step counter. Input selection
runs before the post-lane counter decrement, so tick 52 still observes a
nonzero counter and the first legal grounded selection is tick 53. Slot `0x78`
then executes its one-argument `TransitionAuthor_06(0x008E)` call. The shared
author initializes start/threshold to zero and raises the threshold-now flag;
`LuxMoveVM_ExecuteOpStream` rechecks and commits `0x8E` in that same lane-0
invocation. The first ukemi sampler coordinate is therefore zero on tick 53.

Lane ownership matters independently of that timeline. Defender reaction
playback is lane 1 (solver slots 2/3), while grounded/ukemi playback is lane 0
(slots 0/1). Direct root motion processes slots 0..3 in order and attenuates
an earlier slot by `(1 - laterWeight)` for every later active slot. A full-
weight lane-1 reaction can therefore suppress lane-0 ukemi root without
stopping lane 0's sample cache from advancing. On the attacker, slot 341's
three-argument `CALLCOND 0x07` authors slot 342 on lane 1 at start/threshold
coordinate 10; it does not replace lane 0 in place.

The analyzer still emits `complete: false`. Its pose-state output separates the
four ordered main blend lanes from the fifth physical auxiliary playback
record. The authored-pose API rejects active controller/IK gates instead of
substituting identities, and
Seong Mi-na's `0x8E` descriptor is a concrete motion-flag-`0x80` main-analytic-
IK case. Exact playback-cache/lane-end behavior, facing retrack, and the
remaining controller/IK gates are still open. BODY is proven inactive in this
window and is no longer an unresolved spacing input. Current KHit geometry does
not reproduce the 26 labels, so
the report contains no prediction. Observed hit/escape labels are never used
to choose timing or geometry.

The shipped reaction selection itself gives a strong label-free partition:
all nine reported catches select common reaction motion `0x13DB` or `0x1477`;
all seventeen reported escapes select `0x149A` or `0x1478`. This identifies
reaction selection as the first causal difference, but it is not accepted as
the final explanation until KHit overlap reproduces the observations.

Example:

```powershell
python tools/moveset_parser/static_combo_analyzer.py `
  --battle-root dump/Battle `
  --output astaroth-left-ukemi.json `
  --markdown-output astaroth-left-ukemi.md
```

## What's not implemented

- Remaining HgMotion runtime controller/IK branches beyond the authored core
  channel decoder and compact collision-pose composition.
- AI personality custom-table row decoding (`.dtp` section 3, 18-byte rows).
- VTB per-entry semantics (0x84 bytes — opcode + params).
- LPB sub-section internals.
- `.khd` section C variable-record dispatch table.

These are all reachable from in-game traces by hooking the relevant
loader functions (see plate comments in Ghidra for entry points).

## Ghidra cross-reference

Key addresses (all in `SoulcaliburVI.exe` retail v2.31):

```
LuxBattleChara_LoadMovesetEntries_AndBoneData   0x140312040
LuxMoveVM_UpdateMoveDataTable                   0x14038F7D0
LuxMoveVM_InitStaticMoveDataTable               0x14038F6F0
LuxMoveVM_LoadVTBFile                           0x14038EFA0
LuxBattle_BindLPDMotionData                     0x1402F7670
HgMotion_BindLPBData                            0x14038B740
Lux_KHitChk_DeserializeLinkedList               0x14030C940
LuxBattle_InitCpuPersonalityData                0x140364950
LuxMoveVM_InitCharaFromMoveTable                0x140309B20
LuxBattleChara_ComputeHitDamageFactors          0x140343630
LuxBattleChara_AccumulateDefenseRates           0x140344D10
```

In-Ghidra structs (created during this RE pass):

```
FLuxMoveDataEntry      0x70 bytes — single move entry (in-memory)
FLuxKhdFileHeader      0x50 bytes — .khd header
FLuxMotFileHeader      variable    — .mot header
FLuxDtpFileHeader      variable    — .dtp header
FLuxVtbFileHeader      0x18 bytes — .vtb header
FLuxLpdFileHeader      0x10 bytes — .lpd wrapper
FLuxLpbBlockHeader     0x3C bytes — inner LPB block
FLuxHitDamageRateFactors_Partial                0x64 bytes - runtime damage factor vector
FLuxPlayerExtraSkillDamageRateScratch_Partial   0x18 bytes - vftable + 4 skill damage-rate lanes
```
