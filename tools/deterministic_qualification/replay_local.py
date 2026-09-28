"""Explicit production-boundary test selection; no synthetic replacement harness."""
from __future__ import annotations
from .replay_timing import timed

import fnmatch
import json
from pathlib import Path
import subprocess
import sys
import time
import os
import uuid

from .replay_evidence import store_bytes

MAP = Path(__file__).with_name('replay_test_groups.json')
LAYERS = ('all', 'unit', 'workflow', 'native-contract')


def explicit_tests(root, identifiers):
    """Accept test files/node IDs, never pytest options or unrelated paths."""
    root = Path(root).resolve()
    allowed = root / 'tools/deterministic_qualification/tests'
    result = []
    for identifier in identifiers:
        path, separator, node = identifier.replace('\\', '/').partition('::')
        resolved = (root / path).resolve()
        if (not resolved.is_relative_to(allowed) or not resolved.is_file()
                or not resolved.name.startswith('test_') or resolved.suffix != '.py'
                or (separator and (not node.startswith('test_') or '\n' in node))):
            raise ValueError('expected a qualification test file or test node: ' + identifier)
        result.append(resolved.relative_to(root).as_posix() + (separator + node if separator else ''))
    return sorted(set(result))


def select_tests(group, changed=(), mapping=None):
    mapping = mapping or json.loads(MAP.read_text())
    groups = [group]
    if group == 'auto':
        groups = []
        for path in changed:
            normalized = path.replace('\\', '/')
            matched = [row for row in mapping['source_areas'] if fnmatch.fnmatchcase(normalized, row['pattern'])]
            # Keep explicitly mapped dependents of shared test helpers too.
            if normalized.startswith('tools/deterministic_qualification/tests/test_') and normalized.endswith('.py'):
                groups.append('test-file:' + normalized)
                groups.extend(g for row in matched for g in row['groups'])
                continue
            if not matched:
                groups = ['all']
                break
            groups.extend(g for row in matched for g in row['groups'])
        if not groups:
            groups = ['all']  # unknown change set cannot authorize a narrow pass
    if 'all' in groups:
        return mapping['all'], ['all']
    return sorted({test for name in groups for test in
                   ([name.removeprefix('test-file:')] if name.startswith('test-file:') else mapping['groups'][name])}), sorted(set(groups))


def changed_paths(root):
    paths = []
    for args in (['diff', '--name-only', 'HEAD', '-z'], ['ls-files', '--others', '--exclude-standard', '-z']):
        raw = subprocess.run(['git', *args], cwd=root, check=True, capture_output=True).stdout
        paths.extend(p.decode('utf-8', errors='surrogateescape') for p in raw.split(b'\0') if p)
    return paths


def _require_current_observer_header():
    # Reject before source retention, subprocess launch AND pass-cache lookup.
    # Historical REDs carry their own immutable source snapshots. No alternate
    # header may acquire the current workspace's integration identity.
    if os.environ.get('HORSE_VFX_OBSERVER_RED_HEADER'):
        raise RuntimeError('HORSE_VFX_OBSERVER_RED_HEADER: alternate observer header cannot qualify current source')


@timed('integration')
def run_integration(root, output):
    """Reuse only a retained full Python pass on the exact current inputs.

    Native checks remain the live runner's responsibility on every launch.
    Corrupt evidence blocks reuse rather than silently replacing that evidence.
    """
    _require_current_observer_header()
    from importlib.metadata import version
    from .artifacts import sha256_file
    from .source_retention import workspace_fingerprint, verify_retention
    from .run_journal import atomic_json
    command = [sys.executable, '-m', 'pytest', '-q', '-x', *select_tests('all')[0]]
    key = dict(workspace_fingerprint=workspace_fingerprint(root), command=command,
               python=sys.version, interpreter_sha256=sha256_file(Path(sys.executable)),
               pytest=version('pytest'))
    provenance=output/'build-provenance.json'
    if provenance.is_file():key['build_provenance_sha256']=sha256_file(provenance)
    index = output / 'integration-receipt.json'
    if index.is_file():
        reference = json.loads(index.read_bytes())
        path = Path(reference['path'])
        if sha256_file(path) != reference['sha256']:
            raise RuntimeError('retained integration receipt changed')
        envelope = json.loads(path.read_bytes())
        receipt = envelope['receipt']
        if (envelope['key'] == key and receipt.get('result') == 'pass'
                and receipt.get('groups') == ['all'] and receipt.get('command') == command
                and receipt.get('workspace_fingerprint') == key['workspace_fingerprint']):
            verify_retention(receipt['source_retention'])
            if sha256_file(Path(receipt['raw_log']['path'])) != receipt['raw_log']['sha256']:
                raise RuntimeError('retained integration log changed')
            if workspace_fingerprint(root) != key['workspace_fingerprint']:
                raise RuntimeError('test sources changed during integration receipt verification')
            print('integration: reusing verified full-suite receipt', flush=True)
            return dict(receipt, reused=True, retained_receipt=reference)
    receipt = run_local(root, output, 'all', run_native=False)
    if receipt.get('result') == 'pass':
        if receipt.get('workspace_fingerprint') != key['workspace_fingerprint']:
            raise RuntimeError('test sources changed before integration execution')
        reference = store_bytes(output / 'evidence/objects',
            json.dumps(dict(key=key, receipt=receipt), sort_keys=True).encode('utf-8'))
        atomic_json(index, reference)
        return dict(receipt, reused=False, retained_receipt=reference)
    return receipt


@timed('local')
def run_local(root, output, group, changed=(), list_only=False, run_native=True, *, test_ids=(), layer='all'):
    _require_current_observer_header()
    if layer not in LAYERS:
        raise ValueError('unknown local test layer: ' + layer)
    tests, selected = (explicit_tests(root, test_ids), ['explicit']) if test_ids else select_tests(
        group, changed_paths(root) if group == 'auto' and not changed else changed)
    mapping=json.loads(MAP.read_text())
    exclusions=[test for name in selected for test in mapping.get('known_blockers',{}).get(name,{}).get('excluded_tests',[])]
    command = [sys.executable, '-m', 'pytest', '-q', '-x', *tests,*['--deselect='+test for test in exclusions]]
    if layer != 'all':
        command += ['--replay-layer', layer]
    if list_only:
        mapping = json.loads(MAP.read_text())
        return dict(result='selected', groups=selected, command=command, tests=tests,
                    layer=layer, certifying=False, scope='local test selection only; no tests executed',
                    native_commands=[c for name in selected for c in mapping.get('native_groups', {}).get(name, [])]
                        if layer in ('all', 'native-contract') else [])
    from .source_retention import workspace_fingerprint, retain_sources, verify_retention
    before = workspace_fingerprint(root)
    index = output / 'source-archives' / ('local-' + before + '.json')
    if index.is_file():
        retained_sources = json.loads(index.read_bytes())
        verify_retention(retained_sources)
        retention_cache=dict(status='hit',reason='matching workspace identity and verified snapshot',new_payload_bytes=0)
    else:
        retained_sources = retain_sources(root, output / 'source-archives')
        if workspace_fingerprint(root) != before:
            raise RuntimeError('test sources changed during retention')
        from .run_journal import atomic_json
        atomic_json(index, retained_sources)
        retention_cache=dict(status='miss',reason='workspace identity has no retained snapshot',**retained_sources.get('storage',{}))
    started = time.monotonic()
    fixture_receipts = output / 'fixture-receipts' / uuid.uuid4().hex
    environment = dict(os.environ, HORSE_FIXTURE_RECEIPTS=str(fixture_receipts))
    from .replay_timing import span
    with span('fixture_execution') as execution:
        run = subprocess.run(command, cwd=root, capture_output=True, text=True, env=environment)
        if run.returncode:execution['outcome']='fail'
    cache_rows = [json.loads(p.read_bytes()) for p in sorted(fixture_receipts.glob('*.json'))]
    from .replay_timing import ACTIVE
    if ACTIVE.get():
        for row in cache_rows:ACTIVE.get().external_fixture(row,execution.get('id'))
    raw = (run.stdout + run.stderr).encode('utf-8')
    print(raw.decode('utf-8'), end='', flush=True)
    native_receipts = []
    if workspace_fingerprint(root) != before:
        run.returncode = 1
        native_receipts.append(dict(result='fail', failure='test sources changed during local execution'))
    if run.returncode == 0 and run_native and layer in ('all', 'native-contract'):
        mapping = json.loads(MAP.read_text())
        native_commands = sorted({tuple(c) for name in selected for c in mapping.get('native_groups', {}).get(name, [])})
        if native_commands:
            try:
                from .source_retention import require_build_provenance
                build = output.parent
                binaries = {name: build / 'HorseMod' / name for name, *_ in native_commands}
                # Existing provenance uses these public binary roles.
                proven = require_build_provenance(root, build, {
                    'core_test' if name == 'DeterministicCoreSelfTest.exe' else 'native_test': path
                    for name, path in binaries.items()})
                for name, *args in native_commands:
                    checked = subprocess.run([str(binaries[name]), *args], cwd=root, capture_output=True, text=True)
                    native_receipts.append(dict(command=[name, *args], result='pass' if checked.returncode == 0 else 'fail',
                        binaries=proven['binaries'], raw_log=store_bytes(output/'evidence/objects',
                            (checked.stdout+checked.stderr).encode(), '.log')))
                    if checked.returncode:
                        run.returncode = checked.returncode
                        break
            except (OSError, ValueError, KeyError, RuntimeError) as error:
                run.returncode = 1
                native_receipts.append(dict(result='blocked', failure=str(error), next_step='python tools/replay_test.py build'))
    from .replay_readiness import current_context
    identity=current_context(root,output)
    from .replay_outcomes import check
    full = selected == ['all'] and layer == 'all' and not exclusions and not test_ids
    checks=[check('integration.full' if full else 'integration.selected','observer_validity',
        'pass' if run.returncode==0 else 'fail','requested production-boundary local tests',evidence_field='raw_log')]
    blockers=json.loads(MAP.read_bytes()).get('known_blockers',{})
    layer_path = fixture_receipts.with_suffix('.layers.json')
    layers = json.loads(layer_path.read_bytes()) if layer_path.is_file() else {}
    return dict(result='pass' if run.returncode == 0 else 'fail', groups=selected, native=native_receipts,
                layer=layer, test_layers=layers, scope='local Python and compiled native-contract checks; no live game',
                outcome_schema=1,checks=checks,retention_cache=retention_cache,
                known_blockers={name:blockers[name] for name in selected if name in blockers},
                certifying=full and run.returncode==0,
                gate_evidence={'G3':dict(result='pass' if run.returncode==0 else 'fail')} if full else {},
                build_identities=identity.get('build_identities'),state_policy_sha256=identity.get('state_policy'),
                source_retention=retained_sources, workspace_fingerprint=before,
                command=command, elapsed_seconds=round(time.monotonic()-started, 3),
                fixture_cache=dict(invocations=cache_rows, hits=sum(r['status']=='hit' for r in cache_rows),
                    compiled=sum(r.get('compiled',r['status']=='bypass') for r in cache_rows),
                    compilation_lookup_seconds=sum(r['elapsed_seconds'] for r in cache_rows),
                    execution_reused=False),
                raw_log=store_bytes(output / 'evidence' / 'objects', raw, '.log'), game_launched=False)
