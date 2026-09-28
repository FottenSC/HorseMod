import pytest


def test_nested_timing_and_failed_spans_do_not_double_count():
    from tools.deterministic_qualification.replay_timing import Timeline
    now=[0]
    t=Timeline(clock=lambda:now[0])
    with t.span('outer'):
        now[0]=10
        with pytest.raises(ValueError), t.span('inner'):
            now[0]=30
            raise ValueError('injected')
        now[0]=50
    result=t.snapshot()
    assert result['wall_ns']==50
    assert result['spans'][1]['outcome']=='fail'
    assert result['spans'][1]['parent']==1
    assert result['phase_ns']=={'outer':50,'inner':20}
    assert result['durations_are_additive'] is False


def test_aborted_updates_remain_in_cost_report():
    from tools.deterministic_qualification.replay_timing import runtime_costs
    value=runtime_costs([dict(ordinal=1,full_update_us=16000,status='complete',backlog=0),
                        dict(ordinal=2,full_update_us=20000,status='aborted',backlog=1)])
    assert value['attempted']==2 and value['completed']==1 and value['incomplete']==1
    assert value['overruns']==[{'ordinal':2,'elapsed_us':20000,'status':'aborted'}]
    assert value['result']=='fail' and value['maximum_backlog']==1


def test_subprocess_compile_span_is_nested_and_cancellation_is_retained():
    from tools.deterministic_qualification.replay_timing import Timeline
    now=[0];t=Timeline(clock=lambda:now[0])
    with pytest.raises(KeyboardInterrupt),t.span('fixture_execution'):
        now[0]=50
        raise KeyboardInterrupt()
    t.external_fixture(dict(start_monotonic_ns=10,end_monotonic_ns=40,status='miss',reason='new compiler',returncode=1),1)
    rows=t.snapshot()['spans']
    assert rows[0]['outcome']=='interrupted'
    assert rows[1]['parent']==1 and rows[1]['outcome']=='fail' and rows[1]['elapsed_ns']==30
