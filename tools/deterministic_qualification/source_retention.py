"""Retain reconstructible source bytes, including dirty and untracked inputs.

The archive is local evidence, not a release package. Workspace input trees
are intentionally conservative; Ninja adds ignored/generated and fetched
dependency inputs. Installed compiler/SDK files remain toolchain provenance.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
import zlib
import sqlite3
import re
import time
from contextlib import contextmanager
from .replay_timing import timed, span

SOURCE_TREES = ("HorseMod", "RE-UE4SS", "DotVanisher", "RuntimeOracle", "tools", "release_resources")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl",
                   ".inc", ".cmake", ".py", ".ps1", ".bat", ".cmd", ".rc", ".def", ".asm",
                   ".rs", ".toml", ".lua", ".natvis", ".hlsl", ".glsl", ".fx"}
TARGETS = ("HorseMod", "ReplayQualificationMod", "DeterministicCoreSelfTest", "NativeCandidateRegionsSelfTest", "UE4SS")


def _git(root: Path, *args: str) -> bytes:
    return subprocess.run(["git", *args], cwd=root, check=True, capture_output=True).stdout


def _names(root: Path, *args: str) -> list[str]:
    return [os.fsdecode(p) for p in _git(root, *args, "-z").split(b"\0") if p]


def _selected(relative: str, tracked: bool) -> bool:
    p = Path(relative)
    if p.parts[0] in SOURCE_TREES:
        # Include resources, generators and test fixtures regardless of suffix
        # or tracking status. Run artifacts belong in the ignored build tree.
        return True
    return (p.name in {"CMakeLists.txt", "CMakePresets.json", ".gitmodules"}
            or (len(p.parts) == 1 and p.suffix.lower() in SOURCE_SUFFIXES)
            or (relative.startswith("docs/investigations/") and "manifest" in p.name and tracked))


@timed('source.discovery')
def workspace_inputs(root: Path) -> dict[str, Path | None]:
    """Explicit deleted entries prevent reconstructing a stale tracked file."""
    result: dict[str, Path | None] = {}
    def collect(repository: Path, prefix: str = "") -> None:
        tracked = set(_names(repository, "ls-files", "--cached"))
        others = set(_names(repository, "ls-files", "--others", "--exclude-standard"))
        for relative in sorted(tracked | others):
            name = f"{prefix}/{relative}" if prefix else relative
            if not _selected(name, relative in tracked):
                continue
            path = repository / relative
            if path.is_dir():
                # A gitlink is not its working contents. Descend into the
                # checked-out repository, including its own dirty files.
                if _git(path, "rev-parse", "--show-toplevel").decode().strip().replace("\\", "/").casefold() != str(path.resolve()).replace("\\", "/").casefold():
                    raise RuntimeError(f"source gitlink is not initialized: {path}")
                collect(path, name)
            else:
                result["workspace/" + name.replace("\\", "/")] = path if path.is_file() else None
    collect(root)
    return result


def build_inputs(root: Path, build: Path) -> tuple[dict[str, Path], dict]:
    cache = build / "CMakeCache.txt"
    fields = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep and not key.startswith(("//", "#")):
            fields[key.partition(":")[0]] = value
    ninja = fields["CMAKE_MAKE_PROGRAM"]
    def tool(*args: str) -> str:
        return subprocess.run([ninja, "-t", *args], cwd=build, check=True,
                              capture_output=True, text=True).stdout
    inputs = tool("inputs", *TARGETS).splitlines()
    nodes = {line.rpartition(": ")[0]: line.rpartition(": ")[2]
             for line in tool("targets", "all").splitlines()}
    phony = {name for name, rule in nodes.items() if rule == "phony"}
    inputs = [value for value in inputs if value.replace("\\", "/") not in phony]
    # Ninja's stored compiler dependencies include headers not explicitly
    # listed in CMake. Ignore dependency record headings, not header paths.
    inputs += [line.strip() for line in tool("deps").splitlines() if line.startswith("    ")]
    roots = [("workspace", root.resolve()), ("build", build.resolve())]
    fetched = fields.get("FETCHCONTENT_BASE_DIR")
    if fetched:
        roots.insert(0, ("fetched", Path(fetched).resolve()))
    roots.sort(key=lambda row: len(str(row[1])), reverse=True)
    result = {}
    non_file_nodes = {}
    for value in inputs:
        p = Path(value)
        p = (build / p).resolve() if not p.is_absolute() else p.resolve()
        rule = nodes.get(value.replace("\\", "/"), nodes.get(p.as_posix()))
        if rule and p.suffix.lower() in {".obj", ".o", ".lib", ".a", ".dll", ".exe", ".pdb", ".res", ".exp", ".ilk", ".rlib", ".rmeta"}:
            # Rebuildable outputs are not source. Prebuilt link inputs without
            # a generating rule remain included, along with source recipes.
            continue
        for label, directory in roots:
            if p.is_relative_to(directory):
                if not p.is_file():
                    if rule == "CUSTOM_COMMAND":
                        # CMake utility targets (notably Cargo) have a
                        # command-bearing dependency node with no file.
                        non_file_nodes[value] = rule
                        break
                    raise RuntimeError(f"configured source dependency missing: {p}")
                result[label + "/" + p.relative_to(directory).as_posix()] = p
                break
        else:
            # Do not silently call an unknown external header/library a SDK
            # input. Retain every consumed file, with its original root map.
            if not p.is_file():
                raise RuntimeError(f"external source dependency missing: {p}")
            directory = Path(p.anchor)
            label = "external_" + hashlib.sha256(str(directory).encode()).hexdigest()[:12]
            roots.append((label, directory))
            result[label + "/" + p.relative_to(directory).as_posix()] = p
    # Keep build flags and generated recipes, not just compiler source names.
    for name in ("CMakeCache.txt", "build.ninja", "CMakeFiles/rules.ninja"):
        p = build / name
        if p.is_file(): result["build/" + name] = p
    return result, {"targets": TARGETS, "commands": tool("commands", *TARGETS), "non_file_nodes": non_file_nodes,
                    "roots": {name: str(path) for name, path in roots},
                    "external_toolchain_policy": "Consumed headers and prebuilt link inputs are retained, including external files; the installed compiler itself remains a configured toolchain requirement."}


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


@timed('source.hashing')
def workspace_fingerprint(root: Path) -> str:
    rows = [(name, None if path is None else _digest(path.read_bytes()))
            for name, path in sorted(workspace_inputs(root.resolve()).items())]
    return _digest(json.dumps(rows, separators=(",", ":")).encode())


def _verify_zip_retention(report: dict) -> None:
    path = Path(report["archive"])
    if _digest(path.read_bytes()) != report["archive_sha256"]:
        raise RuntimeError("retained source archive hash mismatch")
    with zipfile.ZipFile(path) as archive:
        raw = archive.read("manifest.json")
        if _digest(raw) != report["manifest_sha256"]:
            raise RuntimeError("retained source manifest hash mismatch")
        manifest = json.loads(raw)
        expected = {"manifest.json"}
        for row in manifest["files"]:
            if row.get("deleted"): continue
            name = row["path"]
            if name.startswith("/") or ".." in Path(name).parts:
                raise RuntimeError("unsafe retained source path")
            data = archive.read(name)
            if len(data) != row["size"] or _digest(data) != row["sha256"]:
                raise RuntimeError(f"retained source payload mismatch: {name}")
            expected.add(name)
        if len(archive.namelist()) != len(expected) or set(archive.namelist()) != expected:
            raise RuntimeError("retained source archive membership mismatch")


@timed('build.verification')
def require_build_provenance(root: Path, build: Path, binaries: dict[str, Path]) -> dict:
    path = build / "replay-tests" / "build-provenance.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("schema") != 1:
        raise RuntimeError("unsupported build source provenance")
    for name, binary in binaries.items():
        expected = report["binaries"][name]
        if _digest(binary.read_bytes()) != expected["sha256"]:
            raise RuntimeError(f"binary is not bound to retained build sources: {name}")
    verify_retention(report["source_retention"])
    manifest = read_manifest(report["source_retention"])
    roots = {name: Path(directory) for name, directory in manifest["build"]["roots"].items()}
    consumed, graph = build_inputs(root, build)
    if json.dumps(graph, sort_keys=True) != json.dumps(manifest['build'], sort_keys=True):
        raise RuntimeError('configured native build graph changed; rebuild before another live run')
    current = workspace_inputs(root)
    current.update(consumed)
    old = {row['path']: row for row in manifest['files']}
    def reporting_only(name):
        # A Python file consumed by Ninja is a generator, never reporting-only.
        if name in consumed:
            return False
        return (name == 'workspace/tools/replay_test.py'
                or (name.startswith('workspace/tools/deterministic_qualification/')
                    and Path(name).suffix in ('.py', '.json')))
    if {n for n in current if not reporting_only(n)} != {n for n in old if not reporting_only(n)}:
        raise RuntimeError('native source membership changed; rebuild before another live run')
    for row in manifest["files"]:
        if reporting_only(row['path']):
            continue
        label, relative = row["path"].split("/", 1)
        original = roots[label] / relative
        if row.get("deleted"):
            if original.exists(): raise RuntimeError(f"deleted build input reappeared: {original}")
        elif not original.is_file() or _digest(original.read_bytes()) != row["sha256"]:
            raise RuntimeError(f"retained build input changed: {original}")
    log = report["build_log"]
    if _digest(Path(log["path"]).read_bytes()) != log["sha256"]:
        raise RuntimeError("retained build log hash mismatch")
    return report


def retain_sources_zip(root: Path, destination: Path, build: Path | None = None) -> dict:
    root = root.resolve()
    files = workspace_inputs(root)
    graph = {}
    if build is not None:
        extra, graph = build_inputs(root, build)
        files.update(extra)
    destination.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="source-", suffix=".tmp", dir=destination)
    os.close(fd)
    temporary = Path(temporary)
    records = []
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            def put(name: str, data: bytes) -> None:
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)
            for name, path in sorted(files.items()):
                if path is None:
                    records.append({"path": name, "deleted": True})
                    continue
                data = path.read_bytes()
                records.append({"path": name, "size": len(data), "sha256": _digest(data)})
                put(name, data)
            manifest = {"schema": 1, "commit": _git(root, "rev-parse", "HEAD").decode().strip(),
                        "files": records, "build": graph}
            encoded = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
            put("manifest.json", encoded)
        # Reject a moving checkout. Both membership and every retained byte
        # must still match before this bundle is published as source evidence.
        current = workspace_inputs(root)
        if build is not None: current.update(build_inputs(root, build)[0])
        if set(current) != set(files): raise RuntimeError("source membership changed during retention")
        for record in records:
            path = current[record["path"]]
            if record.get("deleted"):
                if path is not None: raise RuntimeError("deleted source reappeared during retention")
            elif path is None or _digest(path.read_bytes()) != record["sha256"]:
                raise RuntimeError(f"source changed during retention: {record['path']}")
        identity = _digest(encoded)
        target = destination / f"source-{identity}.zip"
        digest = _digest(temporary.read_bytes())
        if target.exists():
            if _digest(target.read_bytes()) != digest: raise RuntimeError("retained source archive collision/corruption")
        else:
            os.replace(temporary, target)
        return {"manifest_sha256": identity, "archive": str(target.resolve()), "archive_sha256": digest,
                "files": len(records), "uncompressed_bytes": sum(row.get("size", 0) for row in records),
                "reconstructible": True}
    finally:
        if temporary.exists(): temporary.unlink()


def _atomic_payload(path, data):
    path.parent.mkdir(parents=True,exist_ok=True)
    fd, name=tempfile.mkstemp(dir=path.parent,suffix='.tmp')
    try:
        with os.fdopen(fd,'wb') as out:
            out.write(data);out.flush();os.fsync(out.fileno())
        if path.exists():
            if path.read_bytes()!=data: raise RuntimeError('source object collision/corruption: '+str(path))
        else: os.replace(name,path)
    finally: Path(name).unlink(missing_ok=True)


def read_manifest(report):
    if report.get('format')!='objects-v2':
        if _digest(Path(report['archive']).read_bytes())!=report['archive_sha256']:
            raise RuntimeError('retained source archive hash mismatch')
        with zipfile.ZipFile(report['archive']) as archive: raw=archive.read('manifest.json')
    else: raw=Path(report['manifest']).read_bytes()
    if _digest(raw)!=report['manifest_sha256']: raise RuntimeError('retained source manifest hash mismatch')
    manifest=json.loads(raw)
    names=set()
    for row in manifest['files']:
        name=row['path'];p=Path(name)
        if name in names or p.is_absolute() or p.drive or '..' in p.parts or '\\' in name:
            raise RuntimeError('unsafe or duplicate retained source path')
        names.add(name)
        if not row.get('deleted') and (not re.fullmatch('[0-9a-f]{64}',row.get('sha256',''))
                or not isinstance(row.get('size'),int) or row['size']<0):
            raise RuntimeError('invalid retained source digest/size')
    return manifest


@contextmanager
def source_reader(report):
    if report.get('backend') == 'sqlite':
        database=Path(report['objects'])/'objects.sqlite3'
        try:
            connection=sqlite3.connect(database.as_uri()+'?mode=ro',uri=True)
            try: yield connection
            finally: connection.close()
        except sqlite3.Error as error:
            raise RuntimeError('source object database unavailable or corrupt: '+str(database)) from error
    elif report.get('format') != 'objects-v2':
        with zipfile.ZipFile(report['archive']) as archive: yield archive
    else: yield None


def source_bytes(report, row, reader=None):
    if row.get('deleted'): raise ValueError('deleted source has no payload')
    if reader is None and report.get('backend') == 'sqlite':
        with source_reader(report) as opened: return source_bytes(report,row,opened)
    if report.get('backend') == 'sqlite':
        stored=reader.execute('SELECT payload FROM objects WHERE sha256=?',(row['sha256'],)).fetchone()
        if stored is None: raise RuntimeError('source object missing')
        packed=stored[0]
    elif report.get('format') != 'objects-v2':
        if reader is None:
            with source_reader(report) as opened: return source_bytes(report,row,opened)
        data=reader.read(row['path']);packed=None
    else: packed=(Path(report['objects'])/(row['sha256']+'.zlib')).read_bytes()
    if packed is not None:
        try: data=zlib.decompress(packed)
        except zlib.error as error: raise RuntimeError('source object corrupt') from error
    if len(data)!=row['size'] or _digest(data)!=row['sha256']:
        raise RuntimeError('retained source payload mismatch: '+row['path'])
    return data


@timed('source.verification')
def verify_retention(report):
    if report.get('format')!='objects-v2': return _verify_zip_retention(report)
    manifest=read_manifest(report)
    with source_reader(report) as reader:
        for row in manifest['files']:
            if not row.get('deleted'): source_bytes(report,row,reader)


@timed('source.retention')
def retain_sources(root: Path, destination: Path, build: Path | None = None):
    root=root.resolve();files=workspace_inputs(root);graph={}
    if build is not None:
        extra,graph=build_inputs(root,build);files.update(extra)
    objects=destination/'objects';objects.mkdir(parents=True,exist_ok=True)
    records=[];new_bytes=reused_bytes=stored_bytes=0
    database=objects/'objects.sqlite3'
    connection=sqlite3.connect(database,timeout=30)
    try:
        connection.execute('PRAGMA synchronous=FULL')
        connection.execute('CREATE TABLE IF NOT EXISTS objects (sha256 TEXT PRIMARY KEY, payload BLOB NOT NULL)')
        connection.execute('BEGIN IMMEDIATE')
        for name,path in sorted(files.items()):
            if path is None:
                records.append(dict(path=name,deleted=True));continue
            data=path.read_bytes();digest=_digest(data)
            row=dict(path=name,size=len(data),sha256=digest)
            found=connection.execute('SELECT payload FROM objects WHERE sha256=?',(digest,)).fetchone()
            if found:
                try: valid=zlib.decompress(found[0])==data
                except zlib.error: valid=False
                if not valid: raise RuntimeError('source object collision/corruption: '+digest)
                reused_bytes+=len(data)
            else:
                packed=zlib.compress(data,6)
                connection.execute('INSERT INTO objects VALUES (?,?)',(digest,packed))
                new_bytes+=len(data);stored_bytes+=len(packed)
            records.append(row)
        manifest=dict(schema=2,commit=_git(root,'rev-parse','HEAD').decode().strip(),files=records,build=graph,
            repository=dict(root=str(root),common_git_dir=str((root/_git(root,'rev-parse','--git-common-dir').decode().strip()).resolve())))
        encoded=json.dumps(manifest,sort_keys=True,separators=(',',':')).encode()
        verification_started=time.monotonic()
        current=workspace_inputs(root)
        if build is not None: current.update(build_inputs(root,build)[0])
        if set(current)!=set(files): raise RuntimeError('source membership changed during retention')
        for row in records:
            path=current[row['path']]
            if row.get('deleted'):
                if path is not None: raise RuntimeError('source changed during retention: '+row['path'])
            elif path is None or _digest(path.read_bytes())!=row['sha256']:
                raise RuntimeError('source changed during retention: '+row['path'])
        connection.commit()
    except BaseException:
        connection.rollback();raise
    finally: connection.close()
    identity=_digest(encoded);target=destination/('source-'+identity+'.json')
    _atomic_payload(target,encoded)
    return dict(format='objects-v2',backend='sqlite',manifest=str(target.resolve()),manifest_sha256=identity,
        objects=str(objects.resolve()),files=len(records),
        uncompressed_bytes=sum(r.get('size',0) for r in records),reconstructible=True,
        storage=dict(new_payload_bytes=new_bytes,reused_payload_bytes=reused_bytes,
                     new_stored_bytes=stored_bytes,total_stored_bytes=database.stat().st_size,
                     verification_seconds=time.monotonic()-verification_started))


@timed('source.export')
def export_sources(report, target):
    verify_retention(report);manifest=read_manifest(report)
    target=Path(target);target.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(dir=target.parent,suffix='.tmp');os.close(fd)
    try:
        with source_reader(report) as reader, zipfile.ZipFile(name,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
            for row in manifest['files']:
                if row.get('deleted'): continue
                info=zipfile.ZipInfo(row['path'],(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
                archive.writestr(info,source_bytes(report,row,reader))
            info=zipfile.ZipInfo('manifest.json',(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,json.dumps(manifest,sort_keys=True,separators=(',',':')).encode())
        data=Path(name).read_bytes();_atomic_payload(target,data)
        return dict(path=str(target.resolve()),sha256=_digest(data),bytes=len(data))
    finally: Path(name).unlink(missing_ok=True)


@timed('source.seed')
def seed_sources(report, destination):
    """Seed one explicitly selected verified snapshot; never rewrite/delete it."""
    verify_retention(report);manifest=read_manifest(report)
    objects=Path(destination)/'objects';objects.mkdir(parents=True,exist_ok=True)
    database=objects/'objects.sqlite3';new=reused=0
    with sqlite3.connect(database) as connection, source_reader(report) as reader:
        connection.execute('PRAGMA synchronous=FULL')
        connection.execute('CREATE TABLE IF NOT EXISTS objects (sha256 TEXT PRIMARY KEY,payload BLOB NOT NULL)')
        connection.execute('BEGIN IMMEDIATE')
        for row in manifest['files']:
            if row.get('deleted'):continue
            data=source_bytes(report,row,reader)
            old=connection.execute('SELECT payload FROM objects WHERE sha256=?',(row['sha256'],)).fetchone()
            if old:
                if zlib.decompress(old[0])!=data:raise RuntimeError('source object corruption during seed')
                reused+=len(data)
            else:
                connection.execute('INSERT INTO objects VALUES (?,?)',(row['sha256'],zlib.compress(data,6)))
                new+=len(data)
    return dict(source=report,new_payload_bytes=new,reused_payload_bytes=reused,total_stored_bytes=database.stat().st_size)
