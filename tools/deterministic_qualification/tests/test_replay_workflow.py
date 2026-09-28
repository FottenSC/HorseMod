import argparse
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import pytest

from tools.deterministic_qualification.replay_preflight import inspect_preflight
from tools.deterministic_qualification.run_resources import deployment_journal_path
from tools.deterministic_qualification.replay_evidence import retain_run, store_bytes, bounded_context
from tools.deterministic_qualification.replay_local import MAP, select_tests
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.workflow
def test_intermediate_rolling_log_budget():
    from tools.deterministic_qualification import replay_control
    tree=ast.parse(Path(replay_control.__file__).read_text())
    assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign)
                    and any(isinstance(t,ast.Name) and t.id=='log_limit' for t in n.targets))
    expression=compile(ast.Expression(assignment.value),replay_control.__file__,'eval')
    for report,megabytes in [({'rolling_cycles':407},256),({'rolling_cycles':408},256),({'rolling_cycles':600},256),
                             ({'rolling_cycles':174},64),({'rolling_cycles':30},64),
                             ({'index_seek':True},128),({},64)]:
        assert eval(expression,{'report':report})==megabytes*1024*1024


@pytest.mark.workflow
def test_rolling_coverage_does_not_count_startup_repeats_as_combat(tmp_path):
    from tools.deterministic_qualification.replay_evidence import rolling_coverage
    path = tmp_path/'capture.log'
    def boundary(phase, tick):
        return f'[ReplayQualification] boundary ordinal=1 phase={phase} frame={tick} move_state=0\n'
    path.write_text(boundary('input_cache_publication', 21) + boundary('callback_a30', 22)
        + boundary('callback_a30', 23) + boundary('actor_tail', 23)
        + boundary('input_cache_publication', 210) + boundary('callback_a30', 211)
        + boundary('actor_tail', 211))
    result = rolling_coverage(path, 'r', 210, 217)
    assert result['outside_window']['multi_tick_intervals'] == 1
    assert result['outside_window']['repeated_ticks_without_new_publication'] == 1
    assert result['forward']['multi_tick_intervals'] == 0
    assert result['forward']['repeated_ticks_without_new_publication'] == 0
    assert result['forward']['tick_callbacks'] == 1
    assert result['pending_work'] == 'unavailable: no pending-task restoration receipt'


@pytest.mark.workflow
def test_rolling_coverage_separates_regeneration_and_requires_retirement_completion(tmp_path):
    from tools.deterministic_qualification.replay_evidence import rolling_coverage
    path = tmp_path/'capture.log'
    path.write_text('[ReplayQualification] historical combat execution admitted run_id=r from_tick=217 to_tick=210 B_retained=true\n'
        '[ReplayQualification] particle GPU initialized run_id=r tick=214 emitter=aa\n'
        '[ReplayQualification] particle seed run_id=r tick=214 game_thread=true initial=2 current=3\n'
        '[ReplayQualification] particle retirement run_id=r tick=217 vtable_rva=394c100 peer_notifications_and_destructor_returned=true slot_cleared=true\n'
        '[HorseMod] rolling cycle ordinal=1 T=217 A=210 resimulated_ticks=7 committed=true\n')
    result = rolling_coverage(path, 'r', 210, 217)
    assert result['regenerated']['gpu_initializations'] == 1
    assert result['regenerated']['gpu_retirements'] == 1
    assert result['regenerated']['seed_events'] == 1
    assert result['forward']['gpu_initializations'] == 0
    assert result['comparison'] == 'inventory only; independent agreement comes from the enclosing comparator'
    path.write_text(path.read_text().replace('slot_cleared=true', 'slot_cleared=false'))
    with pytest.raises(RuntimeError, match='retirement incomplete'):
        rolling_coverage(path, 'r', 210, 217)


@pytest.mark.workflow
@pytest.mark.parametrize('candidate', [True, False])
def test_retained_rolling_manifest_includes_scoped_coverage(tmp_path, candidate):
    log = tmp_path/'native.log'
    log.write_text('[ReplayQualification] boundary ordinal=1 phase=input_cache_publication frame=210\n'
                   '[ReplayQualification] boundary ordinal=2 phase=callback_a30 frame=211\n'
                   '[ReplayQualification] boundary ordinal=3 phase=actor_tail frame=211\n')
    report = tmp_path/'capture.json'
    report.write_text(json.dumps(dict(result='captured', run_id='r', historical_anchor_tick=210,
        rolling_cycles=1 if candidate else 0, cleanup=dict(complete=True, games_remaining=0),
        raw_log=dict(path=str(log), sha256=hashlib.sha256(log.read_bytes()).hexdigest()))))
    receipt = retain_run(tmp_path/'evidence', dict(stage='evidence', result='pass', rolling_cycles=1), [report])
    manifest = json.loads(Path(receipt['path']).read_bytes())
    assert manifest['captures'][0]['rolling_coverage']['forward']['tick_callbacks'] == 1
    assert manifest['results']['simulation'] == 'not measured'
    assert manifest['diagnostic_complete'] is True


@pytest.mark.workflow
def test_integration_receipt_reuses_only_verified_unchanged_pass(tmp_path, monkeypatch):
    from tools.deterministic_qualification import replay_local, source_retention
    fingerprint = ['first']
    monkeypatch.setattr(source_retention, 'workspace_fingerprint', lambda root: fingerprint[0])
    monkeypatch.setattr(source_retention, 'verify_retention', lambda receipt: None)
    calls = []
    def run(root, output, group, run_native):
        calls.append(fingerprint[0])
        return dict(result='pass', groups=['all'], workspace_fingerprint=fingerprint[0],
                    source_retention={}, command=[sys.executable, '-m', 'pytest', '-q', '-x',
                    *select_tests('all')[0]], raw_log=store_bytes(output/'evidence/objects', b'passed', '.log'))
    monkeypatch.setattr(replay_local, 'run_local', run)
    first = replay_local.run_integration(tmp_path, tmp_path)
    second = replay_local.run_integration(tmp_path, tmp_path)
    assert calls == ['first']
    assert second['reused'] is True
    assert second['raw_log'] == first['raw_log']
    # The override must be rejected even when a valid current-source pass is
    # already cached; rejection only in run_local would leave this path open.
    before_index = (tmp_path/'integration-receipt.json').read_bytes()
    monkeypatch.setenv('HORSE_VFX_OBSERVER_RED_HEADER', str(tmp_path/'alternate.hpp'))
    with pytest.raises(RuntimeError, match='alternate.*header'):
        replay_local.run_integration(tmp_path, tmp_path)
    assert calls == ['first']
    assert (tmp_path/'integration-receipt.json').read_bytes() == before_index
    monkeypatch.delenv('HORSE_VFX_OBSERVER_RED_HEADER')
    Path(first['raw_log']['path']).write_bytes(b'corrupt')
    with pytest.raises(RuntimeError, match='integration log'):
        replay_local.run_integration(tmp_path, tmp_path)
    assert calls == ['first']
    Path(first['raw_log']['path']).write_bytes(b'passed')
    fingerprint[0] = 'second'
    replay_local.run_integration(tmp_path, tmp_path)
    assert calls == ['first', 'second']


@pytest.mark.workflow
@pytest.mark.parametrize('selection', [dict(group='all', layer='unit'), dict(group='auto', test_ids=[
    'tools/deterministic_qualification/tests/test_replay_reporting.py::test_tooling_source_selection_covers_dependents_without_full_suite'])])
def test_narrow_success_cannot_supply_full_suite_gate(tmp_path, monkeypatch, selection):
    from tools.deterministic_qualification import replay_local, source_retention, replay_readiness
    root = Path(__file__).resolve().parents[3]
    monkeypatch.setattr(source_retention, 'workspace_fingerprint', lambda _: 'fixture')
    monkeypatch.setattr(source_retention, 'retain_sources', lambda *_: {})
    monkeypatch.setattr(replay_readiness, 'current_context', lambda *_: {})
    executed = []
    def run(command, **kwargs):
        executed.append(command)
        return SimpleNamespace(returncode=0, stdout='1 passed\n', stderr='')
    monkeypatch.setattr(replay_local.subprocess, 'run', run)
    result = replay_local.run_local(root, tmp_path, run_native=False, **selection)
    assert executed == [result['command']]
    assert result['result'] == 'pass' and result['certifying'] is False
    assert result['gate_evidence'] == {} and result['checks'][0]['id'] == 'integration.selected'


@pytest.mark.workflow
def test_failed_integration_receipt_is_never_reused(tmp_path, monkeypatch):
    from tools.deterministic_qualification import replay_local, source_retention
    monkeypatch.setattr(source_retention, 'workspace_fingerprint', lambda root: 'same')
    calls = []
    def run(*args, **kwargs):
        calls.append(1)
        return dict(result='fail')
    monkeypatch.setattr(replay_local, 'run_local', run)
    replay_local.run_integration(tmp_path, tmp_path)
    replay_local.run_integration(tmp_path, tmp_path)
    assert len(calls) == 2


@pytest.mark.workflow
@pytest.mark.parametrize('entry',('integration','local'))
def test_alternate_observer_header_cannot_qualify_current_source(tmp_path,monkeypatch,entry):
    from tools.deterministic_qualification import replay_local,source_retention,replay_readiness
    alternate=tmp_path/'different.hpp';alternate.write_bytes(b'// alternate source\n')
    monkeypatch.setenv('HORSE_VFX_OBSERVER_RED_HEADER',str(alternate))
    monkeypatch.setattr(source_retention,'workspace_fingerprint',lambda _: 'current')
    monkeypatch.setattr(source_retention,'retain_sources',lambda *_: {})
    monkeypatch.setattr(replay_readiness,'current_context',lambda *_: {})
    calls=[]
    def execute(*args,**kwargs):
        calls.append(args)
        return SimpleNamespace(returncode=0,stdout='',stderr='')
    monkeypatch.setattr(replay_local.subprocess,'run',execute)
    root=Path(__file__).resolve().parents[3]
    with pytest.raises(RuntimeError,match='alternate.*header|HORSE_VFX_OBSERVER_RED_HEADER'):
        if entry=='integration':replay_local.run_integration(root,tmp_path)
        else:replay_local.run_local(root,tmp_path,'all',run_native=False)
    assert calls==[]
    assert not (tmp_path/'integration-receipt.json').exists()


def inspect(tmp_path, document=None, **kwargs):
    deployed = tmp_path / 'main.dll'
    if document is not None:
        deployment_journal_path(deployed).write_text(json.dumps(document))
    return inspect_preflight(deployed, processes=lambda: [],
        disk_usage=lambda _: SimpleNamespace(free=10 * 1024**3), **kwargs)


@pytest.mark.workflow
def test_pending_launch_without_process_stays_blocked_and_read_only(tmp_path):
    doc = dict(schema_version=1, state='cleanup_pending', files=[], launches=[dict(delivery='pending')])
    journal = deployment_journal_path(tmp_path / 'main.dll')
    journal.write_text(json.dumps(doc))
    before = journal.read_bytes()
    result = inspect(tmp_path)
    assert result['result'] == 'blocked'
    assert any(c['check'] == 'launch' for c in result['checks'])
    assert journal.read_bytes() == before
    assert list(tmp_path.iterdir()) == [journal]


@pytest.mark.workflow
@pytest.mark.parametrize('state', ['active', 'cleanup_pending', 'invalid'])
def test_stale_journal_is_not_recovered(tmp_path, state):
    assert inspect(tmp_path, dict(schema_version=1, state=state, files=[]))['result'] == 'blocked'


@pytest.mark.workflow
def test_altered_deployment_and_missing_backup(tmp_path):
    deployed = tmp_path / 'main.dll'
    deployed.write_bytes(b'foreign')
    row = dict(path=str(deployed), before='old', allowed_after=['candidate'], backup=str(tmp_path/'missing'))
    result = inspect(tmp_path, dict(schema_version=1, state='active', run_id='r', files=[row]), active_run='r')
    assert {c['check'] for c in result['checks'] if c['result'] == 'blocked'} == {'deployment', 'backup'}
    assert deployed.read_bytes() == b'foreign'


@pytest.mark.workflow
def test_missing_file_low_disk_and_unrelated_game_block(tmp_path):
    result = inspect_preflight(tmp_path/'main.dll', [tmp_path/'missing'],
        processes=lambda: [SimpleNamespace(pid=33)], disk_usage=lambda _: SimpleNamespace(free=1))
    assert {c['check'] for c in result['checks'] if c['result'] == 'blocked'} == {'required_file', 'processes', 'disk'}


@pytest.mark.workflow
def test_corrupt_journal_is_unknown_not_clean(tmp_path):
    deployment_journal_path(tmp_path/'main.dll').write_text('{')
    assert inspect(tmp_path)['result'] == 'unknown'


@pytest.mark.workflow
def test_preflight_does_not_create_clean_journal(tmp_path):
    assert inspect(tmp_path)['result'] == 'ready'
    assert not list(tmp_path.iterdir())


@pytest.mark.workflow
def test_preflight_checks_loader_before_build_without_publication(tmp_path, monkeypatch):
    from tools.deterministic_qualification import replay_control
    game = tmp_path / 'SoulcaliburVI.exe'
    game.write_bytes(b'game')
    disabled = tmp_path / 'dwmapi.dll.DISABLED'
    target = tmp_path / 'dwmapi.dll'
    monkeypatch.setattr(replay_control, 'DISABLED_REPLAY_LOADER_SHA256', hashlib.sha256(b'verified').hexdigest())
    for payload, expected in [(None, 'blocked'), (b'unknown', 'blocked'), (b'verified', 'ready')]:
        if payload is not None:
            disabled.write_bytes(payload)
        before = {p.name: p.read_bytes() for p in tmp_path.iterdir()}
        result = inspect(tmp_path, loader_executable=game)
        assert result['result'] == expected
        assert {p.name: p.read_bytes() for p in tmp_path.iterdir()} == before
        assert not target.exists()
    target.write_bytes(b'existing installed loader')
    disabled.unlink()
    assert inspect(tmp_path, loader_executable=game)['result'] == 'ready'


@pytest.mark.workflow
def test_group_ids_exist_and_dependency_map_fails_closed():
    mapping = json.loads(MAP.read_text())
    functions = {}
    for group, identifiers in mapping['groups'].items():
        assert identifiers, group
        for identifier in identifiers:
            path, _, name = identifier.partition('::')
            if path not in functions:
                tree = ast.parse(Path(path).read_text())
                functions[path] = {n.name for n in tree.body if isinstance(n, ast.FunctionDef)}
            if name:
                assert name in functions[path], identifier
    assert select_tests('auto', ['HorseMod/unknown.cpp'])[1] == ['all']
    assert select_tests('auto', [])[1] == ['all']
    assert set(select_tests('auto', ['HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl'])[1]) == {'ownership', 'root-transforms', 'contact-latches'}


@pytest.mark.workflow
@pytest.mark.parametrize('name', ['root-transform-417','rolling-cycle42-repro','rolling-cycle54-display-repro','rolling30-native-loading-repro','rolling30-startup-flush-repro','rolling30-gate','rolling600-qualification'])
def test_profile_resolves_existing_cli_and_blocks_before_build(name, monkeypatch, tmp_path):
    import replay_test
    monkeypatch.setattr(sys, 'argv', ['replay_test.py','combat-restore','--profile',name])
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    monkeypatch.setattr(replay_test, 'live_preflight', lambda _: dict(result='blocked', checks=[dict(check='test',result='blocked',detail='no launch')]))
    monkeypatch.setattr(replay_test, 'ensure_build', lambda _: pytest.fail('must not build'))
    assert replay_test.main() == 1
    report = json.loads((tmp_path/'combat-restore-stage.json').read_text())
    assert report['result'] == 'blocked'
    profile = report['profile']
    assert hashlib.sha256(profile['bytes_utf8'].encode()).hexdigest() == profile['sha256']
    assert profile['resolved_settings']['combat_anchor_tick'] == (417 if name == 'root-transform-417' else 210)


@pytest.mark.workflow
@pytest.mark.parametrize('override', [['--candidate-only'],['--rolling-cycles','600'],['--replay-budget-gib','2'],['--flush-startup-loading'],['--no-async-loading-thread']])
def test_profile_rejects_behavioral_override(override, monkeypatch):
    import replay_test
    monkeypatch.setattr(sys, 'argv', ['replay_test.py','combat-restore','--profile','root-transform-417',*override])
    with pytest.raises(SystemExit):
        replay_test.main()


@pytest.mark.workflow
def test_immutable_bundle_deduplicates_and_rejects_corruption(tmp_path):
    a = store_bytes(tmp_path, b'retained')
    assert store_bytes(tmp_path, b'retained') == a
    Path(a['path']).write_bytes(b'damaged')
    with pytest.raises(RuntimeError, match='corrupt'):
        store_bytes(tmp_path, b'retained')


@pytest.mark.workflow
def test_failure_bundle_preserves_traversal_and_bounds_bytes(tmp_path):
    log = tmp_path/'capture.log'
    log.write_text(''.join(f'callback run_id=r tick={tick} generation={gen} hash=bad\n' for gen in (0,1,2) for tick in range(416,425)))
    context = bounded_context(log, {'tick':418, 'generation':2}, maximum_bytes=128)
    assert context['overflow'] and context['bytes'] <= 128
    assert 'generation=0' in ''.join(context['lines'])
    report = tmp_path/'capture.json'
    report.write_text(json.dumps(dict(result='captured',run_id='r',raw_log=dict(path=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest()),cleanup=dict(complete=True,games_remaining=0))))
    receipt = retain_run(tmp_path/'evidence', dict(result='fail',first_mismatch=dict(tick=418,generation=2)), [report])
    bundle = json.loads(Path(receipt['path']).read_text())
    assert bundle['first_failure']['observation']['generation'] == 2
    assert bundle['results']['simulation'] == 'not measured'
    assert bundle['results']['cleanup'] == 'pass'
    log.write_text('overwritten')
    assert Path(bundle['captures'][0]['raw_log']['path']).read_text().startswith('callback')


@pytest.mark.workflow
def test_missing_evidence_never_reports_diagnostic_complete(tmp_path):
    receipt = retain_run(tmp_path/'evidence',dict(result='fail'),[tmp_path/'missing.json'])
    assert not json.loads(Path(receipt['path']).read_text())['diagnostic_complete']


@pytest.mark.workflow
def test_legacy_stage_without_profile_retains_original_failure(tmp_path):
    receipt = retain_run(tmp_path/'evidence', dict(result='fail',profile=None,failure='compiler rejected source'))
    assert json.loads(Path(receipt['path']).read_text())['first_failure']['observation'] == 'compiler rejected source'


def control_fixture(tmp_path):
    from tools.deterministic_qualification.replay_controls_catalog import FIELDS
    log = tmp_path/'control.log'
    log.write_bytes(b'independent observations')
    spec = dict(schema=1, identities={'game':'game','observer':'observer'},
                settings=dict(FIELDS, observations_requested=600, native_fixed_seed=True, serial_particles=True))
    settings = {('coherence_images_requested' if k == 'coherence_images' else k):v for k,v in spec['settings'].items()}
    report = dict(settings, mode='stock',result='captured',control_spec=spec,
        identities=dict(spec['identities'],runtime='absent-candidate-identity'),
        loaded_runtime=dict(verification='owned_process_runtime_absent'),
        cleanup=dict(complete=True,games_remaining=0),
        raw_log=dict(path=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest()))
    return spec,report


@pytest.mark.workflow
@pytest.mark.parametrize('mutation', ['identity','setup','coverage','runtime','cleanup','raw','spec'])
def test_control_reuse_rejects_each_incompatible_dependency(tmp_path, mutation):
    from tools.deterministic_qualification.replay_controls_catalog import validate_control
    spec,report = control_fixture(tmp_path)
    validate_control(report,spec)
    if mutation == 'identity': report['identities']['observer'] = 'other'
    if mutation == 'setup': report['skip_intros'] = True
    if mutation == 'coverage': report['observations_requested'] = 599
    if mutation == 'runtime': report['loaded_runtime']['verification'] = 'runtime_present'
    if mutation == 'cleanup': report['cleanup']['complete'] = False
    if mutation == 'raw': Path(report['raw_log']['path']).write_bytes(b'truncated')
    if mutation == 'spec': report['control_spec'] = {'different':True}
    with pytest.raises(RuntimeError): validate_control(report,spec)


@pytest.mark.workflow
def test_control_catalog_retains_log_and_allows_candidate_only_change(tmp_path):
    from tools.deterministic_qualification.replay_controls_catalog import register_control,find_control,validate_control
    spec,report=control_fixture(tmp_path)
    register_control(tmp_path/'catalog',report,spec)
    Path(report['raw_log']['path']).write_bytes(b'working log overwritten')
    found=find_control(tmp_path/'catalog',spec)
    found['identities']['runtime']='new candidate'
    validate_control(found,spec)


@pytest.mark.workflow
def test_first_failure_cycle_tick_and_missing_generation_are_explicit():
    from tools.deterministic_qualification.replay_evidence import failure_coordinates
    result = failure_coordinates(dict(observation='rolling HUD mismatch/missing cycle208 tick423'))
    assert result['cycle'] == 208 and result['tick'] == 423
    assert result['generation'] == 'unavailable'


@pytest.mark.workflow
def test_native_capacity_failure_retains_first_tick_and_budget(tmp_path):
    from tools.deterministic_qualification.replay_evidence import native_failure
    log = tmp_path/'native.log'
    log.write_text('[HorseMod] checkpoint capacity participant=transient_capture tick=217 owned=1004507950 envelope=89821158 available=69233874\n'
                   '[HorseMod] restore preparation failed participant=complete_B code=9 target=210 original=217 before_publication=true\n'
                   '[HorseMod] rolling failed check=transaction_failed code=9 current=217 target=217 cycles=0 retained_window=true\n'
                   '[ReplayQualification] run failed run_id=owned reason=rolling_native_failure\n')
    assert native_failure(log, 'other') is None
    result = native_failure(log, 'owned')
    coordinates = result['observation']
    assert coordinates['tick'] == 217 and coordinates['cycle'] == 1
    assert coordinates['expected'] == 69233874 and coordinates['actual'] == 89821158
    assert result['check'] == 'native.checkpoint_capacity'


@pytest.mark.workflow
def test_rolling600_requires_current_rolling30_profile_binaries_and_cleanup(tmp_path):
    from tools.deterministic_qualification.replay_profiles import require_rolling30, PROFILE_ROOT
    profile = PROFILE_ROOT/'rolling30-gate.json'
    identities = dict(runtime='runtime', observer='observer', framework='framework')
    summary = dict(result='pass',build_identities=identities, acceptance_results={'simulation':'pass'},
                   profile=dict(definition={'name':'rolling30-gate'},sha256=hashlib.sha256(profile.read_bytes()).hexdigest()))
    manifest = dict(summary=summary, results={'cleanup':'pass'}, diagnostic_complete=True)
    def publish():
        receipt = store_bytes(tmp_path/'manifests',json.dumps(manifest).encode())
        (tmp_path/'rolling30-gate-receipt.json').write_text(json.dumps(receipt))
    publish()
    require_rolling30(tmp_path,identities)
    with pytest.raises(RuntimeError): require_rolling30(tmp_path,dict(identities,runtime='new'))
    manifest['results']['cleanup']='blocked'
    publish()
    with pytest.raises(RuntimeError): require_rolling30(tmp_path,identities)


@pytest.mark.workflow
def test_profile_rejects_false_continuation_metadata(tmp_path, monkeypatch):
    import replay_test
    from tools.deterministic_qualification.replay_profiles import PROFILE_ROOT
    definition=json.loads((PROFILE_ROOT/'root-transform-417.json').read_text())
    definition['arguments'] += ['--index-seek-continuation','121']
    profile=tmp_path/'profile.json'
    profile.write_text(json.dumps(definition))
    monkeypatch.setattr(sys,'argv',['replay_test.py','combat-restore','--profile',str(profile)])
    with pytest.raises(SystemExit): replay_test.main()


@pytest.mark.workflow
def test_profile_allows_verified_candidate_resume(monkeypatch, tmp_path):
    import replay_test
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'combat-restore', '--profile', 'root-transform-417', '--resume-candidate'])
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    monkeypatch.setattr(replay_test, 'live_preflight', lambda _: dict(result='blocked', checks=[]))
    assert replay_test.main() == 1
    report = json.loads((tmp_path/'combat-restore-stage.json').read_text())
    assert report['profile']['resolved_settings']['resume_candidate'] is True


@pytest.mark.workflow
def test_resumed_profile_requires_exact_profile_and_diagnostics():
    from tools.deterministic_qualification.replay_profiles import validate_resumed_profile
    profile = dict(sha256='profile')
    report = dict(experiment_profile=profile, diagnostic_settings={'mask': 7})
    validate_resumed_profile(report, profile, {'mask': 7})
    for changed in [dict(report, experiment_profile={'sha256':'other'}), dict(report, diagnostic_settings={'mask':0})]:
        with pytest.raises(RuntimeError): validate_resumed_profile(changed, profile, {'mask':7})


@pytest.mark.workflow
@pytest.mark.parametrize('point', ['before', 'after'])
def test_cancelled_checkpoint_startup_keeps_empty_native_rewind_plan(tmp_path, point):
    from tools.deterministic_qualification.replay_controls_catalog import startup_signature
    fields = 'round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs'
    common = ' '.join(f'{key}=0' for key in fields.split())
    lines = ['boundaries started run_id=r native_frame=209']
    for ordinal, frame in enumerate((209, 210, 217, 218), 1):
        lines.append(f'[ReplayQualification] boundary ordinal={ordinal} phase=actor_tail sample_version=2 frame={frame} {common}')
    lines += ['boundaries completed run_id=r observations=4 detached=true',
              '[ReplayQualification] particle output run_id=r tick=209 gpu=false count=0 shared_rng=12345678 world_time=0']
    path = tmp_path/'capture.log'
    raw = '\n'.join(lines)
    path.write_text(raw)
    report = dict(mode='runtime', run_id='r', historical_anchor_tick=210, historical_advanced_tick=217,
                  host_seek_target=217, host_seek=True, historical_exact_advance=True,
                  historical_cancel=point, raw_log={'path': str(path)})
    assert startup_signature(report, 210) == startup_signature(dict(report, mode='stock'), 210)
    # Cancellation grants no execution rewind, including one after the startup
    # prefix. Validate the whole capture before selecting compatible controls.
    for invalid in (raw.replace('frame=218', 'frame=209'),
                    raw+'\nhistorical combat execution admitted run_id=r from_tick=217 to_tick=210 B_retained=true'):
        path.write_text(invalid)
        with pytest.raises(RuntimeError):
            startup_signature(report, 210)
    path.write_text(raw)
    with pytest.raises(RuntimeError):
        startup_signature(dict(report, historical_cancel=''), 210)


@pytest.mark.workflow
def test_startup_rng_difference_is_not_a_rollback_failure(tmp_path, monkeypatch):
    from tools.deterministic_qualification import replay_fidelity
    from tools.deterministic_qualification.replay_controls_catalog import require_startup_compatible
    # Callback order is identical in the real incompatible retained controls.
    # Particle clocks/shared RNG reveal the different pre-simulation startup.
    monkeypatch.setattr(replay_fidelity, 'validate_boundary_capture', lambda *a,**k: [{'frame':3,'ordinal':1}])
    reports=[]
    for name, rng in [('stock','d26ef072'), ('runtime','db9c77d7')]:
        log=tmp_path/(name+'.log')
        log.write_text(f'[ReplayQualification] particle output run_id={name} tick=3 gpu=false template=ParticleSpriteEmitter stage count=0 delta=3c888889 time=3ee6666c hash=cbf29ce484222325 shared_rng={rng} game_thread=true world_time=3f66f4fe last_render=3f5c17c0\n')
        reports.append(dict(mode=name,run_id=name,rolling_cycles=30,historical_anchor_tick=210,
                            raw_log=dict(path=str(log))))
    with pytest.raises(RuntimeError,match='startup incompatible'):
        require_startup_compatible(*reports,210)


@pytest.mark.workflow
def test_diagnostic_settings_partial_write_preserves_recoverable_predecessor(tmp_path, monkeypatch):
    import textwrap
    from tools.deterministic_qualification import replay_control, process_control
    from tools.deterministic_qualification.run_resources import RunResources, bytes_hash
    root=tmp_path/'requests'
    root.mkdir()
    settings=root/'replay_diagnostics.ini'
    settings.write_bytes(b'previous settings\n')
    owner=RunResources(tmp_path/'resources.json')
    owner.begin('replay-settings-test')
    source=Path(replay_control.__file__).read_text()
    start=source.index('            diagnostic_settings = getattr(')
    end=source.index('            if any((root / name).exists()',start)
    publication=textwrap.dedent(source[start:end])
    original=Path.write_bytes
    def interrupted(path, data):
        if 'replay_diagnostics.ini' in path.name:
            original(path,b'partial')
            raise OSError('injected write interruption')
        return original(path,data)
    monkeypatch.setattr(Path,'write_bytes',interrupted)
    with pytest.raises(OSError,match='injected write interruption'):
        exec(compile(publication,'production diagnostic publication','exec'),
             dict(args=SimpleNamespace(),resources=owner,root=root,bytes_hash=bytes_hash,report={}))
    monkeypatch.setattr(Path,'write_bytes',original)
    monkeypatch.setattr(process_control,'list_game_processes',lambda: [])
    owner.restore()
    assert settings.read_bytes()==b'previous settings\n'
    assert not list(root.glob('*.tmp'))


@pytest.mark.workflow
def test_native_loading_diagnostic_launch_and_consumption_guards():
    from tools.deterministic_qualification.replay_control import native_launch_options, validate_native_loading_state
    assert native_launch_options(dict(native_fixed_seed=False)) == ()
    assert native_launch_options(dict(native_fixed_seed=True)) == ('-FixedSeed',)
    assert native_launch_options(dict(native_fixed_seed=True,no_async_loading_thread=True)) == ('-FixedSeed','-NoAsyncLoadingThread')
    with pytest.raises(RuntimeError): native_launch_options(dict(native_fixed_seed=False,no_async_loading_thread=True))
    with pytest.raises(RuntimeError,match='native queued thread pool'):
        native_launch_options(dict(native_fixed_seed=True,no_async_loading_thread=True,no_native_threading=True))
    with pytest.raises(RuntimeError): native_launch_options(dict(native_fixed_seed=True,no_native_threading=True))
    good=dict(enabled=0,event_driven=0,thread_created=0,thread_pointer=0,object_epoch=-2147483647,cache_epoch=-2147483647)
    assert validate_native_loading_state(good) == good
    event_driven=dict(good,event_driven=1)
    assert validate_native_loading_state(event_driven) == event_driven
    for key,value in [('enabled',1),('enabled',2),('event_driven',2),('thread_created',1),('thread_pointer',1),('thread_pointer',None),('object_epoch',-1),('object_epoch',None),('cache_epoch',0),('cache_epoch',-1),('cache_epoch',None)]:
        with pytest.raises(RuntimeError): validate_native_loading_state(dict(good,**{key:value}))
    with pytest.raises(RuntimeError): validate_native_loading_state({})


@pytest.mark.workflow
def test_single_game_thread_keeps_native_pool_and_is_control_identity(tmp_path):
    from tools.deterministic_qualification.replay_control import native_launch_options
    from tools.deterministic_qualification.replay_controls_catalog import validate_control
    assert native_launch_options(dict(native_fixed_seed=True,single_game_thread=True,
        no_async_loading_thread=True)) == ('-FixedSeed','-ONETHREAD','-NoAsyncLoadingThread')
    spec,report=control_fixture(tmp_path)
    spec['settings']['single_game_thread']=True;report['single_game_thread']=True
    with pytest.raises(RuntimeError):validate_control(report,spec)
    report['single_game_thread_option_forwarded']=True
    validate_control(report,spec)
    report['single_game_thread']=False
    with pytest.raises(RuntimeError):validate_control(report,spec)


@pytest.mark.workflow
def test_known_null_pool_setup_rejected_before_deployment_access():
    from types import SimpleNamespace
    from tools.deterministic_qualification.replay_control import run_replay_control
    # No filesystem paths: rejecting the proved startup fault must precede
    # even reading deployment identities, not just process launch.
    args=SimpleNamespace(watch_frames=300,skip_intros=True,include_setup=True,
                         no_native_threading=True,no_async_loading_thread=True)
    with pytest.raises(RuntimeError,match='native queued thread pool'):
        run_replay_control(args)


@pytest.mark.workflow
def test_native_loading_control_requires_matching_setup_and_consumption(tmp_path):
    from tools.deterministic_qualification.replay_controls_catalog import validate_control
    spec,report=control_fixture(tmp_path)
    spec['settings']['no_async_loading_thread']=True
    report['no_async_loading_thread']=True
    with pytest.raises(RuntimeError): validate_control(report,spec)
    report['native_loading_option_forwarded']=True
    with pytest.raises(RuntimeError): validate_control(report,spec)
    report['native_loading_state']=dict(enabled=0,event_driven=0,thread_created=0,thread_pointer=0,object_epoch=-2147483647,cache_epoch=-2147483647)
    validate_control(report,spec)
    report['no_async_loading_thread']=False
    with pytest.raises(RuntimeError): validate_control(report,spec)


@pytest.mark.workflow
def test_startup_flush_control_requires_completed_native_receipt(tmp_path):
    from tools.deterministic_qualification.replay_controls_catalog import validate_control
    spec,report=control_fixture(tmp_path)
    spec['settings']['flush_startup_loading']=True; report['flush_startup_loading']=True
    with pytest.raises(RuntimeError): validate_control(report,spec)
    for receipt in [dict(calls=0,first_tick=1,native_callbacks=True),dict(calls=65,first_tick=1,native_callbacks=True),dict(calls=1,first_tick=0,native_callbacks=True),dict(calls=1,first_tick=1,native_callbacks=False)]:
        report['startup_loading_flush']=receipt
        with pytest.raises(RuntimeError): validate_control(report,spec)
    from tools.deterministic_qualification.replay_control import startup_loading_receipt
    row='startup loading flush complete run_id=case calls=3 first_tick=1 native_callbacks=true registration_budget_ms=60000 registration_previous_bits=40a00000 registration_restored=true raw_asset_checks=10 raw_asset_waits=2 raw_asset_completion=true'
    for bad in (row.replace('restored=true','restored=false'),row.replace('60000','0'),row+row,row.replace('run_id=case','run_id=other'),row.replace('raw_asset_completion=true','raw_asset_completion=false'),row.replace('raw_asset_checks=10','raw_asset_checks=0')):
        with pytest.raises(RuntimeError):startup_loading_receipt(bad,'case')
    report['startup_loading_flush']=startup_loading_receipt(row,'case')
    validate_control(report,spec)
    for field,value in [('registration_budget_ms',0),('registration_restored',False),('registration_previous_bits','missing'),('raw_asset_completion',False),('raw_asset_checks',0)]:
        changed=dict(report,startup_loading_flush=dict(report['startup_loading_flush'],**{field:value}))
        with pytest.raises(RuntimeError):validate_control(changed,spec)
    report['flush_startup_loading']=False
    with pytest.raises(RuntimeError): validate_control(report,spec)


@pytest.mark.native_contract
def test_production_startup_loading_registration_budget(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture=tmp_path/'startup.cpp'
    fixture.write_text('#include <cstdio>\n#include <cstdlib>\nvoid expect(bool v,const char* text){if(!v){std::puts(text);std::exit(1);}}\n#include "replay_startup_loading_selftest.inl"\nint main(){StartupLoadingTest::Run();}\n',encoding='utf-8')
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT / "tools"}" "{fixture}" /Fe:startup.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'startup.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.workflow
def test_consumer_suspension_receipt_rejects_missing_or_duplicate_lifecycle():
    import replay_test as module
    held='consumer task held tick=211 epoch=400 owner_index=31 owner_generation=7 native_started=false recovery=false\n'
    done='consumer suspension complete tick=211 epoch=400 polls=20 native_executions=1 application_complete=true recovery=false\n'
    assert module.validate_consumer_suspension(held+done)['recovery'] is False
    for raw in (held,done,held+done+done,held+held+done,held+done.replace('epoch=400','epoch=401'),held+done+'consumer resume rejected'):
        with pytest.raises(RuntimeError):module.validate_consumer_suspension(raw)


@pytest.mark.workflow
@pytest.mark.parametrize("setup", ["no_native_threading", "single_game_thread"])
def test_consumer_capture_preserves_no_async_launch_setup(tmp_path,monkeypatch,setup):
    import json
    from types import SimpleNamespace
    import replay_test as module
    monkeypatch.setattr(module,'ACTIVE_PROFILE',None)
    monkeypatch.setattr(module,'CAPTURE_REPORTS',[])
    args=SimpleNamespace(report=tmp_path/'capture.json',watch_frames=300,mode='runtime',no_async_loading_thread=True,**{setup:True})
    def run(actual):
        assert actual.no_async_loading_thread is True
        actual.report.write_text(json.dumps({'result':'captured'}))
        return 0
    monkeypatch.setattr(module,'run_replay_control',run)
    assert module.capture(args)['result']=='captured'


@pytest.mark.workflow
@pytest.mark.parametrize('flags,expected', [
    ([], 360),
    (['--samples', '600'], 600),
    (['--samples=600'], 600),
    (['--samples', '1200'], 1200),
])
@pytest.mark.parametrize('bootstrap_failure', [False, True])
def test_bootstrap_cli_samples_reach_capture_and_retained_evidence(
        tmp_path, monkeypatch, flags, expected, bootstrap_failure):
    import replay_test as module
    monkeypatch.setattr(module, 'OUTPUT', tmp_path)
    monkeypatch.setattr(module, 'CAPTURE_REPORTS', [])
    for name in ('ACTIVE_PROFILE', 'ACTIVE_CAPTURE_SOURCES', 'ACTIVE_DIAGNOSTICS', 'ACTIVE_STARTUP_LOADING'):
        monkeypatch.setattr(module, name, None)
    preflight_samples, captures, validated, prerequisites = [], [], [], []
    def preflight(options):
        preflight_samples.append(options.samples)
        return dict(result='ready', checks=[])
    monkeypatch.setattr(module, 'live_preflight', preflight)
    monkeypatch.setattr(module, 'ensure_build', lambda jobs: {})
    monkeypatch.setattr(module, 'retain_sources', lambda *args: {})
    monkeypatch.setattr(module, 'workspace_fingerprint', lambda *args: 'fixture')
    monkeypatch.setattr(module, 'sha256_file', lambda path: 'fixture')
    def prerequisite(command, **kwargs):
        prerequisites.append((command, kwargs))
        return subprocess.CompletedProcess(command, 0)
    monkeypatch.setattr(module.subprocess, 'run', prerequisite)
    def native_capture(args):
        captures.append(args)
        args.report.write_text(json.dumps(dict(result='captured', mode=args.mode,
            observations_requested=args.watch_frames, cleanup=dict(complete=True, games_remaining=0))))
        return 0
    monkeypatch.setattr(module, 'run_replay_control', native_capture)
    def bootstrap_check(args):
        validated.append(args)
        if bootstrap_failure:
            raise RuntimeError('bootstrap check rejected fixture')
    monkeypatch.setattr(module, 'require_bootstrap', bootstrap_check)
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'bootstrap', *flags])

    # Exercise the real CLI, control_args, capture and immutable evidence writer;
    # only native/build services and the bootstrap validator are controlled.
    assert module.main() == (1 if bootstrap_failure else 0)
    assert preflight_samples == [expected]
    assert len(captures) == 1 and validated == captures
    args = captures[0]
    assert args.watch_frames == expected and args.mode == 'runtime'
    assert args.skip_intros and args.include_setup and not args.full_match
    assert not any(getattr(args, key, False) for key in ('executor', 'host_seek', 'probe_historical_restore'))
    assert len(prerequisites) == 1 and prerequisites[0][1]['check'] is True
    assert prerequisites[0][0] == [sys.executable, '-m', 'pytest', '-q',
        'tools/deterministic_qualification/tests/test_replay_run.py',
        'tools/deterministic_qualification/tests/test_process_control.py']
    stage = json.loads((tmp_path/'bootstrap-stage.json').read_text())
    manifest = json.loads(Path(stage['retained_evidence']['path']).read_text())
    assert len(manifest['captures']) == 1
    retained = json.loads(Path(manifest['captures'][0]['report']['path']).read_text())
    assert retained['observations_requested'] == expected
    assert manifest['summary']['certifying'] is False
    assert manifest['results']['simulation'] != 'pass'
    if bootstrap_failure:
        assert stage['result'] == 'fail' and stage['failure'] == 'bootstrap check rejected fixture'


@pytest.mark.workflow
def test_non_bootstrap_cli_samples_default_remains_1200(tmp_path, monkeypatch):
    import replay_test as module
    monkeypatch.setattr(module, 'OUTPUT', tmp_path)
    monkeypatch.setattr(module, 'CAPTURE_REPORTS', [])
    for name in ('ACTIVE_PROFILE', 'ACTIVE_CAPTURE_SOURCES', 'ACTIVE_DIAGNOSTICS', 'ACTIVE_STARTUP_LOADING'):
        monkeypatch.setattr(module, name, None)
    observed = []
    def preflight(options):
        observed.append(options.samples)
        return dict(result='ready', checks=[])
    monkeypatch.setattr(module, 'live_preflight', preflight)
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'preflight'])
    assert module.main() == 0
    assert observed == [1200]


@pytest.mark.workflow
@pytest.mark.parametrize('samples', ['119', '36001'])
def test_bootstrap_cli_samples_reject_out_of_bounds(monkeypatch, samples):
    import replay_test as module
    monkeypatch.setattr(module, 'live_preflight', lambda *args: pytest.fail('invalid samples reached preflight'))
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'bootstrap', '--samples', samples])
    with pytest.raises(SystemExit) as error:
        module.main()
    assert error.value.code == 2


@pytest.mark.workflow
def test_c18_diagnostic_cli_uses_ordinary_forward_capture(tmp_path, monkeypatch):
    import replay_test as module
    from deterministic_qualification import replay_evidence
    monkeypatch.setattr(module, 'OUTPUT', tmp_path)
    monkeypatch.setattr(module, 'CAPTURE_REPORTS', [])
    monkeypatch.setattr(module, 'live_preflight', lambda options: dict(result='ready', checks=[]))
    monkeypatch.setattr(module, 'ensure_build', lambda jobs: {})
    monkeypatch.setattr(module, 'retain_sources', lambda *args: {})
    monkeypatch.setattr(module, 'workspace_fingerprint', lambda *args: 'fixture')
    monkeypatch.setattr(module, 'sha256_file', lambda path: 'fixture')
    monkeypatch.setattr(replay_evidence, 'retain_run', lambda *args: {})
    calls = []
    def capture(args):
        calls.append(args)
        assert args.mode == 'runtime' and args.c18_diagnostic is True
        assert args.skip_intros and args.include_setup
        assert args.single_game_thread and args.no_async_loading_thread
        assert args.watch_frames == 360 and not getattr(args, 'executor', False)
        assert not any(getattr(args, key, False) for key in
                       ('probe_consumer_task', 'probe_application_pause', 'probe_historical_restore', 'host_seek'))
        return dict(result='diagnostic_observed', c18_diagnostic_stop={'entry_sequence': 1})
    monkeypatch.setattr(module, 'capture', capture)
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'c18-diagnostic'])
    assert module.main() == 0
    assert len(calls) == 1


@pytest.mark.workflow
@pytest.mark.parametrize('extra', ['--consumer-task', '--application-pause', '--host-seek', '--full-match'])
def test_c18_diagnostic_cli_rejects_execution_options(monkeypatch, extra):
    import replay_test as module
    monkeypatch.setattr(module, 'live_preflight', lambda *args: pytest.fail('incompatible options reached live preflight'))
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'c18-diagnostic', extra])
    with pytest.raises(SystemExit):
        module.main()


@pytest.mark.workflow
def test_c18_diagnostic_control_parks_and_stops_at_separate_boundary(tmp_path):
    """Execute the actual runner branches with external file/process services bounded."""
    import time
    from contextlib import ExitStack
    from deterministic_qualification import replay_control as control
    tree = ast.parse(Path(control.__file__).read_text())
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'run_replay_control')
    def execute(node, scope):
        exec(compile(ast.Module(body=[node], type_ignores=[]), 'production_c18_runner', 'exec'), scope)
    park = next(n for n in ast.walk(function) if isinstance(n, ast.If)
                and "report['c18_diagnostic']" in ast.unparse(n.test)
                and "resources.park_report(vfx_observation)" in ast.unparse(n))
    parked = []; temporary = []
    report = dict(c18_diagnostic=True, probe_consumer_task=False, consumer_mutation=False,
                  diagnostic_reserved_bytes=8192, diagnostic_output_limit_bytes=0)
    with ExitStack() as stack:
        scope = dict(report=report, root=tmp_path, args=SimpleNamespace(report=tmp_path/'capture.json'),
                     resources=SimpleNamespace(park_report=parked.append, prepare_temporary=temporary.append,
                                               document={'run_id':'selected'}), process_diagnostics=stack,
                     retain_vfx_completion_observation=lambda *args: None,
                     VFX_OBSERVATION_OWNED_BYTES=control.VFX_OBSERVATION_OWNED_BYTES,
                     VFX_OBSERVATION_SIDECAR_BYTES=control.VFX_OBSERVATION_SIDECAR_BYTES)
        execute(park, scope)
    assert parked == [tmp_path/'vfx_completion_observation.json']
    assert temporary == [tmp_path/'vfx_completion_observation.json.tmp']
    assert report['diagnostic_reserved_bytes'] == 8192+4*1024*1024
    request = next(n for n in ast.walk(function) if isinstance(n, ast.If)
                   and ast.unparse(n.test) == "report['c18_diagnostic']" and 'request_version = 19' in ast.unparse(n))
    scope = dict(report=report, capture_mode='trajectory', args=SimpleNamespace(mode='runtime', watch_frames=360),
                 executor_options='include_setup=true\nskip_intros=true\n', request_version=13)
    execute(request, scope)
    assert scope['request_version'] == 19 and scope['executor_options'].endswith('c18_diagnostic=true\nc18_selection=combat_170_220\n')
    scope['executor_options'] += 'probe_application_pause=true\n'
    with pytest.raises(RuntimeError, match='exclusive ordinary-forward'):
        execute(request, scope)
    waiting = next(n for n in ast.walk(function) if isinstance(n, ast.Try)
                   and any(isinstance(h.type, ast.Name) and h.type.id == '_C18DiagnosticObserved' for h in n.handlers))
    from deterministic_qualification.replay_entry import wait_for_replay_entry
    receipt = {'entry_sequence':17}
    def guard():
        raise control._C18DiagnosticObserved(receipt)
    scope = dict(report=report, run_id='selected', deadline=time.monotonic()+1, time=time,
                 guard=guard, wait_for_replay_entry=wait_for_replay_entry, _C18DiagnosticObserved=control._C18DiagnosticObserved)
    execute(waiting, scope)
    assert report['c18_diagnostic_stop'] is receipt
    scope['wait_for_replay_entry'] = lambda *args: SimpleNamespace(reason='trajectory_control_complete')
    with pytest.raises(RuntimeError, match='not observed in bounded forward window'):
        execute(waiting, scope)
    def failed(*args):
        raise RuntimeError('native failure')
    scope['wait_for_replay_entry'] = failed
    with pytest.raises(RuntimeError, match='native failure'):
        execute(waiting, scope)


@pytest.mark.workflow
@pytest.mark.parametrize('changed', [None, 'rows', 'hub', 'collection_header', 'descriptor_fields',
                                     'entry_samples_match', 'provider_census_valid', 'status'])
def test_c18_rejected_live_receipt_stays_diagnostic_only(tmp_path, changed):
    import hashlib
    import replay_test as module
    from deterministic_qualification import replay_control as control
    # Immutable output of the real rejected occurrence, not a fixture census.
    digest = 'a974e55ddaa5a4a3b6d692a1f9b7a399bb342cac22c44a0ef7108c229ea0db7d'
    source = module.OUTPUT/'evidence/objects'/f'{digest}.json'
    if not source.is_file():
        pytest.skip('requires immutable 2026-09-27 C18 live rejection')
    raw = source.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == digest
    value = json.loads(raw)
    witness = value['collection18_witness']
    diagnostic = witness['zero_serial_diagnostic']
    assert diagnostic['status'] == 'rejected' and diagnostic['failure'] == 'entry_pointer_or_header'
    expected = {key: witness[key] for key in ('entry_sequence', 'return_sequence', 'thread', 'collection', 'descriptor')}
    if changed:
        diagnostic[changed] = {'rows': [{}], 'hub': {}, 'collection_header': dict(data=0, count=1, capacity=1),
                               'descriptor_fields': {}, 'entry_samples_match': True,
                               'provider_census_valid': True, 'status': 'sampled'}[changed]
    path = tmp_path/'sidecar.json'
    path.write_text(json.dumps(value))
    report = {}
    control.retain_vfx_completion_observation(report, path, value['run_id'], tmp_path/'objects')
    if changed:
        assert 'vfx_completion_observation_collection_error' in report
        assert 'vfx_collection18_zero_serial_diagnostic' not in report
        with pytest.raises(RuntimeError, match='diagnostic schema'):
            control.c18_diagnostic_stop_receipt(path, value['run_id'], tmp_path/'objects', expected)
    else:
        assert 'vfx_completion_observation_collection_error' not in report
        assert report['vfx_collection18_zero_serial_diagnostic'] == diagnostic
        assert report['vfx_collection18_material_provider_census']['entry_copy_valid'] is False
        assert report['vfx_completion_observation_phase_complete'] is False
        receipt = control.c18_diagnostic_stop_receipt(path, value['run_id'], tmp_path/'objects', expected)
        assert receipt['zero_serial_status'] == 'rejected' and receipt['phase_complete'] is False
        assert Path(receipt['raw']['path']).read_bytes() == path.read_bytes()


@pytest.mark.workflow
@pytest.mark.parametrize('opt_in', [None, False, True], ids=['absent', 'disabled', 'enabled'])
def test_c18_diagnostic_close_requires_opt_in_and_clean_exit(tmp_path, opt_in):
    from deterministic_qualification import replay_control as control
    tree = ast.parse(Path(control.__file__).read_text(encoding='utf-8'))
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'run_replay_control')
    body = next(n.body for n in ast.walk(function) if isinstance(n, ast.With)
                and any(isinstance(c, ast.Expr) and isinstance(c.value, ast.Call)
                        and isinstance(c.value.func, ast.Name) and c.value.func.id == 'close_game' for c in n.body))
    begin = next(i for i, n in enumerate(body) if isinstance(n, ast.Expr) and isinstance(n.value, ast.Call)
                 and isinstance(n.value.func, ast.Name) and n.value.func.id == 'close_game')
    block = compile(ast.Module(body=body[begin:], type_ignores=[]), 'production_c18_close', 'exec')
    receipt = {'entry_sequence': 17}
    calls = []
    report = {} if opt_in is None else {'c18_diagnostic': opt_in}
    if opt_in:
        report['c18_diagnostic_stop'] = receipt
    def close(pid, *, timeout_seconds):
        assert pid == 1 and timeout_seconds == 30
        calls.append('close')
    def exit_code():
        calls.append('exit')
        return code
    def retain(path, run_id, objects, expected):
        assert calls == ['close', 'exit'] and 'result' not in report
        assert path == tmp_path/'sidecar.json' and run_id == 'selected'
        assert objects == tmp_path/'evidence/objects' and expected is receipt
        calls.append('receipt')
        return dict(expected, raw='immutable')
    scope = dict(pid=1, report=report, close_game=close, exit_witness=SimpleNamespace(exit_code=exit_code),
                 vfx_observation=tmp_path/'sidecar.json', run_id='selected',
                 args=SimpleNamespace(report=tmp_path/'capture.json'), c18_diagnostic_stop_receipt=retain)
    for code in (0xc0000005, 259, None):
        calls.clear()
        with pytest.raises(RuntimeError, match='did not exit cleanly'):
            exec(block, scope)
        assert calls == ['close', 'exit'] and 'result' not in report
    code = 0
    calls.clear()
    exec(block, scope)
    assert calls == ['close', 'exit'] + (['receipt'] if opt_in else [])
    assert report['result'] == ('diagnostic_observed' if opt_in else 'captured')
    if opt_in:
        assert report['c18_diagnostic_stop'] == dict(receipt, raw='immutable')
        # Failed sidecar association must never become a successful diagnostic.
        report.pop('result')
        calls.clear()
        def reject(*args):
            assert calls == ['close', 'exit']
            calls.append('receipt')
            raise RuntimeError('sidecar association failed')
        scope['c18_diagnostic_stop_receipt'] = reject
        with pytest.raises(RuntimeError, match='sidecar association failed'):
            exec(block, scope)
        assert calls == ['close', 'exit', 'receipt'] and 'result' not in report
    else:
        assert 'c18_diagnostic_stop' not in report


@pytest.mark.workflow
def test_c18_diagnostic_poll_does_not_open_native_publication(tmp_path, monkeypatch):
    from deterministic_qualification import replay_control as control
    tree=ast.parse(Path(control.__file__).read_text())
    function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='run_replay_control')
    guard=next(n for n in ast.walk(function) if isinstance(n,ast.FunctionDef) and n.name=='guard')
    poll=next(n for n in ast.walk(guard) if isinstance(n,ast.If)
              and ast.unparse(n.test)=="report['c18_diagnostic']")
    from deterministic_qualification import replay_entry
    monkeypatch.setattr(replay_entry,'require_replay_request_healthy',lambda run: None)
    marker=('[ReplayQualification] C18 combat diagnostic return run_id=selected entry_sequence=17 return_sequence=18 '
            'thread=123 collection=456 descriptor=789 arm_sequence=16 arm_frame=170 arm_epoch=700 manager=1000 player=2000 '
            'selected_frame=177 selected_epoch=707 selected_round_frame=48 observed_frame=178 observed_epoch=708 '
            'ownership_proven=false completion_proven=false\n')
    log=tmp_path/'UE4SS.log';log.write_text('unrelated log line\n'*4000+marker+'unrelated log line\n'*4000)
    original_open=Path.open
    def guarded_open(path, *args, **kwargs):
        assert path==log, 'live diagnostic polled the native sidecar'
        return original_open(path,*args,**kwargs)
    monkeypatch.setattr(Path, 'open', guarded_open)
    from deterministic_qualification.replay_controls import IndexedControlEvents
    events=IndexedControlEvents();events.keys=(b'C18 combat diagnostic return run_id=',)
    scope=dict(__package__=control.__package__, report={'c18_diagnostic':True}, tail=marker, run_id='selected',
               c18_events=events,args=SimpleNamespace(log=log),
               c18_diagnostic_return_marker=control.c18_diagnostic_return_marker,
               _C18DiagnosticObserved=control._C18DiagnosticObserved)
    with pytest.raises(control._C18DiagnosticObserved) as stopped:
        exec(compile(ast.Module(body=[poll],type_ignores=[]),'production_c18_poll','exec'),scope)
    assert stopped.value.receipt==dict(entry_sequence=17,return_sequence=18,thread=123,collection=456,descriptor=789,
        arm_sequence=16,arm_frame=170,arm_epoch=700,manager=1000,player=2000,selected_frame=177,
        selected_epoch=707,selected_round_frame=48,observed_frame=178,observed_epoch=708)
    assert control.c18_diagnostic_return_marker(marker,'another-run') is None
    assert control.c18_diagnostic_return_marker(marker.replace('completion_proven=false','completion_proven=true'),'selected') is None
    with pytest.raises(RuntimeError,match='ambiguous'):
        control.c18_diagnostic_return_marker(marker+marker,'selected')
    with pytest.raises(RuntimeError,match='coordinates'):
        control.c18_diagnostic_return_marker(marker.replace('return_sequence=18','return_sequence=16'),'selected')
    # Execute the actual post-exit association block: the live guard above never
    # reaches this file reader; only the closed-writer path may retain/compare it.
    closed=next(n for n in ast.walk(function) if isinstance(n,ast.If)
                and ast.unparse(n.test)=="report.get('c18_diagnostic', False)"
                and 'c18_diagnostic_stop_receipt(' in ast.unparse(n))
    calls=[]
    def retain(path, run, objects, expected):
        calls.append(expected);return dict(expected,raw='immutable')
    scope=dict(report={'c18_diagnostic':True,'c18_diagnostic_stop':stopped.value.receipt},
               vfx_observation=tmp_path/'sidecar.json',run_id='selected',args=SimpleNamespace(report=tmp_path/'capture.json'),
               c18_diagnostic_stop_receipt=retain)
    exec(compile(ast.Module(body=[closed],type_ignores=[]),'production_c18_closed','exec'),scope)
    assert calls==[stopped.value.receipt] and scope['report']['c18_diagnostic_stop']['raw']=='immutable'


@pytest.mark.workflow
@pytest.mark.parametrize('key,bad', [('arm_sequence',17),('arm_frame',23),('arm_epoch',708),
    ('selected_frame',23),('selected_frame',221),('selected_epoch',699),('selected_epoch',709),
    ('selected_round_frame',0),('observed_frame',176),('observed_frame',222),('observed_epoch',706)])
def test_c18_combat_marker_rejects_wrong_coordinate(key, bad):
    from deterministic_qualification.replay_control import c18_diagnostic_return_marker
    values=dict(entry_sequence=17,return_sequence=18,thread=123,collection=456,descriptor=789,
        arm_sequence=16,arm_frame=170,arm_epoch=700,manager=1000,player=2000,selected_frame=177,
        selected_epoch=707,selected_round_frame=48,observed_frame=178,observed_epoch=708)
    def marker(prefix='C18 combat diagnostic return'):
        return ('[ReplayQualification] '+prefix+' run_id=selected'+''.join(f' {k}={v}' for k,v in values.items())
                +' ownership_proven=false completion_proven=false\n')
    assert c18_diagnostic_return_marker(marker(),'selected') == values
    assert c18_diagnostic_return_marker(marker('C18 diagnostic return'),'selected') is None
    assert c18_diagnostic_return_marker(marker(),'previous-run') is None
    values[key]=bad
    with pytest.raises(RuntimeError,match='coordinates'):
        c18_diagnostic_return_marker(marker(),'selected')


@pytest.mark.workflow
def test_c18_combat_arm_follows_verified_trajectory_handoff():
    import replay_test as module
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=source.index('    void ObserveReplayTrajectory()')
    end=source.index('        if (trajectory_samples_ != 0 && sample.frame == trajectory_last_.frame',start)
    boundary=source[start:end]
    arm=boundary.index('if(!ObserveC18CombatSample(sample))return;')
    for prerequisite in ('if (battle_terminate_observed_)', 'if (boundary_failed_',
                         'if (!ReadReplayTrajectory(battle_manager_, sample,',
                         'if (!same) { Fail("playback_recording_payload_mismatch")',
                         'if (!sample.initial_reset_equal)'):
        assert boundary.index(prerequisite)<arm
    assert 'horsemod_read_collection18_observation_return' not in boundary
    assert 'if(c18_diagnostic_arm_)Fail("c18_combat_scene_lost")' in boundary
    assert 'if(c18_diagnostic_arm_)Fail("c18_combat_sample_unreadable")' in boundary


@pytest.mark.workflow
@pytest.mark.parametrize('flags,expected', [
    (['--flush-startup-loading','--no-async-loading-thread'],True),
    ([],False),
    (['--profile','cancel-after210-217-startup-serial'],True),
])
def test_cli_loading_setup_reaches_candidate_control_and_catalog(tmp_path,monkeypatch,flags,expected):
    import replay_test as module
    from deterministic_qualification import replay_controls_catalog as catalog, replay_evidence
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module,'CAPTURE_REPORTS',[])
    monkeypatch.setattr(module,'ACTIVE_PROFILE',None)
    monkeypatch.setattr(module,'ACTIVE_STARTUP_LOADING',{},raising=False)
    fixture=tmp_path/'identity.bin';fixture.write_bytes(b'fixture')
    captured=[];lookups=[];registered=[]
    def capture(actual):
        assert actual.flush_startup_loading is expected
        assert actual.no_async_loading_thread is expected
        captured.append(actual.mode)
        actual.report.write_text(json.dumps(dict(result='captured',run_id=actual.mode,control_spec=catalog.control_spec(actual))))
        return 0
    def find(root,spec,startup):
        assert spec['settings']['flush_startup_loading'] is expected
        assert spec['settings']['no_async_loading_thread'] is expected
        lookups.append(spec)
        return None if len(lookups)==1 else dict(run_id='cached-stock',catalog_manifest=str(tmp_path/'cached.json'))
    def register(root,report,spec,startup):
        assert report['control_spec']==spec;registered.append(spec)
    def preflight(options):
        for mode in ('runtime','stock'):
            args=module.control_args(fixture,mode,mode,480,30)
            for field in ('game_executable','framework','replay_mod','ucrt','physx'):setattr(args,field,fixture)
            if mode=='runtime':module.capture(args)
            else:
                module.independent_control(args)
                module.independent_control(args)
        return dict(result='ready',checks=[])
    monkeypatch.setattr(module,'run_replay_control',capture)
    monkeypatch.setattr(module,'live_preflight',preflight)
    monkeypatch.setattr(catalog,'find_control',find)
    monkeypatch.setattr(catalog,'register_control',register)
    monkeypatch.setattr(replay_evidence,'retain_run',lambda *args: {})
    monkeypatch.setattr(sys,'argv',['replay_test.py','preflight',*flags])
    assert module.main()==0
    assert captured==['runtime','stock'] and len(registered)==1 and lookups[0]==lookups[1]


@pytest.mark.workflow
@pytest.mark.parametrize('field',['flush_startup_loading','no_async_loading_thread'])
def test_resumed_candidate_rejects_changed_loading_setup_before_observation(tmp_path,monkeypatch,field):
    import replay_test as module
    monkeypatch.setattr(module,'ACTIVE_PROFILE',None)
    monkeypatch.setattr(module,'ACTIVE_STARTUP_LOADING',{field:True},raising=False)
    report=tmp_path/'candidate.json';report.write_text(json.dumps({field:False}))
    args=SimpleNamespace(report=report)
    with pytest.raises(RuntimeError,match='retained candidate startup loading setup differs'):
        module.revalidate_advance_failure_capture(args)
