"""Bounded CLI views of complete evidence. Never author qualification results."""
from __future__ import annotations

import contextlib
import contextvars
import functools
import json
import re
import sys
import tempfile
import time
from pathlib import Path

_session = contextvars.ContextVar('replay_reporting_session', default=None)
LOG_LIMIT = 256 * 1024


def read_json(path):
    try:
        value = json.loads(Path(path).read_bytes())
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError, TypeError):
        return {}


def log_details(text):
    """Chronological diagnostic selection; secondary recovery is never the cause."""
    from .replay_diagnostics import diagnostic
    result = {}
    for line in text.splitlines():
        if not (re.match(r'^(?:\[[^\]]+\]\s*)?\[(?:HorseMod|ReplayQualification)\]',line)
                or re.match(r'^(?:checkpoint |rolling |historical |seek |GPU |run failed |cleanup |late recovery )',line)):continue
        event = diagnostic(line)
        if event:
            if event['category'] in ('recovery', 'cleanup'):
                result.setdefault(event['category'] + '_failure', line.strip())
            else:
                result.setdefault('first_failure', event)
        phase = re.search(r'ownership state=(\w+)', line)
        if phase: result['phase'] = phase[1]
        for key, pattern in [('tick', r'\b(?:current_tick|current|tick)=(\d+)'),
                             ('cycles', r'\bcycles=(\d+)')]:
            match = re.search(pattern, line)
            if match: result[key] = int(match[1])
    return result


def bounded_log(path, run_id):
    """At most 64 KiB header + 256 KiB tail. Unknown identity means no log data."""
    if not path or not run_id: return {}
    try:
        with Path(path).open('rb') as stream:
            head = stream.read(65536)
            stream.seek(0, 2); size = stream.tell()
            stream.seek(max(0, size - LOG_LIMIT)); tail = stream.read(LOG_LIMIT)
        header_ids = re.findall(rb'\brun_id=(replay-[a-zA-Z0-9]+)', head)
        tail_ids = re.findall(rb'\brun_id=(replay-[a-zA-Z0-9]+)', tail)
        ids = tail_ids or header_ids
        if not ids or ids[-1].decode() != run_id: return {'log_identity': 'unverified'}
        expected = run_id.encode()
        if (header_ids and header_ids[0] != expected) or any(identity != expected for identity in tail_ids):
            # A newly appended run may share the tail with a previous failure.
            markers = list(re.finditer(rb'\brun_id=(replay-[a-zA-Z0-9]+)', tail))
            last_other = max((m.end() for m in markers if m[1] != expected), default=0)
            current = next((m for m in markers if m.start() >= last_other and m[1] == expected), None)
            if current is None: return {'log_identity': 'unverified'}
            tail = tail[tail.rfind(b'\n', 0, current.start())+1:]
        result = log_details(tail.decode('utf-8', errors='replace'))
        result['log_identity'] = 'matched'
        # A tail cannot prove the earliest failure of the complete run.
        if 'first_failure' in result:
            result['first_failure']['scope'] = 'bounded log window'
        return result
    except OSError:
        return {'log_identity': 'unavailable'}


def compact_document(report, *, receipt=None, report_path=None, console_path=None, details=None):
    manifest = read_json(receipt['path']) if isinstance(receipt, dict) and receipt.get('path') else {}
    profile = report.get('profile') or report.get('experiment_profile') or {}
    value = dict(schema_version=2, stage=report.get('stage', 'status'), result=report.get('result', 'unknown'))
    for key in ('run_id', 'elapsed_seconds', 'phase', 'tick', 'cycles', 'groups', 'log_identity', 'scope', 'limitations', 'first_source_boundary',
                'different_fields', 'matches', 'search', 'next_offset', 'scanned_bytes', 'change',
                'changed_fields', 'baseline', 'deployment_observation', 'diagnostic_errors', 'layer', 'test_layers',
                'orchestration_timing', 'latest', 'readiness', 'blockers', 'blocker_details', 'next_experiment', 'certifying', 'reason', 'samples_compared', 'fresh_samples', 'retained_context'):
        if key in report: value[key] = report[key]
    if isinstance(profile, dict) and profile.get('definition', {}).get('name'):
        value['profile'] = profile['definition']['name']
    if 'orchestration_timing' in value:
        t=value['orchestration_timing']; value['orchestration_timing']={k:t[k] for k in ('wall_ns','phase_ns','durations_are_additive')}
    if 'readiness' in value:
        # status --since compares compact snapshots and then renders again.
        # Preserve already compact gate outcomes; formatting never evaluates them.
        value['readiness'] = {k:v['result'] if isinstance(v,dict) else v for k,v in value['readiness'].items()}
    if 'latest' in value:
        value['latest']={role:{k:r[k] for k in ('result','path','compatibility','first_failure') if k in r} for role,r in value['latest'].items()}
    if 'blocker_details' in value:
        value['blocker_details']=[{k:r[k] for k in ('id','resolving_experiment') if k in r} for r in value['blocker_details']]
    if details: value.update(details)
    captures = manifest.get('captures', [])
    if report.get('result') in ('fail', 'blocked'):
        for capture in captures:
            retained = read_json((capture.get('report') or {}).get('path'))
            if retained.get('result') != 'fail': continue
            indexed = indexed_diagnostics(capture)
            if indexed and indexed.get('error'):
                value.setdefault('diagnostic_errors', []).append(indexed['error'])
                observed = {}
            elif indexed is not None:
                observed = {}
                for event in indexed['events']:
                    key = event['category'] + '_failure' if event['category'] in ('recovery', 'cleanup') else 'first_failure'
                    observed.setdefault(key, event)
            else:
                observed = bounded_log((capture.get('raw_log') or {}).get('path'), capture.get('run_id'))
            for key in ('first_failure', 'recovery_failure', 'cleanup_failure', 'tick', 'cycles'):
                if key in observed: value[key] = observed[key]
            value.setdefault('run_id', capture.get('run_id'))
            if observed.get('first_failure'): break
    from .replay_evidence import first_failure
    if 'first_failure' not in value:
        nested = first_failure(report)
        if nested: value['first_failure'] = nested
    # Existing immutable evidence has the authoritative ordered comparator failure.
    failure = manifest.get('first_failure') or report.get('first_failure')
    if failure and (not isinstance(failure, dict) or failure.get('check') != 'failure' or 'first_failure' not in value): value['first_failure'] = failure
    checks = report.get('checks', report.get('preflight', {}).get('checks', []))
    if 'first_failure' not in value:
        blocked = next((row for row in checks if row.get('result') in ('blocked', 'unknown', 'fail')), None)
        if blocked: value['first_failure'] = blocked
    if report.get('failure') and 'first_failure' not in value: value['failure'] = report['failure']
    for key in ('recovery_failure', 'cleanup_failure', 'test_counts', 'tests'):
        if key in report:
            value['test_count' if key == 'tests' else key] = len(report[key]) if key == 'tests' else report[key]
    cleanup = report.get('cleanup')
    if cleanup is None and captures:
        rows = [r.get('cleanup') for r in captures]
        cleanup = {'complete': all(r.get('complete') is True for r in rows) if all(isinstance(r, dict) for r in rows) else 'unknown',
                   'games_remaining': rows[-1].get('games_remaining', 'unknown') if isinstance(rows[-1], dict) else 'unknown'}
    value['cleanup'] = cleanup if cleanup is not None else 'unknown'
    if manifest.get('results'): value['gates'] = manifest['results']
    elif report.get('gates'): value['gates'] = report['gates']
    from .replay_outcomes import outcomes_for, failure_view
    scoped_report = dict(report)
    if isinstance(cleanup, dict): scoped_report['cleanup'] = cleanup
    outcomes = outcomes_for(scoped_report, source=(receipt or {}).get('path') or str(report_path or 'supplied report'))
    if outcomes['checks']:
        value['outcomes'] = outcomes['dimensions']
        if any(c['category'] != 'cleanup' and not c['id'].startswith('native.') for c in outcomes['checks']): value.pop('gates', None)
        value['expected_interventions'] = outcomes['expected_interventions']
        value['unexercised'] = [c['id'] for c in outcomes['checks'] if c['outcome'] == 'not_exercised']
        if outcomes['interpretation']: value['interpretation'] = outcomes['interpretation']
        if outcomes['failures'] and (any(c['category'] != 'cleanup' for c in outcomes['failures']) or 'first_failure' not in value):
            value['first_failure'] = failure_view(scoped_report)
        value['failure_order'] = outcomes['failure_order']
    if report_path: value['report'] = str(report_path)
    if receipt and receipt.get('path'): value['evidence'] = receipt['path']
    if console_path: value['console'] = str(console_path)
    raw = report.get('raw_log')
    if isinstance(raw, dict) and raw.get('path'): value['log'] = raw['path']
    return value


def render(report, *, mode='compact', receipt=None, report_path=None, console_path=None, details=None):
    if mode == 'full': return json.dumps(report, indent=2, default=str) + '\n'
    value = compact_document(report, receipt=receipt, report_path=report_path, console_path=console_path, details=details)
    limit = 4096 if value['result'] in ('fail', 'blocked', 'unknown') or value.get('exit_code', 0) != 0 else 2048
    if value['result'] not in ('fail', 'blocked', 'unknown') and console_path:
        # The immutable console contains the report/manifest links. One entry
        # point avoids repeating long paths on routine successful commands.
        value.pop('report', None); value.pop('evidence', None); value.pop('log', None)
        if isinstance(value.get('gates'), dict) and all(v == 'not measured' for v in value['gates'].values()):
            value['gates'] = 'not measured'
    truncated = False
    def bound(item, depth=0):
        nonlocal truncated
        if depth > 5 and isinstance(item, (dict, list, tuple)):
            truncated = True
            return '[truncated]'
        if isinstance(item, str) and len(item.encode('utf-8')) > 384:
            truncated = True
            return item.encode('utf-8')[:360].decode('utf-8', errors='ignore') + ' [truncated]'
        if isinstance(item, dict):
            pairs = list(item.items())
            if depth and len(pairs) > 24: truncated = True
            return {k: bound(v, depth+1) for k, v in (pairs[:24] if depth else pairs)}
        if isinstance(item, (list, tuple)):
            if len(item) > 6: truncated = True
            return [bound(v, depth+1) for v in item[:6]]
        return item
    # Preserve usable links rather than truncating long filesystem paths.
    links = {k: value.pop(k) for k in ('report', 'evidence', 'console', 'log') if k in value}
    blockers=value.get('blockers')
    value = bound(value); value.update(links)
    if blockers is not None:value['blockers']=blockers
    def encode():
        if mode == 'json': return json.dumps(value, ensure_ascii=False, separators=(',', ':'), default=str) + '\n'
        return '\n'.join(f'{k}: ' + (json.dumps(v, ensure_ascii=False, separators=(',', ':'), default=str) if isinstance(v, (dict, list)) else str(v)) for k, v in value.items() if k != 'schema_version') + '\n'
    if truncated: value['truncated'] = True
    if value.get('stage') == 'inspect' and len(encode().encode('utf-8')) > limit and value.get('matches'):
        # A long immutable log path or excerpt must not displace the indexed
        # coordinates and nearby source text that make inspection useful.
        value['matches'] = [{key: match[key] for key in
            ('category', 'participant', 'tick', 'revision', 'generation', 'byte_offset', 'context')
            if key in match} for match in value['matches'][:1]]
        value['truncated'] = True
    if len(encode().encode('utf-8')) > limit and 'first_failure' in value:
        original = value['first_failure']
        excerpt = json.dumps(original, ensure_ascii=False, default=str).encode()[:384].decode('utf-8', errors='ignore')
        value['first_failure'] = {'excerpt': excerpt + ' [truncated]'}
        value['truncated'] = True
    for key in ('gates', 'groups', 'test_counts', 'failure', 'log', 'console', 'report'):
        if len(encode().encode('utf-8')) <= limit: break
        if key in value: del value[key]; value['truncated'] = True
    if len(encode().encode('utf-8')) > limit:
        # Exceptional path lengths: keep one complete usable link when possible.
        cleanup = value.get('cleanup')
        if isinstance(cleanup, dict):
            cleanup = {key: cleanup.get(key, 'unknown') if isinstance(cleanup.get(key), (bool, int)) else 'unknown'
                       for key in ('complete', 'games_remaining')}
        elif not isinstance(cleanup, (bool, int)):
            cleanup = 'unknown'
        priority={k:value[k] for k in ('first_failure','outcomes','unexercised','failure_order') if k in value}
        value = {k: value[k] for k in ('schema_version', 'stage', 'result', 'exit_code') if k in value}
        value.update(priority)
        value.update(cleanup=cleanup, truncated=True)
        for key, path in links.items():
            if len(str(path).encode()) < limit // 2: value[key] = path; break
    return encode()


def read_status(report_path, *, deployment=None, log_path=None):
    document = read_json(report_path) if report_path else {}
    if 'summary' in document and 'captures' in document:
        if not isinstance(document['summary'], dict): return {'stage': 'status', 'result': 'unknown', 'failure': 'manifest summary invalid'}
        from .replay_outcomes import manifest_summary
        document = dict(manifest_summary(document,report_path), retained_evidence={'path': str(Path(report_path).resolve())})
    result = dict(document) if document else {'result': 'unknown', 'failure': 'report missing, incomplete or unreadable'}
    deployment = deployment or {}
    active = deployment.get('state') in ('active', 'cleanup_pending')
    if active and document.get('run_id') != deployment.get('run_id'):
        result = {'result': 'unknown', 'run_id': deployment.get('run_id'), 'phase': deployment['state'],
                  'failure': 'current run report unavailable; previous report excluded', 'cleanup': 'unknown'}
    if active and result.get('run_id'):
        result.update(bounded_log(log_path, result['run_id']))
    if deployment:
        result['deployment_observation'] = dict(deployment, source='recorded journal; not live process verification')
    result['stage'] = 'status'
    return result


def status_difference(current, previous):
    current = dict(current)
    fields = ('result', 'phase', 'tick', 'cycles', 'first_failure', 'failure',
              'recovery_failure', 'cleanup_failure', 'gates', 'cleanup', 'log_identity',
              'deployment_observation','latest','readiness','blockers','next_experiment')
    if previous.get('schema_version') not in (1, 2) or previous.get('stage') != 'status':
        current.update(change='unknown', baseline='invalid or incomplete status snapshot')
    elif 'latest' in current and 'latest' in previous:
        changed=[key for key in fields if current.get(key)!=previous.get(key)]
        current.update(change='changed' if changed else 'no change',changed_fields=changed)
    elif not current.get('run_id') or not previous.get('run_id'):
        current.update(change='unknown', baseline='run identity unavailable')
    elif current['run_id'] != previous['run_id']:
        current['change'] = 'new run'
    else:
        changed = [key for key in fields if current.get(key) != previous.get(key)]
        current['change'] = 'changed' if changed else 'no change'
        current['changed_fields'] = changed
        current.pop('elapsed_seconds', None)
    return current


def indexed_diagnostics(capture):
    """Validate both immutable objects before trusting stored source coordinates."""
    from .artifacts import sha256_file
    descriptor = capture.get('diagnostic_index') or {}
    if not descriptor: return None
    log = capture.get('raw_log') or {}
    try:
        if not isinstance(descriptor, dict) or not isinstance(log, dict):
            raise ValueError('diagnostic descriptor invalid')
        if sha256_file(Path(descriptor['path'])) != descriptor['sha256']:
            raise ValueError('diagnostic index hash mismatch')
        index = read_json(descriptor['path'])
        if (index.get('schema_version') != 1 or index.get('run_id') != capture.get('run_id')
                or index.get('log', {}).get('sha256') != log.get('sha256')):
            raise ValueError('diagnostic index identity mismatch')
        if not isinstance(index.get('events'), list) or not all(
                isinstance(e, dict) and isinstance(e.get('byte_offset'), int) and e['byte_offset'] >= 0
                and isinstance(e.get('category'), str) for e in index['events']):
            raise ValueError('diagnostic index events incomplete or invalid')
        if sha256_file(Path(log['path'])) != log['sha256']:
            raise ValueError('retained log hash mismatch')
        return index
    except (OSError, KeyError, ValueError, TypeError) as error:
        return {'error': str(error)}


def inspect_report(report_path, *, participant=None, tick=None, offset=0):
    from .replay_diagnostics import scan_lines, IDENTITY
    document = read_json(report_path)
    if document.get('retained_evidence', {}).get('path'):
        document = read_json(document['retained_evidence']['path'])
    result = dict(stage='inspect', result='unknown', matches=[], cleanup='unknown',
                  scope='recorded diagnostics only', scanned_bytes=0)
    if not document:
        result['failure'] = 'report missing, incomplete or unreadable'
        return result
    captures = document.get('captures')
    if captures is None:
        captures = [document]
    if not isinstance(captures, list) or not all(isinstance(c, dict) for c in captures):
        result['failure'] = 'capture metadata incomplete or invalid'
        return result
    summary = document.get('summary', document)
    if not isinstance(summary, dict): summary = {}
    rows = [c.get('cleanup') for c in captures]
    result['cleanup'] = summary.get('cleanup') or (
        {'complete': all(c.get('complete') is True for c in rows),
         'games_remaining': rows[-1].get('games_remaining', 'unknown')}
        if rows and all(isinstance(c, dict) for c in rows) else 'unknown')
    if participant is None and tick is None:
        failure = document.get('first_failure') or summary.get('first_failure') or summary.get('failure')
        if failure:
            result['first_failure'] = failure
            contexts = document.get('contexts', [])
            if contexts:
                lines = contexts[0].get('context', {}).get('lines', [])
                result['retained_context'] = [line[:120] for line in lines[:3]]
    remaining = 1024 * 1024
    # Offsets address the concatenation of capture logs, in retained report order.
    base = 0
    errors = []
    for capture in captures:
        raw = capture.get('raw_log') or capture.get('artifacts', {}).get('raw_log') or {}
        if not raw.get('path'): continue
        path = Path(raw['path']); identity = capture.get('run_id')
        try:
            size = path.stat().st_size
            if offset >= base + size:
                base += size
                continue
            local = max(0, offset - base)
            index = indexed_diagnostics(capture)
            if index and index.get('error'):
                errors.append(index['error']); base += size; continue
            if index is not None:
                events = (e for e in index['events'] if e['byte_offset'] >= local)
                consumed = size - local
            else:
                # A contiguous lookbehind establishes identity for unlabelled lines.
                # If no marker fits, their identity stays unknown, never guessed.
                start = max(0, local - min(65536, remaining // 4))
                with path.open('rb') as stream:
                    stream.seek(start); data = stream.read(remaining)
                result['scanned_bytes'] += len(data); remaining -= len(data)
                read_end = end = start + len(data)
                if end < size:
                    boundary = data.rfind(b'\n')
                    if boundary >= 0: data = data[:boundary+1]; end = start + len(data)
                    else:
                        data = b''
                        errors.append('oversized partial line excluded from bounded scan')
                if end <= local and local < size:
                    end = read_end
                    errors.append('oversized partial line excluded from bounded scan')
                if start:
                    boundary = data.find(b'\n')
                    if boundary >= 0: start += boundary+1; data = data[boundary+1:]
                    else: data = b''
                events = (e for e in scan_lines(data.splitlines(keepends=True), identity,
                           offset=start) if e['byte_offset'] >= local)
                consumed = max(0, end - local)
                if not IDENTITY.search(data.decode('utf-8', errors='replace')):
                    errors.append('run identity unavailable in scanned window; unlabelled events excluded')
            for event in events:
                if index is None and start: event.pop('line_number', None)
                if participant is not None and event.get('participant') != participant: continue
                if tick is not None and event.get('tick') != tick: continue
                if participant is None and tick is None and event['category'] in ('recovery', 'cleanup'):
                    result.setdefault(event['category'] + '_failure', event)
                    continue
                if len(result['matches']) == 1:
                    result.update(next_offset=base + event['byte_offset'], search='more matches available')
                    break
                if index is not None:
                    context_start = max(0, event['byte_offset'] - 512)
                    with path.open('rb') as stream:
                        stream.seek(context_start); context_bytes = stream.read(2048)
                else:
                    context_start, context_bytes = start, data
                relative = event['byte_offset'] - context_start
                before = context_bytes[:relative].splitlines()[-1:]
                after = context_bytes[relative:].splitlines()[:2]
                context = [line[:120].decode('utf-8', errors='replace') for line in before + after]
                result['matches'].append(dict(event, context=context, log=str(path), provenance='verified immutable index' if index is not None else 'bounded legacy scan; whole-log hash not verified'))
                result['run_id'] = identity
            if 'next_offset' in result: break
            if local + consumed < size:
                result['next_offset'] = base + local + consumed
                break
            base += size
            if remaining == 0:
                result['next_offset'] = base
                break
        except (OSError, KeyError, ValueError, TypeError) as error:
            errors.append(str(error))
    result['search'] = result.get('search', 'matches found' if result['matches'] else
                                  'no match in scanned range' if 'next_offset' in result else 'no matching recorded diagnostic')
    if not captures or not any(c.get('raw_log') or c.get('artifacts', {}).get('raw_log') for c in captures):
        result['first_failure'] = document.get('first_failure') or document.get('failure')
        errors.append('raw log unavailable')
    if result['matches']:
        result.pop('first_failure', None); result.pop('retained_context', None)
    if errors: result['diagnostic_errors'] = errors
    result['result'] = 'unknown' if errors else 'pass'
    return result


def publish(report, receipt=None, report_path=None, *, readonly=False):
    state = _session.get()
    if state is not None:
        state.update(report=report, receipt=receipt, report_path=report_path, readonly=readonly)
        if report_path: state['output_root'] = Path(report_path).parent


def output_mode(argv):
    import argparse
    values = []
    for i, arg in enumerate(argv):
        if arg == '--output' and i + 1 < len(argv): values.append(argv[i+1])
        elif arg.startswith('--output='): values.append(arg.split('=', 1)[1])
    if len(set(values)) > 1:
        argparse.ArgumentParser().error('conflicting --output selections')
    return values[-1] if values else 'compact'


class _TranscriptTee:
    def __init__(self, original, transcript):
        self.original, self.transcript = original, transcript

    def write(self, text):
        self.transcript.write(text)
        return self.original.write(text)

    def flush(self):
        self.transcript.flush()
        self.original.flush()


def reported_cli(function=None, *, output_root=None):
    if function is None:
        return lambda fn: reported_cli(fn, output_root=output_root)

    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        argv = sys.argv[1:]; mode = output_mode(argv)
        if _session.get() is not None or '--help' in argv or '-h' in argv:
            return function(*args, **kwargs)
        from .replay_timing import ACTIVE, Timeline
        timeline = Timeline(progress=lambda phase: print('phase: '+phase,file=sys.__stderr__,flush=True)); timing_token = ACTIVE.set(timeline)
        state = {}; token = _session.set(state); started = time.monotonic()
        try:
            with tempfile.TemporaryFile(mode='w+', encoding='utf-8') as stream:
                try:
                    stdout = _TranscriptTee(sys.stdout, stream) if mode == 'full' else stream
                    stderr = _TranscriptTee(sys.stderr, stream) if mode == 'full' else stream
                    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                        result = function(*args, **kwargs)
                        state['exit_code'] = result
                        return result
                finally:
                    stream.seek(0); raw = stream.read()
                    exception=sys.exc_info()[1]
                    if exception is not None and not isinstance(exception,SystemExit):
                        import traceback
                        raw+='\n'+''.join(traceback.format_exception(type(exception),exception,exception.__traceback__))
                    report = state.get('report', {'stage': argv[0] if argv else 'cli', 'result': 'fail',
                                                 'failure': str(exception) if exception is not None and not isinstance(exception,SystemExit) else raw.splitlines()[-1] if raw else 'command interrupted before report'})
                    if state.get('readonly'):
                        if mode != 'full': print(raw, end='')
                        # Read-only commands format once in their handler and retain nothing.
                        raw = ''
                    console = None
                    if not state.get('readonly'):
                        report = dict(report, orchestration_timing=timeline.snapshot())

                    # Preserve the complete original console alongside existing raw evidence.
                    if (raw or state.get('report') is not None) and not state.get('readonly') and not isinstance(sys.exc_info()[1], SystemExit):
                        from .replay_evidence import store_bytes
                        root = Path(output_root() if callable(output_root) else output_root or state.get('output_root') or Path(tempfile.gettempdir()) / 'replay-reporting')
                        receipt = state.get('receipt') or {}
                        if exception is not None and not receipt:
                            from .replay_evidence import retain_run
                            report=dict(stage=report.get('stage','cli'),result='fail',failure=str(exception),
                                exception_type=type(exception).__name__,orchestration_timing=timeline.snapshot(),
                                cleanup='unknown',scope='interrupted command; no qualification')
                            receipt=retain_run(root/'evidence',report)
                            state['receipt']=receipt
                        if receipt.get('path') and Path(receipt['path']).parent.name=='manifests':
                            from .replay_evidence import finalize_timing
                            receipt=finalize_timing(root/'evidence',receipt,timeline.snapshot())
                            state['receipt']=receipt
                        linked = raw + ('\nRetained evidence: ' + str(receipt['path']) + '\n' if receipt.get('path') else '')
                        if state.get('report_path'):
                            # Snapshot the exact published report, independent of future overwrites.
                            snapshot = store_bytes(root / 'evidence' / 'objects', (json.dumps(report, default=str) + '\n').encode())
                            linked += '\nRetained report: ' + snapshot['path'] + '\n'
                        console = store_bytes(root / 'evidence' / 'objects', linked.encode(), '.log')['path']
                    details = log_details(raw) if report.get('result') in ('fail', 'blocked') or state.get('exit_code') not in (None, 0) else {}
                    if 'exit_code' in state: details['exit_code'] = state['exit_code']
                    report = dict(report)
                    if not state.get('readonly'): report['orchestration_timing'] = timeline.snapshot()
                    report.setdefault('stage', next((a for a in argv if not a.startswith('-') and a not in ('compact', 'json', 'full')), 'cli'))
                    if not state.get('readonly'): report.setdefault('elapsed_seconds', round(time.monotonic()-started, 3))
                    counts = re.findall(r'(\d+) (passed|failed|skipped|deselected)', raw)
                    if counts: details['test_counts'] = {name: int(count) for count, name in counts}
                    native = re.search(r'(\d+) tests failed out of (\d+)', raw)
                    if native: details.setdefault('test_counts', {}).update(ctest_failed=int(native[1]), ctest_total=int(native[2]))
                    if mode != 'full' and not state.get('readonly'): print(render(report, mode=mode if mode in ('compact', 'json') else 'compact',
                                 receipt=state.get('receipt'), report_path=state.get('report_path'),
                                 console_path=console, details=details), end='',
                          file=sys.stderr if sys.exc_info()[0] is not None else sys.stdout)
        finally:
            ACTIVE.reset(timing_token)
            _session.reset(token)
    return wrapped
