"""G0 aggregation of retained executions and a fresh, read-only cleanup witness.

No builds, tests, recovery, deployment or process control occur in this stage.
The build's existing native dependency verifier permits reporting-only edits;
the full local receipt must still cover the exact current workspace.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

import psutil

from .artifacts import sha256_file
from .replay_evidence import store_bytes, store_file
from .replay_outcomes import check
from . import source_retention as sources

STAGE = 'certify-g0'
ROLES = ('runtime', 'observer', 'framework', 'core_test', 'native_test')
CHECKS = ('build.source_qualified', 'integration.native', 'cleanup.deployment')
ERRORS = (OSError, ValueError, KeyError, TypeError, AttributeError, RuntimeError,
          subprocess.SubprocessError, psutil.Error)


def _require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def _payload(reference):
    path = Path(reference['path'])
    raw = path.read_bytes()
    _require(hashlib.sha256(raw).hexdigest() == reference['sha256'],
             'evidence payload hash mismatch: ' + str(path))
    return raw


def _manifest(reference, stage):
    from .replay_readiness import index_entry, verify_references
    _payload(reference)
    row = index_entry(Path(reference['path']))
    _require(row['stage'] == stage and row['result'] == 'pass', stage + ' receipt did not pass')
    verify_references(row, set())
    return json.loads(_payload(reference))['summary']


def _latest(output, stage, *, full=False):
    candidates = []
    for path in (output / 'evidence/manifests').glob('*.json'):
        document = json.loads(path.read_bytes())
        summary = document.get('summary', {})
        if summary.get('stage') != stage:
            continue
        if full and not (summary.get('groups') == ['all'] and summary.get('layer') == 'all'):
            continue
        candidates.append((document.get('completed_ns', path.stat().st_mtime_ns), str(path), summary))
    _require(candidates, 'missing ' + ('full local' if full else stage) + ' immutable receipt')
    _, name, _ = max(candidates, key=lambda row: row[:2])
    path = Path(name)
    return dict(path=str(path.resolve()), sha256=sha256_file(path))


def _require_no_newer_local_failure(output, selected, context):
    from .replay_readiness import compatibility, index_entry
    boundary = index_entry(Path(selected['path']))['completed_ns']
    for path in (output / 'evidence/manifests').glob('*.json'):
        document = json.loads(path.read_bytes())
        summary = document.get('summary', {})
        if (summary.get('stage') == 'local' and summary.get('result') in ('fail', 'blocked')
                and document.get('completed_ns', path.stat().st_mtime_ns) > boundary):
            row = index_entry(path)
            _require(compatibility(row, context) != 'current',
                     'newer current local failure must be resolved by a full pass: ' + str(path))


def _binary_hashes(provenance):
    return {role: provenance['binaries'][role]['sha256'] for role in ROLES}


def _verify_build(summary):
    evidence = summary['g0_evidence']
    build = _manifest(evidence['build'], 'build')
    proven = json.loads(_payload(evidence['provenance']))
    _require(proven.get('schema') == 1 and build.get('build_provenance') == proven,
             'build receipt does not bind the verified provenance')
    hashes = _binary_hashes(proven)
    _require(all(hashes.values()) and {k: hashes[k] for k in ROLES[:3]} == summary['build_identities']
             == build.get('build_identities'), 'build binary identities disagree')
    policy = proven['state_policy']['manifest']
    _payload(policy)
    _require(policy['sha256'] == summary['state_policy_sha256'], 'build state-policy identity is stale')
    _payload(proven['build_log'])
    return build, proven


def _verify_integration(summary, build, proven):
    from .replay_local import MAP, select_tests
    from .replay_readiness import compatibility, index_entry
    native = build.get('test_evidence') or {}
    _require(native.get('result') == 'pass' and native.get('binaries') == proven['binaries']
             and native.get('source_retention') == proven['source_retention'],
             'build native checks lack matching binary/source evidence')
    _payload(native)
    _require(native.get('native_fixture_inputs') == summary['native_fixture_inputs'],
             'native game/PhysX fixture identities are missing or stale')
    local_ref = summary['g0_evidence']['local']
    local = _manifest(local_ref, 'local')
    context = dict(workspace_fingerprint=summary['workspace_fingerprint'],
                   build_identities=summary['build_identities'], state_policy=summary['state_policy_sha256'])
    _require(compatibility(index_entry(Path(local_ref['path'])), context) == 'current',
             'full local receipt is stale for current sources, binaries or policy')
    _require(local.get('certifying') is True and local.get('groups') == ['all']
             and local.get('layer') == 'all' and not local.get('known_blockers')
             and local.get('gate_evidence', {}).get('G3', {}).get('result') == 'pass'
             and any(c.get('id') == 'integration.full' and c.get('outcome') == 'pass'
                     for c in local.get('checks', []))
             and local.get('command') == [sys.executable, '-m', 'pytest', '-q', '-x', *select_tests('all')[0]],
             'local evidence is not a full unfiltered integration pass')
    layers = local.get('test_layers', {})
    _require(layers.get('requested') == 'all' and layers.get('exit_code') == 0
             and all(layers.get('selected', {}).get(k, 0) > 0 for k in ('unit', 'workflow', 'native-contract')),
             'full local receipt lacks successful coverage of all three layers')
    sources.verify_retention(local['source_retention'])
    # Bind the claimed workspace identity to the actual retained file inventory.
    inventory = sources.read_manifest(local['source_retention'])
    rows = sorted((r['path'], None if r.get('deleted') else r['sha256']) for r in inventory['files'])
    _require(hashlib.sha256(json.dumps(rows, separators=(',', ':')).encode()).hexdigest()
             == summary['workspace_fingerprint'], 'local source archive does not bind its workspace identity')
    _payload(local['raw_log'])
    expected = sorted(tuple(c) for c in json.loads(MAP.read_bytes())['native_groups']['all'])
    actual = local.get('native', [])
    _require(sorted(tuple(r.get('command', [])) for r in actual) == expected,
             'full local receipt is missing required shipped native commands')
    for row in actual:
        _require(row.get('result') == 'pass' and row.get('binaries') == proven['binaries'],
                 'local native check failed or used different binaries')
        _payload(row['raw_log'])
    return local


def observe_cleanup(deployed):
    """Do not interpret a clean journal, missing journal or timeout as process exit."""
    from .run_resources import RunResources, deployment_journal_path
    from .process_control import list_game_processes
    path = deployment_journal_path(deployed)
    before = path.read_bytes()  # Missing journal is unknown, never an implicit clean baseline.
    owner = RunResources(path)
    doc = owner.document
    _require(doc.get('state') == 'clean' and doc.get('run_id'),
             'deployment journal is not a recorded clean run: ' + str(path))
    _require(not owner.pending_launches(), 'deployment has an unresolved queued launch')
    _require(all(r.get('delivery') in ('direct', 'observed', 'not_started') for r in doc.get('launches', [])),
             'deployment has an unknown launch disposition')
    _require(any(Path(r['path']).resolve() == Path(deployed).resolve() for r in doc['files']),
             'journal does not cover the runtime deployment')
    files = []
    for row in doc['files']:
        target = Path(row['path'])
        actual = sha256_file(target) if target.is_file() else None
        _require(actual == row['before'] and (actual is not None or not target.exists()),
                 'deployment predecessor is not restored: ' + str(target))
        backup = sha256_file(Path(row['backup'])) if row.get('backup') else None
        _require(backup == row['before'], 'deployment recovery backup is missing or stale: ' + str(target))
        files.append(dict(path=str(target), sha256=actual, backup_sha256=backup))
    absent = list(doc.get('temporaries', [])) + list(doc.get('directories', []))
    for root in doc['requests']:
        absent.extend(str(Path(root) / name) for name in ('online_request.txt', 'online_request.publish.tmp',
            'online_room_request.txt', 'online_room_request.txt.tmp', 'replay_request.txt', 'replay_request.tmp', 'replay_result.txt'))
    for name in absent:
        _require(not Path(name).exists(), 'deployment cleanup artifact remains: ' + name)
    owned = owner.live_processes(kind=None)
    _require(not owned, 'journal-owned processes remain: ' + str([p.pid for p in owned]))
    games = list_game_processes()  # Independent OS inventory, including unowned game processes.
    _require(not games, 'live Soulcalibur processes remain: ' + str([p.pid for p in games]))
    for row in doc['files']:
        target = Path(row['path'])
        _require((sha256_file(target) if target.is_file() else None) == row['before']
                 and (row['before'] is not None or not target.exists()),
                 'deployment changed during process observation: ' + str(target))
    _require(before == path.read_bytes() and json.loads(before) == doc,
             'deployment journal changed during cleanup observation')
    return dict(schema=1, observed_ns=time.time_ns(), deployed=str(Path(deployed).resolve()),
                journal_path=str(path), journal_sha256=hashlib.sha256(before).hexdigest(), journal=doc,
                files=files, absent=absent, owned_processes=[], game_processes=[],
                process_source='independent OS inventory: process_control.list_game_processes',
                result='pass', observational=True)


def _verify_cleanup(summary, deployed, run_id):
    snapshot = json.loads(_payload(summary['g0_evidence']['cleanup']))
    _require(snapshot.get('schema') == 1 and snapshot.get('result') == 'pass'
             and snapshot.get('observed_ns', 0) > 0 and snapshot.get('observational') is True
             and snapshot.get('journal', {}).get('state') == 'clean'
             and snapshot.get('owned_processes') == [] and snapshot.get('game_processes') == []
             and snapshot.get('process_source') == 'independent OS inventory: process_control.list_game_processes',
             'G0 cleanup lacks an independent process disposition')
    _require(Path(snapshot['deployed']).resolve() == Path(deployed).resolve()
             and snapshot['journal'].get('run_id') == run_id,
             'G0 cleanup belongs to a different deployment or journal run')
    current = observe_cleanup(Path(deployed))
    _require({k: v for k, v in current.items() if k != 'observed_ns'} ==
             {k: v for k, v in snapshot.items() if k != 'observed_ns'},
             'deployment disposition changed since G0 certification')


def _verify_g0(summary, current, deployment):
    """Status verifies the aggregate contract, not just three passing check labels."""
    _require(summary.get('stage') == STAGE and summary.get('result') == 'pass'
             and summary.get('certifying') is True and summary.get('g0_evidence', {}).get('schema') == 1,
             'G0 requires a certifying aggregate receipt')
    build, proven = _verify_build(summary)
    _verify_integration(summary, build, proven)
    _require(current.get('native_fixture_inputs') == summary['native_fixture_inputs'],
             'G0 native fixture identities are missing or stale')
    # Also recheck configured dependency inputs (including inputs outside the
    # workspace fingerprint) and both shipped native test executables.
    verified = sources.require_build_provenance(Path(current['source_root']), Path(current['build_directory']),
        {k: Path(proven['binaries'][k]['path']) for k in ROLES})
    _require(verified == proven, 'G0 build provenance changed')
    _require(deployment.get('state') == 'clean', 'current deployment journal is not clean')
    _verify_cleanup(summary, current['deployed_runtime'], deployment.get('run_id'))


def verify_g0(summary, current, deployment):
    try:
        _verify_g0(summary, current, deployment)
    except ERRORS as error:
        raise RuntimeError('invalid G0 evidence: ' + str(error)) from error


def certify_g0(root, output, binaries, deployed):
    from .replay_local import _require_current_observer_header
    from .replay_readiness import current_context
    summary = dict(stage=STAGE, result='blocked', certifying=False, outcome_schema=1, checks=[],
                   gate_evidence={}, g0_evidence=dict(schema=1), game_launched=False,
                   scope='G0 baseline only: retained source/build/native/local evidence and observed cleanup')
    identifier = CHECKS[0]
    try:
        _require_current_observer_header()
        context = current_context(root, output)
        summary.update(workspace_fingerprint=context['workspace_fingerprint'],
                       build_identities=context.get('build_identities'), state_policy_sha256=context.get('state_policy'),
                       native_fixture_inputs=context.get('native_fixture_inputs'))
        proven = sources.require_build_provenance(root, output.parent, binaries)
        evidence = summary['g0_evidence']
        evidence['build'] = _latest(output, 'build')
        evidence['provenance'] = store_file(output / 'evidence/objects', output / 'build-provenance.json')
        build, recorded = _verify_build(summary)
        _require(recorded == proven, 'build provenance changed during verification')
        summary['checks'].append(check(identifier, 'observer_validity', 'pass', 'current source-qualified build',
                                       evidence=evidence['build']))
        identifier = CHECKS[1]
        _require(summary['native_fixture_inputs'] and all(summary['native_fixture_inputs'].values()),
                 'current native game/PhysX fixture identities are unavailable')
        evidence['local'] = _latest(output, 'local', full=True)
        local = _verify_integration(summary, build, proven)
        _require_no_newer_local_failure(output, evidence['local'], context)
        summary['source_retention'] = local['source_retention']
        summary['checks'].append(check(identifier, 'observer_validity', 'pass',
            'full unfiltered local suite and required shipped native checks', evidence=evidence['local']))
        # Recheck identities before observing deployment; a moving checkout or
        # rebuilt binary cannot inherit a pass from the beginning of this command.
        identifier = CHECKS[0]
        _require(sources.require_build_provenance(root, output.parent, binaries) == proven,
                 'build provenance changed during certification')
        _require(current_context(root, output) == context, 'source/binary/policy identities changed during certification')
        identifier = CHECKS[2]
        observation = observe_cleanup(deployed)
        evidence['cleanup'] = store_bytes(output / 'evidence/objects', json.dumps(observation, sort_keys=True).encode())
        summary['checks'].append(check(identifier, 'cleanup', 'pass',
            'restored deployment/backups, settled launches and independent live process inventory', evidence=evidence['cleanup']))
        summary.update(result='pass', certifying=True, gate_evidence={'G0': dict(result='pass')})
    except ERRORS as error:
        summary['checks'].append(check(identifier, 'cleanup' if identifier == CHECKS[2] else 'observer_validity',
                                       'blocked', str(error)))
        summary['failure'] = identifier + ': ' + str(error)
    return summary
