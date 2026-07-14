#!/usr/bin/env python3
"""Query an immutable GhidraCalibur generation through a user-local SQLite FTS cache."""

from __future__ import annotations

import argparse
import json
import os
import re
import sqlite3
import time
import uuid
from pathlib import Path
from typing import Any


CACHE_SCHEMA = "3"


def jsonl(path: Path):
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                yield json.loads(line)


def norm(value: str) -> str:
    return str(value).lower().removeprefix("0x")


def function_id(address_space: str, address: str) -> str:
    return f"{address_space.casefold()}:{norm(address)}"


def build_cache(generation: Path, cache: Path) -> None:
    cache.parent.mkdir(parents=True, exist_ok=True)
    temporary = cache.with_name(f".{cache.name}.{uuid.uuid4().hex}.tmp")
    db = sqlite3.connect(temporary)
    try:
        db.executescript("""
            CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE funcs (id TEXT PRIMARY KEY, address_space TEXT NOT NULL, address TEXT NOT NULL, name TEXT, qualified_name TEXT, signature TEXT, area TEXT, module TEXT, family TEXT, origin TEXT, provenance TEXT, browse_file TEXT, browse_line INTEGER, tags TEXT, note TEXT, status TEXT);
            CREATE TABLE calls (caller_id TEXT NOT NULL, callee_id TEXT NOT NULL);
            CREATE TABLE global_refs (function_id TEXT NOT NULL, name TEXT NOT NULL);
            CREATE TABLE string_refs (function_id TEXT NOT NULL, value TEXT NOT NULL);
            CREATE INDEX calls_caller ON calls(caller_id); CREATE INDEX calls_callee ON calls(callee_id);
            CREATE INDEX global_function ON global_refs(function_id); CREATE INDEX string_function ON string_refs(function_id);
        """)
        try:
            db.execute("CREATE VIRTUAL TABLE funcs_fts USING fts5(id, address, address_space, name, qualified_name, signature, area, module, family, origin, tags)")
            fts = True
        except sqlite3.OperationalError:
            fts = False
        data = generation / ".ghidra" / "data"
        rows = []
        fts_rows = []
        functions = list(jsonl(data / "functions.jsonl"))
        legacy_ids: dict[str, str | None] = {}
        for item in functions:
            address = norm(item["address"])
            identifier = function_id(str(item.get("address_space", "unknown")), address)
            legacy_ids[address] = identifier if address not in legacy_ids else None
        legacy_ids = {address: identifier for address, identifier in legacy_ids.items() if identifier is not None}
        for item in functions:
            address = norm(item["address"])
            address_space = str(item.get("address_space", "unknown"))
            identifier = function_id(address_space, address)
            values = (identifier, address_space, address, item.get("name", ""), item.get("qualified_name", ""), item.get("signature", ""), item.get("area", ""), item.get("module", ""), item.get("family", ""), item.get("origin", ""), item.get("origin_provenance", ""), item.get("browse_file", ""), item.get("browse_line", 0), ";".join(item.get("watch_tags", [])), item.get("watch_note", ""), item.get("watch_status", ""))
            rows.append(values)
            fts_rows.append(values[:10] + (values[13],))
        db.executemany("INSERT INTO funcs VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
        if fts:
            db.executemany("INSERT INTO funcs_fts VALUES (?,?,?,?,?,?,?,?,?,?,?)", fts_rows)
        def reference_id(item: dict[str, Any], address_field: str, space_field: str) -> str | None:
            address = norm(item[address_field])
            return function_id(str(item[space_field]), address) if space_field in item else legacy_ids.get(address)
        def referring_ids(item: dict[str, Any]) -> list[str]:
            addresses = item.get("referring_functions", [])
            spaces = item.get("referring_function_address_spaces")
            if spaces is not None:
                return [function_id(str(space), address) for address, space in zip(addresses, spaces)]
            return [identity for address in addresses if (identity := legacy_ids.get(norm(address)))]
        db.executemany("INSERT INTO calls VALUES (?,?)", ((caller, callee) for item in jsonl(data / "calls.jsonl") if (caller := reference_id(item, "caller", "caller_address_space")) and (callee := reference_id(item, "callee", "callee_address_space"))))
        for item in jsonl(data / "globals.jsonl"):
            db.executemany("INSERT INTO global_refs VALUES (?,?)", ((identity, item.get("name", "")) for identity in referring_ids(item)))
        for item in jsonl(data / "strings.jsonl"):
            db.executemany("INSERT INTO string_refs VALUES (?,?)", ((identity, item.get("value", "")) for identity in referring_ids(item)))
        db.executemany("INSERT INTO meta VALUES (?,?)", (("cache_schema", CACHE_SCHEMA), ("generation_id", generation.name), ("fts", str(fts))))
        db.commit()
    finally:
        db.close()
    if not cache_is_usable(temporary, generation):
        temporary.unlink(missing_ok=True)
        raise RuntimeError(f"Refusing to publish an invalid local query cache: {temporary}")
    for attempt in range(8):
        try:
            os.replace(temporary, cache)
            return
        except PermissionError:
            if attempt == 7:
                raise
            time.sleep(0.1 * (attempt + 1))


def cache_is_usable(cache: Path, generation: Path) -> bool:
    if not cache.is_file():
        return False
    try:
        db = sqlite3.connect(cache)
        try:
            values = dict(db.execute("SELECT key, value FROM meta WHERE key IN ('cache_schema', 'generation_id')"))
            return values == {"cache_schema": CACHE_SCHEMA, "generation_id": generation.name}
        finally:
            db.close()
    except sqlite3.DatabaseError:
        return False


def related(db: sqlite3.Connection, identifier: str, direction: str) -> list[str]:
    target, condition = ("caller_id", "callee_id") if direction == "callers" else ("callee_id", "caller_id")
    rows = db.execute(f"SELECT f.name, f.address FROM calls c LEFT JOIN funcs f ON f.id=c.{target} WHERE c.{condition}=? ORDER BY f.name, f.address LIMIT 12", (identifier,)).fetchall()
    return [f"{row[0] or 'unknown'} [0x{row[1] or ''}]" for row in rows]


def query(db: sqlite3.Connection, args: argparse.Namespace) -> list[dict[str, Any]]:
    clauses: list[str] = []
    values: list[str] = []
    joins: list[str] = []
    if args.address:
        clauses.append("f.address = ?"); values.append(norm(args.address))
    if args.query:
        fts = db.execute("SELECT value FROM meta WHERE key='fts'").fetchone()[0] == "True"
        if fts:
            joins.append("JOIN funcs_fts ON funcs_fts.id=f.id")
            # Keep the FTS expression data-only. Prefix terms make partial function-name
            # searches (for example, Decode -> DecodePacket) useful without exposing
            # FTS query syntax to the command line.
            tokens = [token for token in re.findall(r"[^\W_]+", args.query, flags=re.UNICODE) if token]
            if tokens:
                clauses.append("funcs_fts MATCH ?"); values.append(" AND ".join(f'\"{token}\"*' for token in tokens))
        else:
            clauses.append("(f.name LIKE ? OR f.qualified_name LIKE ? OR f.signature LIKE ?)"); values.extend([f"%{args.query}%"] * 3)
    if args.area: clauses.append("f.area = ?"); values.append(args.area)
    if args.module: clauses.append("f.module = ?"); values.append(args.module)
    if args.family: clauses.append("f.family = ?"); values.append(args.family)
    if args.origin: clauses.append("f.origin = ?"); values.append(args.origin)
    if args.tag: clauses.append("f.tags LIKE ?"); values.append(f"%{args.tag}%")
    if args.calls:
        joins.extend(("JOIN calls call_out ON call_out.caller_id=f.id", "JOIN funcs call_target ON call_target.id=call_out.callee_id"))
        clauses.append("(call_target.address LIKE ? OR call_target.name LIKE ? OR call_target.qualified_name LIKE ?)")
        values.extend([f"%{args.calls}%"] * 3)
    if args.called_by:
        joins.extend(("JOIN calls call_in ON call_in.callee_id=f.id", "JOIN funcs call_source ON call_source.id=call_in.caller_id"))
        clauses.append("(call_source.address LIKE ? OR call_source.name LIKE ? OR call_source.qualified_name LIKE ?)")
        values.extend([f"%{args.called_by}%"] * 3)
    for option, table, column in ((args.string, "string_refs", "value"), (args.global_name, "global_refs", "name")):
        if option:
            alias = f"r{len(joins)}"; joins.append(f"JOIN {table} {alias} ON {alias}.function_id=f.id")
            clauses.append(f"{alias}.{column} LIKE ?"); values.append(f"%{option}%")
    sql = "SELECT DISTINCT f.* FROM funcs f " + " ".join(joins)
    if clauses: sql += " WHERE " + " AND ".join(clauses)
    sql += " ORDER BY f.name COLLATE NOCASE, f.address LIMIT ?"; values.append(args.limit)
    result = []
    for row in db.execute(sql, values):
        item = dict(zip(("id", "address_space", "address", "name", "qualified_name", "signature", "area", "module", "family", "origin", "provenance", "browse_file", "browse_line", "tags", "note", "status"), row))
        item["callers"] = related(db, item["id"], "callers")
        item["callees"] = related(db, item["id"], "callees")
        item["globals"] = [value[0] for value in db.execute("SELECT name FROM global_refs WHERE function_id=? ORDER BY name LIMIT 12", (item["id"],))]
        item["strings"] = [value[0] for value in db.execute("SELECT value FROM string_refs WHERE function_id=? ORDER BY value LIMIT 12", (item["id"],))]
        item["tags"] = [tag for tag in item["tags"].split(";") if tag]
        del item["id"]
        result.append(item)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generation", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path, required=True)
    parser.add_argument("--query"); parser.add_argument("--address"); parser.add_argument("--calls"); parser.add_argument("--called-by")
    parser.add_argument("--string"); parser.add_argument("--global", dest="global_name"); parser.add_argument("--area"); parser.add_argument("--module"); parser.add_argument("--family"); parser.add_argument("--origin"); parser.add_argument("--tag")
    parser.add_argument("--limit", type=int, default=50)
    args = parser.parse_args()
    generation = args.generation.resolve()
    cache = args.cache_root / f"{generation.name}.sqlite"
    if not cache_is_usable(cache, generation):
        build_cache(generation, cache)
    db = sqlite3.connect(cache)
    try: print(json.dumps(query(db, args), ensure_ascii=False, indent=2))
    finally: db.close()
    return 0


if __name__ == "__main__": raise SystemExit(main())
