---
name: ghidra-investigate-type
description: Use when investigating Ghidra generic pointer parameters or unknown structs with MCP: discover fields, define structures, and apply types across functions.
---

# Ghidra Type Investigation

Use this workflow to identify correct data types for generic pointer parameters, discover structure layouts from usage patterns, create or update missing structure definitions, and apply consistent types across related functions in Ghidra.

The core rule is to investigate how a parameter is used across all relevant functions before deciding its true type. Do not guess a structure type from one function unless the evidence is already decisive.

## Core Concepts

A parameter named `pUnit` or typed as `int *` only says it is a pointer. To determine the real structure type, answer these questions:

- How is the parameter dereferenced?
- Which offsets are accessed?
- Which fields are read, written, compared, or passed to callees?
- Which other functions access the same layout?
- Does an existing structure already match the observed offsets?

Structure names should describe object identity, not temporary state. Prefer names like `UnitAny`, `Skill`, `SkillData`, or `SkillTableEntry`. Avoid state-based names like `InitializedUnit`, `AllocatedSkill`, or `ProcessedData`.

## Phase 1: Identify Targets

Find functions with generic pointer types that should be properly typed:

- `int *pUnit`, `int *pParameter`
- `uint *pValue`, `uint *pData`
- `void *pBuffer`, `void *pData`
- `dword *` or Windows-style aliases that should be normalized

Useful MCP tools include `search_functions`, `search_functions_enhanced`, `analyze_function_complete`, and `get_function_variables`.

Track initial findings in this shape:

```text
Function      Address     Parameter  Current Type  Usage Pattern              Inferred Type
ProcessUnit   0x401000    pUnit      int *         offsets 0x4, 0xC, 0x20    UnitAny *
ValidateUnit  0x402000    pUnit      int *         offsets 0x4, 0xC          UnitAny *
```

## Phase 2: Analyze One Function

Call `analyze_function_complete` with xrefs, disassembly, callees, callers, and variables enabled where useful.

Inspect decompiled and assembly usage for:

- Null checks.
- Offset reads and writes.
- Field values used as loop bounds, sizes, flags, indexes, IDs, or pointers.
- Calls that pass `param + offset` or loaded fields to other functions.
- Highest offset accessed and likely minimum structure size.

Infer field types from usage:

- Comparisons against small constants often indicate bytes, enums, flags, or IDs.
- Bitwise operations often indicate flags.
- Values used as addresses or dereferenced values indicate pointers.
- Values used in array strides or `memcpy` sizes indicate counts or byte lengths.
- Signed comparisons can distinguish `int` from `uint` when consistent.

## Phase 3: Cross-Reference Analysis

Find all functions using the same structure pattern. Search by parameter name, related function names, call graph neighborhoods, and matching offset access patterns.

Build a master offset map:

```text
Offset  Size  Field Name  Access      Accessed By                 Inferred Type
0x00    4     type        READ        ProcessUnit, ValidateUnit   uint
0x04    4     flags       READ/WRITE  ProcessUnit, ModifyUnit     uint
0x08    4     next        READ        EnumerateUnits              UnitAny *
0x0C    2     health      READ/WRITE  ApplyDamage, HealUnit       ushort
```

Collect every observed offset from every related function. Merge duplicate offsets and note access type, value ranges, constants, and consumers.

Document gaps and alignment. Unknown bytes are normal. Add padding or unknown fields when needed to preserve correct offsets.

## Phase 4: Search Existing Types

Before creating a structure, search for existing candidates with `search_data_types`, `list_data_types`, `get_struct_layout`, and `get_type_size`.

Compare candidate layouts against the offset map:

- Field offsets match observed accesses.
- Field sizes match read/write sizes.
- Pointer fields match dereference or call usage.
- Total size matches allocation size, array stride, or highest observed offset plus field size.

If most offsets match an existing structure, use it instead of creating a duplicate.

## Phase 5: Create Or Update A Structure

Only create a new structure when no suitable existing type matches.

Build fields from the offset map using logical field names. The MCP struct tools apply project naming conventions and prefixes, so pass logical names such as `type`, `flags`, `next`, and `health` unless the tool specifically requires a final field name.

Example structure plan:

```text
Offset  Type        Logical Name  Meaning
0x00    uint        type          entity type identifier
0x04    uint        flags         state flags
0x08    UnitAny *   next          linked-list next pointer
0x0C    ushort      health        current health
```

Use `create_struct`, `add_struct_field`, or `modify_struct_field` as appropriate. Add explicit padding or unknown fields when required to maintain offsets.

Verify structure size against the evidence:

- Highest observed offset plus field size.
- Array stride, such as `base + index * 0x23C`.
- Allocation sizes from constructors, `malloc`, `new`, or stack frame reservations.

For complex related data, create separate helper structures instead of forcing unrelated data into one type. For example, static skill table entries, runtime skill objects, and execution parameter blocks may need different structures.

## Phase 6: Apply Types Across Functions

Identify every function that should receive the structure type.

For each function, apply the type to the relevant parameter or local with `set_parameter_type`, `set_local_variable_type`, or `set_function_prototype` as appropriate.

After batches of type changes:

- Re-fetch variables with `get_function_variables`.
- Force or refresh decompilation when needed with the available decompile or force-decompile tool.
- Confirm decompiled code now shows field names instead of raw offsets.

Do not leave one-off typed functions when the same structure is used by a family of callers or callees. Consistency across the call graph is the point of this workflow.

## Phase 7: Verify

Verify that the final structure and applied types match the binary:

- Every field offset matches actual assembly accesses.
- Field sizes match instruction widths.
- Parameter types are consistent across related functions.
- Structure size matches allocation, stride, or observed bounds.
- Alignment and padding are documented.
- Helper structures exist for distinct identities.

Use `analyze_struct_field_usage`, `get_field_access_context`, `get_bulk_xrefs`, `disassemble_function`, and `analyze_function_complete` where useful.

If a field's semantic role is unclear, trace dataflow from a write or read site if dataflow tools are available:

- Backward from a write to identify producers such as constants, function inputs, or table lookups.
- Forward from a read to identify consumers such as loop bounds, `memcpy` sizes, comparisons, or pointer dereferences.

## Common Pitfalls

- Do not define a structure with only the fields seen in one function if cross-references show more usage.
- Do not create duplicate structures when an existing layout already matches.
- Do not infer signedness from one arithmetic instruction; check comparisons, ranges, and consumers.
- Do not ignore compiler padding or array stride.
- Do not merge static template data, runtime state, and parameter blocks into one structure just because they are in the same feature area.

## Completion Checklist

- All functions using the structure type are identified.
- A complete offset map exists with fields from all analyzed functions.
- Existing structures were searched and compared.
- The selected or created structure has an identity-based name.
- All identified functions have the correct parameter or local types applied.
- Type application was verified with `get_function_variables` and decompilation.
- Field offsets were checked against assembly.
- Structure size matches stride, allocation size, or observed upper bound.
- Alignment and padding are documented.
- Cross-function type consistency is confirmed.

## Output

Report results with:

```text
DONE: TypeName
Evidence: functions and offsets analyzed
Changes: structures created/updated and functions typed
Verification: size, offset, and consistency checks
```
