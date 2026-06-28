# SC6 Player-Facing Move Grouping Study - 2026-06-28

## Question

How can the moveset parser/tool reproduce the way Soulcalibur players group
moves, rather than exposing only native KHD slots, attack cells, or duplicated
DA movelist rows?

This study uses three evidence sources:

- Community frame data sheet rows, because they encode player-facing command
  rows and ordering.
- Current `tools/moveset_parser` export data, including `movelist.moveGroups`.
- FottenSC/Scuffle, because it already parses native `KH11` movelists and
  recovers commands/frame rows from cancel blocks.

## Core Finding

Player grouping is hierarchical:

```text
move family / string
  -> player-facing command rows
       -> native slots / command sets / attack cells / hit timeline
```

The current parser mostly has the bottom and middle layers. It has native cells,
slots, DA movelist rows, and two lightweight group hints. What players expect is
the top layer too: an expandable family such as an `A` string, `6A` string,
hold/release family, or stance-transition family, while preserving the
individual rows (`A`, `AA`, `AAA`, `AAB`, etc.) that have distinct frame data.

So the goal is not to merge rows away. The goal is to show them under the
family players already use mentally.

## What The Community Sheet Implies

The sheet keeps separate rows for command prefixes and branches:

- Mitsurugi: `A`, `AA`, `AAA`
- Taki: `A`, `AA`, `AAA`, `AAB`, `AAK`, `A6`
- Sophitia: `4A`, `4AA`, `4AAA`, `4(A)`, `4(A)AAA`, `4AB`, `4(A)B`
- Siegfried: `A`, `AA`, `AAA`, `AAB`, `AA(B)`, `AA4`, `(A)`
- Maxi: many stance-transition suffixes such as `~ Right Cross`,
  `~ Left Outer`, `~ Behind Lower`

That says player grouping uses these axes:

| Axis | Meaning |
| --- | --- |
| Stance/context | `Neutral`, `FC`, `WR`, `BT`, `SC`, named stances, etc. stay distinct unless explicitly linked as aliases. |
| Command prefix tree | `A -> AA -> AAA/AAB/AAK`; `6A -> 6AA/6AB`; branches matter. |
| Damage/hit-level prefix | A strong signal that one row extends another: `[8]`, `[8,8]`, `[8,8,10,16]`. |
| Root name before `~` | Stance-transition variants like `Starlight Blade` and `Starlight Blade ~ Angel Step` belong together. |
| Hold/release skeleton | `4A` and `4(A)` are separate rows but same family. |
| Direction alternatives | `7A`, `8A`, `9A` with the same name are usually alternatives of one jumping/step family. |
| Branch suffixes | `AA4`, `AAA6`, `6BB4`, etc. are transition branches, not unrelated moves. |

## Reproducible Calibration Pass

I checked in a reproducible calibration tool:

```powershell
cd E:\myMods\tools\moveset_parser
python player_move_families.py --community-xlsx C:\Users\prest\AppData\Local\Temp\sc6_community_framedata.xlsx --parsed-data-dir webui\public\data --summary-json C:\Users\prest\AppData\Local\Temp\sc6_player_family_calibration_report.json --examples 10
```

The tool builds community-family trees using:

- command tokenization that preserves case, holds, chords, and direction tokens,
- same stance/context,
- command-prefix links,
- damage/hit-level prefix confirmation,
- root-name-before-`~` confirmation,
- hold skeleton equivalence,
- directional alternative equivalence,
- explicit parent/child edges with confidence and reasons,
- false-merge guardrails for chords, stance/context, holds, and direction
  alternatives.

This is still calibration code, not a promoted parser export, but the counts
below are now reproducible and diffable.

Results on the downloaded community sheet:

| Metric | Count |
| --- | ---: |
| Community rows | 5,418 |
| Implied player families | 3,696 |
| Multi-row families | 907 |
| Rows inside multi-row families | 2,629 |

Family size distribution, capped at 10:

| Size | Families |
| ---: | ---: |
| 1 | 2,789 |
| 2 | 491 |
| 3 | 217 |
| 4 | 106 |
| 5 | 45 |
| 6 | 20 |
| 7 | 10 |
| 8 | 12 |
| 9 | 2 |
| 10+ | 4 |

Against current generated parser rows:

| Metric | Count |
| --- | ---: |
| Community rows with direct parser name+input key | 2,859 / 5,418 |
| Families with at least one direct parser anchor | 2,347 / 3,696 |
| Community rows in anchored families | 3,485 |
| Non-direct rows attachable through an anchored family | 626 |

That last number is the important one: a family-aware UI can explain hundreds
of rows that strict row matching leaves behind, even before deeper native
timeline reconstruction.

Example anchored-but-missing rows:

- 2B `Slash Sequence`: community has `A`, `AA`, `AAA`, `AAA6`, `AAA4`,
  `AAA8`, `AAAA`, `AAAB`, `AAB`; the calibration tree keeps them as one
  family with explicit prefix and direction-alternative edges.
- Taki has the largest per-character count in this pass: 62 non-direct
  community rows attach to a family with a direct parser anchor.
- Common pattern: prefix rows such as `6A` are missing while longer rows such
  as `6AA` or branch rows are present.

The largest generated families look like expected player strings:

- Talim `Weather Vane Buster`: `A`, `AA`, `AAA`, `AAB`, `AABA`, `AABA28`,
  `AABA6`, `AABA4`, `AABB`, `AAB(B)`, `AAA+B`, `A6`.
- Hwang `Triple Circular Heaven Kick`: `(3)(6)(9)K`,
  `(3)(6)(9)KK`, `(3)(6)(9)KKK`, hold variants, and Soul Charge branches.
- Taki `Shadow Banishment`: `B`, `BA`, `BAK`, `BA6K`, `BA4`, `BB`, `BBB`,
  `BB4`, `BK`, `B(K)`.
- 2B `Slash Sequence`: `A`, `AA`, `AAA`, `AAA6`, `AAA4`, `AAA8`, `AAAA`,
  `AAAB`, `AAB`.

## Current Parser Grouping Gap

Current generated group totals:

| Group kind | Count |
| --- | ---: |
| `input-family` | 923 |
| `duplicate-move-id` | 843 |

These are useful, but they are not enough for player grouping:

- `duplicate-move-id` is a native/data-layout clue, not a player concept.
- `input-family` only groups rows that already exist in the parser export.
- `input-family` only links exact/extended notation at `.` or `~` boundaries.
- It misses community-style compact prefix rows (`A`, `AA`, `6A`, etc.) when
  the game movelist only exposes the longer row.
- It misses sibling branches (`AAB` vs `AAK`, `6AA` vs `6AB`) unless they also
  form direct extension chains.
- It does not model hold/release or directional alternatives as first-class
  player relationships.

## Scuffle Reference Findings

Scuffle is not a community move-grouping table. It is a native `KH11` movelist
parser and live-frame-data tool. The useful ideas to port are:

- `Move` has one native move id, total frames, cancel address, and up to six
  attack indexes.
- `Move.get_frame_data()` emits one frame-data row per attack index.
- `Movelist.parse_neutral()` recovers command strings by walking cancel blocks.
- `Link` records transitions, button conditions, leave/enter frames, and whether
  a transition is an auto cancel.
- Scuffle follows the native graph; it does not try to preserve player family
  names from DA_MoveListTable.

The strongest sanity result was that Scuffle can disagree with the current
parser's row-to-cell anchor in exactly the suspicious cases.

Example:

- Our DA row for Mitsurugi `Double Binder / 6A.A` had selected `cellIdx 36` and
  `slotIdx 268`.
- Scuffle's command recovery for native move/slot `268` reports command `4A`
  with damage `20` and startup `20`, matching community `Drawn Breath / 4A`
  much better than `Double Binder`.

That confirms `DA_MovePlayData.MainIndex -> one cell` is a weak anchor. A valid
cell is not necessarily the player row's cell.

## Proposed Tool Model

Add a new top-level export alongside the raw movelist rows:

```json
{
  "playerMoveFamilies": [
    {
      "id": "player-family-001-00042",
      "kind": "command-string",
      "confidence": "strong-community | community-calibrated | native-inferred | weak | single-row",
      "context": "Neutral",
      "rootCommand": "A",
      "rootName": "Starlight Blade",
      "relations": ["prefix", "hold-variant", "stance-transition"],
      "rows": [
        {
          "id": "community-001-00042",
          "displayCommand": "A",
          "displayName": "Starlight Blade",
          "source": "community | movelist | native-inferred",
          "order": 42,
          "moveOrders": [],
          "moveIds": [],
          "nativeSlots": [],
          "attackCells": [],
          "timelineStatus": "resolved | partial | unresolved",
          "metrics": {
            "startup": 12,
            "damage": [8],
            "hitLevels": ["High"]
          }
        }
      ],
      "edges": [
        {
          "id": "edge-community-001-00042-community-001-00043-prefix",
          "parentRowId": "community-001-00042",
          "childRowId": "community-001-00043",
          "relation": "prefix",
          "confidence": "strong | medium | weak",
          "reasons": [
            "command-token-prefix",
            "damage-or-hit-level-prefix"
          ],
          "source": "community-calibration | native-cancel-graph"
        }
      ]
    }
  ]
}
```

Keep existing raw rows. Families are presentation/navigation metadata, not a
replacement for the native data.

The important schema change from the first draft is that families are trees,
not buckets. `relations` is just the summary; each relation must be an edge with
stable parent/child row ids, reasons, and confidence.

## False-Merge Guardrails

The calibration tests now cover the riskiest over-grouping cases:

- Chords are atomic: `A` is not a parent of `A+B`, and `(B)+(G)` is not a
  parenthesized extension of `B`.
- Stance/context boundaries are hard: neutral `4A` and `SC 4A` stay separate
  unless an explicit alias is later proven.
- Direction alternatives require the same root name and compatible command
  skeleton; `7A` and `8A` with different names do not group.
- Hold variants require the same command skeleton and compatible root name;
  `4A` and `4(A)` can link, but unrelated parenthesized commands cannot.
- `~ Stance` suffixes link through `stance-transition` edges only when the
  root name before `~` and command skeleton are compatible.
- Same move name alone is never enough to group rows.

Promotion risk notes from review:

- A command-prefix edge with no shared root name and no damage/hit-level prefix
  confirmation should be exported as `weak` or `unconfirmed-prefix`, not as a
  player-authoritative relationship.
- Forward/back direction alternatives (`4`/`6`) can encode different move
  semantics even when names match. Keep them visibly confidence-marked until
  native cancel/timeline evidence agrees.

## Implementation Plan

### Pass 1 - Player Command Tokenizer

Create a shared tokenizer for both parser and comparison code:

- Preserve case (`aB` is not always `AB`).
- Treat chords (`A+B`, `B+K`, `A+G`) as one token.
- Treat holds as a variant marker over the same skeleton token.
- Split command chains at actual button/direction token boundaries.
- Keep direction/motion tokens (`6`, `236`, `(3)(6)(9)`) separate from buttons.

This is required before adding new grouping kinds; string prefix by raw
characters is too risky.

### Pass 2 - Community-Calibrated Family Builder

Use the community sheet as an optional calibration/overlay source:

- Build families from community rows using the axes above.
- Attach each row to parser rows by exact name+input, name-root+input,
  command skeleton, and family sibling anchors.
- Mark every attachment with confidence and reason.

The community source should remain a reference/overlay, not native authority.

### Pass 3 - Native-Inferred Family Builder

When no community row exists, infer families from native/exported data:

- Start from DA movelist condition + command + root name before `~`.
- Use command-token prefix/branch links.
- Use `hitClasses` prefix as a player-facing confirmation signal.
- Use slot/cancel graph links for auto follow-ups and button branches.
- Use `CALLCOND 0x26` and transition edges to build hit timelines.
- Treat `MainIndex` direct-cell resolution as a hint, not authority.

### Pass 4 - Hit Timeline Per Player Row

For each family row, export:

- first-impact startup,
- active windows per hit,
- per-hit base damage list,
- total base damage,
- raw cell ids,
- slot ids,
- unresolved branch notes,
- whether the row came from community, DA movelist, native inference, or mixed.

This directly addresses why the current community comparison disagrees: it
compares one selected native cell to a player row, instead of comparing the
row's full hit timeline.

### Pass 5 - UI Behavior

Display families as expandable groups:

- family header: root command, root name, stance/context, relation badges,
  confidence,
- child rows: `A`, `AA`, `AAA`, branches, holds, stance-transition variants,
- each child row links to native move orders, slots, cells, and scripts,
- unresolved rows stay visible but clearly marked.

Players get the structure they expect; reverse engineers still get the native
objects underneath.

## Acceptance Criteria

The grouping work is good enough when:

- On community-calibrated characters, prefix rows no longer disappear just
  because no DA row exists for them.
- Families such as `A/AA/AAA/AAB/AAK`, `6A/6AA/6AB`, hold variants, and
  `~ Stance` variants appear under one expandable heading.
- The UI never hides native ambiguity: weak or inferred links are marked.
- A strict comparison can report family-row agreement separately from native
  cell agreement.
- Known bad anchors like Mitsurugi `Double Binder -> cell 36 / slot 268` are not
  presented as authoritative player frame data unless the timeline proves them.

## Bottom Line

To match how players care about moves, the tool needs a player-family layer.
Scuffle helps by showing how to walk native cancel/transition graphs and emit
per-hit frame rows. The community sheet helps by showing the grouping and row
semantics players actually use. The implementation should combine both:

```text
community sheet = player grouping calibration
DA movelist = names, categories, commands, notes
KHD/MoveVM/Scuffle-style graph = native execution evidence
playerMoveFamilies = UI bridge between them
```
