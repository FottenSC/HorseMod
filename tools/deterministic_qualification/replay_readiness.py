"""Read-only readiness over immutable evidence; indexes never grant gates."""
from __future__ import annotations
import json
import time
from pathlib import Path
from .artifacts import sha256_file
from .replay_outcomes import outcomes_for, failure_view
from .run_journal import atomic_json

DEFINITION = Path(__file__).with_name('replay_readiness.json')


def index_entry(path):
    raw = path.read_bytes()
    import hashlib
    digest = hashlib.sha256(raw).hexdigest()
    if path.stem != digest: raise RuntimeError('evidence manifest hash mismatch: ' + str(path))
    manifest = json.loads(raw)
    summary = manifest.get('summary')
    adaptation = None
    if not isinstance(summary, dict):
        # Known historical audits are evidence documents, not completed runs.
        # Preserve unknown legacy shapes without interpreting prose as results.
        adaptation = dict(version=1, source_manifest=str(path.resolve()),
            adapter='legacy_audit' if 'report' in manifest and 'scope' in manifest else 'unknown_legacy_manifest')
        summary = dict(stage='audit' if adaptation['adapter']=='legacy_audit' else 'unknown',
                       result='unknown', certifying=False)
    else:
        from .replay_outcomes import manifest_summary
        summary=manifest_summary(manifest,path)
    profile = summary.get('profile') or {}
    provenance = summary.get('build_provenance') or {}
    outcomes = outcomes_for(summary, source=str(path))
    return dict(path=str(path.resolve()), sha256=digest,
        completed_ns=manifest.get('completed_ns', path.stat().st_mtime_ns),
        stage=summary.get('stage','unknown'), result=summary.get('result','unknown'),
        run_id=summary.get('run_id'),
        workspace_fingerprint=summary.get('workspace_fingerprint') or provenance.get('workspace_fingerprint'),
        build_identities=summary.get('build_identities'), profile_sha256=profile.get('sha256'),
        identities=summary.get('identities'),
        profile_name=profile.get('definition',{}).get('name'),
        state_policy=summary.get('state_policy_sha256') or provenance.get('state_policy',{}).get('manifest',{}).get('sha256'),
        outcomes=outcomes['dimensions'], checks=outcomes['checks'],
        first_failure=failure_view(summary), certifying=summary.get('certifying',False),
        gate_evidence=summary.get('gate_evidence',{}), adaptation=adaptation)


def verify_references(row, checked):
    """Verify selected evidence, without rewriting history or trusting the index.

    Mutable original deployment/report paths are deliberately not evidence.
    Captures point to retained copies; source payloads use the shared reader.
    """
    manifest=json.loads(Path(row['path']).read_bytes())
    if manifest.get('missing_evidence'):
        raise RuntimeError('manifest records missing evidence: '+row['path'])
    summary=manifest.get('summary',{})
    def reference(value):
        if not isinstance(value,dict):return
        if 'path' in value and 'sha256' in value:
            key=(value['path'],value['sha256'])
            if key not in checked:
                if sha256_file(Path(key[0]))!=key[1]:raise RuntimeError('evidence payload hash mismatch: '+key[0])
                checked.add(key)
        else:
            for child in value.values():
                if isinstance(child,list):
                    for item in child:reference(item)
                else:reference(child)
    for key in ('raw_log','test_evidence','retained_receipt'):
        reference(summary.get(key))
    for capture in manifest.get('captures',[]):
        for key in ('report','raw_log','diagnostic_index'):reference(capture.get(key))
    reference(manifest.get('stage_manifest'))
    from .source_retention import verify_retention
    for receipt in (summary.get('source_retention'),summary.get('capture_source_retention'),
                    summary.get('build_provenance',{}).get('source_retention')):
        if receipt:
            key=('sources',json.dumps(receipt,sort_keys=True))
            if key not in checked:verify_retention(receipt);checked.add(key)


def rebuild_index(root):
    rows, errors = [], []
    for path in sorted((root/'manifests').glob('*.json')):
        try: rows.append(index_entry(path))
        except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
            errors.append(dict(path=str(path),error=str(error)))
    document = dict(schema=1, entries=rows, errors=errors)
    atomic_json(root/'index.json', document)
    return dict(result='pass' if not errors else 'fail', entries=len(rows), errors=errors,
                index=str(root/'index.json'))


def update_index(root, receipt):
    # Separate immutable per-manifest entries avoid concurrent read/modify/write
    # loss. The optional rebuilt index is only a read acceleration base.
    row = index_entry(Path(receipt['path']))
    atomic_json(root/'index-entries'/(row['sha256']+'.json'), row)


def compatibility(row, current):
    for key in ('workspace_fingerprint','build_identities','state_policy'):
        if not row.get(key) or not current.get(key): return 'unverified'
    if not all(row['build_identities'].get(k) and current['build_identities'].get(k)
               for k in ('runtime','observer','framework')):return 'unverified'
    if any(row[k] != current[k] for k in ('workspace_fingerprint','build_identities','state_policy')):
        return 'incompatible'
    if row.get('profile_sha256') and row['profile_sha256'] not in current.get('profiles',set()):
        return 'incompatible'
    if row.get('stage') in ('combat-restore','equivalence','restore','baseline','session-exit'):
        identities=row.get('identities') or {};external=current.get('external_identities') or {}
        if (not row.get('profile_sha256') or not all(identities.get(k) and external.get(k)
                for k in ('game','replay','physx','ucrt'))
            or not all(identities.get(k) for k in ('runtime','observer','framework'))):return 'unverified'
        if (any(identities[k]!=external[k] for k in ('game','replay','physx','ucrt'))
            or any(identities[k]!=row['build_identities'][k] for k in ('runtime','observer','framework'))):return 'incompatible'
    return 'current'


def current_context(root, output, *, replay=None):
    from .source_retention import workspace_fingerprint
    from .replay_profiles import PROFILE_ROOT
    context = dict(workspace_fingerprint=workspace_fingerprint(root),
                   source_root=str(root.resolve()), build_directory=str(output.parent.resolve()),
                   profiles={sha256_file(p) for p in PROFILE_ROOT.glob('*.json')})
    try:
        provenance = json.loads((output/'build-provenance.json').read_bytes())
        context['build_identities'] = {k:sha256_file(Path(provenance['binaries'][k]['path']))
                                       for k in ('runtime','observer','framework')}
        context['state_policy'] = sha256_file(root/'HorseMod/horselib/deterministic/ReplayStatePolicy.hpp')
    except (OSError, ValueError, KeyError): pass
    try:
        import os
        from deterministic_iteration import GAME_ROOT
        context['deployed_runtime'] = str((GAME_ROOT/'ue4ss/Mods/HorseMod/dlls/main.dll').resolve())
        context['native_fixture_inputs'] = dict(game=sha256_file(GAME_ROOT/'SoulcaliburVI.exe'),
            physics=sha256_file(GAME_ROOT.parents[2]/'Engine/Binaries/ThirdParty/PhysX/Win64/VS2015/PhysX3_x64.dll'))
        paths=dict(replay=Path(replay) if replay else root/'ReplayExample/REPLAY_12744704008398858106.bin',
            ucrt=Path(os.environ['SystemRoot'])/'System32/ucrtbase.dll')
        context['external_identities']=dict(game=context['native_fixture_inputs']['game'],
            physx=context['native_fixture_inputs']['physics'], **{k:sha256_file(p) for k,p in paths.items()})
    except (ImportError,OSError,ValueError,KeyError):pass
    return context


def readiness(root, current, deployment):
    rows, errors = {}, []
    try:
        index = json.loads((root/'index.json').read_bytes())
        if index.get('schema') != 1: raise ValueError('unsupported index')
        errors.extend(index.get('errors',[]))
        rows.update({r['sha256']:r for r in index['entries']})
    except FileNotFoundError: pass
    except (OSError, ValueError, KeyError, TypeError) as error: errors.append(str(error))
    for path in sorted((root/'index-entries').glob('*.json')):
        try:
            row=json.loads(path.read_bytes()); rows[row['sha256']]=row
        except (OSError,ValueError,KeyError,TypeError) as error: errors.append(str(error))
    # Include evidence predating index installation; status never publishes it.
    for path in sorted((root/'manifests').glob('*.json')):
        if path.stem in rows:continue
        try: row=index_entry(path);rows[row['sha256']]=row
        except (OSError,ValueError,KeyError,TypeError,RuntimeError) as error: errors.append(str(error))
    verified = []
    for row in rows.values():
        try:
            actual=index_entry(Path(row['path']))
            if actual['sha256'] != row['sha256']: raise RuntimeError('index reference mismatch')
            actual['compatibility']=compatibility(actual,current);verified.append(actual)
        except (OSError,ValueError,KeyError,TypeError,RuntimeError) as error: errors.append(str(error))
    verified.sort(key=lambda r:(r['completed_ns'],r['sha256']),reverse=True)
    checked=set()
    g0_checked=set()
    def validate(row):
        if 'evidence_valid' not in row:
            try:
                verify_references(row,checked)
                if 'G0' in row['gate_evidence']:
                    from .replay_baseline import verify_g0
                    summary=json.loads(Path(row['path']).read_bytes())['summary']
                    # Stage and final-timing receipts share the same aggregate.
                    # Cache only identical contracts within this status call.
                    contract=json.dumps({k:v for k,v in summary.items() if k != 'orchestration_timing'},sort_keys=True)
                    if contract not in g0_checked:
                        verify_g0(summary, current, deployment)
                        g0_checked.add(contract)
                row['evidence_valid']=True
            except (OSError,ValueError,KeyError,TypeError,RuntimeError) as error:
                row['evidence_valid']=False;row['compatibility']='unverified'
                errors.append(dict(path=row['path'],error=str(error)))
        return row['evidence_valid']
    latest = {}
    for role, stages in [('build',{'build'}),('local',{'local'}),
        ('live',{'combat-restore','equivalence','restore','baseline','session-exit'})]:
        row=next((r for r in verified if r['stage'] in stages),None)
        if row:
            validate(row)
            latest[role]={k:row[k] for k in ('result','path','sha256','compatibility','completed_ns','first_failure','evidence_valid')}
    definition=json.loads(DEFINITION.read_bytes())
    gates={}
    for gate, rule in definition['gates'].items():
        candidates=[r for r in verified if gate in r['gate_evidence']]
        relevant=[r for r in verified if (r['stage'] in rule.get('relevant_stages',[])
            or (gate == 'G0' and r['stage'] == 'certify-g0'))
            and (not rule.get('profile_contains') or any(token in (r['profile_name'] or '') for token in rule['profile_contains']))]
        passing=[]
        for row in candidates:
            checks={c['id']:c for c in row['checks']}
            if (row['compatibility']=='current' and row['certifying'] is True
                and row['gate_evidence'][gate].get('result')=='pass'
                and all(checks.get(c,{}).get('outcome')=='pass' for c in rule['checks'])
                and validate(row)):
                passing.append(row)
        attempts = [r for r in verified if gate in r['gate_evidence']
                    or (gate == 'G0' and r['stage'] == 'certify-g0')]
        gates[gate]=dict(result='pass' if passing else 'open', requirements=rule['checks'],
            evidence=passing[0]['path'] if passing else None,
            evidence_status='current' if passing else 'unverified',
            latest_attempt=attempts[0]['path'] if attempts else None)
        gates[gate]['scoped_attempts']=[dict(path=r['path'],result=r['result'],compatibility=r['compatibility'],
            outcomes=r['outcomes'],qualification=False) for r in relevant[:3]]
        newer_failures=[r for r in relevant if r['result'] in ('fail','blocked')
            and passing and r['completed_ns']>passing[0]['completed_ns']]
        if newer_failures:
            gates[gate]['evidence_status']='historical'
            gates[gate]['newer_failure']=dict(path=newer_failures[0]['path'],compatibility=newer_failures[0]['compatibility'])
            if newer_failures[0]['compatibility']=='current':gates[gate]['result']='fail'
    return dict(stage='status', result='unknown' if errors or not verified or deployment.get('state') in ('active','cleanup_pending','unknown') else 'ready' if all(g['result']=='pass' for g in gates.values()) else 'blocked',
        certifying=False, latest=latest, readiness=gates,
        blockers=[g for g,v in gates.items() if v['result']!='pass'],
        blocker_details=definition.get('blockers',[]),
        next_experiment=definition.get('next_experiment',dict(result='undefined',reason='next experiment undefined')),
        deployment_observation=dict(deployment,source='recorded journal; not live process verification'),
        diagnostic_errors=errors, scope='project readiness; scoped experiment outcomes do not qualify gates')
