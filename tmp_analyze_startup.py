import json
from pathlib import Path

obj = json.loads(Path('cmp_all.json').read_text(encoding='utf-8'))
char_cache = {}
rows = []

def load_char(cid):
    if cid in char_cache:
        return char_cache[cid]
    p = Path('tools/moveset_parser/webui/public/data/chars') / f'{cid}.json'
    if not p.exists():
        return None
    data = json.loads(p.read_text(encoding='utf-8'))
    khd = data.get('khd')
    if not khd:
        return None
    cells = {int(c['idx']): c for c in khd['cells']}
    slots = {int(s['idx']): s for s in khd['slots']}
    char_cache[cid] = (cells, slots)
    return char_cache[cid]

for c in obj['characters']:
    cid = c['cid']
    loaded = load_char(cid)
    if not loaded:
        continue
    cells, slots = loaded
    for d in c['details']:
        sd = d.get('diffs', {}).get('startup')
        if not sd or sd.get('equal'):
            continue
        if d['status'] in {'unmatched', 'missingReference'}:
            continue
        parsed = d.get('parsed')
        if not isinstance(parsed, dict):
            continue
        if (d['community']['input'].replace(' ', '').lower() != parsed['input'].replace(' ', '').lower()):
            continue
        delta = abs(sd['delta'])
        if delta < 20:
            continue
        pv = sd['parsed']
        if pv is None or not isinstance(pv, int) or pv >= 500:
            continue
        slot = slots.get(int(parsed['slotIdx']))
        cell = cells.get(int(parsed['cellIdx']))
        if not slot or not cell:
            continue
        rows.append({
            'cid': cid,
            'name': d['community']['name'],
            'input': d['community']['input'],
            'comm': sd['community'],
            'parsed': sd['parsed'],
            'delta': delta,
            'status': d['status'],
            'slotHitWindow': slot.get('hitWindowStart'),
            'cellStart': cell.get('activeStart'),
            'cellEnd': cell.get('activeEnd'),
            'cellDamage': cell.get('damage'),
        })

rows.sort(key=lambda r: r['delta'], reverse=True)
for r in rows[:40]:
    print(f"{r['cid']} {r['status']}: {r['name']} {r['input']} delta={r['delta']} comm={r['comm']} parsed={r['parsed']} cell={r['cellStart']}..{r['cellEnd']} slotHit={r['slotHitWindow']}")

better = 0
for r in rows:
    sw = r['slotHitWindow']
    if isinstance(sw, int) and isinstance(r['comm'], int):
        if abs(sw - r['comm']) < abs(r['parsed'] - r['comm']):
            better += 1
print('ROWS', len(rows), 'slotHitBetter', better)
