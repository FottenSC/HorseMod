"""Monotonic, nested orchestration spans and separately scoped native costs."""
from __future__ import annotations
from contextlib import contextmanager
from contextvars import ContextVar
from functools import wraps
import math
import statistics
import time
import re

ACTIVE = ContextVar('replay_timing', default=None)


class Timeline:
    def __init__(self, clock=time.perf_counter_ns, progress=None):
        self.clock=clock; self.started=clock(); self.rows=[]; self.stack=[]; self.progress=progress;self.updates=0

    @contextmanager
    def span(self, phase, **details):
        if self.progress and len(self.stack)<2 and self.updates<32 and phase not in ('source.discovery','source.hashing'):
            self.updates+=1;self.progress(phase)
        row=dict(id=len(self.rows)+1,phase=phase,parent=self.stack[-1] if self.stack else None,
                 start_ns=self.clock()-self.started,outcome='active',**details)
        self.rows.append(row);self.stack.append(row['id'])
        try:
            yield row
            if row['outcome']=='active':row['outcome']='pass'
        except BaseException as error:
            row.update(outcome='interrupted' if isinstance(error,(KeyboardInterrupt,SystemExit)) else 'fail',
                       error=type(error).__name__)
            raise
        finally:
            row['end_ns']=self.clock()-self.started
            row['elapsed_ns']=row['end_ns']-row['start_ns'];self.stack.pop()

    def external_fixture(self, receipt, parent):
        # Windows perf_counter uses the same monotonic clock in both processes.
        start=receipt['start_monotonic_ns']-self.started
        end=receipt['end_monotonic_ns']-self.started
        if not 0<=start<=end<=self.clock()-self.started:
            raise ValueError('fixture timing outside orchestrator interval')
        self.rows.append(dict(id=len(self.rows)+1,phase='fixture.cache_lookup' if receipt['status']=='hit' else 'fixture.compilation',
            parent=parent,start_ns=start,end_ns=end,elapsed_ns=end-start,
            outcome='pass' if receipt.get('returncode',0)==0 else 'fail',
            cache_status=receipt['status'],reason=receipt['reason'],
            scope='compiler recipe including dependency validation; nested in fixture execution'))

    def snapshot(self):
        phases={}
        for row in self.rows:
            phases[row['phase']]=phases.get(row['phase'],0)+row.get('elapsed_ns',0)
        return dict(schema=1,wall_ns=self.clock()-self.started,spans=[dict(r) for r in self.rows],
                    phase_ns=phases,durations_are_additive=False,
                    scope='monotonic orchestration wall time; nested phases overlap')


@contextmanager
def span(phase, **details):
    timeline=ACTIVE.get()
    if timeline is None: yield {}
    else:
        with timeline.span(phase,**details) as row: yield row


def timed(phase):
    def decorate(function):
        @wraps(function)
        def run(*args,**kwargs):
            with span(phase) as row:
                result=function(*args,**kwargs)
                if isinstance(result,dict) and result.get('result') in ('fail','blocked'):row['outcome']=result['result']
                return result
        return run
    return decorate


def runtime_costs(rows):
    complete=[int(r['full_update_us']) for r in rows if r['status']=='complete']
    ordered=sorted(complete)
    overruns=[dict(ordinal=int(r['ordinal']),elapsed_us=int(r['full_update_us']),status=r['status'])
              for r in rows if int(r['full_update_us'])>16700]
    incomplete=sum(r['status']!='complete' for r in rows)
    return dict(result='not_measured' if not rows else 'fail' if incomplete or overruns else 'pass',
        attempted=len(rows),completed=len(complete),incomplete=incomplete,
        median_us=statistics.median(complete) if complete else None,
        p95_us=ordered[math.ceil(len(ordered)*.95)-1] if ordered else None,
        maximum_us=max((int(r['full_update_us']) for r in rows),default=None),
        overruns=overruns,maximum_backlog=max((int(r.get('backlog',0)) for r in rows),default=0),
        scope='continuous rolling update through next checkpoint and retirement; incomplete samples retained')


def parse_runtime_timing(text):
    rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines()
          if '[HorseMod] rolling timing protocol=1 ordinal=' in line]
    beginnings=[int(n) for n in re.findall(r'rolling timing begin ordinal=(\d+)',text)]
    ordinals=[int(r['ordinal']) for r in rows]
    if len(set(ordinals))!=len(ordinals):raise ValueError('duplicate runtime timing sample')
    for n in beginnings:
        if n not in ordinals:rows.append(dict(ordinal=n,status='missing_end',full_update_us=0,backlog=0))
    result=runtime_costs(rows)
    result.update(samples=rows,protocol=1,missing_samples=sum(r['status']=='missing_end' for r in rows),
        terminal_samples=sum(r['status']=='terminal_no_next_checkpoint' for r in rows),
        backlog_units='pending owner categories, not task/resource count',
        measured_common_observer_cost=False)
    windows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in text.splitlines()
        if '[HorseMod] rolling timing window protocol=1 ' in line]
    result['window']=windows[0] if len(windows)==1 else None
    return result
