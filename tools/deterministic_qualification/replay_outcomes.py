"""Scoped check outcomes shared by producers, evidence and presentation.

Legacy adapters recognize named report contracts only. They never search
arbitrary nested dictionaries for something named ``failure``.
"""
from __future__ import annotations
from functools import wraps

CATEGORIES = ('observer_validity', 'simulation', 'required_lifecycle', 'coherence',
              'recovery', 'epoch_reset', 'ownership', 'performance', 'cleanup')
OUTCOMES = ('pass', 'fail', 'blocked', 'inconclusive', 'not_measured', 'not_exercised')
ADAPTER_VERSION = 1


def check(identifier, category, outcome, scope, **details):
    outcome = {'incomplete': 'inconclusive', 'missing_coverage': 'inconclusive',
               'compared': 'pass', 'unavailable': 'not_measured'}.get(outcome, outcome.replace(' ', '_'))
    if category not in CATEGORIES or outcome not in OUTCOMES or not identifier or not scope:
        raise ValueError('invalid scoped check outcome')
    row=dict(id=identifier, category=category, outcome=outcome, scope=scope,
             tick=None,cycle=None,revision=None,evidence=None)
    row.update(details)
    return row


def legacy_checks(report):
    checks, expected = [], []
    if report.get('stage')=='preflight' or report.get('observational') is True:
        import re
        for ordinal,row in enumerate(report.get('checks',[])):
            if not isinstance(row,dict) or not {'check','result','detail'}<=row.keys():continue
            state={'ready':'pass','unknown':'inconclusive'}.get(row['result'],row['result'])
            if state not in OUTCOMES:state='inconclusive'
            identifier='preflight.'+re.sub(r'[^a-z0-9]+','_',row['check'].lower()).strip('_')
            if isinstance(row['detail'],dict) and row['detail'].get('path'):identifier+=':'+row['detail']['path']
            checks.append(check(identifier,'observer_validity',state,str(row['detail']),
                sequence=ordinal,order_domain='preflight',**row))
    fields = (('simulation_correctness', 'simulation.continuation', 'simulation'),
              ('visual_coherence', 'coherence.scene', 'coherence'),
              ('resume_performance', 'performance.resumed_tps', 'performance'),
              ('seek_performance', 'performance.correction', 'performance'),
              ('full_update_performance', 'performance.full_update', 'performance'),
              ('capture_owner', 'ownership.capture', 'ownership'),
              ('restore_owner', 'ownership.restore', 'ownership'))
    for field, identifier, category in fields:
        row = report.get(field)
        if isinstance(row, dict) and row.get('result'):
            result = row['result']
            if result.replace(' ', '_') not in OUTCOMES and result not in ('incomplete', 'missing_coverage', 'compared'):
                continue
            scope = row.get('scope') or ('resumed native tick cadence; not rollback update cost'
                      if field == 'resume_performance' else field)
            checks.append(check(identifier, category, result, scope,
                evidence_field=field, measurements={k: row[k] for k in ('continuation_ticks',
                'tick_rate_milli', 'elapsed_us', 'gaps_over_20ms', 'max_observation_gap_us') if k in row}))
    for category, result in report.get('acceptance_results', {}).items():
        if category in CATEGORIES:
            checks.append(check('acceptance.' + category, category, result,
                                report.get('scope', 'declared experiment acceptance')))
    request = report.get('seek_request', {})
    recovery = request.get('failed_wrapper_recovery', {})
    if recovery.get('failure') == 'native prerequisite mutation detected before dependent consumption':
        owner = recovery.get('ownership', {})
        b = report.get('advanced_tick')
        valid = (report.get('cancellation') == 'after' and request.get('cancelled_at') == 'after'
            and recovery.get('result') == 'pass' and request.get('result') == 'pass'
            and isinstance(b, int) and request.get('recovered_tick') == b == recovery.get('recovered_tick')
            and owner.get('B_recovered_tick') == b
            and recovery.get('independent_continuation_ticks', 0) >= 120
            and owner.get('cancellation_boundary') == 'ConsumerPrerequisite'
            and owner.get('states') == ['BRetained', 'APublished', 'ExecutionActive',
                'FailedRecoverable', 'RecoveryQuiescing', 'Recovering', 'Recovered']
            and request.get('release', {}).get('completed_release_after_retirement') is True)
        row = check('recovery.prerequisite', 'recovery', 'pass' if valid else 'fail',
                    'prerequisite injection and complete B continuation only', tick=b,
                    evidence_field='seek_request.failed_wrapper_recovery')
        checks.append(row)
        if valid:
            expected.append(dict(row, phase='ConsumerPrerequisite', observation=recovery['failure']))
        checks.append(check('recovery.changed_vfx', 'recovery', 'not_exercised',
                            'changed VFX receiver state was not deliberately recovered'))
    cleanup = report.get('cleanup')
    if isinstance(cleanup, dict):
        complete = cleanup.get('complete')
        result = 'pass' if complete is True and cleanup.get('games_remaining') == 0 else (
            'fail' if complete is False or cleanup.get('games_remaining', 0) else 'not_measured')
        checks.append(check('cleanup.deployment', 'cleanup', result, 'recorded deployment cleanup'))
    return checks, expected


def outcomes_for(report, *, source=None):
    explicit = report.get('outcome_schema') == 1
    if explicit:
        checks = [check(c['id'], c['category'], c['outcome'], c['scope'],
                        **{k:v for k,v in c.items() if k not in ('id','category','outcome','scope')})
                  for c in report.get('checks', [])]
        expected = report.get('expected_interventions', [])
    else:
        checks, expected = legacy_checks(report)
    dimensions = dict.fromkeys(CATEGORIES, 'not_measured')
    for category in CATEGORIES:
        values = [c['outcome'] for c in checks if c['category'] == category]
        # A scoped pass remains visible alongside unexercised checks. It does
        # not close a qualification gate; the readiness evaluator owns that.
        dimensions[category] = next((v for v in ('fail','blocked','inconclusive','pass','not_exercised')
                                     if v in values), 'not_measured')
    failures = [c for c in checks if c['outcome'] in ('fail','blocked')]
    primary = None
    if len(failures) == 1:
        primary = failures[0]
    elif failures and all(isinstance(c.get('sequence'), int) for c in failures) and len({c['order_domain'] for c in failures if c.get('order_domain')})<=1:
        orders = [c['sequence'] for c in failures]
        if len(set(orders)) == len(orders): primary = min(failures, key=lambda c:c['sequence'])
    return dict(schema=1, checks=checks, expected_interventions=expected, dimensions=dimensions,
                failures=failures, primary_failure=primary,
                failure_order='known' if primary or not failures else 'unknown',
                interpretation=report.get('outcome_interpretation') if explicit else dict(adapter='named_legacy_checks',
                    version=ADAPTER_VERSION, source=source or 'supplied report'))


def manifest_summary(manifest, source):
    """Read-only versioned interpretation; the immutable source stays intact."""
    report=dict(manifest['summary'])
    if report.get('cleanup') is None and manifest.get('captures'):
        rows=[c.get('cleanup') or {} for c in manifest['captures']]
        complete=(True if all(r.get('complete') is True and r.get('games_remaining')==0 for r in rows)
            else False if any(r.get('complete') is False or r.get('games_remaining',0) for r in rows) else None)
        report['cleanup']=dict(complete=complete,games_remaining=0 if complete else None)
    if report.get('outcome_schema')!=1:
        value=outcomes_for(report,source=str(source))
        if value['checks']:
            report.update(outcome_schema=1,checks=value['checks'],expected_interventions=value['expected_interventions'],
                          outcome_interpretation=value['interpretation'])
            failure=failure_view(report)
            if failure:
                report['legacy_failure_fields']={k:report[k] for k in ('failure','first_failure') if k in report}
                report['first_failure']=failure
                report['failure']=failure_message(report)
    return report


def attach_outcomes(report):
    """Called at producer/finalization boundaries, not by a formatter."""
    if report.get('outcome_schema') == 1: return report
    value = outcomes_for(report)
    if report.get('failure') and report.get('result') in ('fail','blocked') and not any(c['category']!='cleanup' for c in value['failures']):
        value['checks'].append(check('execution.failure','observer_validity',report['result'],str(report['failure'])))
    if value['checks']:
        report.update(outcome_schema=1, checks=value['checks'], expected_interventions=value['expected_interventions'])
    return report


def producer(function):
    """Publish the named check contract at the comparison boundary."""
    @wraps(function)
    def checked(*args,**kwargs):
        report=function(*args,**kwargs)
        attach_outcomes(report)
        report['outcome_producer']=dict(name=function.__name__,version=1)
        return report
    return checked


def failure_view(report):
    value = outcomes_for(report)
    if (report.get('outcome_schema') != 1 and report.get('failure')
            and not any(c['category'] != 'cleanup' for c in value['failures'])):
        return dict(check='failure', observation=report['failure'])
    primary = value['primary_failure']
    if primary:
        return dict(check=primary.get('check',primary['id']), category=primary['category'],
                    observation=primary.get('measurements') or primary['scope'],
                    **(dict(check_id=primary['id'],detail=primary.get('detail'),result=primary.get('result')) if primary.get('check') else {}),
                    **{k:primary[k] for k in ('tick','cycle','revision','generation','phase','participant','sequence') if k in primary})
    if value['failures']:
        return dict(check='multiple_failed_checks', order='unknown', checks=[c['id'] for c in value['failures']])
    # Explicit top-level comparator differences retain their historical shape.
    for key in ('first_mismatch', 'first_failure', 'first_difference'):
        if report.get(key) is not None: return dict(check=key, observation=report[key])
    if report.get('failure') and report.get('result') in ('fail','blocked','unknown'):
        return dict(check='failure', observation=report['failure'])
    return None


def failure_message(report):
    failure = failure_view(report)
    if failure and failure['check'] == 'performance.resumed_tps':
        return 'resumed native tick cadence failed; simulation comparison is reported separately'
    return 'bounded transaction check failed: ' + str(failure or 'unclassified failure')
