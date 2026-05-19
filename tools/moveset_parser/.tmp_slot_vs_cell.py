import json
from pathlib import Path
chars = json.loads(Path('webui/public/data/chars/001.json').read_text(encoding='utf-8'))
rep = json.loads(Path('tmp_mits_compare_pre.json').read_text(encoding='utf-16'))['characters'][0]

def find_move(mid):
    for m in chars['movelist']['moves']:
        if m['moveId'] == mid:
            return m
    return None

slot_better = 0
count = 0
for d in rep['details']:
    if d.get('status') in {'unmatched', 'missingReference', 'ambiguous'}:
        continue
    if not d.get('diffs') or not d['diffs'].get('startup'):
        continue
    parsed = d.get('parsed') or {}
    mid = parsed.get('moveId')
    if mid is None:
        continue
    pm = find_move(mid)
    if not pm or not pm.get('commandSets'):
        continue
    cs = pm['commandSets'][0]
    cell_idx = cs.get('cellIdx', -1)
    slot_idx = cs.get('slotIdx', -1)
    comm_start = d['community'].get('startup')
    if not isinstance(comm_start, int):
        continue
    count += 1
    cell = chars['khd']['cells'][cell_idx] if isinstance(cell_idx, int) and 0 <= cell_idx < len(chars['khd']['cells']) else None
    slot = chars['khd']['slots'][slot_idx] if isinstance(slot_idx, int) and 0 <= slot_idx < len(chars['khd']['slots']) else None
    if not cell or not slot:
        continue
    cell_start = cell['activeStart']
    slot_start = slot['hitWindowStart']
    if abs(slot_start - comm_start) < abs(cell_start - comm_start):
        slot_better += 1
        print('slot-better', d['community']['name'], '/', d['community']['input'], 'comm', comm_start, 'cell', cell_start, 'slot', slot_start, 'move', mid)

print('count', count, 'slot_better', slot_better)
