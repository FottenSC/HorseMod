# SC6 Community Frame Data Comparison - 2026-06-28

## Scope

Compared the current generated moveset parser web data in
`tools/moveset_parser/webui/public/data` against the community-collected SC6
frame data sheet referenced by `tools/moveset_parser/community_framedata.py`.

Community source:

https://docs.google.com/spreadsheets/d/1R3I_LXfqhvFjlHTuj-wSWwwqYmlUf299a3VY9pVyGEw

The sheet was exported read-only as:

`C:\Users\prest\AppData\Local\Temp\sc6_community_framedata.xlsx`

The full JSON comparison report was written to:

`C:\Users\prest\AppData\Local\Temp\sc6_community_vs_parsed_report_full.json`

Command:

```powershell
python compare_community_vs_parsed.py --data-dir webui\public\data --community-xlsx $env:TEMP\sc6_community_framedata.xlsx --limit 10000 --json
```

## Generated Parser Data Snapshot

- Movelist rows: 5,898
- Move groups: 1,766
- Grouped movelist rows: 2,097
- Duplicate-`MoveListID` groups: 843
- Input-family groups: 923

`compare_community_vs_parsed.py` expands movelist rows by command sets, so its
`movelistMoves` total is higher than the raw movelist row count.

## Headline Results

| Metric | Count |
| --- | ---: |
| Characters examined | 32 |
| Community rows | 5,418 |
| Expanded parser move records | 6,751 |
| Matched community rows | 3,674 |
| Matched rows with resolved cell metrics | 3,369 |
| Unmatched community rows | 1,608 |
| Ambiguous matches | 350 |
| Matched rows without cell metrics | 305 |
| Missing parser reference rows | 136 |
| Startup differences | 2,604 |
| Damage differences | 1,897 |

Match status totals:

| Status | Count |
| --- | ---: |
| exact | 1,581 |
| name-only | 738 |
| name+input-no-cell | 636 |
| attack-cell | 369 |
| ambiguous | 350 |
| missingReference | 136 |

No direct on-block/on-hit mismatch count is meaningful from this pass. The
community sheet stores player-facing frame advantage, while the parsed payload
currently exposes native stun-like values. The comparison script leaves those
disabled unless raw comparison flags are passed.

## Tolerance Check

Rerunning with `--startup-tolerance 2` reduced startup mismatches from 2,604 to
2,110 and ambiguity from 350 to 341. That is not enough to explain the results
as a simple off-by-one/off-by-two frame convention problem.

## Unmatched Row Shape

The 1,608 unmatched community rows break down as follows:

| Classification | Count | Interpretation |
| --- | ---: | --- |
| Same input, different name | 543 | Name aliases, throw variants, capitalization/locale differences, or row naming that is not stable enough for a strict join. |
| Same name, parser has longer command string | 470 | Strong grouping/prefix signal. The sheet has rows like `A` / `AA`, while parser data often has only the longer string row like `A.A.A`. |
| Same name only | 459 | Likely notation, stance, hold, or condition mismatch requiring richer command normalization. |
| No obvious movelist analogue | 136 | Mostly true missing references or rows that need deeper investigation. |

Examples of the grouping/prefix signal:

- Mitsurugi `Prime Moon Shadow Rush / A` and `/ AA` are unmatched while parser
  rows contain longer string forms.
- Mitsurugi `Double Binder / 6A` is unmatched while parser has `Double Binder /
  6A.A`.
- Community `A`, `B`, `K`, `AA`, `BB`, `6A`, and throw inputs are among the most
  common unmatched command shapes.

Inferno (`cid 013`) accounts for all 136 missing parser reference rows in this
run.

## Difference Shape

Rows with at least one startup or damage difference: 3,033.

Difference combinations:

| Difference set | Count |
| --- | ---: |
| startup + damage | 1,468 |
| startup only | 1,136 |
| damage only | 429 |

Startup differences:

- Count: 2,604
- Median absolute delta: 8 frames
- Delta >= 10: 1,102 rows
- Delta >= 30: 200 rows
- Parsed value was higher than community in 1,578 rows and lower in 1,026 rows.
- 2,150 startup differences involve multi-token parsed commands.
- 913 startup differences involve parsed dotted-string notation.

Damage differences:

- Count: 1,897
- Median absolute delta: 12 damage
- Delta >= 10: 1,104 rows
- Delta >= 30: 303 rows
- Parsed value was lower than community in 1,228 rows and higher in 669 rows.

Representative examples:

- Taki `Barbed Blades / AAA` exact-matches parser `A.A.A`, but community startup
  is `10` while parsed startup is `34`. This looks like a whole-string /
  first-impact community row being compared to a later native attack segment.
- Sophitia `Starlight Blade / AAA` exact-matches parser `A.A.A`, but community
  startup is `12` while parsed startup is `27`.
- Siegfried `Progressive Step / AAA` exact-matches parser `A.A.A`, but
  community startup is `16` while parsed startup is `92`.

## Parser Implications

The current comparison is useful as a diagnostic, but it should not be treated
as validation that a parser-exported `startup` or `damage` field is the
player-facing frame data for a move row.

The strongest finding is that player-facing moves and native attack cells are
not the same unit of data. The community sheet often describes a string or a
prefix of a string as one move, while the parser resolves one or more native
command sets / KHD cells. This is exactly the grouping issue we expected.

Recommended next parser improvements:

1. Add a group-aware comparison/export layer. Compare community rows against
   move groups and string-prefix rows, not only individual movelist rows.
2. For each group, expose both native segment metrics and player-facing summary
   metrics: first impact, per-hit damage list, total damage, resolved KHD cell
   ids, and unresolved cells.
3. Generate synthetic prefix entries for strings where the native movelist only
   exposes a longer command row but the community sheet has `A`, `AA`, `AAA`,
   etc.
4. Expand alias handling for names and throws. Strict name+input joins leave
   hundreds of rows behind even when the input exists.
5. Keep raw native stun separate from community frame advantage until the
   conversion formula and reference point are proven.
6. Continue treating the MoveVM / KHD linkage as heuristic until Ghidra confirms
   the native command-set-to-attack-cell path.

Bottom line: the generated grouping metadata is pointing in the right direction,
but the frame-data comparison needs to become group-aware before it can fairly
score parser-vs-community agreement.

## Root-Cause Follow-Up

The large difference is not one bug. It is several mismatched data units being
compared as if they were the same thing.

### 1. One selected KHD cell is not one player-facing move

The current comparison resolves each movelist row to one selected
`commandSets[].cellIdx` and compares that cell's `activeStart` / `damage` to the
community row. That is often the wrong unit:

- The community sheet usually describes the player-facing command row.
- A KHD `LuxBattleAttackCell` describes one native hit/property cell.
- A string such as `AAA`, `6AA`, or `1AB` may involve multiple attack cells
  across multiple slots.

Concrete traces:

| Character | Community row | Sheet | Selected parser cell | Nearby native evidence |
| --- | --- | --- | --- | --- |
| Taki | `Barbed Blades / AAA` | startup `10`, damage `[8,8,10,16]` | cell `27`, damage `16`, active `34..36` | cells `22..27` include `8@9`, `8@13`, `10@29`, `16@34` |
| Sophitia | `Starlight Blade / AAA` | startup `12`, damage `[8,10,25]` | cell `26`, damage `25`, active `27..30` | cells `22`, `24`, `26` line up with the three hits |
| Siegfried | `Progressive Step / AAA` | startup `16`, damage `[16,14,30]` | cell `21`, damage `7`, active `92..93` | cells `22`, `23`, `24` are `16@15`, `14@22`, `30@36` |

This explains why many "exact" name/input matches still disagree badly. Exact
string match does not mean exact hit-timeline match.

### 2. `MainIndex -> cellIdx` is a heuristic and sometimes picks the wrong cell

`export_webui_data.py` treats a `DA_MovePlayData` `MainIndex` as a hybrid value:
usually a direct attack-cell index, sometimes a slot index. The current
heuristic prefers direct cell interpretation when that cell looks like a valid
attack.

That gets some rows into the right area, but not reliably to the first or
correct player-facing hit. The Siegfried `AAA` case is the clearest warning:
`MainIndex 21` resolves to a valid-looking cell, but nearby cells `22..24`
match the community sequence much better. A "valid cell" is not necessarily the
cell the row should expose as player-facing frame data.

### 3. Multi-hit rows inflate startup mismatches

The comparison already skips damage comparison when the community row has
multiple damage segments, but it still compares startup against the one selected
cell. That makes later-hit cells look like huge startup mismatches.

Measured on the full report:

- Startup mismatches: 2,604
- Startup mismatches where the community row has multiple damage segments: 994
- Startup mismatches involving parsed multi-token commands: 2,150
- Startup mismatches involving dotted parser notation: 913
- Cases where the selected cell's damage equals one community damage segment:
  271
- Cases where a nearby KHD cell sequence reproduces the community multi-hit
  damage list: 205
- Of those, nearby sequence first active frame was within one frame of community
  startup: 115

That is direct evidence that much of the disagreement is timeline aggregation,
not absent data.

### 4. Damage differences are split between wrong-cell selection and raw/final damage semantics

Every damage mismatch counted by the existing script is single-hit from the
community side; multi-hit damage rows are skipped. For those 1,897 single-hit
damage mismatches:

| Classification | Count | Meaning |
| --- | ---: | --- |
| Selected cell hit-class disagrees with community hit-level | 581 | Strong wrong-cell or cell-class-inference warning |
| No nearby exact cell | 564 | Could be scaling, variants, throws, or unresolved linkage |
| Small base/final delta (`<=4`) | 271 | Likely base-vs-final damage or minor sheet/native convention |
| Nearby correct damage/class cell | 246 | Wrong selected cell likely |
| Nearby correct cell with matching startup | 235 | Very strong wrong-cell evidence |

Examples:

- Mitsurugi `Twisted Gold / 3A`: community damage `16`, selected cell damage
  `12`, startup convention otherwise lines up (`13` native active start vs
  `14` sheet startup).
- Mitsurugi `Shin Slicer / 1A`: community says low `34` damage / startup `34`,
  selected row can expose cells `18@16` or `32@22` depending duplicate row,
  neither matching the player-facing row cleanly.
- Throws such as `A+G` often resolve to a strike/connect detector cell rather
  than the throw cinematic damage cell. The code already flags this with
  `isThrowInput`.

### 5. Attack-cell `class` is not the same as movelist/community hit level

The parser has two separate concepts:

- `move.hitClasses`: player-facing DA_MoveListTable hit-level metadata.
- `cell.class`: inferred from `LuxBattleAttackCell.wU16AttackFlags`.

The export code already says `hitClasses` is more reliable than deriving class
from cell flags. The root-cause pass confirmed that these disagree often enough
that `cell.class` should not be used as an authority for community-frame joins.

This matters because some apparent wrong-cell cases may be a hit-level inference
problem instead. The stronger signals are damage sequence, startup sequence,
slot/bytecode path, and native xrefs.

## Better Explanation Of The Big Delta

The big delta comes from comparing:

```text
community row = player-facing command/string summary
current parser row = one resolved native command-set cell
```

Those only line up for simple one-hit moves where `MainIndex` happens to resolve
to the correct attack cell and no runtime damage/throw/variant scaling applies.
For strings, throws, duplicate movelist rows, stance variants, and dispatcher
routes, the comparison is currently under-modeled.

The right next implementation is not to tweak tolerance. It is to export a
native hit timeline per movelist row/group:

1. Start from the movelist row/group.
2. Walk the slot transition graph and `CALLCOND 0x26` active-cell switches.
3. Collect all attack cells in execution order.
4. Report first-impact startup, per-hit damage list, total base damage, raw cell
   ids, and unresolved branches separately.
5. Compare the community sheet against that group/timeline summary, not against
   a single selected `cellIdx`.
