import json
rep = json.load(open(r"E:/myMods/tools/moveset_parser/.tmp_mits_compare.json", encoding='utf-8'))
mov = json.load(open(r"E:/myMods/tools/moveset_parser/webui/public/data/chars/001.json", encoding='utf-8'))
move_by_id = {m['moveId']: m for m in mov['movelist']['moves']}
for item in rep['characters'][0]['details'][:40]:
    parsed = item.get('parsed')
    if not parsed:
        continue
    mid = int(parsed['moveId'])
    pm = move_by_id.get(mid)
    if not pm:
        continue
    entries = []
    for c in pm.get('commandSets', []):
        idx = int(c.get('cellIdx', -1))
        slot = c.get('slotIdx')
        res = c.get('resolution')
        if idx >= 0:
            cell = mov['khd']['cells'][idx]
            entries.append((idx, slot, res, cell.get('damage'), cell.get('activeStart')))
        else:
            entries.append((-1, slot, res, None, None))
    print(f"{item['community']['name']} / {item['community']['input']} status={item['status']} matched_move={mid} chosen={parsed['cellIdx']} dmg={parsed['metrics']['damage']} startup={parsed['metrics']['startup']} candidates={entries}")
