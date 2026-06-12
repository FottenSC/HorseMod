# SC6 Move Description Source

Finding: the closest authored source for "what a move does" is the per-character UE4 DataTable `DA_MoveListTable_<cid>`, backed by localized strings in `Game.archive`.

Evidence:
- `FLuxBattleMoveListTableRow` declares the row schema in `HorseMod/include/SoulCaliburVI/LuxorGame/Public/LuxBattleMoveListTableRow.h`:
  - `CommandTextID`, `NameTextID`, `ReadingTextID`, `NoteTextID`
  - `MainMovesTextID` - localized gameplay hint / move description
  - `RethalHitTextID` - localized Lethal Hit trigger text; name is misspelled in the shipped schema
  - `AttributeTag` - per-hit class sequence such as `H.M` or `M.M.M`
  - `EffectTag` - property icons such as `LH`, `BA`, `GI`, `UA`, `RE`, `TH`, `SC`, `SS`, `SGF`, `SGH`, `SGQ`
- `tools/moveset_parser/export_webui_data.py` already parses this in `_load_move_table_metadata()` and joins it to `DA_MovePlayData_<cid>` by `MoveListID` / numeric row key.
- Runtime hit/frame behavior still comes from KHD cells and MoveVM bytecode. `DA_MoveListTable` is UI/authored metadata, not the executor.

Sample from copied evidence assets:
- `dump/Style/024/DA_MoveListTable_024.uasset/.uexp`
- `dump/Localization/Game/Steam/en/Game.archive`
- Zasalamel row `3`: `AttributeTag=H.H`, `MainMovesTextID=ID_CMD_MAIN_009`, localized text `Allows you to move first if the opponent guards`.
- Zasalamel row `75`: `AttributeTag=M.M.M`, `EffectTag=UA`, matching the existing parser regression test for Tiamat's Rampage.

How the sources divide:
- `DA_MovePlayData_<cid>`: canonical movelist categories, order, `MoveListID`, command sets, and `IntroIndex` / `MainIndex` navigation into the demo command-player layer.
- `DA_MoveListTable_<cid>`: what the in-game movelist says the move is/does: name/text IDs, main tip, Lethal Hit condition, hit-class sequence, property tags.
- KHD `hdr<cid>.khd`: actual battle data: slots, attack cells, damage, active windows, hitstun/blockstun, ranges, and MoveVM bytecode transitions/effects.

Ghidra status: no Ghidra MCP instance was running during this pass, so no new native xrefs were inspected. Existing parser references already tie KHD runtime behavior to `LuxMoveVM_*` and KHD cell consumers; a follow-up Ghidra pass should look for DataTable loads of `FLuxBattleMoveListTableRow` / `DA_MoveListTable_*` if native UI callsites are needed.
