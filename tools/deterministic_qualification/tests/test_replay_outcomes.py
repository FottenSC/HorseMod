import copy
import json
from pathlib import Path

from tools.deterministic_qualification import replay_reporting as reporting


def retained_timing_failure():
    return json.loads(Path(__file__).with_name('fixtures').joinpath('timing_failure_55202.json').read_bytes())


def test_retained_timing_failure_does_not_report_expected_rejection_as_cause():
    manifest = retained_timing_failure()
    result = reporting.compact_document(manifest['summary'])
    assert result['first_failure']['check'] == 'performance.resumed_tps'
    assert result['outcomes']['simulation'] == 'pass'
    assert result['outcomes']['performance'] == 'fail'
    assert result['outcomes']['coherence'] == 'inconclusive'
    assert result['outcomes']['cleanup'] == 'pass'
    assert result['expected_interventions'][0]['outcome'] == 'pass'
    assert result['unexercised'] == ['recovery.changed_vfx']


def test_injection_requires_matching_recovery_not_just_failure_text():
    from tools.deterministic_qualification.replay_outcomes import outcomes_for
    report = retained_timing_failure()['summary']
    for field, value in [('recovered_tick', 218), ('independent_continuation_ticks', 0), ('result', 'fail')]:
        broken = copy.deepcopy(report)
        broken['seek_request']['failed_wrapper_recovery'][field] = value
        result = outcomes_for(broken)
        assert not result['expected_interventions']
        assert any(c['id'] == 'recovery.prerequisite' and c['outcome'] == 'fail' for c in result['checks'])


def test_explicit_failures_preserve_secondary_and_unknown_order():
    from tools.deterministic_qualification.replay_outcomes import outcomes_for, check
    report = {'result': 'fail', 'outcome_schema': 1, 'checks': [
        check('cleanup.files', 'cleanup', 'fail', 'deployment'),
        check('simulation.compare', 'simulation', 'fail', '120 ticks'),
        check('recovery.undo', 'recovery', 'fail', 'B')], 'expected_interventions': []}
    value = outcomes_for(report)
    assert value['failure_order'] == 'unknown'
    assert {c['id'] for c in value['failures']} == {'cleanup.files', 'simulation.compare', 'recovery.undo'}
    assert value['primary_failure'] is None


def test_unknown_nested_failure_never_becomes_measured_result():
    from tools.deterministic_qualification.replay_outcomes import outcomes_for
    result = outcomes_for({'result': 'pass', 'arbitrary': {'failure': 'not a failed check'}})
    assert not result['checks'] and result['primary_failure'] is None
    assert result['dimensions']['simulation'] == 'not_measured'


def test_retaining_non_capture_report_keeps_cleanup_unknown(tmp_path):
    from tools.deterministic_qualification.replay_evidence import retain_run
    path=tmp_path/'native.json';path.write_text('{"result":"fail","exit_hex":"0xc00000fd"}')
    receipt=retain_run(tmp_path/'evidence',dict(result='fail'),[path])
    document=json.loads(Path(receipt['path']).read_bytes())
    assert document['outcomes']['dimensions']['cleanup']=='not_measured'


def test_failed_measurement_changes_aggregate_before_publication(tmp_path):
    from tools.deterministic_qualification.replay_evidence import retain_run
    from tools.deterministic_qualification.replay_outcomes import check
    report=dict(result='pass',outcome_schema=1,checks=[
        check('performance.full_update','performance','fail','observed overrun')])
    receipt=retain_run(tmp_path/'evidence',report)
    assert report['result']=='fail'
    assert json.loads(Path(receipt['path']).read_bytes())['summary']['result']=='fail'


def test_profile_preflight_preserves_named_checks_and_blocked_result(tmp_path):
    from tools.deterministic_qualification.replay_evidence import retain_run
    profile=json.loads(Path(__file__).parents[1].joinpath('replay_profiles/short-boundary617-624.json').read_bytes())
    report=dict(stage='preflight',result='blocked',checks=[
        dict(check='profile prerequisite G1',result='blocked',detail='compatible proof required')],
        profile={'definition':profile})
    receipt=retain_run(tmp_path/'evidence',report)
    assert report['result']=='blocked'
    assert report['checks'][0]['check']=='profile prerequisite G1'
    assert report['experiment_outcome']['hypothesis']=='blocked'
    assert json.loads(Path(receipt['path']).read_bytes())['outcomes']['dimensions']['observer_validity']=='blocked'
