import json
from pathlib import Path
# locate community dataset
path_candidates=[Path('community_data.json'), Path('webui/community-data.json'), Path('community_framedata.json'), Path('community_framedata.json'), Path('webui/public/data/community_framedata.json')]
for p in path_candidates:
    if p.exists():
        print('using',p)
        data=json.load(open(p,encoding='utf-8'))
        break
else:
    # fallback from repo import module loader
    from community_framedata import load as load_comm
    data=load_comm()

chars=data.get('chars',{})
for cid in ['001','01','1']:
    if cid in chars:
        moves=chars[cid].get('moves',[])
        hits=[m for m in moves if 'prime moon shadow rush' in (m.get('name') or '').lower()]
        print('cid',cid,'hits',len(hits))
        for m in hits:
            print(m)
        break
