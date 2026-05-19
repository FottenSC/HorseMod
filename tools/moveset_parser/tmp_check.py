from pathlib import Path
import json
from luxformats import parse_auto

root = Path("E:/myMods/tools/moveset_parser")
khd_root = Path("E:/myMods/dump/Battle")
char = json.loads((root / 'webui/public/data/chars/001.json').read_text(encoding='utf-8'))
khd = parse_auto(str(khd_root / 'hdr' / 'hdr001.khd'))
checks = [
    ("Prime Moon Shadow Rush", 1),
    ("Double Binder", 3),
    ("Twisted Gold", 4),
    ("Shin Slicer", 6),
    ("Pattern Dance", 7),
    ("Drawn Breath", 8),
    ("Reverse Slice", 74),
    ("Hidden Slice", 75),
    ("Celestial Divide", 161),
    ("Wheel Kick", 93),
]

moves_by_id = {m['moveId']: m for m in char['movelist']['moves']}
for name, mid in checks:
    m = moves_by_id.get(mid)
    if m is None:
        continue
    cs = m['commandSets'][0]
    cell_idx = cs.get('cellIdx', -1)
    slot_idx = cs.get('slotIdx', -1)
    if cell_idx < 0:
        print(f"{name}: no cell")
        continue
    cell = khd.sections[0].entries[cell_idx]
    slot = khd.slots[slot_idx] if 0 <= slot_idx < len(khd.slots) else None
    slot_hit = slot.nHitWindowStart_36 if slot is not None else None
    print(
        f"{name}: cell={cell_idx} slot={slot_idx} res={cs.get('resolution')}",
        f"active={cell.wI16MasterWindowStart}",
        f"slotHit={slot_hit}",
        f"delta={cell.wI16MasterWindowStart-(slot_hit or 0)}",
        f"cellDamage={cell.wI16BaseDamage}",
    )
