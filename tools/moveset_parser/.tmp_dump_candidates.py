import json, itertools
from pathlib import Path
mov=json.loads(Path('webui/public/data/chars/001.json').read_text(encoding='utf-8'))
rep=json.loads(Path('tmp_mits_compare_pre.json').read_text(encoding='utf-16'))['characters'][0]
move_by_id={m['moveId']:m for m in mov['movelist']['moves']}

for item in rep['details'][:25]:
    parsed=item.get('parsed')
    if not parsed:
        continue
    pm=move_by_id.get(parsed['moveId'])
    if not pm:
        continue
    print('---',item['community']['name'], item['community']['input'], 'status', item['status'], 'move', parsed['moveId'])
    for i,c in enumerate(pm['commandSets']):
        idx = c.get('cellIdx', -1)
        if idx>=0 and idx < len(mov['khd']['cells']):
            cell = mov['khd']['cells'][idx]
            print(f'  cs[{i}] idx={idx} slot={c.get("slotIdx")} res={c.get("resolution")} dmg={cell.get("damage")} startup={cell.get("activeStart")} cond={c.get("candidateCount")}/{c.get("candidateBestRank")}/{c.get("candidateScore")}')
        else:
            print(f'  cs[{i}] idx={idx} slot={c.get("slotIdx")} res={c.get("resolution")} -- no cell --')
