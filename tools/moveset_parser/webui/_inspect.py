import json
from pathlib import Path
moves = json.load(open(Path('public/data/chars/001.json'), encoding='utf-8'))['movelist']['moves']
for m in moves:
    if m.get('moveId') == 1:
        print('name',m['name'],'input',m['input'],'cond',m.get('condition'),'commandSets',len(m['commandSets']))
        for i,cs in enumerate(m['commandSets']):
            print(i,cs)
        print('variants',m.get('inputVariants'))
        break
