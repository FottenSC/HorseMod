"""Versioned named validators. JSON contains data, never executable expressions."""
from __future__ import annotations
from .replay_coverage import validate

VALIDATORS={'positive_lease','rolling_mechanism','b_recovery','prerequisite_recovery','single_transaction'}
REQUIRED={'version','hypothesis','decision','required_event','phase','window','setup','identities',
          'success','falsification','expected_injections','stop_conditions','permitted_claims','validator','coverage'}


def validate_contract(contract):
    if not isinstance(contract,dict) or not REQUIRED<=contract.keys() or contract.get('version')!=1:
        raise ValueError('experiment contract incomplete or unsupported')
    if contract['validator'] not in VALIDATORS:raise ValueError('unknown named experiment validator')
    if (not isinstance(contract['window'],list) or len(contract['window'])!=2
        or not all(isinstance(n,int) for n in contract['window']) or contract['window'][0]>contract['window'][1]):
        raise ValueError('invalid observation window')
    if not isinstance(contract['coverage'],dict) or not contract['stop_conditions']:
        raise ValueError('coverage or stop conditions missing')
    return contract


def evaluate(contract, observation):
    if contract is None:return dict(capture='unknown',hypothesis='inconclusive',reason='legacy profile has no contract',permitted_claims=[])
    validate_contract(contract)
    coverage=validate(contract['coverage'],observation)
    capture='pass' if observation.get('capture_valid') is True else 'inconclusive'
    validator=contract['validator'];hypothesis='inconclusive'
    if observation.get('unexpected_failure') or coverage['result']=='fail':hypothesis='fail'
    elif capture=='pass':
        if validator=='positive_lease':
            if observation.get('effects_without_lease',0)>0:hypothesis='fail'
            elif (observation.get('lease_hits',0)>0 and observation.get('before_effects') is True
                    and observation.get('required_recovery') is True):hypothesis='pass'
        elif validator in ('b_recovery','prerequisite_recovery'):
            if (observation.get('b_recovered') is True and observation.get('continuation_ticks',0)>=120
                and (validator!='prerequisite_recovery' or observation.get('matched_injection') is True)):
                hypothesis='pass'
        elif observation.get('independent_comparison') is True and coverage['result']=='pass':hypothesis='pass'
    if coverage['result']=='inconclusive' and hypothesis=='pass':hypothesis='inconclusive'
    return dict(capture=capture,hypothesis=hypothesis,coverage=coverage,validator=validator,
        permitted_claims=contract['permitted_claims'] if hypothesis=='pass' else [],
        scope='contract decision only; original qualification requirements remain separate')


def finalize(summary, captures):
    """Producer boundary used before immutable publication, not by formatters."""
    definition=(summary.get('profile') or {}).get('definition')
    if not definition:return
    observation=dict(summary.get('observed_coverage') or {})
    observation.update(capture_valid=bool(captures) and all(c.get('capture_result')=='captured' for c in captures),
        independent_comparison=summary.get('result')=='pass' and bool(summary.get('native_callbacks_compared') or summary.get('simulation_correctness',{}).get('result')=='pass'),
        continuation_ticks=summary.get('continuation_ticks',summary.get('simulation_correctness',{}).get('continuation_ticks',0)))
    recovery=summary.get('seek_request',{})
    if recovery.get('recovered_tick') is not None:
        observation['b_recovered']=recovery.get('result')=='pass' and recovery.get('recovered_tick')==summary.get('advanced_tick')
    observation['unexpected_failure']=summary.get('result') in ('fail','blocked')
    observation['matched_injection']=any(r.get('id')=='recovery.prerequisite' for r in summary.get('expected_interventions',[]))
    summary['experiment_outcome']=evaluate(definition.get('contract'),observation)
    from .replay_outcomes import check
    result=summary['experiment_outcome']
    if summary.get('result')=='blocked':result['hypothesis']='blocked'
    summary.setdefault('checks',[]).extend([
        check('experiment.capture','observer_validity',result['capture'] if result['capture']!='unknown' else 'not_measured','complete requested capture'),
        check('experiment.hypothesis','required_lifecycle',result['hypothesis'],definition.get('scope','profile hypothesis'),sequence=10**18+1)])
    summary['outcome_schema']=1
    if result['hypothesis']=='fail' and summary.get('result')!='blocked':summary['result']='fail'
