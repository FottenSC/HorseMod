import json
from pathlib import Path
import pytest
from tools.deterministic_qualification import replay_reporting as reporting


@pytest.mark.workflow
def test_local_explicit_test_selection_stays_narrow(tmp_path, monkeypatch, capsys):
    import replay_test
    target = 'tools/deterministic_qualification/tests/test_replay_reporting.py::test_status_delta_ignores_elapsed_but_detects_run_replacement'
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    monkeypatch.setattr(replay_test.sys, 'argv', ['replay_test', 'local', '--test', target, '--list-tests', '--output', 'full'])
    assert replay_test.main() == 0
    output = json.loads(capsys.readouterr().out)
    selected = output['summary']
    assert selected['tests'] == [target] and selected['groups'] == ['explicit']
    assert selected['certifying'] is False and not selected.get('gate_evidence')


def test_tooling_source_selection_covers_dependents_without_full_suite():
    from tools.deterministic_qualification.replay_local import select_tests
    for source, expected in [('replay_outcomes.py', 'reporting'), ('fixture_cache.py', 'fixture-cache'),
                             ('replay_experiments.py', 'experiments')]:
        tests, groups = select_tests('auto', ['tools/deterministic_qualification/'+source])
        assert expected in groups and 'all' not in groups and tests
    test_file = 'tools/deterministic_qualification/tests/test_replay_reporting.py'
    assert select_tests('auto', [test_file])[0] == [test_file]
    assert select_tests('auto', [test_file, 'HorseMod/unknown.cpp'])[1] == ['all']
    # test_source_objects imports a repository helper from this test module.
    dependents, _ = select_tests('auto', ['tools/deterministic_qualification/tests/test_source_retention.py'])
    assert 'tools/deterministic_qualification/tests/test_source_objects.py' in dependents


@pytest.mark.parametrize('target', ['tools/replay_test.py', '../test_escape.py',
                                  'tools/deterministic_qualification/tests/test_missing.py'])
def test_explicit_selection_rejects_unrelated_or_missing_files(target):
    from tools.deterministic_qualification.replay_local import explicit_tests
    root = Path(__file__).resolve().parents[3]
    with pytest.raises(ValueError, match='qualification test'):
        explicit_tests(root, [target])


def test_nested_reports_are_compact_and_json_is_one_document():
    report = dict(stage='preflight', result='ready', profile={'definition': {'name': 'changed'}, 'bytes_utf8': 'x'*40000}, checks=[{'details': 'x'*2000}]*80)
    for mode in ('compact', 'json'):
        text = reporting.render(report, mode=mode, report_path='report.json')
        assert len(text.encode()) <= 2048
        assert len(text.encode()) < len(json.dumps(report).encode()) / 10
        assert 'report.json' in text
        if mode == 'json': assert json.loads(text)['schema_version'] == 2
    assert 'bytes_utf8' in reporting.render(report, mode='full')


def test_failure_is_bounded_without_losing_cleanup_or_links():
    report = dict(stage='combat-restore', result='fail', failure='failure'*3000, cleanup={'complete': False, 'games_remaining': 1})
    text = reporting.render(report, mode='json', report_path='report.json', console_path='immutable.log')
    value = json.loads(text)
    assert len(text.encode()) <= 4096 and value['truncated']
    assert value['cleanup']['complete'] is False and value['console'] == 'immutable.log'


def test_first_failure_is_ordered_and_recovery_is_separate():
    raw = '[HorseMod] checkpoint component failed component=ground_configuration code=5 tick=617\n[HorseMod] rolling failed check=capture_read code=5 cycles=400\n[HorseMod] historical undo participant=ground code=18\n'
    result = reporting.log_details(raw)
    assert result['first_failure']['tick'] == 617
    assert result['first_failure']['participant'] == 'ground_configuration'
    assert 'undo' in result['recovery_failure'] and result['first_failure']['code'] == 5


def test_ground_update_rejection_precedes_successful_ctest_summary():
    raw = ('100% tests passed, 0 tests failed out of 2\n'
           '[HorseMod] ground update admission rejected check=update_physics_callbacks owner=0 tick=0 roots=0 meshes=0 before_native_application=true owners_retained=true\n')
    result = reporting.log_details(raw)
    assert result['first_failure']['check'] == 'update_physics_callbacks'
    assert result['first_failure']['participant'] == 'ground_update'
    assert result['first_failure']['category'] == 'execution'
    assert 'first_failure' not in reporting.log_details('100% tests passed, 0 tests failed out of 2\n')


def test_status_rejects_stale_and_partial_reports_without_writes(tmp_path):
    p = tmp_path/'report.json'; p.write_text(json.dumps({'run_id': 'old', 'result': 'pass'}))
    before = p.read_bytes()
    result = reporting.read_status(p, deployment={'run_id': 'new', 'state': 'active'})
    assert result['result'] == 'unknown' and result['run_id'] == 'new'
    assert p.read_bytes() == before
    p.write_text('{')
    assert reporting.read_status(p)['result'] == 'unknown'


def test_readonly_rejections_and_duplicate_failures_are_not_root_causes():
    raw = 'GPU pending work rejected phase=2 read_only=true guard_unchanged=true\ncheckpoint component failed component=ground code=5 tick=617\ncheckpoint component failed component=ground code=5 tick=617\nrolling failed check=capture_read code=5 cycles=400\n'
    value = reporting.log_details(raw)
    assert value['first_failure']['participant'] == 'ground'
    assert 'GPU' not in value['first_failure']['excerpt']


def test_log_reads_are_bounded_and_identity_checked(tmp_path):
    p = tmp_path/'native.log'
    p.write_text('run_id=replay-old\n'+'x'*400000+'\nrun_id=replay-current\nseek ownership state=APublished current_tick=617\n')
    assert reporting.bounded_log(p, 'replay-other') == {'log_identity': 'unverified'}
    result = reporting.bounded_log(p, 'replay-current')
    assert result['tick'] == 617 and result['phase'] == 'APublished'
    assert len(json.dumps(result)) < 1000


@pytest.mark.workflow
@pytest.mark.parametrize('mode', ['compact', 'json', 'full'])
def test_cli_modes_preserve_preflight_and_retained_report(mode, tmp_path, monkeypatch, capsys):
    import replay_test
    from deterministic_qualification import replay_evidence
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    summary = {'result': 'ready', 'checks': [{'name': 'identity', 'details': 'x'*30000}]}
    monkeypatch.setattr(replay_test, 'live_preflight', lambda _: dict(summary))
    retained = []
    def retain(root, document, reports):
        retained.append(document.copy())
        path = tmp_path/'manifest.json'; path.write_text(json.dumps({'summary': document}))
        return {'path': str(path)}
    monkeypatch.setattr(replay_evidence, 'retain_run', retain)
    monkeypatch.setattr(replay_test.sys, 'argv', ['replay_test.py', 'preflight', '--output', mode])
    assert replay_test.main() == 0
    out = capsys.readouterr(); assert not out.err
    assert retained == [dict(summary, stage='preflight', profile=None)]
    if mode == 'full': assert len(out.out) > 30000
    else:
        assert len(out.out.encode()) < 2048
        if mode == 'json': assert json.loads(out.out)['result'] == 'ready'
        logs = list((tmp_path/'evidence/objects').glob('*.log'))
        assert len(logs) == 1 and 'x'*30000 in logs[0].read_text()


@pytest.mark.workflow
def test_profile_output_is_operational(monkeypatch, tmp_path, capsys):
    import replay_test
    from deterministic_qualification import replay_evidence
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    monkeypatch.setattr(replay_test, 'live_preflight', lambda _: {'result': 'ready'})
    settings = []
    def retain(root, summary, reports):
        settings.append(summary['profile'])
        return {'path': str(tmp_path/'absent.json')}
    monkeypatch.setattr(replay_evidence, 'retain_run', retain)
    for mode in ('compact', 'json', 'full'):
        monkeypatch.setattr(replay_test.sys, 'argv', ['replay_test.py', 'preflight', '--profile', 'changed-rolling30-proposed', '--output', mode])
        assert replay_test.main() == 0
    capsys.readouterr()
    assert settings[0] == settings[1] == settings[2]
    assert 'output' not in settings[0]['resolved_settings']


@pytest.mark.workflow
@pytest.mark.parametrize('prefix', [[], ['--output', 'json']])
def test_status_cli_never_writes_or_acquires_resources(prefix, tmp_path, monkeypatch, capsys):
    import replay_test
    from deterministic_qualification import replay_run
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    monkeypatch.setattr(replay_test, 'GAME_ROOT', tmp_path)
    def forbidden(*a, **k): raise AssertionError('status must not execute')
    monkeypatch.setattr(replay_test, 'live_preflight', forbidden)
    monkeypatch.setattr(replay_run, 'RunResources', forbidden)
    monkeypatch.setattr(replay_run, 'acquire_deployment_lock', forbidden)
    path = tmp_path/'sample-stage.json'; path.write_text(json.dumps({'result': 'fail', 'run_id': 'replay-current', 'cleanup': {'complete': True, 'games_remaining': 0}}))
    before = {p: p.read_bytes() for p in tmp_path.rglob('*') if p.is_file()}
    monkeypatch.setattr(replay_test.sys, 'argv', ['replay_test.py', *prefix, 'status', '--report', str(path)])
    assert replay_test.main() == 0
    assert 'status' in capsys.readouterr().out
    assert before == {p: p.read_bytes() for p in tmp_path.rglob('*') if p.is_file()}


def test_unicode_and_large_failures_keep_size_limits():
    report = {'stage': 'local', 'result': 'fail', 'first_failure': {'observation': [{'expected': '\u754c'*2000, 'actual': 'a'*5000}]*50}, 'cleanup': {'complete': False}, 'recovery_failure': 'undo failed'}
    for mode in ('compact', 'json'):
        text = reporting.render(report, mode=mode, report_path='report.json', console_path='full.log')
        assert len(text.encode()) <= 4096 and 'truncated' in text and 'full.log' in text


def test_local_success_console_reduction(tmp_path, monkeypatch, capsys):
    monkeypatch.setitem(globals(), 'OUTPUT', tmp_path)
    raw = 'Native retained kinematic node slot=0 before=5 after=5\n'*100
    @reporting.reported_cli(output_root=lambda: OUTPUT)
    def command():
        print(raw)
        print('550 passed in 232.27s')
        reporting.publish({'stage': 'local', 'result': 'pass'})
        return 0
    monkeypatch.setattr(reporting.sys, 'argv', ['replay_test.py', 'local'])
    assert command() == 0
    output = capsys.readouterr().out
    assert len(output.encode()) < len(raw.encode())/10 and '550' in output
    assert raw in next((tmp_path/'evidence/objects').glob('*.log')).read_text()


def test_new_run_does_not_inherit_previous_tail_failure(tmp_path):
    path=tmp_path/'native.log'
    path.write_text('run_id=replay-old\ncheckpoint failed component=old code=7 tick=1\nrun_id=replay-new\nseek ownership state=APublished current_tick=210\n')
    result=reporting.bounded_log(path,'replay-new')
    assert 'first_failure' not in result and result['tick']==210


def test_preflight_blocker_is_visible():
    report={'stage':'preflight','result':'blocked','checks':[{'check':'steam','result':'ready'},{'check':'journal','result':'blocked','detail':'cleanup pending'}]}
    output=json.loads(reporting.render(report,mode='json'))
    assert output['first_failure']['check']=='journal'
    assert output['first_failure']['detail']=='cleanup pending'


def test_compact_success_console_links_to_full_evidence(tmp_path,monkeypatch,capsys):
    monkeypatch.setitem(globals(),'OUTPUT',tmp_path)
    manifest=tmp_path/'immutable-manifest.json'
    manifest.write_text(json.dumps({'results':{'simulation':'not measured'}}))
    @reporting.reported_cli(output_root=lambda: OUTPUT)
    def command():
        print('Native fixture passed')
        reporting.publish({'stage':'build','result':'pass'},{'path':str(manifest)})
        return 0
    monkeypatch.setattr(reporting.sys,'argv',['replay_test.py','build','--output','json'])
    assert command()==0
    result=json.loads(capsys.readouterr().out)
    assert str(manifest) in Path(result['console']).read_text()
    assert 'evidence' not in result


@pytest.mark.workflow
def test_compact_boundary_fixes_existing_unbounded_cli_body(tmp_path,monkeypatch,capsys):
    import replay_test
    from deterministic_qualification import replay_evidence
    monkeypatch.setattr(replay_test,'OUTPUT',tmp_path)
    monkeypatch.setattr(replay_test,'live_preflight',lambda _: {'result':'ready','checks':[{'detail':'x'*10000}]})
    monkeypatch.setattr(replay_evidence,'retain_run',lambda *a: {'path':str(tmp_path/'manifest.json')})
    monkeypatch.setattr(replay_test.sys,'argv',['replay_test.py','preflight'])
    # The original CLI body still emits its full retained representation.
    assert replay_test.main.__wrapped__()==0
    original=capsys.readouterr().out
    assert len(original.encode())>2048
    assert replay_test.main()==0
    assert len(capsys.readouterr().out.encode())<=2048


@pytest.mark.workflow
def test_failure_exit_code_and_test_selection_are_format_independent(tmp_path,monkeypatch,capsys):
    import replay_test
    from deterministic_qualification import replay_evidence
    monkeypatch.setattr(replay_test,'OUTPUT',tmp_path)
    monkeypatch.setattr(replay_test,'live_preflight',lambda _: {'result':'blocked','checks':[{'check':'journal','result':'blocked','detail':'pending cleanup'}]})
    receipts=[]
    def retain(root,summary,reports):
        receipts.append(summary.copy())
        return {'path':str(tmp_path/'manifest.json')}
    monkeypatch.setattr(replay_evidence,'retain_run',retain)
    for stage,extra,code in [('preflight',[],1),('local',['--list-tests','--group','runner'],0)]:
        receipts.clear()
        for mode in ('compact','json','full'):
            monkeypatch.setattr(replay_test.sys,'argv',['replay_test.py',stage,*extra,'--output',mode])
            assert replay_test.main()==code
            capsys.readouterr()
        assert receipts[0]==receipts[1]==receipts[2]


def test_native_predicates_keep_traversal_order_and_secondary_failures(tmp_path):
    from tools.deterministic_qualification.replay_evidence import diagnostic_index
    p = tmp_path/'native.log'
    p.write_text('run_id=replay-old\ncheckpoint failed component=old code=7 tick=2\n'
                 'run_id=replay-new\nhistorical publication failed code=7 tick=617 revision=2 generation=3\n'
                 'checkpoint failed component=ground code=5 tick=210 revision=3\n'
                 'late recovery failed participant=ground code=18 tick=624\n')
    index = diagnostic_index(p, 'replay-new')
    assert index['events'][0]['tick'] == 617
    assert index['events'][0]['revision'] == 2
    assert index['events'][-1]['category'] == 'recovery'
    assert all(e['run_id'] == 'replay-new' for e in index['events'])


def test_status_delta_ignores_elapsed_but_detects_run_replacement():
    before = {'schema_version': 1, 'stage': 'status', 'result': 'pass', 'run_id': 'a', 'tick': 217}
    after = dict(before, elapsed_seconds=500)
    assert reporting.status_difference(after, before)['change'] == 'no change'
    assert reporting.status_difference(dict(after, run_id='b'), before)['change'] == 'new run'


def test_legacy_inspection_is_readonly_bounded_and_continuable(tmp_path):
    from tools.deterministic_qualification.artifacts import sha256_file
    log = tmp_path/'raw.log'
    log.write_bytes(b'run_id=replay-new\n' + b'run_id=replay-new ordinary observation\n'*35000 +
                    b'checkpoint failed component=ground code=7 tick=617\n')
    report = tmp_path/'report.json'
    report.write_text(json.dumps({'run_id':'replay-new','raw_log':{'path':str(log),'sha256':sha256_file(log)}}))
    value = reporting.inspect_report(report, participant='ground', tick=617)
    assert value['scanned_bytes'] <= 1024*1024
    assert value['search'] == 'no match in scanned range'
    value = reporting.inspect_report(report, participant='ground', tick=617, offset=value['next_offset'])
    assert value['matches'][0]['tick'] == 617


@pytest.mark.workflow
def test_runner_defaults_to_compact_and_rejects_conflicting_modes(tmp_path, monkeypatch, capsys):
    from tools.deterministic_qualification import runner
    def handler(args):
        from tools.deterministic_qualification.report import write_report
        result = {'result':'pass', 'scope':'comparison only', 'limitations':['not qualification'], 'rows':['x'*50000]}
        write_report(args.report, result)
        print(json.dumps(result))
        return 0
    monkeypatch.setattr(runner, 'run_trajectory_comparison', handler)
    monkeypatch.setattr(reporting.sys, 'argv', ['runner', 'replay-compare-trajectory', '--reference', 'a', '--candidate', 'b', '--report', str(tmp_path/'r.json')])
    assert runner.main() == 0
    assert len(capsys.readouterr().out.encode()) <= 2048


@pytest.mark.parametrize('message,category', [
    ('checkpoint component failed component=ground code=5 tick=617','capture'),
    ('historical scheduler preparation failed code=7 check=missing_owner','preparation'),
    ('historical publication failed code=7 recovering=true','publication'),
    ('rolling failed check=transaction_failed code=7 cycles=407','execution'),
    ('run failed run_id=replay-new reason=executor_completion_failed','completion'),
    ('run failed run_id=replay-new reason=particle_gpu_retirement_queue_failed','retirement'),
    ('late recovery failed participant=ground code=18','recovery'),
    ('cleanup failed participant=deployment code=1','cleanup'),
])
def test_observed_native_producer_categories(message, category):
    from tools.deterministic_qualification.replay_diagnostics import diagnostic
    assert diagnostic(message)['category'] == category


def retained_fixture(tmp_path, message):
    from tools.deterministic_qualification.replay_evidence import retain_run
    from tools.deterministic_qualification.artifacts import sha256_file
    log=tmp_path/'raw.log'; log.write_text('run_id=replay-new\n'+message+'\nrun failed run_id=replay-new reason=native_failure\n')
    report=tmp_path/'capture.json'
    report.write_text(json.dumps({'result':'fail','run_id':'replay-new','raw_log':{'path':str(log),'sha256':sha256_file(log)},'cleanup':{'complete':False,'games_remaining':1}}))
    receipt=retain_run(tmp_path/'evidence',{'result':'fail','failure':'native rejected'},[report])
    return receipt, json.loads(Path(receipt['path']).read_bytes())


def test_index_addition_does_not_promote_simulation_gate(tmp_path):
    receipt, manifest=retained_fixture(tmp_path,'historical publication failed code=7 tick=617 revision=2 generation=3')
    assert manifest['results']['simulation']=='not measured'
    value=json.loads(reporting.render(manifest['summary'],mode='json',receipt=receipt))
    assert value['first_failure']['tick']==617
    assert value['first_failure']['generation']==3
    assert value['cleanup']['complete'] is False
    assert value['gates']['simulation']=='not measured'
    index=reporting.indexed_diagnostics(manifest['captures'][0])
    assert index['events'][0]['byte_offset']==Path(manifest['captures'][0]['raw_log']['path']).read_bytes().index(b'\n')+1
    assert index['events'][0]['line_number']==2


def test_capacity_classification_is_preserved(tmp_path):
    _, manifest=retained_fixture(tmp_path,'checkpoint capacity participant=ground tick=617 owned=10 envelope=20 available=5')
    assert manifest['results']['simulation']=='fail'
    assert manifest['first_failure']['check']=='native.checkpoint_capacity'


@pytest.mark.parametrize('damage',['log','index','identity','missing'])
def test_index_identity_and_missing_evidence_fail_closed(damage,tmp_path):
    receipt, manifest=retained_fixture(tmp_path,'historical publication failed code=7 tick=617')
    capture=manifest['captures'][0]
    if damage=='log': Path(capture['raw_log']['path']).write_text('changed')
    elif damage=='index': Path(capture['diagnostic_index']['path']).write_text('{}')
    elif damage=='identity': capture['run_id']='replay-other'
    else: Path(capture['raw_log']['path']).unlink()
    assert 'error' in reporting.indexed_diagnostics(capture)
    if damage!='identity':
        value=reporting.inspect_report(receipt['path'])
        assert value['result']=='unknown' and not value['matches']


def test_index_filters_combine_and_preserve_repeated_tick_revisions(tmp_path):
    receipt,_=retained_fixture(tmp_path,'checkpoint failed component=ground code=7 tick=617 revision=2\ncheckpoint failed component=ground code=7 tick=617 revision=3\nlate recovery failed participant=ground code=18 tick=624')
    first=reporting.inspect_report(receipt['path'],participant='ground',tick=617)
    assert first['matches'][0]['revision']==2
    second=reporting.inspect_report(receipt['path'],participant='ground',tick=617,offset=first['next_offset'])
    assert second['matches'][0]['revision']==3
    assert not reporting.inspect_report(receipt['path'],participant='other',tick=617)['matches']


@pytest.mark.workflow
def test_inspect_cli_is_readonly_in_every_mode(tmp_path,monkeypatch,capsys):
    import replay_test
    from deterministic_qualification import replay_run
    receipt,_=retained_fixture(tmp_path,'checkpoint failed component=ground code=7 tick=617')
    def forbidden(*a,**k): raise AssertionError('read-only command mutated resources')
    monkeypatch.setattr(replay_run,'inspect_deployment',forbidden)
    monkeypatch.setattr(replay_run,'RunResources',forbidden)
    monkeypatch.setattr(replay_run,'acquire_deployment_lock',forbidden)
    before={p:p.read_bytes() for p in tmp_path.rglob('*') if p.is_file()}
    for mode in ('compact','json','full'):
        monkeypatch.setattr(replay_test.sys,'argv',['replay_test','--output',mode,'inspect','--report',receipt['path'],'--participant','ground','--tick','617'])
        assert replay_test.main()==0
        out=capsys.readouterr();assert not out.err
        if mode=='json': assert json.loads(out.out)['matches'][0]['tick']==617
    assert before=={p:p.read_bytes() for p in tmp_path.rglob('*') if p.is_file()}


@pytest.mark.workflow
def test_status_since_retains_gates_and_detects_cleanup(tmp_path,monkeypatch,capsys):
    import replay_test
    path=tmp_path/'status.json'; baseline=tmp_path/'previous.json'
    path.write_text(json.dumps({'result':'fail','run_id':'replay-new','phase':'failed','cleanup':{'complete':False}}))
    baseline.write_text(reporting.render(reporting.read_status(path),mode='json'))
    for complete,expected in [(False,'no change'),(True,'changed')]:
        data=json.loads(path.read_bytes());data['cleanup']['complete']=complete;path.write_text(json.dumps(data))
        monkeypatch.setattr(replay_test.sys,'argv',['replay_test','status','--report',str(path),'--since',str(baseline),'--output','json'])
        assert replay_test.main()==0
        value=json.loads(capsys.readouterr().out)
        assert value['change']==expected
        if complete: assert 'cleanup' in value['changed_fields']


@pytest.mark.workflow
@pytest.mark.parametrize('mode',['compact','json','full'])
@pytest.mark.parametrize('code',[0,1,2])
def test_standalone_modes_preserve_reports_and_exit_codes(mode,code,tmp_path,monkeypatch,capsys):
    from tools.deterministic_qualification import runner
    document={'result':'pass' if code==0 else 'fail','scope':'comparison only','limitations':['not qualification'], 'first_difference':None if code==0 else {'tick':617,'field':'pose'},'rows':['x'*40000]}
    def handler(args):
        from tools.deterministic_qualification.report import write_report
        write_report(args.report,document)
        print(json.dumps(document))
        return code
    monkeypatch.setattr(runner,'run_trajectory_comparison',handler)
    path=tmp_path/'report.json'
    monkeypatch.setattr(reporting.sys,'argv',['runner','--output',mode,'replay-compare-trajectory','--reference','a','--candidate','b','--report',str(path)])
    assert runner.main()==code
    out=capsys.readouterr();assert not out.err
    assert json.loads(path.read_bytes())==document
    if mode=='full': assert 'x'*40000 in out.out
    else:
        assert len(out.out.encode()) <= (2048 if code==0 else 4096)
        assert 'comparison only' in out.out and 'not qualification' in out.out
        if mode=='json': assert json.loads(out.out)['stage']=='replay-compare-trajectory'
        console=next((tmp_path/'evidence/objects').glob('*.log')).read_text()
        assert 'x'*40000 in console and 'Retained report:' in console
        if code: assert '617' in out.out


@pytest.mark.workflow
def test_output_flag_placement_conflicts_and_nested_formatter(tmp_path,monkeypatch,capsys):
    from tools.deterministic_qualification import runner
    suffix=['replay-compare-trajectory','--reference','a','--candidate','b','--report',str(tmp_path/'r.json')]
    assert runner.build_parser().parse_args(['--output','json',*suffix]).output=='json'
    assert runner.build_parser().parse_args([*suffix,'--output','json']).output=='json'
    monkeypatch.setattr(reporting.sys,'argv',['runner','--output','json',*suffix,'--output','full'])
    with pytest.raises(SystemExit) as error: runner.main()
    assert error.value.code==2
    assert 'conflicting' in capsys.readouterr().err
    @reporting.reported_cli(output_root=tmp_path)
    def inner():
        reporting.publish({'stage':'nested','result':'pass'})
        print('full diagnostic '*1000)
        return 0
    @reporting.reported_cli(output_root=tmp_path)
    def outer(): return inner()
    monkeypatch.setattr(reporting.sys,'argv',['runner','--output','json'])
    assert outer()==0
    assert json.loads(capsys.readouterr().out)['stage']=='nested'


def test_legacy_unattributed_window_is_unknown_and_stale_run_is_excluded(tmp_path):
    log=tmp_path/'log';log.write_bytes(b'run_id=replay-old\ncheckpoint failed component=old code=7 tick=1\nrun_id=replay-new\n'+b'x\n'*600000+b'checkpoint failed component=ground code=7 tick=617\n')
    report=tmp_path/'r.json';report.write_text(json.dumps({'run_id':'replay-new','raw_log':{'path':str(log)}}))
    value=reporting.inspect_report(report)
    assert not value['matches'] and value['next_offset']
    value=reporting.inspect_report(report,offset=value['next_offset'])
    assert value['result']=='unknown' and not value['matches']
    assert value['scanned_bytes']<=1024*1024


@pytest.mark.workflow
def test_new_inspection_defaults_do_not_enter_profile_compatibility(monkeypatch,tmp_path,capsys):
    import replay_test
    from deterministic_qualification import replay_evidence
    monkeypatch.setattr(replay_test,'OUTPUT',tmp_path)
    monkeypatch.setattr(replay_test,'live_preflight',lambda _: {'result':'ready'})
    receipts=[]
    monkeypatch.setattr(replay_evidence,'retain_run',lambda root,summary,reports: receipts.append(summary) or {})
    monkeypatch.setattr(replay_test.sys,'argv',['replay_test','preflight','--profile','changed-rolling30-proposed'])
    assert replay_test.main()==0
    capsys.readouterr()
    assert not {'output','report','participant','tick','offset','since'} & receipts[0]['profile']['resolved_settings'].keys()



def test_status_differences_can_be_chained_without_false_changes():
    baseline={'schema_version':1,'stage':'status','result':'pass','run_id':'a','phase':'running','tick':217,'cleanup':'unknown'}
    first=reporting.status_difference(baseline,baseline)
    encoded=json.loads(reporting.render(first,mode='json'))
    assert reporting.status_difference(baseline,encoded)['change']=='no change'
    assert reporting.status_difference(dict(baseline,tick=218),encoded)['changed_fields']==['tick']


@pytest.mark.workflow
@pytest.mark.parametrize('version', [1, 2])
def test_default_readiness_since_renders_without_writes(version, tmp_path, monkeypatch, capsys):
    import replay_test
    from deterministic_qualification import replay_readiness, replay_run
    summary = dict(stage='status', result='blocked', latest={
        'build': dict(result='pass', path='build.json', compatibility='current'),
        'live': dict(result='fail', path='live.json', compatibility='historical')},
        readiness={'G1': dict(result='open', requirements=['policy.consumer_boundary'])},
        blockers=['G1'], next_experiment=dict(result='undefined'))
    monkeypatch.setattr(replay_readiness, 'current_context', lambda *a, **k: {})
    monkeypatch.setattr(replay_readiness, 'readiness', lambda *a, **k: summary)
    monkeypatch.setattr(replay_run, 'inspect_deployment', lambda *a, **k: {'state': 'clean'})
    monkeypatch.setattr(replay_test, 'OUTPUT', tmp_path)
    baseline = tmp_path/'previous.json'
    previous = json.loads(reporting.render(summary, mode='json'))
    baseline.write_text(json.dumps(dict(previous, schema_version=version)))
    before = {p: p.read_bytes() for p in tmp_path.rglob('*') if p.is_file()}
    monkeypatch.setattr(replay_test.sys, 'argv', ['replay_test', 'status', '--since', str(baseline), '--output', 'json'])
    assert replay_test.main() == 0
    value = json.loads(capsys.readouterr().out)
    assert value['change'] == 'no change' and value['readiness'] == {'G1': 'open'}
    assert value['latest']['live']['result'] == 'fail'
    assert before == {p: p.read_bytes() for p in tmp_path.rglob('*') if p.is_file()}


@pytest.mark.workflow
def test_standalone_handled_exception_keeps_exit_two_and_original_full_stderr(tmp_path,monkeypatch,capsys):
    from tools.deterministic_qualification import runner
    def handler(args): raise RuntimeError('earliest comparison predicate')
    monkeypatch.setattr(runner,'run_trajectory_comparison',handler)
    monkeypatch.setattr(reporting.tempfile,'gettempdir',lambda: str(tmp_path))
    for mode in ('compact','json','full'):
        monkeypatch.setattr(reporting.sys,'argv',['runner','replay-compare-trajectory','--reference','a','--candidate','b','--report',str(tmp_path/'r.json'),'--output',mode])
        assert runner.main()==2
        out=capsys.readouterr()
        assert 'earliest comparison predicate' in out.out+out.err
        if mode=='full': assert not out.out and 'qualification failed' in out.err
        elif mode=='json': assert json.loads(out.out)['exit_code']==2


def test_index_inspection_console_bound_keeps_coordinates_and_context(tmp_path):
    receipt,_=retained_fixture(tmp_path,'checkpoint failed component=ground code=7 tick=617 revision=2 generation=3 '+ 'detail='+'x'*10000)
    report=reporting.inspect_report(receipt['path'])
    for mode in ('compact','json'):
        text=reporting.render(report,mode=mode,report_path=receipt['path'])
        assert len(text.encode())<=2048
        assert '617' in text and 'byte_offset' in text and 'context' in text


def test_incomplete_inspection_and_partial_oversized_line_are_unknown(tmp_path):
    report=tmp_path/'report.json'; report.write_text('{')
    assert reporting.inspect_report(report)['result']=='unknown'
    log=tmp_path/'raw.log';log.write_bytes(b'run_id=replay-new\n'+b'x'*2000000)
    report.write_text(json.dumps({'run_id':'replay-new','raw_log':{'path':str(log)}}))
    first=reporting.inspect_report(report)
    second=reporting.inspect_report(report,offset=first['next_offset'])
    assert second['result']=='unknown'
    assert second['next_offset']>first['next_offset']
    assert second['scanned_bytes']<=1024*1024



def test_native_index_coalesces_only_adjacent_duplicates(tmp_path):
    from tools.deterministic_qualification.replay_evidence import diagnostic_index
    line='checkpoint failed component=ground code=7 tick=617 revision=2\n'
    log=tmp_path/'raw.log'; log.write_text('run_id=replay-new\n'+line+line+'ordinary traversal boundary\n'+line)
    events=diagnostic_index(log,'replay-new')['events']
    assert len(events)==2 and events[0]['duplicate_count']==2
    assert 'duplicate_count' not in events[1]



def test_oversized_cleanup_still_obeys_failure_limit():
    report={'stage':'local','result':'fail','cleanup':dict(complete=False,games_remaining=1,**{str(i):'x'*2000 for i in range(100)})}
    for mode in ('compact','json'):
        text=reporting.render(report,mode=mode,console_path='retained.log')
        assert len(text.encode())<=4096 and 'retained.log' in text and 'truncated' in text
        if mode=='json': assert json.loads(text)['cleanup']['complete'] is False


def test_legacy_inspection_exposes_retained_failure_context_without_rescanning(tmp_path):
    p=tmp_path/'old.json';p.write_text(json.dumps({'summary':{'result':'fail'},'first_failure':{'check':'first_mismatch','observation':{'tick':617}},'captures':[{'cleanup':{'complete':True,'games_remaining':0}}],'contexts':[{'context':{'lines':['tick=617 missing ground dependency']}}]}))
    result=reporting.inspect_report(p)
    assert result['first_failure']['observation']['tick']==617
    assert result['retained_context']==['tick=617 missing ground dependency']
    assert result['cleanup']['complete'] is True
    assert result['scanned_bytes']==0



def test_invalid_capture_metadata_and_string_failure_are_readable(tmp_path):
    p=tmp_path/'r.json';p.write_text(json.dumps({'captures':'incomplete'}))
    assert reporting.inspect_report(p)['result']=='unknown'
    output=json.loads(reporting.render({'stage':'status','result':'fail','first_failure':'original predicate'},mode='json'))
    assert output['first_failure']=='original predicate'
def test_historical_full_status_uses_same_outcomes_without_mutating_source(tmp_path):
    import json
    from pathlib import Path
    from tools.deterministic_qualification.replay_reporting import read_status, render
    manifest=json.loads(Path(__file__).with_name('fixtures').joinpath('timing_failure_55202.json').read_bytes())
    cleanup=manifest['summary'].pop('cleanup')
    manifest['captures']=[{'cleanup':cleanup}]
    path=tmp_path/'manifest.json';path.write_text(json.dumps(manifest))
    before=path.read_bytes()
    report=read_status(path)
    full=json.loads(render(report,mode='full'))
    checks={c['id']:c for c in full['checks']}
    assert full['first_failure']['check']=='performance.resumed_tps'
    assert checks['cleanup.deployment']['outcome']=='pass'
    assert checks['recovery.changed_vfx']['outcome']=='not_exercised'
    assert full['outcome_interpretation']['source']==str(path)
    assert path.read_bytes()==before


def test_unpublished_exception_retains_trace_and_timing(tmp_path,monkeypatch,capsys):
    import sys
    import pytest
    from tools.deterministic_qualification.replay_reporting import reported_cli
    monkeypatch.setattr(sys,'argv',['replay_test.py','local','--output','json'])
    @reported_cli(output_root=tmp_path)
    def failing():raise ValueError('specific boundary failure')
    with pytest.raises(ValueError,match='specific boundary'):failing()
    manifests=list((tmp_path/'evidence/manifests').glob('*.json'))
    assert manifests
    latest=json.loads(max(manifests,key=lambda p:p.stat().st_mtime_ns).read_bytes())
    assert latest['summary']['exception_type']=='ValueError'
    assert latest['summary']['orchestration_timing']['wall_ns']>0
    assert any('specific boundary failure' in p.read_text() for p in (tmp_path/'evidence/objects').glob('*.log'))
