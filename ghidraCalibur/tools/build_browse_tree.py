#!/usr/bin/env python3
"""Build a Visual Studio-friendly browse tree from Ghidra CppExporter output."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import re
import shutil
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CPP = ROOT / "exported" / "sc6_decompiled.cpp"
DEFAULT_FUNCTIONS = ROOT / "exported" / "functions.csv"
DEFAULT_BROWSE = ROOT / "browse"
DEFAULT_INDEX = ROOT / "index"
PROJECT_GUID = "{DCACF24E-57AB-4C52-8873-844609C95C76}"

FUNC_NAME_RE = re.compile(r"([A-Za-z_~][A-Za-z0-9_:~]*)\s*$")
ADDR_IN_NAME_RE = re.compile(r"(?:FUN|SUB|THUNK|LAB)_?(14[0-9a-fA-F]{7,})")
UE_CLASS_TOKEN_RE = re.compile(r"^[AUFI][A-Z][A-Za-z0-9_]{1,}$")
STRING_CLASS_RE = re.compile(r'L?"([AUFI][A-Za-z0-9_]{2,})"')
CLASS_NAME_PATTERNS = [
    re.compile(r"(?P<class>[AUFI][A-Za-z0-9_]+)::"),
    re.compile(r"GetPrivateStaticClassBody_(?P<class>[AUFI][A-Za-z0-9_]+)"),
    re.compile(r"Z_Construct_UClass_(?P<class>[AUFI][A-Za-z0-9_]+?)(?:_Statics|_NoRegister|_singleton|$)"),
    re.compile(r"StaticRegisterNatives_(?P<class>[AUFI][A-Za-z0-9_]+)"),
    re.compile(r"RegisterNatives(?P<class>[AUFI][A-Za-z0-9_]+)"),
]


@dataclass
class FunctionMeta:
    address: str
    name: str
    namespace: str = ""
    signature: str = ""
    is_thunk: str = ""
    is_external: str = ""


@dataclass
class FunctionBlock:
    name: str
    address: str
    category: str
    namespace: str
    signature: str
    class_name: str
    raw_start: int
    raw_end: int
    lines: list[str]
    browse_file: str = ""
    browse_line: int = 0
    class_file: str = ""
    class_line: int = 0


def clean_tree(path: Path) -> None:
    path = path.resolve()
    root = ROOT.resolve()
    if path.exists():
        if not str(path).lower().startswith(str(root).lower()):
            raise RuntimeError(f"Refusing to remove path outside ghidraCalibur: {path}")
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def load_function_metadata(path: Path) -> dict[str, deque[FunctionMeta]]:
    by_name: dict[str, deque[FunctionMeta]] = defaultdict(deque)
    if not path.exists():
        return by_name

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            meta = FunctionMeta(
                address=row.get("address", "").strip(),
                name=row.get("name", "").strip(),
                namespace=row.get("namespace", "").strip(),
                signature=row.get("signature", "").strip(),
                is_thunk=row.get("is_thunk", "").strip(),
                is_external=row.get("is_external", "").strip(),
            )
            if meta.name:
                by_name[meta.name].append(meta)
    return by_name


def infer_address(name: str) -> str:
    match = ADDR_IN_NAME_RE.search(name)
    if match:
        return match.group(1).lower()
    return ""


def category_for(name: str) -> str:
    lower = name.lower()
    if "replay" in lower or "frameinputlog" in lower or "inputlog" in lower:
        return "Replay"
    if "movevm" in lower or "move" in lower or "attack" in lower or "skill" in lower or "combo" in lower:
        return "Move"
    if "input" in lower or "pad" in lower or "command" in lower:
        return "Input"
    if "steam" in lower or "network" in lower or "online" in lower or "asynctask" in lower:
        return "Online"
    if "hud" in lower or "widget" in lower or "menu" in lower or "window" in lower or "ui" in lower:
        return "UI"
    if "luxbattle" in lower or "battle" in lower or "round" in lower or "guard" in lower or "hit" in lower:
        return "Battle"
    if (
        "uobject" in lower
        or "ufunction" in lower
        or "staticclass" in lower
        or "registernative" in lower
        or "z_construct" in lower
        or "compiledindefer" in lower
    ):
        return "UE"
    if name.startswith("FUN_"):
        return "Unknown"
    return "Other"


def normalize_class_candidate(value: str) -> str:
    candidate = " ".join(value.strip().split())
    if not candidate or candidate == "Global":
        return ""
    candidate = candidate.split("<", 1)[0]
    candidate = candidate.rsplit("::", 1)[-1]
    candidate = candidate.strip("`'\"*& ")
    for suffix in ["_Statics", "_NoRegister", "_singleton"]:
        if candidate.endswith(suffix):
            candidate = candidate[: -len(suffix)]
    if candidate.upper() == candidate:
        return ""
    if UE_CLASS_TOKEN_RE.match(candidate):
        return candidate
    return ""


def infer_class_from_name(name: str) -> str:
    for pattern in CLASS_NAME_PATTERNS:
        match = pattern.search(name)
        if not match:
            continue
        candidate = normalize_class_candidate(match.group("class"))
        if candidate:
            return candidate
    return ""


def infer_class_from_body(lines: list[str]) -> str:
    candidates: set[str] = set()
    for line in lines[:80]:
        for match in STRING_CLASS_RE.finditer(line):
            candidate = normalize_class_candidate(match.group(1))
            if candidate:
                candidates.add(candidate)
        if len(candidates) > 1:
            return ""
    return next(iter(candidates)) if len(candidates) == 1 else ""


def infer_class_for(name: str, meta: FunctionMeta | None, lines: list[str]) -> str:
    if meta:
        namespace_class = normalize_class_candidate(meta.namespace)
        if namespace_class:
            return namespace_class

    name_class = infer_class_from_name(name)
    if name_class:
        return name_class

    if meta and meta.name != name:
        meta_name_class = infer_class_from_name(meta.name)
        if meta_name_class:
            return meta_name_class

    return infer_class_from_body(lines)


def is_comment_or_blank(line: str) -> bool:
    stripped = line.strip()
    return not stripped or stripped.startswith("//")


def is_split_return_type(line: str) -> bool:
    stripped = line.strip()
    if not stripped or "(" in stripped or ")" in stripped or stripped.endswith(";"):
        return False
    return bool(re.match(r"^[A-Za-z_][A-Za-z0-9_:<>\s\*&]+$", stripped))


def detect_function_starts(lines: list[str]) -> list[tuple[int, str]]:
    starts: list[tuple[int, str]] = []
    total = len(lines)
    for index, line in enumerate(lines):
        if line.startswith((" ", "\t", "#", "typedef ", "struct ", "enum ", "union ", "using ")):
            continue
        if "(" not in line or line.rstrip().endswith(";"):
            continue
        prefix = line.split("(", 1)[0].rstrip()
        match = FUNC_NAME_RE.search(prefix)
        if not match:
            continue

        probe = index + 1
        paren_depth = line.count("(") - line.count(")")
        while probe < total and paren_depth > 0 and probe - index < 80:
            paren_depth += lines[probe].count("(") - lines[probe].count(")")
            probe += 1
        while probe < total and not lines[probe].strip():
            probe += 1
        if probe < total and lines[probe].lstrip().startswith("{"):
            starts.append((index, match.group(1)))
    return starts


def adjusted_starts(lines: list[str], starts: list[tuple[int, str]]) -> list[tuple[int, str]]:
    adjusted: list[tuple[int, str]] = []
    previous = 0
    for start, name in starts:
        adjusted_start = start
        probe = start - 1
        while probe >= previous and is_comment_or_blank(lines[probe]):
            adjusted_start = probe
            probe -= 1
        if probe >= previous and is_split_return_type(lines[probe]):
            adjusted_start = probe
        adjusted.append((adjusted_start, name))
        previous = start + 1
    return adjusted


def build_blocks(lines: list[str], metadata: dict[str, deque[FunctionMeta]]) -> tuple[list[str], list[FunctionBlock]]:
    starts = adjusted_starts(lines, detect_function_starts(lines))
    if not starts:
        return lines, []

    preamble = lines[: starts[0][0]]
    blocks: list[FunctionBlock] = []
    for pos, (start, name) in enumerate(starts):
        end = starts[pos + 1][0] if pos + 1 < len(starts) else len(lines)
        meta_queue = metadata.get(name)
        meta = meta_queue.popleft() if meta_queue else None
        address = meta.address.lower() if meta else infer_address(name)
        category = category_for(name)
        block_lines = lines[start:end]
        blocks.append(
            FunctionBlock(
                name=name,
                address=address,
                category=category,
                namespace=meta.namespace if meta else "",
                signature=meta.signature if meta else "",
                class_name=infer_class_for(name, meta, block_lines),
                raw_start=start + 1,
                raw_end=end,
                lines=block_lines,
            )
        )
    return preamble, blocks


def safe_filename(text: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.-]+", "_", text).strip("_")
    return cleaned[:120] or "unnamed"


def short_hash(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:8]


def unique_safe_names(names: list[str]) -> dict[str, str]:
    used: set[str] = set()
    result: dict[str, str] = {}
    for name in sorted(names):
        base = safe_filename(name)
        candidate = base
        if candidate.lower() in used:
            candidate = f"{base}_{short_hash(name)}"
            while candidate.lower() in used:
                candidate = f"{base}_{short_hash(candidate)}"
        result[name] = candidate
        used.add(candidate.lower())
    return result


def address_shard(address: str) -> str:
    if len(address) >= 4:
        return address[:4]
    return "noaddr"


def display_address(address: str) -> str:
    if not address:
        return "unknown"
    return address if address.startswith("0x") else f"0x{address}"


def one_line(value: str) -> str:
    return " ".join(value.split())


def function_header_lines(block: FunctionBlock) -> list[str]:
    header = [
        "// GhidraCalibur Function\n",
        f"// Address: {display_address(block.address)}\n",
        f"// Name: {block.name}\n",
    ]
    if block.class_name:
        header.append(f"// Class: {block.class_name}\n")
    header.append(f"// Category: {block.category}\n")
    if block.signature:
        header.append(f"// Signature: {one_line(block.signature)}\n")
    header.append(f"// Raw export lines: {block.raw_start}-{block.raw_end}\n")
    return header


def block_sort_key(block: FunctionBlock) -> tuple[int, int, str]:
    try:
        return (0, int(block.address, 16), block.name)
    except ValueError:
        return (1, 0, block.name)


def write_chunk(
    path: Path,
    category: str,
    blocks: list[FunctionBlock],
    source_cpp: Path,
    placement: str,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        line_number = 1

        def emit(text: str) -> None:
            nonlocal line_number
            handle.write(text)
            line_number += text.count("\n")

        def emit_lines(lines: list[str]) -> None:
            for line in lines:
                emit(line)

        emit("// Generated by tools/build_browse_tree.py. Do not hand-edit.\n")
        emit(f"// Source: {source_cpp}\n")
        emit(f"// Category: {category}\n\n")
        for block in blocks:
            region_name = safe_filename(f"{block.name}_{block.address or 'noaddr'}")
            relative_path = str(path.relative_to(ROOT)).replace("\\", "/")
            emit(f"#pragma region {region_name}\n")
            if placement == "class":
                block.class_file = relative_path
                block.class_line = line_number
            else:
                block.browse_file = relative_path
                block.browse_line = line_number
            emit_lines(function_header_lines(block))
            emit_lines(block.lines)
            if not block.lines or not block.lines[-1].endswith("\n"):
                emit("\n")
            emit(f"#pragma endregion // {region_name}\n\n")


def write_blocks(blocks: list[FunctionBlock], browse_dir: Path, source_cpp: Path, max_functions: int) -> None:
    named_groups: dict[str, list[FunctionBlock]] = defaultdict(list)
    range_groups: dict[str, list[FunctionBlock]] = defaultdict(list)

    for block in blocks:
        if block.category in {"Unknown", "Other"}:
            range_groups[address_shard(block.address)].append(block)
        else:
            named_groups[block.category].append(block)

    for category, category_blocks in sorted(named_groups.items()):
        for chunk_index in range(0, len(category_blocks), max_functions):
            chunk = category_blocks[chunk_index : chunk_index + max_functions]
            number = (chunk_index // max_functions) + 1
            path = browse_dir / "named" / category / f"{category}_{number:03d}.cpp"
            write_chunk(path, category, chunk, source_cpp, placement="primary")

    for shard, shard_blocks in sorted(range_groups.items()):
        for chunk_index in range(0, len(shard_blocks), max_functions):
            chunk = shard_blocks[chunk_index : chunk_index + max_functions]
            number = (chunk_index // max_functions) + 1
            path = browse_dir / "address_ranges" / shard / f"{shard}_{number:03d}.cpp"
            write_chunk(path, f"Address Range {shard}", chunk, source_cpp, placement="primary")


def write_class_files(blocks: list[FunctionBlock], browse_dir: Path, source_cpp: Path, max_functions: int) -> None:
    class_groups: dict[str, list[FunctionBlock]] = defaultdict(list)
    for block in blocks:
        if block.class_name:
            class_groups[block.class_name].append(block)

    safe_class_names = unique_safe_names(list(class_groups.keys()))
    for class_name, class_blocks in sorted(class_groups.items()):
        sorted_blocks = sorted(class_blocks, key=block_sort_key)
        safe_class_name = safe_class_names[class_name]
        if len(sorted_blocks) <= max_functions:
            path = browse_dir / "classes" / f"{safe_class_name}.cpp"
            write_chunk(path, f"Class {class_name}", sorted_blocks, source_cpp, placement="class")
            continue

        for chunk_index in range(0, len(sorted_blocks), max_functions):
            chunk = sorted_blocks[chunk_index : chunk_index + max_functions]
            number = (chunk_index // max_functions) + 1
            path = browse_dir / "classes" / safe_class_name / f"{safe_class_name}_{number:03d}.cpp"
            write_chunk(path, f"Class {class_name}", chunk, source_cpp, placement="class")


def write_preamble(preamble: list[str], browse_dir: Path, source_cpp: Path) -> None:
    path = browse_dir / "preamble" / "sc6_globals_and_declarations.cpp"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        handle.write("// Generated by tools/build_browse_tree.py. Do not hand-edit.\n")
        handle.write(f"// Source: {source_cpp}\n")
        handle.write("// Contents before the first decompiled function.\n\n")
        handle.writelines(preamble)


def remaining_metadata(metadata: dict[str, deque[FunctionMeta]]) -> list[FunctionMeta]:
    remaining: list[FunctionMeta] = []
    for queue in metadata.values():
        remaining.extend(queue)
    remaining.sort(key=lambda meta: meta.address)
    return remaining


def write_index(
    blocks: list[FunctionBlock],
    index_dir: Path,
    browse_dir: Path,
    metadata: dict[str, deque[FunctionMeta]],
) -> None:
    index_dir.mkdir(parents=True, exist_ok=True)
    counts = Counter(block.category for block in blocks)
    missing = remaining_metadata(metadata)

    with (index_dir / "functions.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "address",
                "name",
                "class",
                "category",
                "signature",
                "browse_file",
                "browse_line",
                "raw_line_start",
                "raw_line_end",
            ]
        )
        for block in blocks:
            writer.writerow(
                [
                    block.address,
                    block.name,
                    block.class_name,
                    block.category,
                    block.signature,
                    block.browse_file,
                    block.browse_line,
                    block.raw_start,
                    block.raw_end,
                ]
            )

    class_groups: dict[str, list[FunctionBlock]] = defaultdict(list)
    for block in blocks:
        if block.class_name:
            class_groups[block.class_name].append(block)

    with (index_dir / "class_members.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["class", "address", "name", "category", "signature", "browse_file", "browse_line"])
        for class_name, class_blocks in sorted(class_groups.items()):
            for block in sorted(class_blocks, key=block_sort_key):
                writer.writerow(
                    [
                        class_name,
                        block.address,
                        block.name,
                        block.category,
                        block.signature,
                        block.class_file,
                        block.class_line,
                    ]
                )

    with (index_dir / "missing_bodies.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["address", "name", "namespace", "signature", "is_thunk", "is_external"])
        for meta in missing:
            writer.writerow([meta.address, meta.name, meta.namespace, meta.signature, meta.is_thunk, meta.is_external])

    with (index_dir / "functions.md").open("w", encoding="utf-8", newline="") as handle:
        handle.write("# GhidraCalibur Function Index\n\n")
        handle.write("Generated by `tools/build_browse_tree.py`.\n\n")
        handle.write("## Summary\n\n")
        handle.write("| Category | Functions |\n")
        handle.write("| --- | ---: |\n")
        for category, count in sorted(counts.items()):
            handle.write(f"| {category} | {count} |\n")
        handle.write(f"| Total | {len(blocks)} |\n\n")
        handle.write(f"Known Ghidra functions without parsed CppExporter bodies: {len(missing)}.\n\n")
        handle.write("Use `functions.csv` for the full machine-readable index.\n")

    with (index_dir / "classes.md").open("w", encoding="utf-8", newline="") as handle:
        handle.write("# GhidraCalibur Class Index\n\n")
        handle.write("Generated by `tools/build_browse_tree.py`.\n\n")
        handle.write("| Class | Functions | First Address | Browse File | Categories |\n")
        handle.write("| --- | ---: | --- | --- | --- |\n")
        for class_name, class_blocks in sorted(class_groups.items()):
            sorted_blocks = sorted(class_blocks, key=block_sort_key)
            first = sorted_blocks[0]
            categories_for_class = ", ".join(sorted(set(block.category for block in sorted_blocks)))
            class_file = first.class_file
            link = class_file.replace(" ", "%20")
            handle.write(
                f"| `{class_name}` | {len(sorted_blocks)} | `{first.address}` | "
                f"[{class_file}](../{link}) | {categories_for_class} |\n"
            )

    categories: dict[str, list[FunctionBlock]] = defaultdict(list)
    for block in blocks:
        if block.category not in {"Unknown", "Other"}:
            categories[block.category].append(block)

    for category, category_blocks in sorted(categories.items()):
        with (index_dir / f"{category.lower()}.md").open("w", encoding="utf-8", newline="") as handle:
            handle.write(f"# {category} Functions\n\n")
            handle.write("| Address | Name | Browse File | Raw Lines |\n")
            handle.write("| --- | --- | --- | --- |\n")
            for block in category_blocks:
                link = block.browse_file.replace(" ", "%20")
                handle.write(
                    f"| `{block.address or ''}` | `{block.name}` | [{block.browse_file}](../{link}) | "
                    f"{block.raw_start}-{block.raw_end} |\n"
                )

    stats = {
        "total_functions": len(blocks),
        "class_count": len(class_groups),
        "class_member_functions": sum(len(class_blocks) for class_blocks in class_groups.values()),
        "metadata_unmatched_functions": len(missing),
        "category_counts": dict(sorted(counts.items())),
        "browse_dir": str(browse_dir),
    }
    (index_dir / "stats.json").write_text(json.dumps(stats, indent=2), encoding="utf-8")


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace("/", "\\")


def xml(value: str) -> str:
    return html.escape(value, quote=True)


def filter_for(path: Path) -> str:
    parts = path.relative_to(ROOT).parts
    if not parts:
        return ""
    if parts[0] == "browse":
        if len(parts) >= 3 and parts[1] == "classes":
            if len(parts) >= 4:
                return f"Browse\\Classes\\{parts[2]}"
            return "Browse\\Classes"
        if len(parts) >= 4 and parts[1] == "named":
            return f"Browse\\{parts[2]}"
        if len(parts) >= 4 and parts[1] == "address_ranges":
            return f"Browse\\Address Ranges\\{parts[2]}"
        if len(parts) >= 3 and parts[1] == "preamble":
            return "Browse\\Preamble"
        return "Browse"
    if parts[0] == "index":
        return "Index"
    if parts[0] == "notes":
        return "Notes"
    if parts[0] == "tools":
        return "Tools"
    if parts[0] == "exported":
        return "Exported Raw"
    return ""


def deterministic_guid(seed: str) -> str:
    import hashlib

    digest = hashlib.md5(seed.encode("utf-8")).hexdigest().upper()
    return "{" + "-".join([digest[:8], digest[8:12], digest[12:16], digest[16:20], digest[20:32]]) + "}"


def write_vs_project() -> None:
    cpp_files = sorted(
        [path for path in (ROOT / "browse").rglob("*") if path.suffix.lower() in {".c", ".cpp"}]
    )
    header_files = sorted(
        [path for path in (ROOT / "browse").rglob("*") if path.suffix.lower() in {".h", ".hpp"}]
    )

    none_files = [
        ROOT / "README.md",
        ROOT / "exported" / "README.md",
    ]
    for folder in [ROOT / "index", ROOT / "notes", ROOT / "tools"]:
        if folder.exists():
            none_files.extend(path for path in folder.rglob("*") if path.is_file())
    none_files = sorted(set(path for path in none_files if path.exists()))

    vcxproj = ROOT / "GhidraCalibur.vcxproj"
    filters = ROOT / "GhidraCalibur.vcxproj.filters"

    compile_items = "\n".join(
        f'''    <ClCompile Include="{xml(rel(path))}">
      <ExcludedFromBuild Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">true</ExcludedFromBuild>
      <ExcludedFromBuild Condition="'$(Configuration)|$(Platform)'=='Release|x64'">true</ExcludedFromBuild>
    </ClCompile>'''
        for path in cpp_files
    )
    header_items = "\n".join(f'    <ClInclude Include="{xml(rel(path))}" />' for path in header_files)
    none_items = "\n".join(f'    <None Include="{xml(rel(path))}" />' for path in none_files)

    vcxproj.write_text(
        f'''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <Keyword>Win32Proj</Keyword>
    <ProjectGuid>{PROJECT_GUID}</ProjectGuid>
    <RootNamespace>GhidraCalibur</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'" Label="Configuration">
    <ConfigurationType>Utility</ConfigurationType>
    <UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>Utility</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>false</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings" />
  <ImportGroup Label="Shared" />
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <ImportGroup Label="PropertySheets" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>false</SDLCheck>
      <ConformanceMode>false</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(ProjectDir)exported;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>false</SDLCheck>
      <ConformanceMode>false</ConformanceMode>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(ProjectDir)exported;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
{compile_items}
  </ItemGroup>
  <ItemGroup>
{header_items}
  </ItemGroup>
  <ItemGroup>
{none_items}
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets" />
</Project>
''',
        encoding="utf-8",
    )

    filter_names = {
        "Exported Raw",
        "Browse",
        "Browse\\Classes",
        "Browse\\Replay",
        "Browse\\Battle",
        "Browse\\Move",
        "Browse\\Input",
        "Browse\\UI",
        "Browse\\Online",
        "Browse\\UE",
        "Browse\\Address Ranges",
        "Browse\\Preamble",
        "Index",
        "Notes",
        "Tools",
    }
    for path in cpp_files + header_files:
        assigned = filter_for(path)
        if assigned.startswith("Browse\\Address Ranges\\") or assigned.startswith("Browse\\Classes\\"):
            filter_names.add(assigned)

    filter_items = "\n".join(
        f'''    <Filter Include="{xml(name)}">
      <UniqueIdentifier>{deterministic_guid(name)}</UniqueIdentifier>
    </Filter>'''
        for name in sorted(filter_names)
    )

    compile_filter_items = "\n".join(
        f'''    <ClCompile Include="{xml(rel(path))}">
      <Filter>{xml(filter_for(path))}</Filter>
    </ClCompile>'''
        for path in cpp_files
    )
    header_filter_items = "\n".join(
        f'''    <ClInclude Include="{xml(rel(path))}">
      <Filter>{xml(filter_for(path))}</Filter>
    </ClInclude>'''
        for path in header_files
    )
    none_filter_items = "\n".join(
        f'''    <None Include="{xml(rel(path))}">
      <Filter>{xml(filter_for(path))}</Filter>
    </None>'''
        for path in none_files
    )

    filters.write_text(
        f'''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
{filter_items}
  </ItemGroup>
  <ItemGroup>
{compile_filter_items}
  </ItemGroup>
  <ItemGroup>
{header_filter_items}
  </ItemGroup>
  <ItemGroup>
{none_filter_items}
  </ItemGroup>
</Project>
''',
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", type=Path, default=DEFAULT_CPP)
    parser.add_argument("--functions", type=Path, default=DEFAULT_FUNCTIONS)
    parser.add_argument("--browse-dir", type=Path, default=DEFAULT_BROWSE)
    parser.add_argument("--index-dir", type=Path, default=DEFAULT_INDEX)
    parser.add_argument("--max-functions-per-file", type=int, default=500)
    parser.add_argument("--max-functions-per-class-file", type=int, default=500)
    args = parser.parse_args()

    if not args.cpp.exists():
        raise FileNotFoundError(args.cpp)

    print(f"Loading function metadata: {args.functions}")
    metadata = load_function_metadata(args.functions)

    print(f"Reading CppExporter output: {args.cpp}")
    lines = args.cpp.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)

    print("Detecting function boundaries...")
    preamble, blocks = build_blocks(lines, metadata)
    print(f"Detected {len(blocks)} function blocks.")

    clean_tree(args.browse_dir)
    clean_tree(args.index_dir)

    print("Writing browse tree...")
    write_preamble(preamble, args.browse_dir, args.cpp)
    write_blocks(blocks, args.browse_dir, args.cpp, args.max_functions_per_file)
    write_class_files(blocks, args.browse_dir, args.cpp, args.max_functions_per_class_file)

    print("Writing indexes...")
    write_index(blocks, args.index_dir, args.browse_dir, metadata)

    print("Writing Visual Studio project file lists...")
    write_vs_project()

    print("Browse tree complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
