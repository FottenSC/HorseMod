"""Recover only resources acquired by this paired run, with identity checks."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import time

import psutil

from .artifacts import sha256_file
from .run_journal import atomic_json


def deployment_journal_path(deployed: Path) -> Path:
    return deployed.resolve().with_suffix(".qualification-resources.json")


def acquire_deployment_lock(journal_path: Path):
    """Share one OS lease between replay and paired runs of this installation."""
    lock_path = journal_path.with_suffix(".lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    handle = lock_path.open("a+b")
    try:
        if os.name == "nt":
            import msvcrt
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as error:
        handle.close()
        raise RuntimeError("qualification deployment is owned by another runner") from error
    # Keep the inode/path stable. Closing also releases the lock after a crash.
    return handle


def recover_resources(journal_path: Path):
    # Load after locking, so recovery cannot act on an active runner's journal.
    with acquire_deployment_lock(journal_path):
        RunResources(journal_path).recover()


class RunResources:
    def __init__(self, path: Path):
        self.path = path
        self.document = (json.loads(path.read_text(encoding="utf-8")) if path.exists()
            else {"schema_version": 1, "state": "clean", "files": [], "processes": [], "requests": []})
        if self.document.get("schema_version") != 1:
            raise RuntimeError("unsupported resource journal")

    def save(self):
        atomic_json(self.path, self.document)

    def begin(self, run_id: str):
        if self.document["state"] != "clean":
            raise RuntimeError("prior run requires resource recovery")
        self.document.update(state="active", run_id=run_id, files=[], processes=[], requests=[], launches=[], directories=[], temporaries=[])
        self.save()

    def prepare_process_launch(self, executable: Path, marker: str, *, deferred=False,
                               kind="game", environment_key=None):
        if not marker or self.document["run_id"] not in marker:
            raise RuntimeError("process launch requires a unique run marker")
        self.document.setdefault("launches", []).append({
            "exe": str(executable.resolve()), "marker": marker, "after": time.time(),
            "delivery": "pending" if deferred else "direct", "kind": kind,
            "environment_key": environment_key})
        self.save()

    def pending_launches(self):
        return any(row.get("delivery") == "pending"
                   for row in self.document.get("launches", []))

    def cancel_unstarted_launch(self, marker: str):
        # Only a proven CreateProcess rejection permits cancellation. A
        # launcher timeout/exit does not prove its deferred game was canceled.
        matches = [row for row in self.document.get("launches", [])
                   if row["marker"] == marker and row.get("delivery") == "pending"]
        if len(matches) != 1:
            raise RuntimeError("unstarted launch identity is ambiguous")
        matches[0]["delivery"] = "not_started"
        self.save()

    def recover_launched_processes(self):
        # A launcher can die after CreateProcess and before saving its PID.
        # Claim only a process carrying this run's exact command-line marker,
        # executable path, and creation-time bound.
        launches = self.document.get("launches", [])
        if not launches:
            return
        for process in psutil.process_iter():
            try:
                for launch in launches:
                    if not launch.get("environment_key") and launch["marker"] not in process.cmdline():
                        continue
                    if Path(process.exe()).resolve() != Path(launch["exe"]):
                        continue
                    if process.create_time() < launch["after"] - 1:
                        continue
                    if launch.get("environment_key"):
                        try:
                            marked = process.environ().get(launch["environment_key"]) == launch["marker"]
                        except psutil.AccessDenied as error:
                            raise RuntimeError("cannot verify possible owned helper environment") from error
                    else:
                        marked = True
                    if marked:
                        # Persist the identity just validated, never replace it
                        # with whichever process later inherits this PID.
                        row = {"pid": process.pid, "created": process.create_time(),
                               "exe": process.exe(), "kind": launch.get("kind", "game")}
                        current = psutil.Process(process.pid)
                        if (current.create_time() != row["created"]
                                or current.exe() != row["exe"]):
                            raise RuntimeError("launch identity changed during adoption")
                        changed = row not in self.document["processes"]
                        if changed:
                            self.document["processes"].append(row)
                        if launch.get("delivery") == "pending":
                            launch["delivery"] = "observed"
                            changed = True
                        if changed:
                            self.save()
                        break
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue

    def prepare_file(self, path: Path, allowed_after: list[str | None]):
        path = path.resolve()
        before = sha256_file(path) if path.is_file() else None
        backup = None
        if before is not None:
            backup = self.path.parent / "resource-backups" / before
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(path, backup)
            if sha256_file(backup) != before:
                raise RuntimeError("resource predecessor backup failed")
        self.document["files"].append({"path": str(path), "before": before,
            "backup": str(backup) if backup else None, "allowed_after": allowed_after})
        self.save()  # Intent is durable before the caller changes the target.

    def _publication_temporary(self, target: Path):
        target = target.resolve()
        rows = [row for row in self.document["files"] if Path(row["path"]) == target]
        if len(rows) != 1:
            raise RuntimeError("publication requires one journaled predecessor")
        row = rows[0]
        current = sha256_file(target) if target.is_file() else None
        if current != row["before"] and current not in row["allowed_after"]:
            raise RuntimeError("deployment changed before publication")
        temporary = target.with_name(target.name + "." + self.document["run_id"] + ".deploy.tmp")
        if temporary.exists():
            raise RuntimeError("deployment temporary already exists")
        self.document.setdefault("temporaries", []).append(str(temporary))
        self.save()  # A partial copy is owned even if copying never returns.
        return target, temporary, row, current

    @staticmethod
    def _commit_publication(target, temporary, predecessor):
        current = sha256_file(target) if target.is_file() else None
        if current != predecessor:
            raise RuntimeError("deployment changed during publication")
        os.replace(temporary, target)

    def publish_copy(self, source: Path, target: Path):
        target, temporary, row, predecessor = self._publication_temporary(target)
        shutil.copy2(source, temporary)
        if sha256_file(temporary) not in row["allowed_after"]:
            raise RuntimeError("copied deployment identity does not match admitted bytes")
        self._commit_publication(target, temporary, predecessor)

    def publish_text(self, value: str, target: Path):
        target, temporary, row, predecessor = self._publication_temporary(target)
        temporary.write_bytes(value.encode("utf-8"))
        if sha256_file(temporary) not in row["allowed_after"]:
            raise RuntimeError("written deployment identity does not match admitted bytes")
        self._commit_publication(target, temporary, predecessor)

    def prepare_requests(self, root: Path):
        # These names are opened by native/Python writers before their run ID
        # is durable. Claim the absent temporary before either writer starts.
        for name in ("replay_request.tmp", "replay_result.tmp"):
            self.prepare_temporary(root / name)
        self.document["requests"].append(str(root.resolve()))
        self.save()

    def prepare_temporary(self, path: Path):
        path = path.resolve()
        if path.exists():
            raise RuntimeError(f"unowned temporary prevents launch: {path}")
        if str(path) not in self.document.setdefault("temporaries", []):
            self.document["temporaries"].append(str(path))
            self.save()

    def park_report(self, path: Path, *, allow_empty=False):
        # The existing report is evidence, not scratch. Back it up before
        # removing it from the native writer's fixed destination.
        # A crash writer may preopen its destination before there is a failure.
        # Journal this exact allowed state before launch, only for that writer.
        self.prepare_file(path, [None, hashlib.sha256(b'').hexdigest()] if allow_empty else [None])
        row = self.document["files"][-1]
        row["generated_report"] = True
        self.save()
        if (sha256_file(path) if path.is_file() else None) != row["before"]:
            raise RuntimeError("report changed before parking")
        path.unlink(missing_ok=True)

    def _owned_report(self, target):
        if target.name == "physics_callback_observation.jsonl":
            return self._owned_physics_report(target)
        try:
            run_id = json.loads(target.read_text(encoding="utf-8")).get("run_id")
            prefix = self.document["run_id"]
            return isinstance(run_id, str) and (run_id == prefix or run_id.startswith(prefix + "-"))
        except (OSError, ValueError, AttributeError):
            return False

    def _owned_physics_report(self, target):
        # Only this journaled writer publishes JSONL. Crash/partial writes stay
        # in its separately journaled .tmp; a published file ends in a phase.
        # Ownership is not completeness: overflowed diagnostic evidence is still
        # this run's file. Keep the previous 8192-record format recoverable.
        capacities = {8192: 1024 * 1024, 16384: 2 * 1024 * 1024}
        byte_limit = (max(capacities) + 3) * 1024

        def unique_object(pairs):
            value = {}
            for key, item in pairs:
                if key in value:
                    raise ValueError("duplicate report key")
                value[key] = item
            return value

        def shape(row, numbers, literals):
            if not isinstance(row, dict) or row.keys() != set(numbers) | literals.keys():
                raise ValueError("unexpected report fields")
            if any(type(row[key]) is not int or not 0 <= row[key] < 2**64 for key in numbers):
                raise ValueError("invalid report number")
            if any(type(row[key]) is not type(value) or row[key] != value for key, value in literals.items()):
                raise ValueError("invalid report value")

        try:
            if not self.document.get("run_id") or target.stat().st_size > byte_limit:
                return False
            with target.open("rb") as stream:
                used = 0

                def read_row():
                    nonlocal used
                    raw = stream.readline(1025)
                    used += len(raw)
                    if not raw:
                        return None
                    if used > byte_limit or len(raw) > 1024 or not raw.endswith(b"\n"):
                        raise ValueError("report exceeds line/file bound or is unfinished")
                    value = json.loads(raw, object_pairs_hook=unique_object)
                    if not isinstance(value, dict):
                        raise ValueError("report row is not an object")
                    return value

                header = read_row()
                if header is None:
                    return False
                capacity = header.get("max_events")
                if type(capacity) is not int or capacity not in capacities:
                    return False
                version=header.get("version")
                if version not in (1,2):return False
                writer_header=dict(writer_filter="observed_scene_collections_32", pre_discovery_writers_covered=False, max_pending=128) if version==2 else {}
                shape(header, ("time_ms", "pid", "image_base"), dict(
                    kind="header", version=version, run_id=self.document["run_id"],
                    observation_only=True, opaque_scene_identity=True, lifetime_proven=False,
                    recovery_proven=False, writer_coverage_proven=False, native_quiescence_proven=False,
                    whole_process_coverage=False, scope="observer_admission_to_trajectory_completion",
                    max_events=capacity, overlap_sample="entry", preexisting_calls_covered=False,
                    owned_bytes=capacities[capacity], entry_rvas=([0x29b4530,0x20113e0,0x21377c0,0x1f605a0] if version==2 else [0x29b4530,0x20113e0]), **writer_header))
                records = 0
                overflow = phase = False
                for _ in range(capacity + 3):
                    row = read_row()
                    if row is None:
                        return phase
                    if phase:
                        return False
                    kind = row.get("kind")
                    if kind == "event":
                        if (row.get("boundary") not in ("entry", "return")
                                or row.get("route") not in ("scene_startup", "scene_shutdown",
                                    "other_lifecycle_collection", "physics_pre_step", "physics_step",
                                    "other_physics_collection")
                                or row.get("entry_rva") not in ((0x29b4530,0x20113e0,0x21377c0,0x1f605a0) if version==2 else (0x29b4530,0x20113e0))):
                            return False
                        writer_numbers=()
                        writer_literals={}
                        if row["entry_rva"] in (0x21377c0,0x1f605a0):
                            ancestry=row.get("ancestry")
                            if not isinstance(ancestry,list) or len(ancestry)!=8 or any(type(p) is not int or not 0<=p<2**64 for p in ancestry):return False
                            writer_numbers=("handle_argument","receiver","method","returned_pointer","returned_handle","handle_read")
                            writer_literals=dict(ancestry=ancestry)
                        shape(row, ("time_ms", "entry_rva", "pair", "parent_pair", "thread", "depth",
                            "same_thread_same_scene", "other_thread_same_scene", "collection", "scene",
                            "scene_type", "delta_bits", "caller")+writer_numbers, dict(kind="event",
                            boundary=row["boundary"], route=row["route"], **writer_literals))
                        records += 1
                        if records > capacity:
                            return False
                    elif kind == "overflow" and not overflow:
                        shape(row, ("time_ms",), dict(kind="overflow", dropped=1, lower_bound=True))
                        overflow = True
                    elif kind == "phase":
                        if any(type(row.get(k)) is not bool for k in ("persistence_failed", "complete")):
                            return False
                        shape(row, ("time_ms", "end_tick", "close_thread", "records", "pending_at_close", "dropped"),
                            dict(kind="phase", boundary="CompleteTrajectory.physics_status",
                                persistence_failed=row["persistence_failed"], complete=row["complete"]))
                        if row["records"] != records or bool(row["dropped"]) != overflow:
                            return False
                        phase = True
                    else:
                        return False
            return False
        except (OSError, ValueError, TypeError, RecursionError):
            return False

    def create_directory(self, path: Path):
        path = path.resolve()
        if path.exists():
            return
        self.document.setdefault("directories", []).append(str(path))
        self.save()
        path.mkdir()  # Parent must already exist or be explicitly owned.

    def register_process(self, process, kind="game"):
        try:
            native = psutil.Process(process.pid)
            row = {"pid": native.pid, "created": native.create_time(), "exe": native.exe(), "kind": kind}
        except psutil.NoSuchProcess:
            return
        if row not in self.document["processes"]:
            self.document["processes"].append(row)
            self.save()

    def live_processes(self, kind="game"):
        from .process_control import GameProcess
        result = []
        for row in self.document["processes"]:
            if kind is not None and row.get("kind", "game") != kind:
                continue
            try:
                process = psutil.Process(row["pid"])
                if process.create_time() != row["created"] or process.exe() != row["exe"]:
                    raise RuntimeError("owned process identity changed; refusing PID reuse")
                result.append(GameProcess(row["pid"], ""))
            except psutil.NoSuchProcess:
                pass
        return tuple(result)

    def restore(self):
        if self.pending_launches():
            raise RuntimeError("Steam launch remains unresolved; deployment recovery is pending")
        if self.live_processes(kind=None):
            raise RuntimeError("cannot restore deployment while owned processes remain")
        from .process_control import list_game_processes
        if list_game_processes():
            raise RuntimeError("unowned game process prevents deployment recovery")
        files = self.document["files"]
        for row in files:
            target = Path(row["path"])
            current = sha256_file(target) if target.is_file() else None
            if (current != row["before"] and current not in row["allowed_after"]
                    and not (row.get("generated_report") and self._owned_report(target))):
                raise RuntimeError(f"resource changed outside this run: {target}")
            if row["backup"] and sha256_file(Path(row["backup"])) != row["before"]:
                raise RuntimeError("resource predecessor backup is stale")
        prefix = self.document.get("run_id", "")
        for root in self.document["requests"]:
            for name in ("online_request.txt", "online_request.publish.tmp",
                         "online_room_request.txt", "online_room_request.txt.tmp",
                         "replay_request.txt", "replay_request.tmp", "replay_result.txt"):
                path = Path(root) / name
                if str(path.resolve()) in self.document.get("temporaries", []):
                    continue  # Durable pre-write ownership covers partial bytes.
                if not path.is_file():
                    continue
                text = path.read_text(encoding="utf-8")
                if not prefix or not any(line == "run_id=" + prefix
                        or line.startswith("run_id=" + prefix + "-") for line in text.splitlines()):
                    raise RuntimeError(f"request is not owned by interrupted run: {path}")
                path.unlink()
        for row in files:
            target = Path(row["path"])
            if row["before"] is None:
                target.unlink(missing_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                temporary = target.with_suffix(target.suffix + ".restore.tmp")
                shutil.copy2(row["backup"], temporary)
                os.replace(temporary, target)
        for temporary in self.document.get("temporaries", []):
            Path(temporary).unlink(missing_ok=True)
        for directory in reversed(self.document.get("directories", [])):
            path = Path(directory)
            if path.exists():
                path.rmdir()  # Unexpected contents block cleanup, never recurse.
        self.document["state"] = "clean"
        self.save()

    def recover(self):
        if self.document["state"] == "clean":
            return
        self.document["state"] = "cleanup_pending"
        self.save()
        self.recover_launched_processes()
        deadline = time.monotonic() + 30
        self.stop_helpers(timeout_seconds=max(0.1, deadline - time.monotonic()))
        self.stop_games(graceful_timeout_seconds=max(0.1, deadline - time.monotonic()))
        self.restore()

    def stop_helpers(self, *, timeout_seconds=30.0):
        self.recover_launched_processes()
        deadline = time.monotonic() + timeout_seconds
        for row in self.document["processes"]:
            if row.get("kind") != "impairment":
                continue
            try:
                native = psutil.Process(row["pid"])
                if native.create_time() != row["created"] or native.exe() != row["exe"]:
                    raise RuntimeError("helper identity changed; refusing PID reuse")
                native.terminate()
                try:
                    native.wait(timeout=max(0.1, deadline - time.monotonic()))
                except psutil.TimeoutExpired:
                    native.kill()
                    native.wait(timeout=1)
            except psutil.NoSuchProcess:
                pass  # An independently exiting helper is already stopped.
            except psutil.Error as error:
                raise RuntimeError("owned helper could not be stopped") from error

    def stop_games(self, *, require_graceful=False, graceful_timeout_seconds=30.0):
        """Settle marked launches and stop only their verified game identities."""
        from .process_control import stop_game_processes
        deadline = time.monotonic() + graceful_timeout_seconds
        graceful = True
        while True:
            self.recover_launched_processes()
            processes = self.live_processes()
            if processes:
                stopped_gracefully = stop_game_processes(
                    processes, require_graceful=require_graceful,
                    graceful_timeout_seconds=max(0.1, deadline - time.monotonic()))
                graceful = bool(stopped_gracefully) and graceful
            if not self.pending_launches():
                return graceful
            if time.monotonic() >= deadline:
                self.document["state"] = "cleanup_pending"
                self.save()
                raise RuntimeError("Steam launch remains unresolved; deployment recovery is pending")
            time.sleep(0.1)


def bytes_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()
