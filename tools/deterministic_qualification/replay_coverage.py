"""Executable coverage requirements over observed receipts, never requested counts."""
from __future__ import annotations
import re


def validate(required, observed):
    failures=[]; unknown=[]
    def need(condition, message):
        if not condition: failures.append(message)
    if 'cycles' in required:
        expected=list(range(1,required['cycles']+1))
        for name in ('attempted_cycles','committed_cycles'):
            need(observed.get(name)==expected, name+' absent, duplicate or out of order')
    corrections=observed.get('corrections',[])
    if 'ages' in required:
        need([r['arrival']-r['first_consumption']+1 for r in corrections]==required['ages'], 'first-consumption ages differ')
        need([r['revision'] for r in corrections]==list(range(1,len(required['ages'])+1)), 'revision transitions differ')
        need(all(r.get('committed') is True for r in corrections),'admitted correction not committed')
    if 'arrival_thirds' in required:
        first,last=required['arrival_thirds'];length=last-first+1
        thirds={min(2,(r['arrival']-first)*3//length) for r in corrections if first<=r['arrival']<=last}
        need(thirds=={0,1,2},'early, middle or late arrival missing')
    for name in ('continuation_ticks','unique_forward_ticks','unique_regenerated_ticks'):
        if name in required:need(observed.get(name,-1)>=required[name],name+' insufficient')
    for name in ('lifecycle_difference','b_recovered','normal_renderer','visual_coherence'):
        if required.get(name):
            if name not in observed:unknown.append(name)
            else:need(observed[name] is True,name+' not demonstrated')
    if required.get('ownership'):
        for name in required['ownership']:
            if observed.get('ownership',{}).get(name)!='measured':unknown.append('ownership.'+name)
    return dict(result='fail' if failures else 'inconclusive' if unknown else 'pass',failures=failures,
        unknown=unknown,requirements=required,observed=observed,scope='execution coverage only; no gate promotion')


def rolling_receipts(text, scheduled=''):
    rows=lambda marker:[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines() if marker in line]
    committed=rows('[HorseMod] rolling cycle ordinal=')
    admissions=rows('[HorseMod] rolling correction admitted ')
    attempts=rows('historical combat execution admitted ')
    beginnings=rows('[HorseMod] rolling timing begin ordinal=')
    schedules=rows('[HorseMod] rolling correction scheduled ordinal=')
    corrections=[]
    for r in admissions:
        revision=int(r['revision'])+1;arrival=int(r['arrival'])
        corrections.append(dict(arrival=arrival,first_consumption=int(r['first_consumption']),revision=revision,
            committed=any(int(c['T'])==arrival and int(c.get('revision',-1))==revision for c in committed)))
    forward=set();regenerated=set();active=False
    for line in text.splitlines():
        if 'historical combat execution admitted ' in line:active=True
        if '[HorseMod] rolling cycle ordinal=' in line:active=False
        if '[ReplayQualification] boundary ordinal=' in line and 'phase=actor_tail ' in line:
            match=re.search(r'\bframe=(\d+)',line)
            if match:(regenerated if active else forward).add(int(match[1]))
    revisions=[int(c.get('revision',0)) for c in committed]
    checkpoints=rows('[HorseMod] rolling checkpoint tick=')
    first=min((int(c['first']) for c in checkpoints),default=min((int(c['A']) for c in committed),default=None))
    last=max([int(c['T']) for c in committed]+[int(r['tick']) for r in beginnings],default=None)
    window=dict(first_exclusive=first,last_inclusive=last)
    if first is not None and last is not None:
        forward={t for t in forward if first<t<=last}
        regenerated={t for t in regenerated if first<t<=last}
    else:
        # A missing window cannot turn startup observations into combat coverage.
        forward=set();regenerated=set()
    return dict(attempted_cycles=[int(r['ordinal']) for r in beginnings] if beginnings else list(range(1,len(attempts)+1)),
        attempt_scope='B correction entry' if beginnings else 'legacy native execution admissions only',
        committed_cycles=[int(r['ordinal']) for r in committed],
        unique_forward_ticks=len(forward),unique_regenerated_ticks=len(regenerated),
        tick_window=window,
        requested_corrections=len(scheduled.split(';')) if scheduled else 0,
        scheduled_corrections=len(schedules) if schedules else None,
        admitted_corrections=len(admissions),committed_corrections=sum(c['committed'] for c in corrections),
        corrections=corrections,revision_transitions=revisions,
        unchanged_revision_cycles=sum(a==b for a,b in zip([0]+revisions,revisions)),
        ownership={'native':'unknown','GPU':'unknown','deferred':'unknown'})
