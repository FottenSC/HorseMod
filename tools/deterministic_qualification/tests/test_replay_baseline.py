"""Exercise the actual G0 runner/evidence/status boundary with controlled OS inputs."""
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace

import pytest

from tools.deterministic_qualification import replay_baseline as baseline
from tools.deterministic_qualification import replay_readiness as readiness
from tools.deterministic_qualification import source_retention as sources
from tools.deterministic_qualification import process_control
from tools.deterministic_qualification.artifacts import sha256_file
from tools.deterministic_qualification.replay_evidence import retain_run, store_bytes, store_file
from tools.deterministic_qualification.replay_local import MAP, select_tests
from tools.deterministic_qualification.replay_outcomes import check, failure_view
from tools.deterministic_qualification.run_resources import RunResources, deployment_journal_path

pytestmark = pytest.mark.workflow


@pytest.fixture
def g0(tmp_path, monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    import replay_test as runner
    import deterministic_iteration
    from deterministic_qualification import source_retention as runner_sources
    from deterministic_qualification import process_control as runner_process_control
    from deterministic_qualification.run_resources import RunResources as RunnerRunResources
    root = tmp_path / 'checkout'
    root.mkdir()
    subprocess.run(['git', 'init', str(root)], check=True, capture_output=True)
    native_source = root / 'HorseMod/native.cpp'
    policy = root / 'HorseMod/horselib/deterministic/ReplayStatePolicy.hpp'
    reporting = root / 'tools/replay_test.py'
    for path, data in ((native_source, b'native implementation'), (policy, b'policy version 1'),
                       (reporting, b'reporting implementation')):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    subprocess.run(['git', 'add', '.'], cwd=root, check=True, capture_output=True)
    subprocess.run(['git', '-c', 'user.name=G0 fixture', '-c', 'user.email=test@invalid',
                    'commit', '-m', 'controlled source inputs'], cwd=root, check=True, capture_output=True)
    output = root / 'build/replay-tests'
    objects = output / 'evidence/objects'
    binaries = {}
    for role in baseline.ROLES:
        path = output.parent / (role + '.dll')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(('compiled ' + role).encode())
        binaries[role] = path
    game_root = root / 'installation/SC6/SoulcaliburVI/Binaries/Win64'
    game = game_root / 'SoulcaliburVI.exe'
    physics = game_root.parents[2] / 'Engine/Binaries/ThirdParty/PhysX/Win64/VS2015/PhysX3_x64.dll'
    windows = root / 'windows'
    replay = root / 'ReplayExample/REPLAY_12744704008398858106.bin'
    for path in (game, physics, windows / 'System32/ucrtbase.dll', replay):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(path.name.encode())
    monkeypatch.setenv('SystemRoot', str(windows))
    monkeypatch.delenv('HORSE_VFX_OBSERVER_RED_HEADER', raising=False)
    monkeypatch.setattr(deterministic_iteration, 'GAME_ROOT', game_root)
    monkeypatch.setattr(runner, 'ROOT', root)
    monkeypatch.setattr(runner, 'BUILD', output.parent)
    monkeypatch.setattr(runner, 'OUTPUT', output)
    monkeypatch.setattr(runner, 'GAME_ROOT', game_root)
    monkeypatch.setattr(runner, 'DEFAULT_REPLAY', replay)
    monkeypatch.setattr(runner, 'build_binary_paths', lambda: binaries)
    # Only the configured compiler dependency inventory and OS process service
    # are substituted. Source retention, provenance, aggregation, status and
    # journal restoration are production code, with real hashed file payloads.
    graph = dict(roots={'workspace': str(root)}, commands='controlled compiler recipe')
    monkeypatch.setattr(sources, 'build_inputs', lambda *_: ({}, graph))
    monkeypatch.setattr(process_control, 'list_game_processes', lambda: ())
    # The CLI imports the script package; tests use the repository package.
    # Forward fixture services so later fault injections reach both copies.
    monkeypatch.setattr(runner_sources, 'build_inputs', lambda *args: sources.build_inputs(*args))
    monkeypatch.setattr(runner_process_control, 'list_game_processes', lambda: process_control.list_game_processes())
    retained = sources.retain_sources(root, output / 'source-archives', output.parent)
    fingerprint = sources.workspace_fingerprint(root)
    proven = dict(schema=1, source_retention=retained, workspace_fingerprint=fingerprint,
        binaries={k: dict(path=str(p), sha256=sha256_file(p)) for k, p in binaries.items()},
        build_log=store_bytes(objects, b'controlled compiler success', '.log'),
        state_policy=dict(version=1, manifest=store_file(objects, policy)))
    (output / 'build-provenance.json').write_text(json.dumps(proven))
    identity = {k: proven['binaries'][k]['sha256'] for k in baseline.ROLES[:3]}
    native_log = store_bytes(objects, b'controlled CTest and physics-marker success', '.log')
    build = dict(stage='build', result='pass', certifying=False, build_provenance=proven,
        workspace_fingerprint=fingerprint, build_identities=identity,
        test_evidence=dict(native_log, result='pass', binaries=proven['binaries'], source_retention=retained,
                           native_fixture_inputs=dict(game=sha256_file(game), physics=sha256_file(physics))))
    build_ref = retain_run(output / 'evidence', build)
    local_sources = sources.retain_sources(root, output / 'source-archives')
    local = dict(stage='local', result='pass', groups=['all'], layer='all', certifying=True,
        workspace_fingerprint=fingerprint, build_identities=identity, state_policy_sha256=sha256_file(policy),
        source_retention=local_sources, command=[sys.executable, '-m', 'pytest', '-q', '-x', *select_tests('all')[0]],
        test_layers=dict(requested='all', exit_code=0, selected={'unit': 1, 'workflow': 1, 'native-contract': 1}),
        outcome_schema=1, checks=[check('integration.full', 'observer_validity', 'pass', 'controlled full execution')],
        gate_evidence={'G3': dict(result='pass')}, raw_log=store_bytes(objects, b'3 passed', '.log'),
        native=[dict(command=c, result='pass', binaries=proven['binaries'],
                     raw_log=store_bytes(objects, b'controlled native command success', '.log'))
                for c in json.loads(MAP.read_bytes())['native_groups']['all']])
    local_ref = retain_run(output / 'evidence', local)
    deployed = game_root / 'ue4ss/Mods/HorseMod/dlls/main.dll'
    deployed.parent.mkdir(parents=True)
    deployed.write_bytes(b'predecessor runtime')
    owner = RunResources(deployment_journal_path(deployed))
    owner.begin('g0-fixture')
    owner.prepare_file(deployed, ['candidate'])
    owner.restore()
    def forbidden(*args, **kwargs):
        pytest.fail('G0 certification must not build, test, recover or deploy')
    monkeypatch.setattr(runner, 'build', forbidden)
    monkeypatch.setattr(runner, 'ensure_build', forbidden)
    monkeypatch.setattr(runner, 'run_local_regressions', forbidden)
    monkeypatch.setattr(RunResources, 'recover', forbidden)
    monkeypatch.setattr(RunResources, 'restore', forbidden)
    monkeypatch.setattr(RunnerRunResources, 'recover', forbidden)
    monkeypatch.setattr(RunnerRunResources, 'restore', forbidden)
    from deterministic_qualification import replay_local, replay_run
    monkeypatch.setattr(replay_local, 'run_local', forbidden)
    monkeypatch.setattr(replay_run.ReplayRun, '__enter__', forbidden)
    return SimpleNamespace(runner=runner, root=root, output=output, binaries=binaries, proven=proven, build=build, local=local,
        build_ref=build_ref, local_ref=local_ref, deployed=deployed, owner=owner,
        native_source=native_source, reporting=reporting, policy=policy, physics=physics)


def certify(g0, monkeypatch):
    monkeypatch.setattr(sys, 'argv', ['replay_test.py', 'certify-g0', '--output', 'full'])
    code = g0.runner.main()
    reference = baseline._latest(g0.output, 'certify-g0')
    return code, json.loads(Path(reference['path']).read_bytes())['summary'], reference


def status(g0):
    from tools.deterministic_qualification.replay_run import inspect_deployment
    return readiness.readiness(g0.output / 'evidence', readiness.current_context(g0.root, g0.output),
                               inspect_deployment(g0.deployed))


def test_g0_runner_certifies_only_aggregate_and_status_revalidates_it(g0, monkeypatch):
    originals = {r['path']: Path(r['path']).read_bytes() for r in (g0.build_ref, g0.local_ref)}
    before = status(g0)
    assert before['readiness']['G3']['result'] == 'pass'
    assert before['readiness']['G0']['result'] == 'open'
    code, summary, reference = certify(g0, monkeypatch)
    assert code == 0 and summary['certifying'] is True
    assert [c['id'] for c in summary['checks']] == list(baseline.CHECKS)
    assert summary['gate_evidence'] == {'G0': {'result': 'pass'}}
    assert status(g0)['readiness']['G0']['evidence'] == reference['path']
    assert all(Path(p).read_bytes() == data for p, data in originals.items())
    # This is a fresh live inventory on status, not just a previously clean
    # journal or a self-declared cleanup pass in the aggregate.
    monkeypatch.setattr(process_control, 'list_game_processes', lambda: (process_control.GameProcess(123, ''),))
    assert status(g0)['readiness']['G0']['result'] != 'pass'


def test_g0_reporting_edit_requires_new_full_local_receipt_but_can_reuse_native_build(g0, monkeypatch):
    original_build = Path(g0.build_ref['path']).read_bytes()
    g0.reporting.write_bytes(b'updated reporting implementation')
    code, summary, _ = certify(g0, monkeypatch)
    assert code == 1 and failure_view(summary)['check'] == 'integration.native'
    local = dict(g0.local, workspace_fingerprint=sources.workspace_fingerprint(g0.root),
                 source_retention=sources.retain_sources(g0.root, g0.output / 'source-archives'))
    retain_run(g0.output / 'evidence', local)
    code, summary, _ = certify(g0, monkeypatch)
    assert code == 0 and summary['certifying'] is True
    assert status(g0)['readiness']['G0']['result'] == 'pass'
    assert Path(g0.build_ref['path']).read_bytes() == original_build


@pytest.mark.parametrize('defect,expected', [
    ('missing-build', 'build.source_qualified'), ('missing-provenance', 'build.source_qualified'),
    ('native-source', 'build.source_qualified'), ('binary', 'build.source_qualified'),
    ('policy', 'build.source_qualified'), ('missing-local', 'integration.native'),
    ('stale-local', 'integration.native'), ('filtered-local', 'integration.native'),
    ('missing-native', 'integration.native'), ('stale-native', 'integration.native'),
    ('native-payload', 'integration.native'), ('native-fixture', 'integration.native'),
    ('newer-selected-failure', 'integration.native'),
    ('missing-journal', 'cleanup.deployment'), ('active-journal', 'cleanup.deployment'),
    ('queued-launch', 'cleanup.deployment'), ('deployment', 'cleanup.deployment'),
    ('backup', 'cleanup.deployment'), ('live-process', 'cleanup.deployment'),
    ('process-unknown', 'cleanup.deployment'),
])
def test_g0_first_unmet_check_cannot_emit_certifying_gate_evidence(g0, monkeypatch, defect, expected):
    if defect == 'missing-build':
        Path(g0.build_ref['path']).unlink()
    elif defect == 'missing-provenance':
        (g0.output / 'build-provenance.json').unlink()
    elif defect in ('native-source', 'binary', 'policy', 'stale-local', 'native-fixture'):
        path = {'native-source': g0.native_source, 'binary': g0.binaries['runtime'], 'policy': g0.policy,
                'stale-local': g0.reporting, 'native-fixture': g0.physics}[defect]
        path.write_bytes(b'changed input')
    elif defect == 'missing-local':
        Path(g0.local_ref['path']).unlink()
    elif defect in ('filtered-local', 'missing-native', 'stale-native'):
        local = json.loads(json.dumps(g0.local))
        if defect == 'filtered-local':
            local['command'].extend(['--replay-layer', 'workflow'])
        elif defect == 'missing-native':
            local['native'].pop()
        else:
            local['native'][0]['binaries']['native_test']['sha256'] = 'stale'
        retain_run(g0.output / 'evidence', local)
    elif defect == 'native-payload':
        Path(g0.local['native'][0]['raw_log']['path']).write_bytes(b'corrupt native log')
    elif defect == 'newer-selected-failure':
        local = dict(g0.local, result='fail', groups=['explicit'], certifying=False, gate_evidence={},
                     checks=[check('integration.selected', 'observer_validity', 'fail', 'controlled failure')])
        retain_run(g0.output / 'evidence', local)
    elif defect == 'missing-journal':
        g0.owner.path.unlink()
    elif defect in ('active-journal', 'queued-launch'):
        if defect == 'active-journal':
            g0.owner.document['state'] = 'cleanup_pending'
        else:
            g0.owner.document['launches'] = [dict(delivery='pending')]
        g0.owner.save()
    elif defect == 'deployment':
        g0.deployed.write_bytes(b'unrestored candidate')
    elif defect == 'backup':
        Path(g0.owner.document['files'][0]['backup']).unlink()
    elif defect == 'live-process':
        monkeypatch.setattr(process_control, 'list_game_processes', lambda: (process_control.GameProcess(123, ''),))
    elif defect == 'process-unknown':
        def unavailable():
            raise OSError('OS inventory unavailable')
        monkeypatch.setattr(process_control, 'list_game_processes', unavailable)
    code, summary, _ = certify(g0, monkeypatch)
    assert code == 1 and summary['result'] == 'blocked' and summary['certifying'] is False
    assert summary['gate_evidence'] == {}
    assert failure_view(summary)['check'] == expected
    assert summary['checks'][-1]['outcome'] == 'blocked'
    assert status(g0)['readiness']['G0']['result'] != 'pass'


@pytest.mark.parametrize('defect', ['workspace', 'binary', 'policy', 'native-test', 'native-fixture',
                                   'local-payload', 'deployment', 'journal', 'aggregate-only'])
def test_g0_status_does_not_trust_stale_or_label_only_aggregate(g0, monkeypatch, defect):
    code, summary, _ = certify(g0, monkeypatch)
    assert code == 0
    if defect == 'aggregate-only':
        # Remove real receipts from the candidate set and retain a label-only
        # forgery. All three labels pass; the aggregate contract is absent.
        for path in (g0.output / 'evidence/manifests').glob('*.json'):
            if json.loads(path.read_bytes()).get('summary', {}).get('stage') == baseline.STAGE:
                path.unlink()
        summary.pop('g0_evidence')
        retain_run(g0.output / 'evidence', summary)
    elif defect == 'journal':
        g0.owner.document['run_id'] = 'new-run'
        g0.owner.save()
    else:
        path = {'workspace': g0.reporting, 'binary': g0.binaries['runtime'], 'policy': g0.policy,
                'native-test': g0.binaries['native_test'], 'native-fixture': g0.physics,
                'local-payload': Path(g0.local['native'][0]['raw_log']['path']), 'deployment': g0.deployed}[defect]
        path.write_bytes(b'changed after certification')
    assert status(g0)['readiness']['G0']['result'] != 'pass'
