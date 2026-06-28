# SC6 Move Count Difference Investigation - 2026-06-28

## Counts Compared

| Count unit | Total |
| --- | ---: |
| Current parser raw `movelist.moves` rows | 5,898 |
| Community/player sheet rows | 5,418 |
| Calibrated player move families | 3,696 |

These are different units. The parser count is Bandai's in-game movelist rows.
The community sheet count is player-authored frame-data rows. The family count
is the new proposed top-level grouping layer where strings and variants are
expandable children.

## Raw Parser Rows vs Community Rows

The raw parser has 480 more rows than the community sheet overall. Inferno
distorts this because the community sheet has 136 Inferno rows and the current
parser export has zero Inferno movelist rows. Excluding Inferno, the parser has
616 more rows.

This net difference hides two large opposing effects:

| Exact normalized comparison | Count |
| --- | ---: |
| Parser rows with exact community name+input key | 3,232 |
| Parser rows with no exact community key | 2,666 |
| Community rows with exact parser name+input key | 2,859 |
| Community rows with no exact parser key | 2,559 |

The existing scored comparison can match more rows than this because it uses
broader candidate logic. This table is intentionally strict to expose where row
notation/counting differs.

### Biggest Parser-Side Surplus Sources

1. **Duplicate in-game category listings by `MoveListID`**

   Duplicate `MoveListID` rows create 904 parser-row surplus slots before any
   player-facing grouping. This is larger than the net 480-row difference, so
   other missing/collapsed rows partially cancel it.

   | Category receiving duplicate rows | Duplicate-row surplus |
   | --- | ---: |
   | Lethal Hit Attacks | 461 |
   | Special Moves | 190 |
   | 8-Way Run Moves | 62 |
   | Vertical Attacks | 51 |
   | Throws | 46 |
   | Horizontal Attacks | 41 |

   Examples: Mitsurugi `Heaven Cannon / 3B` appears in main/vertical/lethal-hit
   categories; Reversal Edge followups can appear in both Reversal Edge and
   Lethal Hit categories.

2. **In-game movelist utility/system rows**

   Parser rows include Bandai UI rows that are not always player frame-data
   rows:

   - 112 unmatched `Combo N` rows.
   - 85 movement-only rows in Reversal Edge category.
   - 27 `Guard`, 25 `Side Step`, 25 `Back Step`, and 24 `Forward Step` rows.
   - 26 `Appeal` rows.

3. **Stance-transition and cancel rows**

   The strict parser-unmatched set contains:

   - 762 rows with `~` in the move name.
   - 773 rows tagged `SS`.
   - 380 `Special Moves` rows.

   Players often treat these as branches/suffixes of a known move family or as
   stance rows, while the game movelist often gives them separate rows.

4. **Multi-input alternatives stored in one game row**

   1,022 strict-unmatched parser rows have `|` input alternatives. Examples:

   - `(3)|(6)|(9)A.A`
   - `(2)|(8)B.B`
   - `(1)|(4)|(7)A+B`

   These are one parser row, but they often correspond to multiple community
   notations, directional alternatives, or prefix rows.

5. **Condition/stance notation mismatch**

   Many strict community misses are not absent moves; the parser and sheet
   split notation differently:

   - Parser: `condition="While crouching"`, `input="A"`
   - Community: `stance="FC"`, `command="2A"` or a stance-coded command row

   In the strict community-unmatched set:

   - 1,973 rows have a matching move name but input/stance notation differs.
   - 1,008 rows are stance-coded.
   - 938 rows are compact prefix/simple rows.
   - 770 rows are multi-hit rows.

## Community Rows vs Player Families

The new calibrated player-family layer folds 5,418 community rows into 3,696
families. The 1,722-row reduction is intentional: rows are not deleted; they
become children of expandable families.

| Relation evidence | Edge count | Families touched | Folded-row overlap |
| --- | ---: | ---: | ---: |
| Prefix/string branch | 1,266 | 566 | 1,316 |
| Hold variant | 421 | 385 | 703 |
| Stance transition | 334 | 222 | 569 |
| Direction alternative | 208 | 105 | 289 |

The folded-row overlap column can exceed 1,722 in total because one family can
have multiple relation types.

Non-overlapping family-combination contributions:

| Relation combination | Rows folded |
| --- | ---: |
| Prefix only | 568 |
| Hold variant only | 229 |
| Hold + prefix | 217 |
| Prefix + stance transition | 193 |
| Hold + prefix + stance transition | 170 |
| Direction + prefix + stance transition | 117 |
| Direction alternative only | 110 |

Top characters by player rows folded into families:

| Character | Rows folded |
| --- | ---: |
| Taki | 119 |
| Talim | 104 |
| 2B | 97 |
| Xianghua | 91 |
| Amy | 78 |
| Hwang | 78 |
| Siegfried | 78 |
| Voldo | 69 |

Example large families:

- Talim `Weather Vane Buster`: `A`, `AA`, `AAA`, `AAB`, `AABA`, `AABA28`,
  `AABA6`, `AABA4`, `AABB`, `AAB(B)`, `AAA+B`, `A6`.
- Taki `Shadow Banishment`: `B`, `BA`, `BAK`, `BA6K`, `BA4`, `BB`, `BBB`,
  `BB4`, `BK`, `B(K)`.
- 2B `Slash Sequence`: `A`, `AA`, `AAA`, `AAA6`, `AAA4`, `AAA8`, `AAAA`,
  `AAAB`, `AAB`.

## Conclusion

The count gap is mainly a unit mismatch:

```text
raw game movelist rows = duplicated UI/category entries + system rows + stance rows
community rows = player frame-data rows
player families = expandable top-level strings/branches containing player rows
```

For the move editor, expose all three numbers rather than pretending one is the
truth:

- raw game rows for reverse-engineering completeness,
- player rows for frame-data parity,
- player families for the UI grouping players expect.
