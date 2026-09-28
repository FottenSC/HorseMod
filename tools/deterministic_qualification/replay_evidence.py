"""Immutable manifests and bounded offline diagnostics; never drive simulation."""
from __future__ import annotations
from .replay_timing import timed

from collections import deque
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile
import os

from .artifacts import sha256_file


def store_bytes(root: Path, data: bytes, suffix='.json') -> dict:
    digest = hashlib.sha256(data).hexdigest()
    root.mkdir(parents=True, exist_ok=True)
    target = root / (digest + suffix)
    if target.exists():
        if sha256_file(target) != digest:
            raise RuntimeError(f'corrupt retained object: {target}')
    else:
        fd, name = tempfile.mkstemp(dir=root, suffix='.tmp')
        try:
            with os.fdopen(fd, 'wb') as out:
                out.write(data)
            if target.exists():
                if sha256_file(target) != digest:
                    raise RuntimeError('retained object collision')
            else:
                os.replace(name, target)
        finally:
            Path(name).unlink(missing_ok=True)
    return dict(path=str(target.resolve()), sha256=digest, bytes=len(data))


def store_file(root: Path, source: Path, expected=None) -> dict:
    digest = sha256_file(source)
    if expected is not None and digest != expected:
        raise RuntimeError(f'evidence hash mismatch: {source}')
    root.mkdir(parents=True, exist_ok=True)
    target = root / (digest + source.suffix)
    if not target.exists():
        fd, temporary = tempfile.mkstemp(dir=root, suffix='.tmp')
        os.close(fd)
        try:
            shutil.copyfile(source, temporary)
            if sha256_file(Path(temporary)) != digest:
                raise RuntimeError('evidence changed while retaining')
            if not target.exists():
                os.replace(temporary, target)
        finally:
            Path(temporary).unlink(missing_ok=True)
    if sha256_file(target) != digest:
        raise RuntimeError('retained evidence corruption')
    return dict(path=str(target.resolve()), sha256=digest, bytes=target.stat().st_size)


def first_failure(document):
    from .replay_outcomes import failure_view
    return failure_view(document) if isinstance(document, dict) else None


def finalize_timing(root, receipt, timing):
    """Immutable finalization receipt; original stage evidence remains intact."""
    path=Path(receipt['path'])
    if sha256_file(path)!=receipt['sha256']:raise RuntimeError('stage manifest changed before finalization')
    manifest=json.loads(path.read_bytes())
    manifest['summary']['orchestration_timing']=dict(timing,excluded=['publication of this final timing receipt and console rendering'])
    manifest['stage_manifest']=receipt
    manifest['completed_ns']=__import__('time').time_ns()
    final=store_bytes(root/'manifests',(json.dumps(manifest,indent=2,default=str)+'\n').encode())
    from .replay_readiness import update_index
    update_index(root,final)
    return final


def bounded_context(path, failure, *, maximum_bytes=256 * 1024):
    encoded = json.dumps(failure)
    ticks = re.findall(r'["\']?(?:tick|native_tick|native_frame)["\']?\s*[:=]\s*(\d+)', encoded)
    if not ticks:
        ticks = re.findall(r'\btick\s*(\d+)', encoded)
    tick = int(ticks[0]) if ticks else None
    lines, used, overflow = [], 0, False
    tail = deque(maxlen=24)
    categories = {'hashes': False, 'callbacks': False, 'rng': False, 'owners': False, 'fields': False}
    with Path(path).open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            tail.append(line)
            match = re.search(r'\b(?:tick|native_frame)=(\d+)\b', line)
            if tick is None or not match or abs(int(match[1]) - tick) > 2:
                continue
            size = len(line.encode('utf-8'))
            if used + size > maximum_bytes:
                overflow = True
                continue
            lines.append(line)
            used += size
            for key, pattern in [('hashes','hash'), ('callbacks','callback'), ('rng','rng|rand|seed'),
                                 ('owners','owner|birth|death|retire'), ('fields','field|transform|origin')]:
                categories[key] |= bool(re.search(pattern, line, re.I))
    if tick is None:
        for line in tail:
            if used + len(line.encode()) > maximum_bytes:
                overflow = True
                break
            lines.append(line)
            used += len(line.encode())
    return dict(tick=tick, lines=lines, bytes=used, overflow=overflow,
                categories={k: 'recorded' if v else 'unavailable' for k, v in categories.items()},
                scope='already recorded ticks ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â±2, all traversal identities preserved; never post-failure execution')


def failure_coordinates(failure):
    observation = (failure or {}).get('observation')
    row = observation if isinstance(observation, dict) else {}
    text = str(observation)
    result = {key: row.get(key, 'unavailable') for key in ('cycle', 'tick', 'generation', 'phase')}
    for key in ('cycle', 'tick'):
        if result[key] == 'unavailable':
            match = re.search(r'\b' + key + r'\s*[:=]?\s*(\d+)', text)
            if match:
                result[key] = int(match[1])
    result['expected'] = row.get('expected', row.get('original', 'unavailable'))
    result['actual'] = row.get('actual', row.get('restored', row.get('observed', 'unavailable')))
    return result


def diagnostic_index(path, run_id):
    from .replay_diagnostics import scan_lines
    events = []
    with Path(path).open('rb') as stream:
        for event in scan_lines(stream, run_id):
            previous = events[-1] if events else None
            # Coalesce only adjacent repeated lines, never distinct traversals.
            normalized = lambda e: re.sub(r'^\[\d{4}-[^]]+\]\s*', '', e['excerpt'])
            if (previous and normalized(previous) == normalized(event)
                    and previous['line_number'] + previous.get('duplicate_count', 1) == event['line_number']):
                previous['duplicate_count'] = previous.get('duplicate_count', 1) + 1
            else:
                events.append(event)
    return dict(schema_version=1, run_id=run_id, events=events,
                scope='recorded native failures in log order; diagnostic metadata only')


def native_failure(path, run_id):
    """First recorded native predicate, independently of acceptance classification."""
    index = diagnostic_index(path, run_id)
    event = next((e for e in index['events'] if e['category'] not in ('recovery', 'cleanup')), None)
    capacity = _capacity_failure(path, run_id)
    if event is None or 'checkpoint capacity participant=' in event['excerpt'] or (capacity and 'run failed run_id=' in event['excerpt']):
        return capacity
    return dict(check='native.' + event['category'], observation=event)


def _capacity_failure(path, run_id):
    """Read the first capacity rejection only from a witnessed failed native run."""
    if not run_id:
        return None
    first, cycle, witnessed = None, None, False
    with Path(path).open(encoding='utf-8', errors='replace') as stream:
        for line in stream:
            if f'run failed run_id={run_id} ' in line:
                witnessed = True
            match = re.search(r'checkpoint capacity participant=(\w+) tick=(\d+) owned=(\d+) envelope=(\d+) available=(\d+)', line)
            if match and first is None:
                first = dict(check='native.checkpoint_capacity', observation=dict(
                    participant=match[1], tick=int(match[2]), owned_bytes=int(match[3]),
                    expected=int(match[5]), actual=int(match[4]),
                    comparison='required transient bytes must not exceed available bytes',
                    phase='capture_before_publication', line=line.strip()))
            match = re.search(r'rolling failed .*cycles=(\d+)', line)
            if match and cycle is None:
                cycle = int(match[1]) + 1
    if not witnessed or first is None:
        return None
    first['observation']['cycle'] = cycle if cycle is not None else 'unavailable'
    return first


def rolling_coverage(path: Path, run_id: str, first: int, last: int) -> dict:
    """Inventory retained native observations without promoting a test gate.

    Attribute whole native intervals by their completed tick, retaining startup
    separately. A reused input in startup is not coverage of rolling combat.
    """
    if not 0 <= first < last:
        raise RuntimeError('invalid rolling coverage window')
    keys = ('intervals', 'tick_callbacks', 'zero_tick_intervals', 'multi_tick_intervals',
            'input_cache_publications', 'repeated_ticks_without_new_publication',
            'gpu_initializations', 'seed_events', 'cpu_sprite_retirements',
            'cpu_mesh_retirements', 'gpu_retirements', 'unknown_retirements')
    result = {name: dict.fromkeys(keys, 0) for name in ('forward', 'regenerated', 'outside_window')}
    result.update(first_exclusive=first, last_inclusive=last, regenerated_traversals=0,
        pending_work='unavailable: no pending-task restoration receipt',
        cpu_births='unavailable: RNG seeds alone do not identify CPU construction',
        comparison='inventory only; independent agreement comes from the enclosing comparator')
    active = None
    ticks = publications = repeats = 0
    used = False
    def bucket(tick):
        return result['outside_window' if not first < tick <= last
                      else 'regenerated' if active is not None else 'forward']
    with path.open(encoding='utf-8', errors='replace') as lines:
        for line in lines:
            admission = re.search(r'historical combat execution admitted run_id=(\S+) from_tick=(\d+) to_tick=(\d+) B_retained=true', line)
            if admission:
                identity, target, anchor = admission.groups()
                if identity != run_id or active is not None or ticks or publications or int(target)-int(anchor) != 7:
                    raise RuntimeError('invalid rolling coverage traversal admission')
                active = (int(anchor), int(target))
                result['regenerated_traversals'] += 1
            committed = re.search(r'rolling cycle ordinal=\d+ T=(\d+) A=(\d+) resimulated_ticks=7 committed=true', line)
            if committed:
                if active != (int(committed[2]), int(committed[1])) or ticks or publications:
                    raise RuntimeError('invalid rolling coverage traversal completion')
                active = None
            if '[ReplayQualification] boundary ordinal=' in line:
                fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
                phase = fields['phase']
                if phase == 'input_cache_publication':
                    publications += 1
                    used = False
                elif phase == 'callback_a30':
                    ticks += 1
                    repeats += used
                    used = True
                elif phase == 'actor_tail':
                    row = bucket(int(fields['frame']))
                    row['intervals'] += 1
                    row['tick_callbacks'] += ticks
                    row['zero_tick_intervals'] += ticks == 0
                    row['multi_tick_intervals'] += ticks > 1
                    row['input_cache_publications'] += publications
                    row['repeated_ticks_without_new_publication'] += repeats
                    ticks = publications = repeats = 0
                    used = False
            event = re.search(r'\[ReplayQualification\] particle (GPU initialized|retirement|seed) run_id=(\S+) tick=(\d+) (.*)', line)
            if not event:
                continue
            kind, identity, tick, fields = event.groups()
            if identity != run_id:
                raise RuntimeError('rolling coverage lifecycle identity changed')
            row = bucket(int(tick))
            if kind == 'retirement':
                if 'peer_notifications_and_destructor_returned=true' not in fields or 'slot_cleared=true' not in fields:
                    raise RuntimeError('rolling coverage retirement incomplete')
                table = re.search(r'vtable_rva=(\w+)', fields)
                # Sc6ReplayCpuEmitterState::Vtable defines these native kinds.
                key = {'3949b60': 'cpu_sprite_retirements', '3949d88': 'cpu_mesh_retirements',
                       '394c100': 'gpu_retirements'}.get(table[1] if table else '', 'unknown_retirements')
            elif kind == 'seed':
                key = 'seed_events'
            else:
                key = 'gpu_initializations'
            row[key] += 1
    if active is not None or ticks or publications:
        raise RuntimeError('rolling coverage ends inside a traversal or native interval')
    return result


@timed('evidence.publication')
def retain_run(root: Path, summary: dict, reports=()) -> dict:
    from .replay_outcomes import attach_outcomes, outcomes_for
    attach_outcomes(summary)
    objects = root / 'objects'
    retained, contexts, missing = [], [], []
    failure = first_failure(summary)
    native_detected = False
    for path in dict.fromkeys(map(Path, reports)):
        try:
            report = json.loads(path.read_bytes())
            record = dict(report=store_file(objects, path))
            if report.get('raw_log'):
                raw = report['raw_log']
                record['raw_log'] = store_file(objects, Path(raw['path']), raw['sha256'])
                index = diagnostic_index(record['raw_log']['path'], report.get('run_id'))
                index['log'] = record['raw_log']
                record['diagnostic_index'] = store_bytes(objects, (json.dumps(index) + '\n').encode())
                from .replay_timing import parse_runtime_timing
                timing=parse_runtime_timing(Path(record['raw_log']['path']).read_text(encoding='utf-8',errors='replace'))
                if timing['attempted']:
                    record['runtime_timing']=timing
                    summary['full_update_performance']=timing
                coverage_cycles = report.get('rolling_cycles') or summary.get('rolling_cycles', 0)
                if coverage_cycles and report.get('result') == 'captured':
                    first = report.get('historical_anchor_tick', 210)
                    record['rolling_coverage'] = rolling_coverage(Path(record['raw_log']['path']),
                        report['run_id'], first, first + 6 + coverage_cycles)
                if report.get('result') == 'fail':
                    from .replay_outcomes import check
                    for event in index['events']:
                        if event['category'] in ('recovery','cleanup'): category=event['category']
                        else: category='ownership'
                        summary['checks']=[c for c in summary.get('checks',[]) if c['id']!='execution.failure']
                        summary['checks'].append(check('native.'+event['category'],category,'fail',event['excerpt'],
                            sequence=event['byte_offset'],order_domain=report.get('run_id'),evidence=record['raw_log'],
                            **{k:event[k] for k in ('tick','cycle','revision','generation','phase','participant') if k in event}))
                        summary['outcome_schema']=1
                    # Preserve established classification; the broader index is diagnostic only.
                    detected = _capacity_failure(record['raw_log']['path'], report.get('run_id'))
                    if detected and not native_detected:
                        failure, native_detected = detected, True
                        summary.setdefault('checks',[]).append(check('native.checkpoint_capacity','ownership','fail',
                            'native checkpoint admission exceeded available bytes',sequence=-1,
                            evidence=record['raw_log'],measurements=detected['observation']))
                        summary['outcome_schema']=1
                if failure or report.get('result') == 'fail':
                    contexts.append(dict(run_id=report.get('run_id'),
                        context=bounded_context(record['raw_log']['path'], failure or first_failure(report))))
            record.update(run_id=report.get('run_id'), identities=report.get('identities'),
                          capture_result=report.get('result'),
                          cleanup=report.get('cleanup'), recovery=report.get('recovery', 'not measured'))
            retained.append(record)
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            missing.append(dict(path=str(path), error=str(error)))
    from .replay_experiments import finalize
    if retained:
        cleanup_rows=[r.get('cleanup') or {} for r in retained]
        complete=(False if any(r.get('complete') is False for r in cleanup_rows) else
            True if all(r.get('complete') is True and r.get('games_remaining')==0 for r in cleanup_rows) else None)
        counts=[r.get('games_remaining') for r in cleanup_rows]
        summary['cleanup']={'complete':complete,'games_remaining':max(counts) if all(type(n) is int for n in counts) else None}
        from .replay_outcomes import check
        additional=[]
        if summary.get('full_update_performance'):
            additional.append(check('performance.full_update','performance',summary['full_update_performance']['result'],
                summary['full_update_performance']['scope'],evidence_field='full_update_performance'))
        additional.append(check('cleanup.deployment','cleanup','not_measured' if complete is None else 'pass' if complete else 'fail','all retained capture cleanup receipts',sequence=10**18))
        summary.setdefault('checks',[]).extend(additional);summary['outcome_schema']=1
    finalize(summary, retained)
    # Producers determine the aggregate before publication. A renderer must not
    # repair a successful aggregate that contains an explicit failed check.
    values=outcomes_for(summary)
    if summary.get('result')!='blocked' and any(c['outcome']=='fail' for c in values['checks']):
        summary['result']='fail'
    failure=first_failure(summary) or failure
    from .replay_timing import ACTIVE
    if ACTIVE.get(): summary['orchestration_timing']=ACTIVE.get().snapshot()
    state = summary.get('result', 'not measured')
    dimensions = dict.fromkeys(('observer_validity', 'simulation', 'coherence', 'recovery', 'performance'), 'not measured')
    if native_detected:
        dimensions['simulation'] = 'fail'
    if summary.get('preflight', {}).get('result') in ('blocked', 'unknown'):
        dimensions['simulation'] = 'blocked'
    cleanup = [r['cleanup'] for r in retained]
    dimensions['cleanup'] = ('pass' if cleanup and all(isinstance(c, dict) and c.get('complete') for c in cleanup)
                             else 'blocked' if any(c for c in cleanup) else 'not measured')
    # Never infer individual acceptance gates from an aggregate diagnostic pass.
    for key in dimensions:
        if key in summary.get('acceptance_results', {}):
            dimensions[key] = summary['acceptance_results'][key]
    scoped = outcomes_for(summary)
    for key, outcome in scoped['dimensions'].items():
        if outcome != 'not_measured': dimensions[key] = outcome
    manifest = dict(schema=1, completed_ns=__import__("time").time_ns(), summary=summary, captures=retained, first_failure=failure,
                    failure_coordinates=failure_coordinates(failure),
                    detection=('native rejection witnessed in failed run; context contains only already-recorded events' if native_detected
                               else 'offline comparison or capture failure; no retroactive claim of live stop'),
                    contexts=contexts, missing_evidence=missing, results=dimensions,
                    pins=['qualification' if (summary.get('profile') or {}).get('definition', {}).get('purpose') == 'qualification'
                          else 'failure' if state in ('fail', 'blocked') else 'experiment'],
                    outcomes=scoped, diagnostic_complete=not missing and not any(c['context']['overflow'] for c in contexts))
    receipt = store_bytes(root / 'manifests', (json.dumps(manifest, indent=2, default=str) + '\n').encode())
    if failure:
        signature = hashlib.sha256(json.dumps(failure, sort_keys=True, default=str).encode()).hexdigest()
        pins = root / 'first-failures'
        pins.mkdir(parents=True, exist_ok=True)
        try:
            with (pins / (signature + '.json')).open('x', encoding='utf-8') as out:
                json.dump(receipt, out, indent=2)
        except FileExistsError:
            pass  # immutable first receipt; repeated attempts share stored objects
    text = '\n'.join([f"# {summary.get('stage', 'retained experiment')}: {state}",
                       '', f"First failure: {json.dumps(failure)}", '',
                       *[f'- {k}: {v}' for k, v in dimensions.items()], '',
                       f"Manifest: [{receipt['sha256']}]({receipt['path']})", '',
                       'Hypothesis and next decision: record explicitly in the current status; no automatic gate promotion.'])
    receipt['summary'] = store_bytes(root / 'summaries', text.encode(), '.md')
    from .replay_readiness import update_index
    update_index(root, receipt)
    return receipt
