#!/usr/bin/env python3
"""Validate a structured Ghidra export and build an immutable VS workspace."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import os
import re
import shutil
import sys
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, BinaryIO, Iterator


SCHEMA = "ghidra-calibur-export/v1"
UE_ORIGIN_SCHEMA = "ghidra-calibur-ue-origin/v1"
WATCHLIST_SCHEMA = "ghidra-calibur-watchlist/v1"
UE4172_COMMIT = "8c46d0805b8efa845cc693b76030b7cab2796c0a"
UE_MODULES = {"core", "coreuobject", "engine"}
PROJECT_GUID = "{DCACF24E-57AB-4C52-8873-844609C95C76}"
MAX_FUNCTIONS_PER_SHARD = 250
MAX_BYTES_PER_SHARD = 8 * 1024 * 1024
MAX_RELATIONSHIP_ITEMS = 6
MAX_DECLARATIONS_PER_HEADER = 250
MAX_NAVIGATION_ITEMS = 24
MAX_SOURCE_RELATIVE_PATH = 180
WINDOWS_RESERVED_NAMES = {"con", "prn", "aux", "nul", "clock$", *(f"com{number}" for number in range(1, 10)), *(f"lpt{number}" for number in range(1, 10))}
ALLOWED_STATUSES = {
    "ok",
    "external",
    "no-instruction",
    "timeout",
    "decompile-error",
    "cancelled",
}
NON_BODY_STATUSES = {"external", "no-instruction"}
FAILURE_STATUSES = {"timeout", "decompile-error"}

CLASS_TOKEN_RE = re.compile(r"^[AUFI][A-Z][A-Za-z0-9_]{1,}$")
CLASS_LITERAL_RE = re.compile(rb'L?"([AUFI][A-Za-z0-9_]{2,})"')
CLASS_PATTERNS = (
    re.compile(r"(?P<class>[AUFI][A-Za-z0-9_]+)::"),
    re.compile(r"GetPrivateStaticClassBody_(?P<class>[AUFI][A-Za-z0-9_]+)"),
    re.compile(r"Z_Construct_UClass_(?P<class>[AUFI][A-Za-z0-9_]+?)(?:_Statics|_NoRegister|_singleton|$)"),
    re.compile(r"StaticRegisterNatives_(?P<class>[AUFI][A-Za-z0-9_]+)"),
    re.compile(r"RegisterNatives(?P<class>[AUFI][A-Za-z0-9_]+)"),
)
TOKEN_RE = re.compile(r"[A-Z]+(?=[A-Z][a-z]|\d|$)|[A-Z]?[a-z]+|\d+")
CATEGORY_TOKENS = {
    "Replay": {"replay", "frameinputlog", "inputlog"},
    "Move": {"move", "movevm", "attack", "skill", "combo"},
    "Input": {"input", "pad", "command"},
    "Online": {"steam", "network", "online", "net", "async", "session"},
    "UI": {"hud", "widget", "menu", "window", "ui"},
    "Battle": {"luxbattle", "battle", "round", "guard", "hit"},
    "Runtime": {"uobject", "ufunction", "staticclass", "registernative", "construct", "compiledindefer"},
}

# First matching rule wins.  This is deliberately local and deterministic: no
# model/agent classification is invoked while refreshing a workspace.
AREA_RULES: tuple[tuple[str, set[str]], ...] = (
    ("gameplay/replay", {"replay", "frameinputlog", "inputlog"}),
    ("gameplay/combat", {"luxbattle", "battle", "round", "guard", "hit", "move", "movevm", "attack", "skill", "combo"}),
    ("online", {"steam", "network", "online", "net", "session"}),
    ("ui", {"hud", "widget", "menu", "window", "ui"}),
    ("input", {"input", "pad", "command"}),
    ("save_load", {"save", "load", "serialize", "archive"}),
    ("assets", {"asset", "package", "mesh", "texture", "material"}),
    ("runtime", {"uobject", "ufunction", "staticclass", "registernative", "construct", "compiledindefer", "fname", "namepool"}),
    ("platform", {"kernel", "win32", "user32", "d3d", "xaudio", "platform"}),
)

# A feature module is intentionally narrower than an area.  Rules are ordered
# so a function with several descriptive tokens still has one stable home.
# These rules only use symbol/namespace tokens; refresh never invokes an agent
# or model to classify a function.
MODULE_RULES: dict[str, tuple[tuple[str, set[str]], ...]] = {
    "gameplay/replay": (
        ("codec", {"encode", "decode", "packet", "buffer", "stream", "compress", "decompress"}),
        ("playback", {"playback", "interactive", "seek", "rewind", "resume"}),
        ("input", {"input", "event", "frame", "cursor", "controller", "command"}),
    ),
    "gameplay/combat": (
        ("moves", {"move", "movevm", "attack", "skill", "combo"}),
        ("rounds", {"round", "result", "winner"}),
        ("guard", {"guard", "impact", "break"}),
        ("hits", {"hit", "damage", "counter"}),
    ),
    "online": (
        ("matchmaking", {"match", "lobby", "matchmaking"}),
        ("sessions", {"session", "steam", "online"}),
        ("transport", {"network", "net", "socket", "packet"}),
    ),
    "ui": (
        ("widgets", {"widget", "window"}),
        ("menus", {"menu", "dialog", "screen"}),
        ("hud", {"hud", "overlay"}),
    ),
    "input": (
        ("commands", {"command", "sequence"}),
        ("controllers", {"controller", "pad", "device"}),
        ("actions", {"input", "button", "axis"}),
    ),
    "save_load": (
        ("serialization", {"serialize", "deserialize", "archive"}),
        ("save", {"save", "write"}),
        ("load", {"load", "read"}),
    ),
    "assets": (
        ("animation", {"animation", "anim", "skeleton"}),
        ("meshes", {"mesh", "model", "geometry"}),
        ("textures", {"texture", "material", "shader"}),
        ("packages", {"asset", "package", "resource"}),
    ),
    "runtime": (
        ("names", {"fname", "name", "namepool"}),
        ("reflection", {"uobject", "ufunction", "class", "property", "struct"}),
        ("registration", {"registernative", "construct", "compiledindefer"}),
    ),
    "platform": (
        ("graphics", {"d3d", "dxgi", "graphics", "render"}),
        ("audio", {"xaudio", "audio", "sound"}),
        ("windows", {"kernel", "win32", "user32", "windows"}),
    ),
}

FAMILY_VERBS = {
    "add", "allocate", "apply", "build", "cancel", "clear", "create", "decode", "destroy", "draw",
    "encode", "find", "get", "handle", "init", "load", "open", "parse", "play", "read", "register",
    "remove", "reset", "save", "set", "start", "stop", "tick", "update", "write",
}


class ExportValidationError(RuntimeError):
    pass


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ExportValidationError(f"Invalid JSON file {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ExportValidationError(f"Expected JSON object in {path}")
    return value


def iter_jsonl(path: Path) -> Iterator[dict[str, Any]]:
    try:
        with path.open("r", encoding="utf-8", newline="") as handle:
            for line_number, line in enumerate(handle, 1):
                if not line.strip():
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ExportValidationError(f"Invalid JSONL {path}:{line_number}: {exc}") from exc
                if not isinstance(value, dict):
                    raise ExportValidationError(f"Expected object at {path}:{line_number}")
                yield value
    except UnicodeDecodeError as exc:
        raise ExportValidationError(f"Invalid UTF-8 in {path}: {exc}") from exc


def ensure_empty_directory(path: Path) -> None:
    if path.exists():
        resolved = path.resolve()
        if resolved.parent == resolved or path.is_symlink():
            raise ExportValidationError(f"Refusing to clean unsafe path: {path}")
        shutil.rmtree(path)
    path.mkdir(parents=True)


def safe_component(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("._")
    cleaned = cleaned[:100]
    return "unnamed" if not cleaned or cleaned.casefold().split(".", 1)[0] in WINDOWS_RESERVED_NAMES else cleaned


def readable_file_stem(value: str) -> str:
    """Return a safe, human-readable filename component without address data."""
    candidate = unicodedata.normalize("NFKC", value).strip()
    if not candidate or re.fullmatch(r"(?:0x)?[0-9a-fA-F]+", candidate):
        return ""
    if candidate.casefold().split(".", 1)[0] in WINDOWS_RESERVED_NAMES:
        return ""
    if re.match(r"^(?:FUN|THUNK|LAB|SUB|DAT)(?:_|$)", candidate, re.IGNORECASE):
        return ""
    candidate = re.sub(r"(?<!^)(?=[A-Z])", "_", candidate)
    candidate = re.sub(r"[^\w]+", "_", candidate, flags=re.UNICODE).strip("._ ").lower()
    candidate = re.sub(r"_+", "_", candidate)
    if candidate.casefold().split(".", 1)[0] in WINDOWS_RESERVED_NAMES:
        return ""
    return candidate[:72].rstrip("._ ")


def address_key(record: dict[str, Any]) -> tuple[str, int]:
    try:
        address_space = record["address_space"]
        address = record["address"]
        if not isinstance(address_space, str) or not address_space:
            raise ValueError("address_space must be a non-empty string")
        if not isinstance(address, str) or not address:
            raise ValueError("address must be a non-empty string")
        return address_space, int(address.removeprefix("0x"), 16)
    except (KeyError, TypeError, ValueError) as exc:
        raise ExportValidationError(f"Invalid function address record: {record!r}") from exc


def address_text(record: dict[str, Any]) -> str:
    return f"0x{int(str(record['address']).removeprefix('0x'), 16):x}"


def require_fields(record: dict[str, Any], kind: str, line_number: int,
                   strings: tuple[str, ...] = (), integers: tuple[str, ...] = (),
                   booleans: tuple[str, ...] = (), lists: tuple[str, ...] = ()) -> None:
    """Validate the stable JSONL contract before any browse-tree processing."""
    for name in strings:
        if not isinstance(record.get(name), str):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} field {name!r} must be a string")
    for name in integers:
        # bool is an int subclass, but is never valid for an offset, length, or enum value.
        if isinstance(record.get(name), bool) or not isinstance(record.get(name), int):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} field {name!r} must be an integer")
    for name in booleans:
        if not isinstance(record.get(name), bool):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} field {name!r} must be a boolean")
    for name in lists:
        if not isinstance(record.get(name), list):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} field {name!r} must be an array")


def core_order_key(kind: str, record: dict[str, Any]) -> tuple[Any, ...]:
    if kind == "types":
        return (record["category"], record["name"])
    if kind == "calls":
        return (record["caller_address_space"], record["caller"], record["callee_address_space"], record["callee"])
    address = address_key(record)
    if kind == "comments":
        comment_order = {"plate": 0, "pre": 1, "post": 2, "eol": 3, "repeatable": 4, "decompiler": 5}
        return address + (comment_order[record["type"]],)
    return address


def validate_core_record(kind: str, record: dict[str, Any], line_number: int) -> None:
    if kind == "types":
        require_fields(record, kind, line_number,
                       strings=("category", "name", "display_name", "kind", "description"),
                       integers=("length", "alignment"))
        if record["kind"] not in {"struct", "union", "enum", "typedef", "pointer", "array", "type"}:
            raise ExportValidationError(f"types.jsonl:{line_number} has unsupported kind {record['kind']!r}")
        if "components" in record:
            if not isinstance(record["components"], list):
                raise ExportValidationError(f"types.jsonl:{line_number} components must be an array")
            for component in record["components"]:
                if not isinstance(component, dict):
                    raise ExportValidationError(f"types.jsonl:{line_number} component must be an object")
                require_fields(component, kind, line_number,
                               strings=("name", "type", "comment"), integers=("offset", "length"))
        if "values" in record:
            if not isinstance(record["values"], list):
                raise ExportValidationError(f"types.jsonl:{line_number} values must be an array")
            for value in record["values"]:
                if not isinstance(value, dict):
                    raise ExportValidationError(f"types.jsonl:{line_number} enum value must be an object")
                require_fields(value, kind, line_number, strings=("name",), integers=("value",))
        return
    if kind == "globals":
        require_fields(record, kind, line_number,
                       strings=("address_space", "address", "name", "qualified_name", "type"),
                       integers=("length",), lists=("referring_functions", "referring_function_address_spaces"))
    elif kind == "strings":
        require_fields(record, kind, line_number,
                       strings=("address_space", "address", "value", "type"),
                       lists=("referring_functions", "referring_function_address_spaces"))
    elif kind == "comments":
        require_fields(record, kind, line_number,
                       strings=("address_space", "address", "function_address", "type", "text"))
        if record["type"] not in {"plate", "pre", "post", "eol", "repeatable", "decompiler"}:
            raise ExportValidationError(f"comments.jsonl:{line_number} has unsupported comment type {record['type']!r}")
    elif kind == "calls":
        require_fields(record, kind, line_number, strings=("caller", "caller_address_space", "callee", "callee_address_space"))
    elif kind == "imports":
        require_fields(record, kind, line_number,
                       strings=("address_space", "address", "name", "signature", "library"))
    elif kind == "exports":
        require_fields(record, kind, line_number,
                       strings=("address_space", "address", "name", "signature"), integers=("ordinal",))
    else:
        raise ExportValidationError(f"Unsupported core artifact kind {kind!r}")
    if kind in {"globals", "strings"}:
        if not all(isinstance(item, str) for item in record["referring_functions"]):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} referring_functions must contain strings")
        if not all(isinstance(item, str) for item in record["referring_function_address_spaces"]):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} referring_function_address_spaces must contain strings")
        if len(record["referring_functions"]) != len(record["referring_function_address_spaces"]):
            raise ExportValidationError(f"{kind}.jsonl:{line_number} referrer address/space arrays must have equal lengths")
    if kind != "calls":
        address_key(record)


def tokenize_name(value: str) -> set[str]:
    pieces: list[str] = []
    for segment in re.split(r"[^A-Za-z0-9]+", value):
        pieces.extend(TOKEN_RE.findall(segment))
    return {piece.lower() for piece in pieces if piece}


def record_tokens(record: dict[str, Any]) -> set[str]:
    tokens = set()
    for key in ("name", "qualified_name", "namespace"):
        tokens.update(tokenize_name(str(record.get(key, ""))))
    return tokens


def name_words(value: str) -> list[str]:
    words: list[str] = []
    for segment in re.split(r"[^A-Za-z0-9]+", value):
        words.extend(piece.lower() for piece in TOKEN_RE.findall(segment) if piece)
    return words


def categories_for(record: dict[str, Any]) -> tuple[list[str], str]:
    tokens = record_tokens(record)
    categories = sorted(
        category for category, keywords in CATEGORY_TOKENS.items() if tokens.intersection(keywords)
    )
    return categories, "tokenized-name" if categories else "none"


def area_for(record: dict[str, Any], override: dict[str, Any] | None = None) -> tuple[str, str]:
    if override and "area" in override:
        if not isinstance(override["area"], str):
            raise ExportValidationError(f"Source area override for {record['address']} must be a string")
        requested = override["area"].strip().replace("\\", "/")
        parts = [safe_component(part) for part in requested.split("/") if part]
        if not parts or "/".join(parts) != requested or any(part in {".", ".."} for part in parts):
            raise ExportValidationError(f"Invalid source area override for {record['address']}: {requested!r}")
        return "/".join(parts), "override"
    tokens = record_tokens(record)
    for area, keywords in AREA_RULES:
        if tokens.intersection(keywords):
            return area, "tokenized-name"
    return "unknown", "none"


def module_for(record: dict[str, Any], area: str, override: dict[str, Any] | None = None) -> tuple[str, str]:
    if override and "module" in override:
        if not isinstance(override["module"], str):
            raise ExportValidationError(f"Source module override for {record['address']} must be a string")
        requested = override["module"].strip()
        normalized = safe_component(requested)
        if not requested or normalized != requested or requested in {".", ".."}:
            raise ExportValidationError(f"Invalid source module override for {record['address']}: {requested!r}")
        return normalized, "override"
    tokens = record_tokens(record)
    for module, keywords in MODULE_RULES.get(area, ()):
        if tokens.intersection(keywords):
            return module, "tokenized-name"
    # A nested explicit area (for example runtime/names) already conveys its
    # feature.  Reusing its final component avoids a redundant names/names
    # directory while retaining a readable fallback filename.
    return safe_component(area.rsplit("/", 1)[-1]), "area-fallback"


def family_for(record: dict[str, Any], class_name: str, module: str, override: dict[str, Any] | None = None) -> tuple[str, str]:
    if override and "family" in override:
        if not isinstance(override["family"], str):
            raise ExportValidationError(f"Family override for {record['address']} must be a string")
        requested = override["family"]
        family = readable_file_stem(requested)
        if not family:
            raise ExportValidationError(f"Invalid family override for {record['address']}: {requested!r}")
        return family, "override"
    if class_name:
        family = readable_file_stem(class_name)
        if family:
            return family, "class"
    words = name_words(str(record.get("name", "")))
    prefix: list[str] = []
    for word in words:
        if word in FAMILY_VERBS:
            break
        prefix.append(word)
    family = readable_file_stem("_".join(prefix))
    if family and family not in {"global", "function"}:
        return family, "name-prefix"
    return readable_file_stem(module) or "unknown", "module-fallback"


def normalize_class(value: str) -> str:
    candidate = value.strip().split("<", 1)[0].rsplit("::", 1)[-1].strip("`'\"*& ")
    for suffix in ("_Statics", "_NoRegister", "_singleton"):
        if candidate.endswith(suffix):
            candidate = candidate[: -len(suffix)]
    return candidate if CLASS_TOKEN_RE.fullmatch(candidate) else ""


def infer_class(record: dict[str, Any]) -> tuple[str, str]:
    namespace = normalize_class(str(record.get("namespace", "")))
    if namespace:
        return namespace, "namespace"
    for key in ("qualified_name", "name"):
        value = str(record.get(key, ""))
        for pattern in CLASS_PATTERNS:
            match = pattern.search(value)
            if match:
                candidate = normalize_class(match.group("class"))
                if candidate:
                    return candidate, "name-pattern"
    return "", "none"


def candidate_class_from_body(body: bytes) -> str:
    candidates = {
        match.group(1).decode("ascii")
        for match in CLASS_LITERAL_RE.finditer(body[:64 * 1024])
        if CLASS_TOKEN_RE.fullmatch(match.group(1).decode("ascii"))
    }
    return next(iter(candidates)) if len(candidates) == 1 else ""


def load_overrides(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None or not path.exists():
        return {}
    value = read_json(path)
    addresses = value.get("addresses", {})
    if not isinstance(addresses, dict):
        raise ExportValidationError("classification overrides 'addresses' must be an object")
    normalized: dict[str, dict[str, Any]] = {}
    for key, item in addresses.items():
        address = str(key).lower().removeprefix("0x")
        if not address:
            raise ExportValidationError("classification override address must not be empty")
        if not isinstance(item, dict):
            raise ExportValidationError(f"Classification override for {key!r} must be an object")
        if address in normalized:
            raise ExportValidationError(f"Duplicate classification override address after normalization: {address}")
        allowed = {"area", "module", "family", "file_stem", "class", "categories"}
        unknown = sorted(set(item).difference(allowed))
        if unknown:
            raise ExportValidationError(f"Unknown classification override field(s) for {key!r}: {', '.join(unknown)}")
        probe = {"address": address, "name": "OverrideProbe", "qualified_name": "OverrideProbe", "namespace": "Global"}
        area, _ = area_for(probe, item)
        module, _ = module_for(probe, area, item)
        family_for(probe, "", module, item)
        if "file_stem" in item:
            if not isinstance(item["file_stem"], str) or not readable_file_stem(item["file_stem"]):
                raise ExportValidationError(f"Invalid file stem override for {key!r}")
        if "class" in item:
            if not isinstance(item["class"], str) or not normalize_class(item["class"]):
                raise ExportValidationError(f"Invalid class override for {key!r}")
        if "categories" in item and (not isinstance(item["categories"], list) or not all(isinstance(value, str) for value in item["categories"])):
            raise ExportValidationError(f"Invalid category override for {key!r}")
        normalized[address] = item
    return normalized


def load_origin_registry(path: Path | None, executable_sha256: str) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    value = read_json(path)
    if value.get("schema") != UE_ORIGIN_SCHEMA:
        raise ExportValidationError("Unsupported UE origin registry schema")
    if value.get("executable_sha256") != executable_sha256:
        raise ExportValidationError("UE origin registry executable hash does not match this export")
    entries = value.get("entries")
    if not isinstance(entries, list):
        raise ExportValidationError("UE origin registry entries must be an array")
    result: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ExportValidationError("UE origin registry entry must be an object")
        address = str(entry.get("address", "")).lower().removeprefix("0x")
        if entry.get("address_space") != "ram" or not re.fullmatch(r"[0-9a-f]+", address) or address in result:
            raise ExportValidationError(f"Invalid or duplicate UE origin address: {entry.get('address')!r}")
        disposition = entry.get("disposition")
        if disposition not in {"sc6", "ue4-confirmed"}:
            raise ExportValidationError(f"Unsupported UE origin disposition at {address}")
        evidence = entry.get("evidence")
        reviewer = entry.get("review")
        allowed_evidence = {"source", "binary", "control-flow", "reflection", "layout", "serializer"}
        if not isinstance(evidence, list) or len(evidence) < 2 or not all(isinstance(item, dict) and item.get("kind") in allowed_evidence and isinstance(item.get("detail"), str) and item["detail"] for item in evidence) or not any(item["kind"] == "source" for item in evidence) or not any(item["kind"] in allowed_evidence - {"source"} for item in evidence) or not isinstance(reviewer, dict) or reviewer.get("reviewer") != "Hermes" or reviewer.get("status") != "approved":
            raise ExportValidationError(f"Insufficient independent Hermes-reviewed origin evidence at {address}")
        if disposition == "ue4-confirmed":
            module = entry.get("module")
            feature = entry.get("feature")
            baseline = entry.get("baseline")
            if module not in UE_MODULES or not isinstance(feature, str) or safe_component(feature) != feature:
                raise ExportValidationError(f"Invalid UE module or feature at {address}")
            if not isinstance(baseline, dict) or baseline.get("commit") != UE4172_COMMIT or not all(
                isinstance(baseline.get(name), str) and baseline[name] for name in ("source_path", "symbol")
            ):
                raise ExportValidationError(f"Invalid UE baseline provenance at {address}")
        result[address] = entry
    return result


def load_watchlist(path: Path | None, executable_sha256: str) -> dict[str, dict[str, Any]]:
    if path is None:
        return {}
    value = read_json(path)
    if value.get("schema") != WATCHLIST_SCHEMA:
        raise ExportValidationError("Unsupported RE watchlist schema")
    if value.get("executable_sha256") != executable_sha256:
        raise ExportValidationError("RE watchlist executable hash does not match this export")
    entries = value.get("entries")
    if not isinstance(entries, list):
        raise ExportValidationError("RE watchlist entries must be an array")
    result: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ExportValidationError("RE watchlist entry must be an object")
        address = str(entry.get("address", "")).lower().removeprefix("0x")
        if entry.get("address_space") != "ram" or not re.fullmatch(r"[0-9a-f]+", address) or address in result:
            raise ExportValidationError(f"Invalid or duplicate RE watchlist address: {entry.get('address')!r}")
        tags = entry.get("tags", [])
        if not isinstance(tags, list) or not all(isinstance(tag, str) and tag.strip() and not any(ord(character) < 32 or ord(character) == 127 for character in tag) for tag in tags):
            raise ExportValidationError(f"Invalid RE watchlist tags at {address}")
        if (not isinstance(entry.get("note", ""), str) or not isinstance(entry.get("status", "active"), str)
                or not entry.get("status", "active").strip()
                or any(ord(character) < 32 or ord(character) == 127 for character in entry.get("status", "active"))):
            raise ExportValidationError(f"Invalid RE watchlist note/status at {address}")
        if "entry_point" in entry and not isinstance(entry["entry_point"], bool):
            raise ExportValidationError(f"Invalid RE watchlist entry_point at {address}")
        result[address] = {
            "tags": sorted(set(tag.strip() for tag in tags), key=lambda tag: (tag.casefold(), tag)),
            "note": entry.get("note", "").strip(),
            "status": entry.get("status", "active").strip(),
            "entry_point": bool(entry.get("entry_point", False)),
        }
    return result


def apply_classification(
    record: dict[str, Any], body: bytes, overrides: dict[str, dict[str, Any]], origins: dict[str, dict[str, Any]], watchlist: dict[str, dict[str, Any]]
) -> None:
    class_name, class_source = infer_class(record)
    categories, category_source = categories_for(record)
    candidate = candidate_class_from_body(body) if body else ""
    override = overrides.get(str(record["address"]).lower().removeprefix("0x"))
    area, area_source = area_for(record, override)
    module, module_source = module_for(record, area, override)
    sc6_area, sc6_module = area, module
    origin_entry = origins.get(str(record["address"]).lower().removeprefix("0x")) if record.get("address_space") == "ram" else None
    origin = "not-assessed"
    origin_source = "none"
    origin_confidence = "not-assessed"
    ue_module = ""
    ue_feature = ""
    provenance = ""
    if origin_entry:
        if origin_entry["disposition"] == "sc6":
            origin, origin_source, origin_confidence = "sc6", "registry", "reviewed"
        else:
            ue_module, ue_feature = origin_entry["module"], origin_entry["feature"]
            area, module = f"engine/ue4/{ue_module}/{ue_feature}", ue_feature
            area_source = module_source = "ue-origin-registry"
            origin, origin_source, origin_confidence = "ue4-confirmed", "registry", "high"
            provenance = f"{origin_entry['baseline']['source_path']}#{origin_entry['baseline']['symbol']}"
    if override:
        if "file_stem" in override:
            if not isinstance(override["file_stem"], str):
                raise ExportValidationError(f"File stem override for {record['address']} must be a string")
            requested = override["file_stem"]
            stem = readable_file_stem(requested)
            if not stem:
                raise ExportValidationError(f"Invalid file stem override for {record['address']}: {requested!r}")
            record["file_stem_override"] = stem
        if "class" in override:
            if not isinstance(override["class"], str):
                raise ExportValidationError(f"Class override for {record['address']} must be a string")
            class_name = normalize_class(override["class"])
            if not class_name:
                raise ExportValidationError(f"Invalid class override for {record['address']}")
            class_source = "override"
        if "categories" in override:
            requested = override["categories"]
            if not isinstance(requested, list) or not all(isinstance(item, str) for item in requested):
                raise ExportValidationError(f"Invalid category override for {record['address']}")
            categories = sorted(set(requested))
            category_source = "override"
    family, family_source = family_for(record, class_name, module, override)
    record["class"] = class_name
    record["class_source"] = class_source
    record["candidate_class"] = candidate if candidate != class_name else ""
    record["categories"] = categories
    record["category_source"] = category_source
    record["area"] = area
    record["area_source"] = area_source
    record["module"] = module
    record["module_source"] = module_source
    record["family"] = family
    record["family_source"] = family_source
    record["sc6_area"] = sc6_area
    record["sc6_module"] = sc6_module
    record["origin"] = origin
    record["origin_source"] = origin_source
    record["origin_confidence"] = origin_confidence
    record["ue_module"] = ue_module
    record["ue_feature"] = ue_feature
    record["origin_provenance"] = provenance
    watched = watchlist.get(str(record["address"]).lower().removeprefix("0x")) if record.get("address_space") == "ram" else None
    record["watch_tags"] = watched["tags"] if watched else []
    record["watch_note"] = watched["note"] if watched else ""
    record["watch_status"] = watched["status"] if watched else ""
    record["watch_entry_point"] = watched["entry_point"] if watched else False


def relationship_text(values: list[str]) -> str:
    shown = values[:MAX_RELATIONSHIP_ITEMS]
    suffix = "" if len(values) <= len(shown) else f" (+{len(values) - len(shown)} more)"
    return ", ".join(shown) + suffix


def validate_function_shape(record: dict[str, Any], line_number: int) -> tuple[tuple[str, int], str]:
    """Validate every non-body function field before it can influence browse text."""
    require_fields(
        record, "functions", line_number,
        strings=(
            "address_space", "address", "name", "qualified_name", "namespace", "signature",
            "calling_convention", "thunk_target", "body_status", "body_sha256",
        ),
        integers=("body_offset", "body_length"),
        booleans=("is_thunk", "is_external", "no_return"),
    )
    key = address_key(record)
    status = record["body_status"]
    if status not in ALLOWED_STATUSES:
        raise ExportValidationError(f"Unknown body status {status!r} at {key}")
    if bool(record["is_external"]) != (status == "external"):
        raise ExportValidationError(
            f"External-function status invariant failed at {key}: "
            f"is_external={record['is_external']!r}, body_status={status!r}"
        )
    return key, status


def function_identity(record: dict[str, Any]) -> str:
    return function_identity_parts(str(record.get("address_space", "unknown")), str(record["address"]))


def function_identity_parts(address_space: str, address: str) -> str:
    return f"{address_space.casefold()}:{address.lower().removeprefix('0x')}"


def validated_function_names(functions_path: Path) -> dict[str, str]:
    names: dict[str, str] = {}
    previous: tuple[str, int] | None = None
    for line_number, record in enumerate(iter_jsonl(functions_path), 1):
        key, _ = validate_function_shape(record, line_number)
        if previous is not None and key <= previous:
            raise ExportValidationError(f"Function records are not strictly address ordered at {key}")
        previous = key
        address = str(record["address"]).lower().removeprefix("0x")
        identity = function_identity(record)
        names[identity] = f"{record['name']} [0x{address}]"
    return names


def load_relationships(data_dir: Path, names: dict[str, str]) -> dict[str, dict[str, list[str]]]:
    relationships: defaultdict[str, dict[str, list[str]]] = defaultdict(
        lambda: {"callers": [], "callees": [], "globals": [], "strings": []}
    )
    def label(address: str, address_space: str) -> str:
        normalized = str(address).lower().removeprefix("0x")
        identity = function_identity_parts(address_space, normalized)
        return names.get(identity, f"0x{normalized}")
    for item in iter_jsonl(data_dir / "calls.jsonl"):
        caller, callee = str(item.get("caller", "")), str(item.get("callee", ""))
        caller_id = function_identity_parts(str(item["caller_address_space"]), caller)
        callee_id = function_identity_parts(str(item["callee_address_space"]), callee)
        if caller_id and callee_id:
            relationships[caller_id]["callees"].append(label(callee, str(item["callee_address_space"])))
            relationships[callee_id]["callers"].append(label(caller, str(item["caller_address_space"])))
    for kind, display in (("globals", "globals"), ("strings", "strings")):
        for item in iter_jsonl(data_dir / f"{kind}.jsonl"):
            value = str(item.get("name") if kind == "globals" else item.get("value", ""))
            value = " ".join(value.split())[:96]
            if kind == "strings":
                value = json.dumps(value, ensure_ascii=False)
            for address, address_space in zip(item.get("referring_functions", []), item.get("referring_function_address_spaces", [])):
                relationships[function_identity_parts(str(address_space), str(address))][display].append(value)
    for values in relationships.values():
        for key, entries in values.items():
            values[key] = sorted(set(entry for entry in entries if entry), key=lambda entry: (entry.casefold(), entry))
    return dict(relationships)


def render_function(record: dict[str, Any], body: bytes, relationships: dict[str, dict[str, list[str]]] | None = None) -> bytes:
    region = safe_component(f"{record['name']}_{record['address']}")
    lines = [
        f"#pragma region {region}\n",
        "// GhidraCalibur Function\n",
        f"// Address: {address_text(record)}\n",
        f"// Name: {record['name']}\n",
        f"// Qualified name: {record.get('qualified_name', '')}\n",
        f"// Signature: {' '.join(str(record.get('signature', '')).split())}\n",
        f"// Body status: {record['body_status']}\n",
    ]
    if record.get("class"):
        lines.append(f"// Class: {record['class']} ({record['class_source']})\n")
    if record.get("candidate_class"):
        lines.append(f"// Candidate class: {record['candidate_class']} (string-literal, low confidence)\n")
    if record.get("categories"):
        lines.append(f"// Categories: {', '.join(record['categories'])}\n")
    lines.append(
        f"// Placement: {record.get('area', 'unknown')}/{record.get('module', 'unknown')}/{record.get('family', 'unknown')} "
        f"({record.get('area_source', 'none')}; {record.get('module_source', 'none')}; {record.get('family_source', 'none')})\n"
    )
    if record.get("watch_tags"):
        lines.append(f"// Watchlist: {', '.join(record['watch_tags'])} ({record.get('watch_status', 'active')})\n")
    relation = (relationships or {}).get(function_identity(record), {})
    for label, key in (("Callers", "callers"), ("Calls", "callees"), ("Globals", "globals"), ("Strings", "strings")):
        if relation.get(key):
            lines.append(f"// {label}: {relationship_text(relation[key])}\n")
    prefix = "".join(lines).encode("utf-8")
    suffix = f"\n#pragma endregion // {region}\n\n".encode("utf-8")
    # Ghidra can return CRLF, LF, or a mixture of both from the decompiler.
    # The generated shard also has LF-only wrapper and index lines; normalize the
    # body at this presentation boundary so opening it never dirties an immutable
    # generation through Visual Studio's line-ending prompt.
    normalized_body = body.replace(b"\r\n", b"\n").replace(b"\r", b"\n").rstrip(b"\n")
    return prefix + normalized_body + suffix


class ShardWriter:
    def __init__(
        self,
        workspace_dir: Path,
        area: str,
        module: str,
        family: str,
        path_owners: dict[str, tuple[str, str]],
    ):
        self.workspace_dir = workspace_dir
        self.area = area
        self.module = module
        self.family = family
        self.path_owners = path_owners
        self.items: list[tuple[dict[str, Any], bytes]] = []
        self.size = 0
        self.number_by_space: defaultdict[str, int] = defaultdict(int)

    def add(self, record: dict[str, Any], rendered: bytes) -> None:
        requested_stem = str(record.get("file_stem_override", ""))
        active_override = next((str(item[0]["file_stem_override"]) for item in self.items if item[0].get("file_stem_override")), "")
        if self.items and (
            len(self.items) >= MAX_FUNCTIONS_PER_SHARD
            or self.size + len(rendered) > MAX_BYTES_PER_SHARD
            or self.items[0][0]["address_space"] != record["address_space"]
            or bool(requested_stem) != bool(active_override)
            or (requested_stem and requested_stem != active_override)
        ):
            self.flush()
        self.items.append((record, rendered))
        self.size += len(rendered)

    def flush(self) -> None:
        if not self.items:
            return
        space = safe_component(str(self.items[0][0]["address_space"]))
        self.number_by_space[space] += 1
        number = self.number_by_space[space]
        stem = next((str(record["file_stem_override"]) for record, _ in self.items if record.get("file_stem_override")), "")
        if not stem and len(self.items) == 1:
            stem = readable_file_stem(str(self.items[0][0].get("name", "")))
        if not stem:
            stem = self.family or readable_file_stem(self.module) or "unknown"
        first = str(self.items[0][0]["address"]).lower()
        last = str(self.items[-1][0]["address"]).lower()
        area_parts = [safe_component(part) for part in self.area.split("/")]
        relative = Path("src", *area_parts)
        # The fallback module repeats the final area component (for example
        # gameplay/replay -> replay).  Keep its files in that area instead of
        # producing a distracting replay/replay directory.
        if self.module != area_parts[-1]:
            relative /= self.module
        if self.family != self.module:
            relative /= self.family
        if space != "ram":
            relative /= space
        relative /= f"{stem}.cpp" if len(self.items) == 1 else f"{stem}_{number:04d}.cpp"
        if len(relative.parts) > 9 or len(relative.as_posix()) > MAX_SOURCE_RELATIVE_PATH:
            raise ExportValidationError(f"Generated source path is too deep or long: {relative.as_posix()}")
        relative_key = relative.as_posix()
        owner = (self.area, self.module, self.family)
        if relative_key.casefold() in self.path_owners:
            relative = relative.with_name(f"{stem}_{number:04d}.cpp")
            relative_key = relative.as_posix()
            suffix = 2
            while relative_key.casefold() in self.path_owners:
                relative = relative.with_name(f"{stem}_{number:04d}_{suffix}.cpp")
                relative_key = relative.as_posix()
                suffix += 1
        self.path_owners[relative_key.casefold()] = owner
        path = self.workspace_dir / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        header = (
            "// Generated by GhidraCalibur. Do not hand-edit.\n"
            f"// Workspace areas: {', '.join(sorted({str(record.get('area', 'unknown')) for record, _ in self.items}))}; "
            f"module: {self.module}; family: {self.family}; shard stem: {stem}; address shard: {first}-{last}\n\n"
        ).encode("utf-8")
        line = header.count(b"\n") + 1
        with path.open("wb") as output:
            output.write(header)
            for record, rendered in self.items:
                record["browse_file"] = relative.as_posix()
                record["browse_line"] = line
                record["browse_shard_stem"] = stem
                output.write(rendered)
                line += rendered.count(b"\n")
        canonical_hash = sha256_file(path)
        for record, _ in self.items:
            record["canonical_shard_sha256"] = canonical_hash
        self.items.clear()
        self.size = 0


class SourceShardWriter:
    """Keeps one bounded address-ordered shard buffer per deterministic SC6 module."""

    def __init__(self, workspace_dir: Path):
        self.workspace_dir = workspace_dir
        self.writers: dict[tuple[str, str, str], ShardWriter] = {}
        self.path_owners: dict[str, tuple[str, str, str]] = {}

    @staticmethod
    def source_group(area: str, module: str) -> tuple[str, str]:
        parts = [safe_component(part) for part in area.split("/")]
        # Nested areas whose fallback repeats their leaf (runtime/names ->
        # names) name the same visible folder as a parent-area feature module
        # (runtime + names).  Canonicalize both to one writer so they share a
        # single body copy instead of competing for the same shard path.
        if len(parts) > 1 and module == parts[-1]:
            return "/".join(parts[:-1]), module
        return "/".join(parts), module

    def add(self, record: dict[str, Any], rendered: bytes) -> None:
        area = str(record.get("area") or "unknown")
        module = str(record.get("module") or safe_component(area.rsplit("/", 1)[-1]))
        family = str(record.get("family") or module)
        group = self.source_group(area, module)
        key = (*group, family)
        writer = self.writers.get(key)
        if writer is None:
            writer = ShardWriter(self.workspace_dir, key[0], key[1], key[2], self.path_owners)
            self.writers[key] = writer
        writer.add(record, rendered)

    def flush(self) -> None:
        for key in sorted(self.writers):
            self.writers[key].flush()


def validate_and_write_functions(
    export_dir: Path,
    workspace_dir: Path,
    overrides: dict[str, dict[str, Any]],
    origins: dict[str, dict[str, Any]],
    relationships: dict[str, dict[str, list[str]]],
    watchlist: dict[str, dict[str, Any]],
) -> tuple[list[dict[str, Any]], Counter[str]]:
    functions_path = export_dir / "functions.jsonl"
    bodies_path = export_dir / "bodies.dat"
    if not functions_path.is_file() or not bodies_path.is_file():
        raise ExportValidationError("Structured export is missing functions.jsonl or bodies.dat")

    records: list[dict[str, Any]] = []
    statuses: Counter[str] = Counter()
    seen: set[tuple[str, int]] = set()
    previous_key: tuple[str, int] | None = None
    body_size = bodies_path.stat().st_size
    body_intervals: list[tuple[int, int, tuple[str, int]]] = []
    writer = SourceShardWriter(workspace_dir)

    with bodies_path.open("rb") as bodies:
        for line_number, record in enumerate(iter_jsonl(functions_path), 1):
            key, status = validate_function_shape(record, line_number)
            if previous_key is not None and key <= previous_key:
                raise ExportValidationError(f"Function records are not strictly address ordered at {key}")
            if key in seen:
                raise ExportValidationError(f"Duplicate function address: {key}")
            previous_key = key
            seen.add(key)

            statuses[status] += 1
            try:
                offset = int(record["body_offset"])
                length = int(record["body_length"])
            except (KeyError, TypeError, ValueError) as exc:
                raise ExportValidationError(f"Invalid body bounds at {key}") from exc

            body = b""
            if status == "ok":
                if offset < 0 or length <= 0 or offset + length > body_size:
                    raise ExportValidationError(
                        f"Out-of-bounds or empty body at {key}: offset={offset}, length={length}, file={body_size}"
                    )
                bodies.seek(offset)
                body = bodies.read(length)
                if len(body) != length:
                    raise ExportValidationError(f"Truncated body at {key}")
                try:
                    body.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise ExportValidationError(f"Invalid UTF-8 body at {key}: {exc}") from exc
                if not re.fullmatch(r"[0-9a-f]{64}", record["body_sha256"]):
                    raise ExportValidationError(f"Invalid body SHA-256 at {key}")
                if sha256_bytes(body) != record["body_sha256"]:
                    raise ExportValidationError(f"Body SHA-256 mismatch at {key}")
                body_intervals.append((offset, offset + length, key))
                record["canonical_body_length"] = length
            elif offset != -1 or length != 0 or record["body_sha256"]:
                raise ExportValidationError(f"Non-body status has body data at {key}")
            else:
                record["canonical_body_length"] = 0

            apply_classification(record, body, overrides, origins, watchlist)
            if body:
                writer.add(record, render_function(record, body, relationships))
            records.append(record)
        writer.flush()
        expected_offset = 0
        for start, end, key in sorted(body_intervals):
            if start != expected_offset:
                raise ExportValidationError(
                    f"Body ranges overlap or leave unreferenced bytes before {key}: expected={expected_offset}, actual={start}"
                )
            expected_offset = end
        if expected_offset != body_size:
            raise ExportValidationError(
                f"bodies.dat contains trailing unreferenced bytes: referenced={expected_offset}, size={body_size}"
            )

    for record in records:
        if record["body_status"] != "ok":
            continue
        shard = workspace_dir / str(record.get("browse_file", ""))
        shard_hash = str(record.get("canonical_shard_sha256", ""))
        if not shard.is_file() or not re.fullmatch(r"[0-9a-f]{64}", shard_hash) or sha256_file(shard) != shard_hash:
            raise ExportValidationError(f"Canonical shard verification failed at {address_key(record)}")

    data_dir = workspace_dir / ".ghidra" / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    with (data_dir / "functions.jsonl").open("w", encoding="utf-8", newline="\n") as output:
        for record in records:
            published = {key: value for key, value in record.items() if key not in {"body_offset", "body_length"}}
            output.write(json.dumps(published, sort_keys=True, ensure_ascii=False) + "\n")
    authoritative_fields = (
        "address_space", "address", "name", "qualified_name", "namespace", "signature",
        "calling_convention", "thunk_target", "is_thunk", "is_external", "no_return", "origin",
        "origin_source", "origin_confidence", "ue_module", "ue_feature", "origin_provenance",
        "area", "area_source", "module", "module_source", "family", "family_source",
        "watch_tags", "watch_note", "watch_status", "watch_entry_point",
    )
    with (data_dir / "function_metadata.jsonl").open("w", encoding="utf-8", newline="\n") as output:
        for record in records:
            metadata = {field: record.get(field) for field in authoritative_fields}
            output.write(json.dumps(metadata, sort_keys=True, ensure_ascii=False) + "\n")
    return records, statuses


def validate_core_artifacts(export_dir: Path, workspace_dir: Path) -> dict[str, int]:
    counts: dict[str, int] = {}
    data_dir = workspace_dir / ".ghidra" / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    for name in ("types", "globals", "strings", "comments", "calls", "imports", "exports"):
        source = export_dir / f"{name}.jsonl"
        if not source.is_file():
            raise ExportValidationError(f"Missing core export artifact: {source}")
        previous: tuple[Any, ...] | None = None
        seen: set[tuple[Any, ...]] = set()
        count = 0
        for line_number, record in enumerate(iter_jsonl(source), 1):
            validate_core_record(name, record, line_number)
            key = core_order_key(name, record)
            if previous is not None and key <= previous:
                raise ExportValidationError(f"{name}.jsonl:{line_number} is not strictly ordered")
            if key in seen:
                raise ExportValidationError(f"{name}.jsonl:{line_number} has duplicate key {key!r}")
            previous = key
            seen.add(key)
            count += 1
        counts[name] = count
        shutil.copyfile(source, data_dir / source.name)
    return counts


def write_function_indexes(records: list[dict[str, Any]], index_dir: Path, partial: bool) -> None:
    index_dir.mkdir(parents=True, exist_ok=True)
    with (index_dir / "functions.csv").open("w", encoding="utf-8", newline="") as output:
        fields = [
            "address_space", "address", "name", "qualified_name", "signature", "body_status",
            "class", "class_source", "candidate_class", "categories", "category_source", "area", "area_source",
            "module", "module_source", "family", "family_source", "sc6_area", "sc6_module", "origin", "origin_source",
            "origin_confidence", "ue_module", "ue_feature", "origin_provenance", "browse_shard_stem",
            "watch_tags", "watch_note", "watch_status", "watch_entry_point",
            "browse_file", "browse_line",
        ]
        writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for record in records:
            row = dict(record)
            row["categories"] = ";".join(record.get("categories", []))
            row["watch_tags"] = ";".join(record.get("watch_tags", []))
            writer.writerow(row)

    for name, selector in (
        ("classes.csv", lambda record: record.get("class")),
        ("areas.csv", lambda record: record.get("area")),
        ("modules.csv", lambda record: record.get("module")),
        ("families.csv", lambda record: record.get("family")),
        ("origins.csv", lambda record: record.get("origin")),
    ):
        with (index_dir / name).open("w", encoding="utf-8", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=("group", "address", "name", "signature", "browse_file", "browse_line"))
            writer.writeheader()
            for record in records:
                group = selector(record)
                if not group:
                    continue
                writer.writerow({
                    "group": group,
                    "address": address_text(record),
                    "name": record["name"],
                    "signature": record["signature"],
                    "browse_file": record.get("browse_file", ""),
                    "browse_line": record.get("browse_line", ""),
                })
    confirmed = [record for record in records if record.get("origin") == "ue4-confirmed"]
    with (index_dir / "ue4_confirmed.csv").open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=("address", "name", "ue_module", "ue_feature", "origin_provenance", "browse_file", "browse_line"))
        writer.writeheader()
        for record in confirmed:
            writer.writerow({"address": address_text(record), "name": record["name"], "ue_module": record["ue_module"], "ue_feature": record["ue_feature"], "origin_provenance": record["origin_provenance"], "browse_file": record.get("browse_file", ""), "browse_line": record.get("browse_line", "")})
def write_core_indexes(workspace_dir: Path) -> None:
    index_dir = workspace_dir / ".ghidra" / "index"
    data_dir = workspace_dir / ".ghidra" / "data"
    index_dir.mkdir(parents=True, exist_ok=True)
    fields_by_kind = {
        "strings": ("address_space", "address", "value", "type", "referring_functions", "referring_function_address_spaces"),
        "globals": ("address_space", "address", "name", "qualified_name", "type", "length", "referring_functions", "referring_function_address_spaces"),
        "comments": ("address_space", "address", "function_address", "type", "text"),
        "calls": ("caller", "caller_address_space", "callee", "callee_address_space"),
        "imports": ("address_space", "address", "name", "signature", "library"),
        "exports": ("address_space", "address", "name", "signature", "ordinal"),
    }
    for name, fields in fields_by_kind.items():
        with (index_dir / f"{name}.csv").open("w", encoding="utf-8", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=fields)
            writer.writeheader()
            for record in iter_jsonl(data_dir / f"{name}.jsonl"):
                row = dict(record)
                for field in fields:
                    if isinstance(row.get(field), list):
                        row[field] = json.dumps(row[field], ensure_ascii=False)
                writer.writerow(row)
    return
    """Removed legacy Markdown index emitter.  Published workspaces use CSV only.
    specs = {
        "strings": ("String Index", lambda item: f"`0x{item.get('address', '')}` `{item.get('value', '')}`"),
        "globals": ("Global Index", lambda item: f"`0x{item.get('address', '')}` `{item.get('qualified_name', item.get('name', ''))}`: `{item.get('type', '')}`"),
        "comments": ("Comment Index", lambda item: f"`0x{item.get('address', '')}` **{item.get('type', '')}** — {str(item.get('text', '')).replace(chr(10), ' ')}"),
        "calls": ("Call Graph", lambda item: f"`0x{item.get('caller', '')}` → `0x{item.get('callee', '')}`"),
    }
    for name, (title, formatter) in specs.items():
        with (index_dir / f"{name}.md").open("w", encoding="utf-8", newline="\n") as output:
            output.write(f"# {title}\n\n")
            for item in iter_jsonl(data_dir / f"{name}.jsonl"):
                output.write(f"- {formatter(item)}\n")


    """


def c_identifier(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not result or result[0].isdigit():
        result = "type_" + result
    return result


def write_type_browse(workspace_dir: Path) -> None:
    groups: defaultdict[tuple[str, ...], list[dict[str, Any]]] = defaultdict(list)
    owners: dict[str, str] = {}
    for item in iter_jsonl(workspace_dir / ".ghidra" / "data" / "types.jsonl"):
        category = str(item.get("category", "")).strip("/")
        group = tuple(safe_component(part) for part in category.split("/") if part) or ("unknown",)
        key = "/".join(group).casefold()
        previous = owners.setdefault(key, category)
        if previous != category:
            raise ExportValidationError(f"Distinct type categories normalize to the same path: {previous!r}, {category!r}")
        groups[group].append(item)
    for group, items in sorted(groups.items()):
        stem = group[-1]
        for chunk_number, start in enumerate(range(0, len(items), MAX_DECLARATIONS_PER_HEADER), 1):
            output_path = workspace_dir / "include" / "types" / Path(*group) / f"{stem}_{chunk_number:04d}.h"
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with output_path.open("w", encoding="utf-8", newline="\n") as output:
                output.write("// Generated type declarations for browsing only. Do not compile.\n\n")
                for item in items[start:start + MAX_DECLARATIONS_PER_HEADER]:
                    name = c_identifier(str(item.get("name", "unnamed")))
                    kind = item.get("kind")
                    output.write(f"// Category: {item.get('category', '')}; length: {item.get('length', -1)}\n")
                    if kind in {"struct", "union"}:
                        output.write(f"{kind} {name} {{\n")
                        for component in item.get("components", []):
                            field_name = c_identifier(str(component.get("name") or f"field_{component.get('offset', 0):x}"))
                            output.write(f"    /* +0x{int(component.get('offset', 0)):x} */ {component.get('type', 'byte')} {field_name};")
                            if component.get("comment"):
                                output.write(f" // {str(component['comment']).replace(chr(10), ' ')}")
                            output.write("\n")
                        output.write("};\n\n")
                    elif kind == "enum":
                        output.write(f"enum {name} {{\n")
                        for value in item.get("values", []):
                            output.write(f"    {c_identifier(str(value.get('name', 'value')))} = {value.get('value', 0)},\n")
                        output.write("};\n\n")
                    elif kind == "typedef":
                        output.write(f"typedef {item.get('base_type', 'void')} {name};\n\n")
                    else:
                        output.write(f"// {item.get('display_name', name)}\n\n")


def write_global_browse(workspace_dir: Path, functions: list[dict[str, Any]]) -> None:
    function_groups = {function_identity(record): (str(record.get("area") or "unknown"), str(record.get("module") or "unknown")) for record in functions}
    groups: defaultdict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for item in iter_jsonl(workspace_dir / ".ghidra" / "data" / "globals.jsonl"):
        related = [function_groups.get(function_identity_parts(str(address_space), str(address))) for address, address_space in zip(item.get("referring_functions", []), item.get("referring_function_address_spaces", []))]
        related = [group for group in related if group]
        counts = Counter(related)
        if not counts:
            group = ("unknown", "unknown")
        else:
            ranked = sorted(counts.items(), key=lambda item: (-item[1], item[0][0], item[0][1]))
            group, top_count = ranked[0]
            # A tie or a weak (60% or lower) plurality is not a trustworthy
            # subsystem assignment. Keep those globals visibly shared.
            if len(ranked) > 1 and (top_count * 2 <= len(related) or top_count * 5 <= len(related) * 3):
                group = ("shared", "shared")
        groups[group].append(item)
    for (area, module), items in sorted(groups.items()):
        for chunk_number, start in enumerate(range(0, len(items), MAX_DECLARATIONS_PER_HEADER), 1):
            # Shared globals intentionally have no artificial second directory:
            # include/globals/shared/globals_0001.h.
            group_path = Path(*area.split("/")) if area == module else Path(*area.split("/")) / module
            output_path = workspace_dir / "include" / "globals" / group_path / f"globals_{chunk_number:04d}.h"
            output_path.parent.mkdir(parents=True, exist_ok=True)
            with output_path.open("w", encoding="utf-8", newline="\n") as output:
                output.write("// Generated SC6 globals for browsing only. Do not compile.\n\n")
                for item in items[start:start + MAX_DECLARATIONS_PER_HEADER]:
                    output.write(f"// {item['address_space']}:0x{item['address']}\n")
                    output.write(f"extern {item['type']} {c_identifier(item['name'])};\n\n")


def write_import_export_headers(workspace_dir: Path) -> None:
    data_dir = workspace_dir / ".ghidra" / "data"
    imports: defaultdict[str, list[dict[str, Any]]] = defaultdict(list)
    display_names: defaultdict[str, set[str]] = defaultdict(set)
    for item in iter_jsonl(data_dir / "imports.jsonl"):
        library = str(item.get("library") or "unknown")
        canonical_library = library.casefold()
        imports[canonical_library].append(item)
        display_names[canonical_library].add(library)
    paths: dict[str, str] = {}
    for canonical_library in sorted(imports):
        filename = safe_component(canonical_library) + ".h"
        previous = paths.setdefault(filename, canonical_library)
        if previous != canonical_library:
            raise ExportValidationError(
                f"Distinct import libraries normalize to the same header path: {previous!r}, {canonical_library!r}"
            )
    for canonical_library, records in sorted(imports.items()):
        library = sorted(display_names[canonical_library], key=lambda value: (value.casefold(), value))[0]
        output_path = workspace_dir / "include" / "imports" / f"{safe_component(canonical_library)}.h"
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8", newline="\n") as output:
            output.write(f"// Imports from {library}; generated for browsing only. Do not compile.\n\n")
            for item in sorted(records, key=lambda record: (record["name"], address_key(record))):
                output.write(f"// {item['address_space']}:0x{item['address']}\n{item['signature']};\n\n")

    output_path = workspace_dir / "include" / "exports" / "sc6_exports.h"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    exports = list(iter_jsonl(data_dir / "exports.jsonl"))
    with output_path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("// Actual PE exports from SoulcaliburVI.exe; generated for browsing only. Do not compile.\n\n")
        if not exports:
            output.write("// The validated PE export table contains no local export entry points.\n")
        for item in sorted(exports, key=lambda record: address_key(record)):
            ordinal = "" if int(item["ordinal"]) < 0 else f" ordinal {item['ordinal']}"
            output.write(f"// {item['address_space']}:0x{item['address']}{ordinal}\n")
            if item["signature"]:
                output.write(f"{item['signature']};\n\n")
            else:
                output.write(f"// {item['name']}: prototype unavailable in Ghidra.\n\n")


def navigation_reference(record: dict[str, Any]) -> str:
    base = f"{record.get('name', 'unknown')} [0x{str(record.get('address', '')).lower()}]"
    if record.get("browse_file"):
        return f"{base} — {record['browse_file']}:{record.get('browse_line', 0)}"
    return base


def write_navigation_headers(workspace_dir: Path, records: list[dict[str, Any]]) -> None:
    navigation_dir = workspace_dir / "include" / "navigation"
    navigation_dir.mkdir(parents=True, exist_ok=True)
    by_identity = {function_identity(record): record for record in records}
    watched = [record for record in records if record.get("watch_tags")]
    entry_points = [record for record in records if record.get("watch_entry_point")]
    for filename, title, items in (("watchlist.h", "Reverse-engineering watchlist", watched), ("entry_points.h", "Approved navigation entry points", entry_points)):
        with (navigation_dir / filename).open("w", encoding="utf-8", newline="\n") as output:
            output.write(f"// {title}; generated for browsing only. Do not compile.\n\n")
            if not items:
                output.write("// No entries are currently recorded. Add an executable-bound reviewed entry to re_watchlist.json.\n")
            for record in sorted(items, key=address_key):
                output.write(f"// {navigation_reference(record)}\n")
                output.write(f"// Tags: {', '.join(record.get('watch_tags', []))}; Status: {record.get('watch_status', 'active')}\n")
                if record.get("watch_note"):
                    output.write(f"// Note: {' '.join(str(record['watch_note']).split())}\n")
                output.write("\n")

    callers: Counter[str] = Counter()
    bridges: list[tuple[dict[str, Any], dict[str, Any]]] = []
    unknown_entries: list[tuple[dict[str, Any], dict[str, Any]]] = []
    for item in iter_jsonl(workspace_dir / ".ghidra" / "data" / "calls.jsonl"):
        caller = by_identity.get(function_identity_parts(str(item["caller_address_space"]), str(item["caller"])))
        callee = by_identity.get(function_identity_parts(str(item["callee_address_space"]), str(item["callee"])))
        if callee:
            callers[function_identity(callee)] += 1
        if caller and callee and (caller.get("area"), caller.get("module")) != (callee.get("area"), callee.get("module")):
            bridges.append((caller, callee))
        if caller and callee and caller.get("area") != "unknown" and callee.get("area") == "unknown" and readable_file_stem(str(callee.get("name", ""))):
            unknown_entries.append((caller, callee))
    global_hubs: Counter[str] = Counter()
    string_hubs: Counter[str] = Counter()
    for kind, target in (("globals", global_hubs), ("strings", string_hubs)):
        for item in iter_jsonl(workspace_dir / ".ghidra" / "data" / f"{kind}.jsonl"):
            for address, address_space in zip(item["referring_functions"], item["referring_function_address_spaces"]):
                identity = function_identity_parts(str(address_space), str(address))
                if identity in by_identity:
                    target[identity] += 1
    def top_records(counter: Counter[str]) -> list[tuple[dict[str, Any], int]]:
        return [(by_identity[identity], count) for identity, count in sorted(counter.items(), key=lambda item: (-item[1], item[0])) if identity in by_identity][:MAX_NAVIGATION_ITEMS]
    with (navigation_dir / "insights.h").open("w", encoding="utf-8", newline="\n") as output:
        output.write("// Heuristic navigation insights derived from the structured export. They are not proof of behavior or provenance.\n\n")
        for title, values in (("High fan-in functions", top_records(callers)), ("Global-reference hubs", top_records(global_hubs)), ("String-reference hubs", top_records(string_hubs))):
            output.write(f"// {title}\n")
            for record, count in values:
                output.write(f"// {navigation_reference(record)} — {count} references\n")
            if not values:
                output.write("// No qualifying records.\n")
            output.write("\n")
        output.write("// Cross-subsystem bridge calls\n")
        for caller, callee in sorted(bridges, key=lambda pair: (address_key(pair[0]), address_key(pair[1])))[:MAX_NAVIGATION_ITEMS]:
            output.write(f"// {navigation_reference(caller)} -> {navigation_reference(callee)}\n")
        output.write("\n// Named entry points into unknown code\n")
        for caller, callee in sorted(unknown_entries, key=lambda pair: (address_key(pair[0]), address_key(pair[1])))[:MAX_NAVIGATION_ITEMS]:
            output.write(f"// {navigation_reference(caller)} -> {navigation_reference(callee)}\n")


def header_tree_hash(workspace_dir: Path, relative_root: str) -> str:
    root = workspace_dir / relative_root
    values = {
        path.relative_to(workspace_dir).as_posix(): sha256_file(path)
        for path in sorted(root.rglob("*")) if path.is_file()
    } if root.exists() else {}
    return sha256_bytes(canonical_json(values))


def xml(value: str) -> str:
    return html.escape(value, quote=True)


def deterministic_guid(seed: str) -> str:
    digest = hashlib.md5(seed.encode("utf-8"), usedforsecurity=False).hexdigest().upper()
    return "{" + "-".join((digest[:8], digest[8:12], digest[12:16], digest[16:20], digest[20:])) + "}"


def filter_for(relative: Path) -> str:
    parts = relative.parts
    if not parts or parts[0] not in {"src", "include"}:
        return ""
    return "\\".join(parts[:-1])


def write_visual_studio_workspace(workspace_dir: Path) -> None:
    files = sorted(
        path for root in (workspace_dir / "src", workspace_dir / "include") if root.exists()
        for path in root.rglob("*") if path.is_file()
    )
    relatives = [path.relative_to(workspace_dir) for path in files]
    items = "\n".join(f'    <None Include="{xml(str(path).replace(os.sep, chr(92)))}" />' for path in relatives)
    project = workspace_dir / "GhidraCalibur.vcxproj"
    project.write_text(
        f'''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion><Keyword>MakeFileProj</Keyword>
    <ProjectGuid>{PROJECT_GUID}</ProjectGuid><RootNamespace>GhidraCalibur</RootNamespace>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration"><ConfigurationType>Utility</ConfigurationType><PlatformToolset>v143</PlatformToolset></PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ItemGroup>
{items}
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
''',
        encoding="utf-8",
        newline="\n",
    )

    filters = sorted({filter_for(path) for path in relatives if filter_for(path)})
    filter_items = "\n".join(
        f'    <Filter Include="{xml(name)}"><UniqueIdentifier>{deterministic_guid(name)}</UniqueIdentifier></Filter>'
        for name in filters
    )
    file_items = "\n".join(
        f'    <None Include="{xml(str(path).replace(os.sep, chr(92)))}"><Filter>{xml(filter_for(path))}</Filter></None>'
        for path in relatives
    )
    (workspace_dir / "GhidraCalibur.vcxproj.filters").write_text(
        f'''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
{filter_items}
  </ItemGroup>
  <ItemGroup>
{file_items}
  </ItemGroup>
</Project>
''',
        encoding="utf-8",
        newline="\n",
    )

    (workspace_dir / "GhidraCalibur.sln").write_text(
        f'''Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}") = "GhidraCalibur", "GhidraCalibur.vcxproj", "{PROJECT_GUID}"
EndProject
Global
\tGlobalSection(SolutionConfigurationPlatforms) = preSolution
\t\tDebug|x64 = Debug|x64
\t\tRelease|x64 = Release|x64
\tEndGlobalSection
\tGlobalSection(ProjectConfigurationPlatforms) = postSolution
\t\t{PROJECT_GUID}.Debug|x64.ActiveCfg = Debug|x64
\t\t{PROJECT_GUID}.Release|x64.ActiveCfg = Release|x64
\tEndGlobalSection
EndGlobal
''',
        encoding="utf-8-sig",
        newline="\r\n",
    )


def validate_project_references(workspace_dir: Path) -> None:
    import xml.etree.ElementTree as ET

    for name in ("GhidraCalibur.vcxproj", "GhidraCalibur.vcxproj.filters"):
        try:
            tree = ET.parse(workspace_dir / name)
        except ET.ParseError as exc:
            raise ExportValidationError(f"Generated XML is invalid: {name}: {exc}") from exc
        for element in tree.iter():
            include = element.attrib.get("Include")
            if include and element.tag.endswith("None"):
                target = workspace_dir / include
                if not target.exists():
                    raise ExportValidationError(f"Generated project references missing file: {include}")


def artifact_hashes(workspace_dir: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    excluded = {"content_manifest.json", "run_report.json", ".complete"}
    for path in sorted(workspace_dir.rglob("*")):
        if path.is_file() and path.name not in excluded:
            result[path.relative_to(workspace_dir).as_posix()] = sha256_file(path)
    return result


def previous_failures(path: Path | None, export_info: dict[str, Any]) -> int | None:
    if path is None or not path.is_file():
        return None
    previous = read_json(path)
    content = previous.get("content", {})
    identity = content.get("program", {})
    options = content.get("export_options", {})
    if (
        identity.get("executable_sha256") == export_info.get("executable_sha256")
        and options.get("timeout_seconds") == export_info.get("timeout_seconds")
        and options.get("decompiler_options") == export_info.get("decompiler_options", {})
    ):
        return int(content.get("coverage", {}).get("failures", 0))
    return None


def enforce_coverage(
    statuses: Counter[str], previous_failure_count: int | None, allow_partial: bool
) -> tuple[bool, dict[str, Any]]:
    cancelled = statuses["cancelled"]
    failures = sum(statuses[name] for name in FAILURE_STATUSES)
    eligible = sum(statuses.values()) - sum(statuses[name] for name in NON_BODY_STATUSES)
    rate = failures / eligible if eligible else 0.0
    if cancelled:
        raise ExportValidationError(f"Export contains {cancelled} cancelled functions")
    violations: list[str] = []
    if failures > 250:
        violations.append(f"{failures} decompiler failures exceeds 250")
    if rate > 0.0025:
        violations.append(f"decompiler failure rate {rate:.4%} exceeds 0.25%")
    if previous_failure_count is not None and failures > previous_failure_count:
        violations.append(f"failure count regressed from {previous_failure_count} to {failures}")
    if violations and not allow_partial:
        raise ExportValidationError("; ".join(violations))
    return bool(violations), {
        "statuses": dict(sorted(statuses.items())),
        "eligible_functions": eligible,
        "failures": failures,
        "failure_rate": rate,
        "overridden_violations": violations if allow_partial else [],
    }


def write_manifest(
    workspace_dir: Path,
    export_info: dict[str, Any],
    coverage: dict[str, Any],
    partial: bool,
    pipeline_sha256: str,
) -> tuple[str, Path]:
    program = {
        key: export_info.get(key, "")
        for key in (
            "program_name", "executable_sha256", "image_base", "language", "compiler_spec",
            "ghidra_version", "modification_number",
        )
    }
    content = {
        "schema": SCHEMA,
        "pipeline_sha256": pipeline_sha256,
        "program": program,
        "export_options": {
            "timeout_seconds": export_info.get("timeout_seconds"),
            "decompiler_options": export_info.get("decompiler_options", {}),
        },
        "counts": export_info.get("counts", {}),
        "coverage": coverage,
        "partial": partial,
        "browse_headers": {
            "types_sha256": header_tree_hash(workspace_dir, "include/types"),
            "globals_sha256": header_tree_hash(workspace_dir, "include/globals"),
        },
        "artifacts": artifact_hashes(workspace_dir),
    }
    generation_id = sha256_bytes(canonical_json(content))[:24]
    manifest = {"generation_id": generation_id, "content": content}
    path = workspace_dir / "content_manifest.json"
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    return generation_id, path


def build_workspace(args: argparse.Namespace) -> dict[str, Any]:
    export_dir = args.export_dir.resolve()
    workspace_dir = args.workspace_dir.resolve()
    ensure_empty_directory(workspace_dir)
    export_info = read_json(export_dir / "export_info.json")
    if export_info.get("schema") != SCHEMA:
        raise ExportValidationError(
            f"Unsupported export schema {export_info.get('schema')!r}; expected {SCHEMA!r}"
        )
    overrides = load_overrides(args.overrides)
    origins = load_origin_registry(args.origin_registry, str(export_info.get("executable_sha256", "")))
    watchlist = load_watchlist(args.watchlist, str(export_info.get("executable_sha256", "")))
    core_counts = validate_core_artifacts(export_dir, workspace_dir)
    function_names = validated_function_names(export_dir / "functions.jsonl")
    relationships = load_relationships(workspace_dir / ".ghidra" / "data", function_names)
    records, statuses = validate_and_write_functions(export_dir, workspace_dir, overrides, origins, relationships, watchlist)
    unused_watchlist = sorted(set(watchlist).difference(str(record["address"]).lower().removeprefix("0x") for record in records if record.get("address_space") == "ram"))
    if unused_watchlist:
        raise ExportValidationError(f"RE watchlist entries are absent from this export: {', '.join(unused_watchlist)}")
    expected = int(export_info.get("counts", {}).get("functions", -1))
    if expected != len(records):
        raise ExportValidationError(f"Function count mismatch: info={expected}, records={len(records)}")
    for name, count in core_counts.items():
        declared = int(export_info.get("counts", {}).get(name, -1))
        if declared != count:
            raise ExportValidationError(f"{name} count mismatch: info={declared}, records={count}")
    previous_count = previous_failures(args.previous_manifest, export_info)
    partial, coverage = enforce_coverage(statuses, previous_count, args.allow_partial)
    write_function_indexes(records, workspace_dir / ".ghidra" / "index", partial)
    write_core_indexes(workspace_dir)
    write_type_browse(workspace_dir)
    write_global_browse(workspace_dir, records)
    write_import_export_headers(workspace_dir)
    write_navigation_headers(workspace_dir, records)
    write_visual_studio_workspace(workspace_dir)
    validate_project_references(workspace_dir)
    generation_id, manifest_path = write_manifest(
        workspace_dir, export_info, coverage, partial, args.pipeline_sha256
    )
    return {
        "generation_id": generation_id,
        "manifest": str(manifest_path),
        "solution": str(workspace_dir / "GhidraCalibur.sln"),
        "partial": partial,
        "coverage": coverage,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export-dir", type=Path, required=True)
    parser.add_argument("--workspace-dir", type=Path, required=True)
    parser.add_argument("--previous-manifest", type=Path)
    parser.add_argument("--overrides", type=Path)
    parser.add_argument("--origin-registry", type=Path)
    parser.add_argument("--watchlist", type=Path)
    parser.add_argument("--pipeline-sha256", required=True)
    parser.add_argument("--allow-partial", action="store_true")
    args = parser.parse_args()
    try:
        result = build_workspace(args)
    except ExportValidationError as exc:
        print(f"GhidraCalibur validation failed: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
