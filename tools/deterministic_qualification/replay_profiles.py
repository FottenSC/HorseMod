"""Named experiments resolve through the existing argparse validators."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

PROFILE_ROOT = Path(__file__).with_name('replay_profiles')


def validate_resumed_profile(report, profile, diagnostics):
    if profile is not None and (
            (report.get('experiment_profile') or {}).get('sha256') != profile['sha256']
            or report.get('diagnostic_settings') != diagnostics):
        raise RuntimeError('retained candidate profile or diagnostics changed')


def require_rolling30(output, identities):
    from .artifacts import sha256_file
    receipt = json.loads((output / 'rolling30-gate-receipt.json').read_bytes())
    path = Path(receipt['path'])
    if sha256_file(path) != receipt['sha256']:
        raise RuntimeError('rolling30 prerequisite manifest changed')
    manifest = json.loads(path.read_bytes())
    summary = manifest['summary']
    if (summary.get('result') != 'pass' or summary.get('build_identities') != identities
            or summary.get('acceptance_results', {}).get('simulation') != 'pass'
            or manifest['results']['cleanup'] != 'pass'
            or not manifest['diagnostic_complete']
            or (summary.get('profile') or {}).get('definition', {}).get('name') != 'rolling30-gate'
            or summary['profile']['sha256'] != sha256_file(PROFILE_ROOT / 'rolling30-gate.json')):
        raise RuntimeError('rolling600 requires an independently passed rolling30 on these binaries with complete cleanup')


def parse_profile_args(parser, argv):
    probe = argparse.ArgumentParser(add_help=False)
    probe.add_argument('--profile')
    selected, _ = probe.parse_known_args(argv)
    if not selected.profile:
        return parser.parse_args(argv), None
    path = Path(selected.profile)
    if path.suffix != '.json':
        path = PROFILE_ROOT / (selected.profile + '.json')
    try:
        raw = path.read_bytes()
        profile = json.loads(raw)
        if profile['schema'] != 1 or profile['continuation_ticks'] != 120:
            raise ValueError('profile requires schema 1 and 120 continuation ticks')
        if profile.get('contract') is not None:
            from .replay_experiments import validate_contract
            validate_contract(profile['contract'])
        if profile.get('authored_sample_evidence',{}).get('native_protocol')==1:
            from .replay_schedule import validate_frozen
            validate_frozen(profile)
        if profile['purpose'] not in ('diagnostic', 'qualification'):
            raise ValueError('invalid profile purpose')
        if not isinstance(profile.get('name'), str) or not profile['name']:
            raise ValueError('profile requires a name')
        args = profile['arguments']
        if not isinstance(args, list) or not all(isinstance(x, str) for x in args):
            raise ValueError('profile arguments must be strings')
        baseline = parser.parse_args(args)
        if baseline.stage != 'combat-restore' or baseline.index_seek_continuation != 120:
            raise ValueError('experiment profile requires combat-restore and 120 continuation ticks')
        setup = dict(skip_intros=True, include_setup=True, native_fixed_seed=True, serial_particles=True)
        if baseline.no_async_loading_thread: setup['no_async_loading_thread'] = True
        if baseline.flush_startup_loading: setup['flush_startup_loading'] = True
        if profile.get('setup') != setup:
            raise ValueError('profile setup must match the native combat protocol')
        # Profile owns behavioral flags, including defaults. Only these operational
        # selectors may differ. In particular candidate-only cannot weaken a gate.
        operational = {'profile', 'jobs', 'list_tests', 'group', 'test', 'layer', 'changed_path', 'evidence_report', 'resume_candidate', 'output'}
        supplied = parser.parse_args(argv)
        explicit = {a.dest for a in parser._actions if any(
            token.split('=', 1)[0] in a.option_strings for token in argv)}
        if supplied.stage not in (baseline.stage, 'preflight'):
            raise ValueError('profile stage conflicts with requested stage')
        for key in explicit - operational:
            if getattr(supplied, key) != getattr(baseline, key):
                raise ValueError(f'profile conflicts with --{key.replace("_", "-")}')
        resolved = vars(baseline).copy()
        resolved.update({k: getattr(supplied, k) for k in operational if hasattr(supplied, k)})
        resolved['stage'] = supplied.stage
        options = argparse.Namespace(**resolved)
        receipt = dict(path=str(path.resolve()), sha256=hashlib.sha256(raw).hexdigest(),
                       bytes_utf8=raw.decode('utf-8'), definition=profile,
                       resolved_settings={k: str(v) if isinstance(v, Path) else v for k, v in resolved.items() if k not in ('output', 'report', 'participant', 'tick', 'offset', 'since', 'test', 'layer')})
        return options, receipt
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
