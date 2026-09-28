"""Validated independent controls. Selection never consults candidate outcomes."""
from __future__ import annotations
from .replay_timing import timed

import hashlib
import json
import re
from pathlib import Path

from .artifacts import sha256_file
from .replay_evidence import store_bytes, store_file

FIELDS = {
    'flush_startup_loading': False, 'include_setup': False, 'skip_intros': False, 'no_async_loading_thread': False, 'no_native_threading': False, 'single_game_thread': False, 'changed_inputs': False,
    'corrected_inputs': False, 'source_revision': False, 'rolling_corrections': '', 'authored_prefix': '', 'source_revision_profile': 'historical',
    'pixel_diagnostics': False, 'pass_diagnostics': False, 'coherence_images': False,
    'full_match': False, 'observation_target': 0, 'record_index': False,
    'index_seek_continuation': 120,
}


def control_spec(args):
    root = Path(__file__).resolve().parents[2]
    # This small dependency set excludes offline evaluation/reporting code.
    paths = ['tools/deterministic_qualification/' + n for n in
             ('replay_control.py', 'replay_run.py', 'replay_entry.py', 'configuration.py',
              'process_control.py', 'run_resources.py', 'replay_controls_catalog.py')]
    code = {p: sha256_file(root / p) for p in paths}
    identities = {key: sha256_file(Path(getattr(args, attribute))) for key, attribute in
                  [('game','game_executable'),('framework','framework'),('observer','replay_mod'),
                   ('replay','replay'),('ucrt','ucrt'),('physx','physx')] if getattr(args, attribute, None)}
    settings = {k: getattr(args, k, default) for k, default in FIELDS.items()}
    settings.update(observations_requested=args.watch_frames, native_fixed_seed=True,
                    serial_particles=bool(settings['skip_intros']))
    profile = getattr(args, 'experiment_profile', None)
    return dict(schema=1, identities=identities, settings=settings, capture_code=code,
                diagnostics=getattr(args, 'diagnostic_settings', None),
                profile_sha256=profile['sha256'] if profile else None)


def key(spec):
    return hashlib.sha256(json.dumps(spec, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate_control(report, spec):
    if report.get('control_spec') != spec:
        raise RuntimeError('control compatibility specification differs')
    if report.get('mode') != 'stock' or report.get('result') != 'captured':
        raise RuntimeError('control is not a completed independent stock capture')
    if report.get('cleanup') != {'complete': True, 'games_remaining': 0}:
        raise RuntimeError('control cleanup incomplete')
    if report.get('loaded_runtime', {}).get('verification') != 'owned_process_runtime_absent':
        raise RuntimeError('control did not prove runtime absence')
    for name, value in spec['identities'].items():
        if report.get('identities', {}).get(name) != value:
            raise RuntimeError('control identity mismatch: ' + name)
    for name, value in spec['settings'].items():
        report_key = 'coherence_images_requested' if name == 'coherence_images' else name
        if report.get(report_key) != value:
            raise RuntimeError('control setting mismatch: ' + name)
    if spec['settings'].get('single_game_thread') and not report.get('single_game_thread_option_forwarded'):
        raise RuntimeError('single game thread option not verified')
    if spec['settings'].get('no_native_threading') and not report.get('native_threading_option_forwarded'):
        raise RuntimeError('native threading option not verified')
    if spec['settings'].get('no_async_loading_thread'):
        from .replay_control import validate_native_loading_state
        if not report.get('native_loading_option_forwarded'):
            raise RuntimeError('native loading option not verified')
        validate_native_loading_state(report.get('native_loading_state', {}))
    if spec['settings'].get('flush_startup_loading'):
        receipt=report.get('startup_loading_flush', {})
        if (receipt.get('native_callbacks') is not True or not 1<=receipt.get('calls',0)<=64 or receipt.get('first_tick',0)<1
                or receipt.get('registration_budget_ms')!=60000 or receipt.get('registration_restored') is not True
                or receipt.get('raw_asset_completion') is not True or not 1<=receipt.get('raw_asset_checks',0)<=65536
                or not re.fullmatch('[0-9a-f]{8}',receipt.get('registration_previous_bits',''))):
            raise RuntimeError('startup loading flush coverage unavailable')
    raw = report['raw_log']
    if sha256_file(Path(raw['path'])) != raw['sha256']:
        raise RuntimeError('control raw log changed')


@timed('control.retention')
def register_control(root, report, spec, startup=None):
    validate_control(report, spec)
    raw = store_file(root / 'objects', Path(report['raw_log']['path']), report['raw_log']['sha256'])
    frozen = dict(report, raw_log=raw)
    destination = root / key(spec)
    if startup is not None:
        if startup_signature(report, startup['through_tick']) != startup:
            raise RuntimeError('independent control startup incompatible; not a rollback divergence')
        frozen['startup_compatibility'] = startup
        destination = destination / startup['sha256']
    return store_bytes(destination, (json.dumps(frozen, indent=2) + '\n').encode())


@timed('control.lookup')
def find_control(root, spec, startup=None):
    # Deterministic first capture; never cycle through controls seeking agreement.
    destination = root / key(spec)
    if startup is not None:
        destination = destination / startup['sha256']
    entries = sorted(destination.glob('*.json'))
    if not entries:
        return None
    path = entries[0]
    if path.stem != sha256_file(path):
        raise RuntimeError('control catalog manifest changed')
    report = json.loads(path.read_bytes())
    validate_control(report, spec)
    if startup is not None and startup_signature(report, startup['through_tick']) != startup:
        raise RuntimeError('cached startup compatibility changed')
    report['catalog_manifest'] = str(path)
    return report


def startup_signature(report, first_tick):
    """Only original-forward observations: independent of corrected outcomes."""
    from .replay_fidelity import validate_boundary_capture
    from .replay_control import expected_historical_rewinds
    raw = Path(report['raw_log']['path']).read_text(encoding='utf-8', errors='replace')
    candidate_mode = report.get('mode') == 'runtime'
    rewinds = expected_historical_rewinds(report) if candidate_mode else None
    rows = validate_boundary_capture(raw, report['run_id'],
        # Before/after-publication cancellation has an explicit empty traversal
        # plan. Runtime mode alone does not mean historical execution occurred.
        **({'historical_restore': rewinds is None or bool(rewinds), 'expected_rewinds': rewinds,
            'retained_execution': True} if candidate_mode else {}))
    prefix = []
    for row in rows:
        if int(row['frame']) > first_tick:
            break
        prefix.append({k: v for k, v in row.items() if k != 'ordinal'})
    # Native world/render wall clocks are presentation observations; retain
    # particle clocks, counts, payload hashes and shared RNG consumed by simulation.
    particles = []
    pattern = re.compile(r'particle output run_id=(\S+) tick=(\d+) (gpu=.*?) world_time=')
    for line in raw.splitlines():
        match = pattern.search(line)
        if not match:
            continue
        if match[1] != report['run_id']:
            raise RuntimeError('startup particle identity differs')
        if int(match[2]) > first_tick:
            break
        particles.append((int(match[2]), match[3]))
    if not prefix or not particles:
        raise RuntimeError('independent control startup incompatible: required original-forward observations missing')
    payload = json.dumps(dict(callbacks=prefix, particles=particles), sort_keys=True, separators=(',', ':')).encode()
    return dict(sha256=hashlib.sha256(payload).hexdigest(), through_tick=first_tick,
                compared_callbacks=len(prefix), particle_observations=len(particles))


def require_startup_compatible(native, candidate, first_tick):
    expected, actual = (startup_signature(r,first_tick) for r in (native,candidate))
    if expected != actual:
        raise RuntimeError('independent control startup incompatible; not a rollback divergence')
    return dict(result='pass', **expected)
