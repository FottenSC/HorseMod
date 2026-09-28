"""Freeze diagnostic schedules from native consumption receipts, never tick guesses."""
from __future__ import annotations
import json
from pathlib import Path
import re
from .artifacts import sha256_file


def select_schedule(rows, first, cycles):
    selected=[];seen=set()
    for row in rows:
        key=(row['round'],row['sample'])
        if key in seen:continue
        seen.add(key)
        if row['p2'] and row['native_return'] and row['round']==0:
            selected.append(row)
    output=[]
    for third,age in enumerate((1,4,7)):
        start=first+(cycles*third+2)//3;end=first+(cycles*(third+1)+2)//3-1
        eligible=[r for r in selected if start<=r['tick'] and r['tick']+age-1<=end]
        if not eligible:raise ValueError(f'no verified non-neutral P2 sample in third {third+1}')
        chosen=min(eligible,key=lambda r:(r['tick'],r['sample']))
        output.append(dict(chosen,arrival=chosen['tick']+age-1,age=age,expected_revision=third))
    return output


def generate(report_path, cycles, destination, objects):
    report_path=Path(report_path);report=json.loads(report_path.read_bytes())
    if (cycles not in (30,600) or report.get('mode')!='stock' or report.get('result')!='captured'
        or report.get('cleanup')!={'complete':True,'games_remaining':0}
        or report.get('loaded_runtime',{}).get('verification')!='owned_process_runtime_absent'
        or not all(report.get(k) is True for k in ('skip_intros','native_fixed_seed','serial_particles'))
        or report.get('rolling_corrections') or report.get('authored_prefix')):
        raise ValueError('schedule generation requires an unchanged verified independent native capture')
    raw=report['raw_log'];path=Path(raw['path'])
    if sha256_file(path)!=raw['sha256']:raise ValueError('schedule source log changed')
    rows=[]
    for number,line in enumerate(path.read_text(encoding='utf-8',errors='replace').splitlines(),1):
        if '[ReplayQualification] input consumption protocol=1 ' not in line:continue
        r=dict(re.findall(r'(\w+)=([^\s]+)',line))
        if r['run_id']!=report['run_id']:raise ValueError('mixed native consumption identities')
        rows.append(dict(tick=int(r['tick']),round=int(r['round']),sample=int(r['sample']),
            p2=int(r['p2'],16),native_return=r['native_return']=='true',line=number))
    selected=select_schedule(rows,217,cycles)
    from .replay_evidence import store_file
    report_reference=store_file(Path(objects),report_path)
    raw=store_file(Path(objects),path,raw['sha256'])
    from .replay_profiles import PROFILE_ROOT
    v=json.loads((PROFILE_ROOT/f'changed-rolling{cycles}-startup-serial.json').read_bytes())
    v['name']=f'changed-rolling{cycles}-distributed';v['purpose']='diagnostic'
    wire=';'.join(f"{r['arrival']}:{r['expected_revision']}:{r['round']}:{r['sample']}:2:0:0" for r in selected)
    v['arguments'][v['arguments'].index('--rolling-corrections')+1]=wire
    v['authored_sample_evidence']=dict(raw_log=raw,source_report=report_reference,
        native_protocol=1,rows=selected,identities=report['identities'])
    v['arrival_scope']='distributed native-consumption diagnostic; lifecycle difference unproven'
    v['contract']['coverage']['arrival_thirds']=[217,216+cycles]
    v['contract']['hypothesis']='Revisions arriving in all three thirds preserve independently authored histories.'
    from .run_journal import atomic_json
    atomic_json(Path(destination),v)
    return dict(profile=str(Path(destination).resolve()),sha256=sha256_file(Path(destination)),rows=selected,wire=wire)


def validate_frozen(profile):
    evidence=profile['authored_sample_evidence'];rows=evidence['rows']
    for name in ('source_report','raw_log'):
        r=evidence[name]
        if sha256_file(Path(r['path']))!=r['sha256']:raise ValueError('frozen schedule evidence changed')
    lines=Path(evidence['raw_log']['path']).read_text(encoding='utf-8',errors='replace').splitlines()
    for row in rows:
        fields=dict(re.findall(r'(\w+)=([^\s]+)',lines[row['line']-1]))
        if (fields.get('protocol')!='1' or fields.get('native_return')!='true'
            or int(fields.get('tick',-1))!=row['tick'] or int(fields.get('sample',-1))!=row['sample']
            or int(fields.get('p2','0'),16)!=row['p2'] or not row['p2']):
            raise ValueError('frozen native sample receipt differs')
    wire=';'.join(f"{r['arrival']}:{r['expected_revision']}:{r['round']}:{r['sample']}:2:0:0" for r in rows)
    if profile['arguments'][profile['arguments'].index('--rolling-corrections')+1]!=wire:
        raise ValueError('frozen schedule wire differs from native sample receipts')
