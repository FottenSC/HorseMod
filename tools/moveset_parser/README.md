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

### Caveat about section-A tag bytes

Section A `+0x5F` bytes are **all 0x00 or 0xFF** in every shipped file
(6881 x 0x00, 90 x 0xFF, no other values). This is a flag finding:
the `FLuxMoveDataEntry` typeTag enum that `LuxMoveVM_UpdateMoveDataTable`
dispatches on (0x04..0x1E) does NOT appear on-disk in section A.

Likely interpretation: section A is an **index / lookup record** array
(one slot per move; engine rewrites the type-tag byte after asset-loader
pre-processing). The actual typed move-record payload lives in
section C, which has variable record sizes and was not auto-decoded
by the parser (the engine walks it via in-memory pointer fixups
done by the UE4 asset loader before the C++ loader sees it).

## What it parses

| Extension | Source folder | Format | Coverage |
|-----------|--------------|--------|----------|
| `.khd` | `Battle/hdr/` | KH11 moveset header — 3 sub-sections | header + section-shape ✓; section C variant records TBD |
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
+0x1C..+0x2F     u16 pairs (counts / aux indices)
+0x30..section_A   large pad / trailer (~200 KB in shipping files)
```

**Section A** is a 0x70-stride array of lookup records (150-452 entries
per chara). Type-tag byte at `+0x5F` is always `0x00` (slot in use) or
`0xFF` (sentinel / cleared). The full FLuxMoveDataEntry layout
documented in Ghidra applies AFTER the UE4 asset loader's in-memory
preprocessing; the on-disk variant is a related-but-different shape.

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
| C (offset @ +0x18) | (event records, partial) | variable | Move-event records — typed header-prefix (0x30 stride, ~3-94 records) + opaque payload (~99% of section) |

**Section A field list per cell** (canonical from Ghidra
`LuxBattleAttackCell` struct):

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| +0x00 | qword | `u64SlotMask` | Per-attacker bits; bits 31/55 = throw partition |
| +0x32 | ushort | `wU16AttackFlags` | High/Mid/Low/UB bitmask (see below) |
| +0x36 / +0x38 | i16 | active-frame window | start/end (60ths) |
| +0x3A | i16 | `wI16BaseDamage` | **damage on hit** |
| +0x44 | i16 | `wI16BlockstunFrames` | block disadvantage |
| +0x46 / +0x48 | i16 | hitstun (standing / standing-air) | hit advantage |
| +0x4C / +0x4E | i16 | hitstun (crouch / crouch-air) | |
| +0x50 / +0x52 / +0x54 | i16 | reaction IDs (standing / air / throw-escape) | indexes into chara+0x43DD8 (0x14 stride) |
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
+0x04  u32[N+1] motion_offsets    (last is end-sentinel)
@offset[i] : per-motion frame data (HgMotion-private layout)
@end_sentinel..file_end : authored lookup trailer (always present, 2-18 KB)
```

`chr0ff.mot` is the "common motion" file (783 motions, only 4 empty —
i.e., almost fully populated, unlike per-chara files which are 34-81%
empty since the table is reserved for the full motion-id namespace).

### CPU AI (`.dtp`)

Same generic offset-table layout as `.mot`. Always **9 sections**, with
several at FIXED sizes across all characters (strong invariants):

| Index | Size (all chars)       | Role |
|-------|------------------------|------|
| 0     | 27184-73424 (varies)   | Decision data — u16 weight/value pairs |
| 1     | 4400-10064 (varies)    | Personality custom table — u16 slot count at +0x08, per-slot 18-byte rows |
| 2     | **always 2320**        | Fixed weight slot block (1 entry) |
| 3     | 560-1264 (varies)      | (additional weight block) |
| 4     | 83200 or 89600         | Big lookup table (only 2 distinct sizes) |
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

## What's not implemented

- Motion frame data inside `.mot` sections (HgMotion's internal animation format).
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
```
