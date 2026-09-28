import copy
import pytest


def contract(validator='positive_lease'):
    return dict(version=1, hypothesis='lease exists before effects', decision='admit ownership or keep blocked',
        required_event='lease', phase='before_effects', window=[210,217], setup={'serial_particles':True},
        identities=['runtime','observer'], success='positive lease with successful continuation',
        falsification='effects without lease', expected_injections=[], stop_conditions=['unexpected failure'],
        permitted_claims=['scoped diagnostic'], validator=validator, coverage={})


def test_zero_hit_capture_is_valid_but_inconclusive():
    from tools.deterministic_qualification.replay_experiments import evaluate
    r=evaluate(contract(), dict(capture_valid=True, event_hits=0, lease_hits=0, correlation_hits=10))
    assert r['capture']=='pass' and r['hypothesis']=='inconclusive'


def test_correlation_never_satisfies_before_effects_lease():
    from tools.deterministic_qualification.replay_experiments import evaluate
    r=evaluate(contract(), dict(capture_valid=True,event_hits=2,lease_hits=0,correlation_hits=10))
    assert r['hypothesis']=='inconclusive'
    assert evaluate(contract(),dict(capture_valid=True,event_hits=2,effects_without_lease=1))['hypothesis']=='fail'


def test_contract_rejects_executable_expression_and_missing_field():
    from tools.deterministic_qualification.replay_experiments import validate_contract
    with pytest.raises(ValueError):validate_contract(contract('eval(foo)'))
    c=contract();del c['phase']
    with pytest.raises(ValueError):validate_contract(c)


def test_coverage_rejects_wrong_age_revision_duplicates_and_late_omission():
    from tools.deterministic_qualification.replay_coverage import validate
    receipts=dict(attempted_cycles=[1,2,3],committed_cycles=[1,2,3], forward_ticks=[217,218,219],
        regenerated_ticks=[211,212],corrections=[dict(arrival=217,first_consumption=217,revision=1,committed=True),
        dict(arrival=227,first_consumption=224,revision=2,committed=True),
        dict(arrival=237,first_consumption=231,revision=3,committed=True)],continuation_ticks=120)
    required=dict(cycles=3,ages=[1,4,7],arrival_thirds=[217,246],continuation_ticks=120)
    assert validate(required,receipts)['result']=='pass'
    for mutate in (lambda r:r['committed_cycles'].append(3),
                   lambda r:r['corrections'][1].update(revision=4),
                   lambda r:r['corrections'][1].update(first_consumption=225),
                   lambda r:r['corrections'].pop(),
                   lambda r:r.update(continuation_ticks=119)):
        r=copy.deepcopy(receipts);mutate(r)
        assert validate(required,r)['result']=='fail'


def test_unknown_lifecycle_and_b_continuation_do_not_pass():
    from tools.deterministic_qualification.replay_coverage import validate
    assert validate(dict(lifecycle_difference=True),{})['result']=='inconclusive'
    assert validate(dict(b_recovered=True,continuation_ticks=120),dict(b_recovered=True))['result']!='pass'


def test_distributed_generation_uses_first_native_consumption_and_fails_missing_third():
    from tools.deterministic_qualification.replay_schedule import select_schedule
    rows=[dict(tick=t,round=0,sample=t-40,p2=8,native_return=True) for t in (217,227,237)]
    chosen=select_schedule(rows,217,30)
    assert [r['arrival'] for r in chosen]==[217,230,243]
    with pytest.raises(ValueError,match='third 3'):select_schedule(rows[:2],217,30)


def test_interrupted_native_update_cannot_disappear():
    from tools.deterministic_qualification.replay_timing import parse_runtime_timing
    value=parse_runtime_timing('[HorseMod] rolling timing begin ordinal=1 tick=217\n')
    assert value['missing_samples']==1 and value['incomplete']==1 and value['result']=='fail'


def test_unique_rolling_ticks_exclude_startup_and_continuation():
    from tools.deterministic_qualification.replay_coverage import rolling_receipts
    text='''[HorseMod] rolling checkpoint tick=210 first=210 capture_us=1
[ReplayQualification] boundary ordinal=1 phase=actor_tail frame=200
[ReplayQualification] boundary ordinal=2 phase=actor_tail frame=211
[ReplayQualification] boundary ordinal=3 phase=actor_tail frame=211
[HorseMod] rolling timing begin ordinal=1 tick=217
[HorseMod] historical combat execution admitted run_id=test from_tick=217 to_tick=210 B_retained=true
[ReplayQualification] boundary ordinal=4 phase=actor_tail frame=211
[HorseMod] rolling cycle ordinal=1 T=217 A=210 resimulated_ticks=7 committed=true revision=0
[ReplayQualification] boundary ordinal=5 phase=actor_tail frame=218
'''
    coverage=rolling_receipts(text)
    assert coverage['unique_forward_ticks']==1 and coverage['unique_regenerated_ticks']==1
    assert coverage['tick_window']=={'first_exclusive':210,'last_inclusive':217}
