"""Read-only admission checks. Recovery remains owned by RunResources."""
from __future__ import annotations
from .replay_timing import timed

from pathlib import Path
import shutil

from .artifacts import sha256_file
from .process_control import list_game_processes
from .run_resources import RunResources, deployment_journal_path


def inspect_steam():
    """Check launcher availability; cloud cancellation is proven by the journal owner."""
    try:
        import winreg
        import psutil
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
            executable = Path(winreg.QueryValueEx(key, 'SteamExe')[0]).resolve()
        running = []
        for process in psutil.process_iter(['pid', 'name']):
            if (process.info['name'] or '').lower() == 'steam.exe':
                running.append(process.info['pid'])
        return dict(check='steam', result='ready' if executable.is_file() and running else 'blocked',
                    detail=dict(executable=str(executable), pids=running,
                                scope='launcher available; pending cloud decisions remain journal blockers'))
    except (ImportError, OSError, RuntimeError) as error:
        return dict(check='steam', result='unknown', detail=str(error))


@timed('preflight')
def inspect_preflight(deployed: Path, required=(), *, space_paths=(),
                      minimum_free_bytes=2 * 1024**3, active_run=None,
                      processes=None, disk_usage=None, steam=None, loader_executable=None) -> dict:
    checks = []
    if steam is not None:
        checks.append(steam())
    def add(name, state, detail):
        checks.append(dict(check=name, result=state, detail=detail))
    if loader_executable is not None:
        from .replay_control import DISABLED_REPLAY_LOADER_SHA256
        target = Path(loader_executable).resolve().parent / 'dwmapi.dll'
        disabled = target.with_name('dwmapi.dll.DISABLED')
        try:
            source = target if target.is_file() else disabled
            actual = sha256_file(source) if source.is_file() else None
            valid = target.is_file() or actual == DISABLED_REPLAY_LOADER_SHA256
            add('replay_loader', 'ready' if valid else 'blocked',
                dict(path=str(source), sha256=actual, temporary=source == disabled,
                     required_disabled_sha256=DISABLED_REPLAY_LOADER_SHA256))
        except OSError as error:
            add('replay_loader', 'unknown', str(error))
    journal = deployment_journal_path(deployed)
    try:
        owner = RunResources(journal)  # constructor only reads; never recover here
        doc = owner.document
        authorized_active = active_run is not None and doc.get('run_id') == active_run
        if owner.pending_launches():
            add('launch', 'blocked', 'queued launch unresolved; no process is not cancellation proof')
        if doc.get('state') != 'clean' and not authorized_active:
            add('journal', 'blocked', f"{doc.get('state')}: existing resource owner must recover {journal}")
        else:
            add('journal', 'ready', str(journal))
        for row in doc.get('files', []):
            path = Path(row['path'])
            actual = sha256_file(path) if path.is_file() else None
            permitted = [row['before']] if doc.get('state') == 'clean' else [row['before'], *row['allowed_after']]
            add('deployment', 'ready' if actual in permitted else 'blocked',
                {'path': str(path), 'actual': actual, 'permitted': permitted})
            if row.get('before') is not None and doc.get('state') != 'clean':
                backup = Path(row['backup']) if row.get('backup') else None
                valid = backup is not None and backup.is_file() and sha256_file(backup) == row['before']
                add('backup', 'ready' if valid else 'blocked', str(backup))
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        add('journal', 'unknown', str(error))
    try:
        games = (processes or list_game_processes)()
        add('processes', 'blocked' if games else 'ready',
            [dict(pid=p.pid, identity=str(p)) for p in games])
    except (OSError, RuntimeError) as error:
        add('processes', 'unknown', str(error))
    for path in required:
        path = Path(path)
        try:
            add('required_file', 'ready' if path.is_file() else 'blocked',
                {'path': str(path), 'sha256': sha256_file(path) if path.is_file() else None})
        except OSError as error:
            add('required_file', 'unknown', str(error))
    for path in dict.fromkeys([deployed.parent, *space_paths]):
        try:
            existing = Path(path).resolve()
            while not existing.exists() and existing != existing.parent:
                existing = existing.parent
            free = (disk_usage or shutil.disk_usage)(existing).free
            add('disk', 'ready' if free >= minimum_free_bytes else 'blocked',
                dict(path=str(path), free_bytes=free, required_bytes=minimum_free_bytes))
        except OSError as error:
            add('disk', 'unknown', str(error))
    state = 'blocked' if any(c['result'] == 'blocked' for c in checks) else (
        'unknown' if any(c['result'] == 'unknown' for c in checks) else 'ready')
    return dict(schema=1, result=state, checks=checks, journal=str(journal),
                observational=True, steam_state='pending journal checked; no cancellation inferred')


def require_ready(report):
    if report['result'] != 'ready':
        reasons = '; '.join(f"{c['check']}: {c['detail']}" for c in report['checks'] if c['result'] != 'ready')
        raise RuntimeError('live preflight ' + report['result'] + ': ' + reasons)
