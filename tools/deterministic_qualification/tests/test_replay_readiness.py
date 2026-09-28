import json
from pathlib import Path

from tools.deterministic_qualification.replay_evidence import store_bytes


def test_new_build_cannot_hide_failed_live_attempt(tmp_path):
    from tools.deterministic_qualification.replay_readiness import rebuild_index, readiness
    evidence = tmp_path/'evidence'
    for stage, result, stamp in [('combat-restore','fail',1),('build','pass',2)]:
        store_bytes(evidence/'manifests', json.dumps(dict(summary=dict(stage=stage,result=result),
            completed_ns=stamp, captures=[], results={})).encode())
    rebuild_index(evidence)
    before = {p:p.read_bytes() for p in evidence.rglob('*') if p.is_file()}
    result = readiness(evidence, {}, {'state':'clean'})
    assert result['latest']['build']['result'] == 'pass'
    assert result['latest']['live']['result'] == 'fail'
    assert all(v['result'] != 'pass' for v in result['readiness'].values())
    assert {p:p.read_bytes() for p in evidence.rglob('*') if p.is_file()} == before


def test_corrupt_index_reference_is_unverified(tmp_path):
    from tools.deterministic_qualification.replay_readiness import rebuild_index, readiness
    evidence = tmp_path/'evidence'
    ref = store_bytes(evidence/'manifests', b'{"summary":{"stage":"build","result":"pass"}}')
    rebuild_index(evidence)
    Path(ref['path']).write_text('{}')
    result = readiness(evidence, {}, {})
    assert result['diagnostic_errors'] and result['result'] == 'unknown'


def test_compatibility_requires_all_recorded_identities():
    from tools.deterministic_qualification.replay_readiness import compatibility
    binaries=dict(runtime='r',observer='o',framework='f')
    row = dict(workspace_fingerprint='a', build_identities=binaries, profile_sha256='p', state_policy='s')
    current = dict(workspace_fingerprint='a', build_identities=binaries, profiles={'p'}, state_policy='s')
    assert compatibility(row,current) == 'current'
    assert compatibility(row,dict(current,workspace_fingerprint='b')) == 'incompatible'
    assert compatibility(row,{}) == 'unverified'
    assert compatibility(dict(row,build_identities={'runtime':'r'}),current)=='unverified'


def test_live_compatibility_needs_current_game_replay_and_native_dependencies():
    from tools.deterministic_qualification.replay_readiness import compatibility
    binaries=dict(runtime='r',observer='o',framework='f')
    row=dict(stage='combat-restore',workspace_fingerprint='s',build_identities=binaries,state_policy='p',profile_sha256='profile')
    current=dict(workspace_fingerprint='s',build_identities=binaries,state_policy='p',profiles={'profile'})
    assert compatibility(row,current)=='unverified'
    external=dict(game='g',replay='replay',physx='x',ucrt='u')
    row['identities']=dict(external,**binaries);current['external_identities']=external
    assert compatibility(row,current)=='current'
    current['external_identities']=dict(external,replay='changed')
    assert compatibility(row,current)=='incompatible'


def test_active_run_is_separate_from_last_completed_run(tmp_path):
    from tools.deterministic_qualification.replay_readiness import readiness
    result = readiness(tmp_path, {}, {'state':'active','run_id':'new'})
    assert result['deployment_observation']['run_id'] == 'new'
    assert result['result'] == 'unknown'
    assert result['next_experiment']['result'] == 'undefined'


def test_legacy_audit_rebuilds_as_unknown_without_rewriting(tmp_path):
    from tools.deterministic_qualification.replay_readiness import rebuild_index, index_entry
    ref = store_bytes(tmp_path/'manifests', json.dumps(dict(scope='source/native audit',
        report={'path':'historical.md'}, project_build_or_live='not run')).encode())
    before = Path(ref['path']).read_bytes()
    assert rebuild_index(tmp_path)['result'] == 'pass'
    row = index_entry(Path(ref['path']))
    assert row['stage'] == 'audit' and row['result'] == 'unknown' and not row['certifying']
    assert row['adaptation']['source_manifest'] == ref['path']
    assert Path(ref['path']).read_bytes() == before


def test_missing_or_corrupt_payload_cannot_qualify_and_new_failure_is_visible(tmp_path):
    from tools.deterministic_qualification.replay_readiness import readiness
    from tools.deterministic_qualification.replay_outcomes import check
    payload = store_bytes(tmp_path/'objects', b'local execution')
    binaries=dict(runtime='r',observer='o',framework='f')
    identity = dict(workspace_fingerprint='a',build_identities=binaries,state_policy='s')
    summary = dict(stage='local',result='pass',certifying=True,outcome_schema=1,
        workspace_fingerprint='a',build_identities=binaries,state_policy_sha256='s',
        checks=[check('integration.full','observer_validity','pass','full suite')],
        gate_evidence={'G3':{'result':'pass'}},raw_log=payload)
    good = store_bytes(tmp_path/'manifests',json.dumps(dict(summary=summary,completed_ns=1)).encode())
    failure = dict(summary,result='fail',certifying=False,gate_evidence={'G3':{'result':'fail'}},
        checks=[check('integration.full','observer_validity','fail','full suite')])
    bad = store_bytes(tmp_path/'manifests',json.dumps(dict(summary=failure,completed_ns=2)).encode())
    result=readiness(tmp_path,identity,{'state':'clean'})
    gate=result['readiness']['G3']
    assert gate['result']=='fail' and gate['evidence']==good['path']
    assert gate['latest_attempt']==bad['path'] and gate['evidence_status']=='historical'
    Path(payload['path']).write_bytes(b'corrupt')
    result=readiness(tmp_path,identity,{'state':'clean'})
    assert result['readiness']['G3']['result']!='pass'
    assert result['latest']['local']['compatibility']=='unverified'
    assert result['diagnostic_errors']
    Path(payload['path']).unlink()
    assert readiness(tmp_path,identity,{'state':'clean'})['readiness']['G3']['result']!='pass'
