import json
from pathlib import Path
root = Path('tools/moveset_parser/webui/public/data/chars')
rows=[]
for p in sorted(root.glob('*.json')):
    data=json.loads(p.read_text(encoding='utf-8'))
    if 'movelist' not in data or 'khd' not in data:
        continue
    cells={int(c['idx']): c for c in data['khd']['cells']}
    slots={int(s['idx']): s for s in data['khd']['slots']}
    for m in data['movelist']['moves']:
        for i,cs in enumerate(m.get('commandSets') or []):
            if cs.get('resolution')!='cell':
                continue
            ci=cs.get('cellIdx')
            si=cs.get('slotIdx')
            cell=cells.get(ci)
            if not cell:
                continue
            st=cell['activeStart']
            if st>=500:
                slot=slots.get(si)
                alt=[]
                if slot:
                    for vi in slot['cellVariants']:
                        if vi>=0 and vi in cells:
                            ac=cells[vi]['activeStart']
                            if ac<500:
                                alt.append((vi,ac,cells[vi]['damage'], cells[vi]['role']))
                rows.append((data['cid'],m['name'],m['input'],m['condition'],i,ci,si,st,slot['idx'] if slot else -1,alt))

print('count',len(rows))
for r in rows[:120]:
    cid,name,input_,cond,cs_i,ci,si,st,sloti,alt=r
    print(f"{cid} {name} input={input_!r} cond={cond!r} cs={cs_i} selected={ci}@slot{sloti} start={st} alt={alt}")
