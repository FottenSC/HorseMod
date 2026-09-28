"""Own a replay runtime deployment and prove which module the game loaded."""
from __future__ import annotations
from .replay_timing import timed

from pathlib import Path
import subprocess
import time
import uuid

import psutil

from .artifacts import sha256_file
from .process_control import GameProcess, list_game_processes, require_game_process
from .run_resources import RunResources, acquire_deployment_lock, deployment_journal_path


def inspect_deployment(deployed: Path) -> dict:
    """Read recorded ownership only; never acquire locks, recover or inspect Steam."""
    import json
    try:
        document = json.loads(deployment_journal_path(deployed).read_bytes())
        if document.get('schema_version') != 1:
            return {'state': 'unknown'}
        return {key: document.get(key, 'unknown') for key in ('state', 'run_id')}
    except (OSError, ValueError, AttributeError):
        return {'state': 'unknown'}


class ReplayRun:
    def __init__(self, source: Path, deployed: Path, report: Path, *, runtime_enabled=True):
        self.source = source.resolve()
        self.deployed = deployed.resolve()
        # The deployment, not a report filename, owns its recovery journal.
        self.journal_path = deployment_journal_path(self.deployed)
        self._lock = None
        self._child = None
        self.runtime_enabled = runtime_enabled
        self.expected_hash = sha256_file(self.source)
        self.loaded_module: dict[str, object] | None = None

    @timed('deployment')
    def __enter__(self):
        self._acquire_deployment()
        try:
            # Read only after acquiring ownership. A contender must never
            # recover the active owner's processes or overwrite its journal.
            self.resources = RunResources(self.journal_path)
            self.resources.recover()
            if list_game_processes():
                raise RuntimeError("existing game prevents replay deployment")
            self.resources.begin("replay-" + uuid.uuid4().hex)
            if not self.runtime_enabled and self.source == self.deployed:
                raise RuntimeError("stock control requires a separate source artifact")
            self.resources.prepare_file(self.deployed,
                [self.expected_hash if self.runtime_enabled else None])
            if not self.runtime_enabled:
                self.deployed.unlink(missing_ok=True)
            elif self.source != self.deployed:
                self.deployed.parent.mkdir(parents=True, exist_ok=True)
                self.resources.publish_copy(self.source, self.deployed)
            self.require_deployed_identity()
        except BaseException:
            try:
                if hasattr(self, "resources"):
                    self.resources.recover()
            finally:
                self._release_deployment()
            raise
        return self

    def _acquire_deployment(self):
        if self._lock is not None:
            raise RuntimeError("replay deployment already owned by this context")
        self._lock = acquire_deployment_lock(self.journal_path)

    def _release_deployment(self):
        if self._lock is not None:
            # Closing releases the OS lock, including on process death. Keep
            # the inode/path stable so contenders cannot lock a replacement.
            self._lock.close()
            self._lock = None

    def require_deployed_identity(self):
        deployment_matches = (sha256_file(self.deployed) == self.expected_hash
            if self.runtime_enabled else not self.deployed.exists())
        if sha256_file(self.source) != self.expected_hash or not deployment_matches:
            raise RuntimeError("replay runtime binary changed during capture")

    def register_process(self, pid: int):
        self.resources.register_process(GameProcess(pid, ""))

    @timed('startup')
    def launch(self, executable: Path, extra_args: tuple[str, ...] = ()) -> int:
        # Steam supplies its client context to the actual game. Direct launch
        # can fail SteamAPI_Init even while a signed-in client is running.
        from .replay_preflight import inspect_preflight, require_ready, inspect_steam
        if self._lock is None:
            raise RuntimeError("launch requires held deployment lock")
        require_ready(inspect_preflight(self.deployed, [self.source, executable],
                                       active_run=self.resources.document['run_id'], steam=inspect_steam,
                                       loader_executable=executable))
        self.require_deployed_identity()
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            steam = Path(winreg.QueryValueEx(key, "SteamExe")[0]).resolve()
        if not steam.is_file():
            raise RuntimeError("Steam launcher is missing")
        marker = "-HorseQualificationRun=" + self.resources.document["run_id"]
        self.resources.prepare_process_launch(executable, marker, deferred=True)
        try:
            subprocess.run([str(steam), "-applaunch", "544750", marker, *extra_args],
                           check=True, timeout=10)
        except (FileNotFoundError, PermissionError):
            self.resources.cancel_unstarted_launch(marker)
            raise
        # Steam can queue a launch while closing the previous app. A verified
        # second capture arrived after the old ten-second discovery deadline.
        # Keep waiting for this same marked request; never dispatch a duplicate.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            self.resources.recover_launched_processes()
            owned = self.resources.live_processes()
            if len(owned) > 1:
                raise RuntimeError("Steam launched multiple games for one replay request")
            if owned:
                self._child = psutil.Process(owned[0].pid)
                return self._child.pid
            time.sleep(0.1)
        raise RuntimeError("Steam did not launch the marked replay process before deadline")

    def require_process(self, pid: int):
        if self._child is not None and self._child.pid == pid:
            try:
                code = self._child.wait(timeout=0)
            except psutil.TimeoutExpired:
                code = None
            if code is not None:
                raise RuntimeError(f"owned game exited before evidence completed: "
                    f"pid={pid} exit_code={code} exit_hex=0x{code & 0xffffffff:08x}")
        require_game_process(pid)

    def wait_for_loaded_module(self, pid: int, timeout_seconds: float):
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            try:
                return self.verify_loaded_module(pid)
            except ModuleNotLoaded:
                time.sleep(0.25)
        raise RuntimeError("game did not load the requested HorseMod before setup deadline")

    def verify_loaded_module(self, pid: int):
        self.require_deployed_identity()
        owned = self.resources.live_processes()
        if not any(process.pid == pid for process in owned):
            raise RuntimeError("loaded-module proof requires the owned game process")
        process = psutil.Process(pid)
        paths = {Path(mapping.path).resolve() for mapping in process.memory_maps()
                 if mapping.path and not mapping.path.startswith("[")}
        if not self.runtime_enabled:
            if self.deployed in paths or self.source in paths:
                raise RuntimeError("stock control loaded HorseMod")
            self.loaded_module = {
                "pid": pid, "process_created": process.create_time(),
                "path": str(self.deployed), "sha256": None,
                "verification": "owned_process_runtime_absent",
            }
            return self.loaded_module
        if self.deployed not in paths:
            raise ModuleNotLoaded("game did not load the requested HorseMod deployment")
        self.loaded_module = {
            "pid": pid, "process_created": process.create_time(),
            "path": str(self.deployed), "sha256": self.expected_hash,
            "verification": "owned_process_mapped_file_and_sha256",
        }
        return self.loaded_module

    @timed('cleanup')
    def __exit__(self, exc_type, exc, traceback):
        try:
            self.resources.recover()
        finally:
            self._release_deployment()


class ModuleNotLoaded(RuntimeError):
    pass
