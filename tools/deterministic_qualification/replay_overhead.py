"""Compare optional witness overhead while preserving all common observers."""
import json
from pathlib import Path
import statistics
from .artifacts import sha256_file


def compare(core_path,witness_path):
    documents=[]
    for path in map(Path,(core_path,witness_path)):
        if path.stem!=sha256_file(path):raise ValueError('overhead comparison requires immutable manifests')
        documents.append(json.loads(path.read_bytes()))
    a,b=[d['summary'] for d in documents]
    for name in ('workspace_fingerprint','build_identities','state_policy_sha256'):
        # Older new-run summaries carry policy under source-qualified provenance.
        def value(r):return r.get(name) or r.get('build_provenance',{}).get('state_policy',{}).get('manifest',{}).get('sha256') if name=='state_policy_sha256' else r.get(name)
        if not value(a) or value(a)!=value(b):raise ValueError('overhead identities differ or are unverified: '+name)
    def settings(r):
        value=dict(r['profile']['resolved_settings'])
        for name in ('profile','diagnostic_mask','diagnostic_stack_depth','diagnostic_byte_limit'):
            value.pop(name,None)
        return value
    if settings(a)!=settings(b):raise ValueError('overhead workloads/setup differ')
    if a['profile']['resolved_settings']['diagnostic_mask']!=0 or b['profile']['resolved_settings']['diagnostic_mask']==0:
        raise ValueError('overhead requires core versus core plus optional witnesses')
    samples=[]
    for summary in (a,b):
        if summary.get('acceptance_results',{}).get('simulation')!='pass' or not summary.get('cleanup',{}).get('complete'):
            raise ValueError('overhead workload lacks complete independent comparison/cleanup')
        timing=summary['full_update_performance'];rows=timing['samples']
        if any(r['status'] not in ('complete','terminal_no_next_checkpoint') for r in rows):
            raise ValueError('overhead workload has aborted or missing intervals')
        samples.append({(int(r['ordinal']),int(r['tick'])):int(r['full_update_us']) for r in rows if r['status']=='complete'})
    if not samples[0] or samples[0].keys()!=samples[1].keys():raise ValueError('overhead has no matching complete update intervals')
    differences=[dict(ordinal=k[0],tick=k[1],core_us=samples[0][k],witness_us=samples[1][k],incremental_us=samples[1][k]-samples[0][k]) for k in samples[0]]
    return dict(result='pass',source_manifests=list(map(str,(core_path,witness_path))),samples=differences,
        median_incremental_us=statistics.median(r['incremental_us'] for r in differences),
        common_observer_cost='not measured',scope='two-run optional-witness increment, including run-to-run variation; safety and common comparisons enabled in both')
