---
name: ghidra-doc-function
description: Use when documenting a single Ghidra reverse-engineered function with MCP: classify, rename, set prototype/types, comment, and verify completeness.
---

# Ghidra Function Documentation

Use this workflow to document one reverse-engineered function in Ghidra through MCP tools. Apply all changes directly in Ghidra. Do not create or edit filesystem files for the documentation result.

Do not use `run_script_inline`, `run_ghidra_script`, or `run_script` for Ghidra database edits unless the user explicitly overrides project policy. Use native MCP tools such as `rename_function_by_address`, `set_function_prototype`, `get_function_variables`, `set_local_variable_type`, `rename_variables`, `batch_set_comments`, and `analyze_function_completeness`.

## Critical Rules

1. Complete all naming, prototype, and type changes before plate comments and inline comments. `set_function_prototype` wipes existing plate comments.
2. Batch related operations. Prefer `rename_variables` for variable renames and `batch_set_comments` for plate, PRE, and EOL comments in one operation.
3. Treat `extraout_*` and `in_*` variables with undefined types as decompiler artifacts. Note them in the plate comment Special Cases section and do not repeatedly retry type-setting.
4. When re-documenting, overwrite existing names and comments if analysis produces better results, even if existing values are custom.
5. Never rename a variable with a Hungarian prefix (`dw`, `n`, `b`, `p`, `sz`, `w`, etc.) while its type is still `undefined*`. Resolve the type first. If the type cannot be determined, use a descriptive name without a type prefix, such as `questBits` instead of `dwQuestBits`.
6. After setting a prototype, verify parameter types match Hungarian prefixes. A parameter named `pGame` typed as `int` is a violation; fix the type to a pointer.
7. Always call `analyze_function_completeness` at the end. If fixable deductions are greater than 10 points, address them and verify again before reporting completion.

## Hungarian Reference

```text
b:byte  c:char  f:bool  n:int/short  dw:uint/dword  w:ushort  l:long
fl:float  d:double  ll:longlong  qw:ulonglong  ld:float10  h:HANDLE
p:void*/ptr  pb:byte*  pw:ushort*  pdw:uint*  pn:int*  pp:void**
sz:char*(local)  lpsz:char*(param)  wsz:wchar_t*  lpcsz:const char*(param)
ab:byte[N]  aw:ushort[N]  ad:uint[N]  an:int[N]
g_:global prefix (g_dwCount, g_pMain, g_szPath)  pfn:func_ptr (PascalCase, no g_)
Struct pointers: p+StructName (pUnit, pInventory, ppItem for double ptr)
```

Normalize `undefined1` to `byte`, `undefined2` to `ushort`, and `undefined4` to `uint`, `int`, `float`, or pointer by usage. Use Ghidra builtins such as `dword`, `byte`, and `ushort` instead of Windows aliases such as `DWORD` or `BYTE` when calling variable type tools.

## Step 1: Initialize And Classify

Call `analyze_for_documentation` for the function address. Use the results to:

- Verify function boundaries.
- Resolve uncertain return types by checking return instructions and wrapper hints.
- Validate existing function names, including custom names.
- Identify thunks and wrappers. For single-call wrappers with no logic, use the fast path: rename, set prototype, comment, and verify.

## Step 2: Rename Function And Set Prototype

Call `rename_function_by_address` and `set_function_prototype` before comments. Parallelize if the tools and context allow it.

Use PascalCase, verb-first function names, such as `GetPlayerHealth`, `ProcessInputEvent`, or `ValidateItemSlot`. Avoid names like `processData` or module-prefixed names that hide the action.

Use typed struct pointers when identifiable, such as `UnitAny *` instead of `int *`. Use Hungarian camelCase parameter names. Verify the calling convention from disassembly. If parameters are implicit register parameters, document that in the plate comment.

Prototype changes can create new SSA variables. Always re-fetch variables after setting the prototype.

## Step 3: Type Audit And Variable Renaming

Always call `get_function_variables` explicitly. Do not rely only on `analyze_for_documentation` for variable storage types.

Skip to comments only if all variables already have custom names and resolved storage types with no `undefined` type fields.

Type audit checklist:

1. Fetch the full variable list with `get_function_variables`.
2. For each variable whose type contains `undefined`, infer the correct type from usage and call `set_local_variable_type`. Skip phantoms after the first failure.
3. For each parameter whose name has a pointer prefix (`p`, `pp`, `lpsz`) but whose type is `int` or `uint`, fix the type to a pointer or a specific struct pointer.
4. For `__thiscall` functions with a `void *` this pointer, identify the class or struct and update the prototype accordingly.
5. Fetch variables again after type changes to discover new SSA variables.
6. Issue a single `rename_variables` call for variables with Hungarian names matching their resolved types.
7. Fetch variables once more to confirm no resolvable `undefined` storage types remain.

For raw pointer-plus-offset accesses, search for a matching struct type and apply it when available. If no matching struct exists, add concise EOL comments at each access documenting the offset, such as `+0x10: flags`, instead of forcing a premature struct.

When `set_local_variable_type` fails for a register-only variable, document the type with a PRE comment, such as `nIterator: int - loop counter (register-only)`. The completeness scorer excludes these when documented.

## Step 4: Comments

Comments must be after all naming, prototype, and type changes.

First, label visible `DAT_*` or `s_*` globals when their purpose is clear. Apply a data type and rename or label them with `g_` plus Hungarian notation.

Use `batch_set_comments` to set plate, PRE, and EOL comments in one operation.

Plate comment template:

```text
One-line function summary.

Algorithm:
1. Describe the first significant action, including magic constants.
2. Describe each following action clearly.

Parameters:
  paramName: Type - purpose description [IMPLICIT EDX if register-passed]

Returns:
  type: meaning. Success=non-zero, Failure=0/NULL. Mention all return paths.

Special Cases:
  - Edge cases, phantom variables, or decompiler discrepancies.
  - Magic number explanations and sentinel values.

Structure Layout: (if accessing structs)
  Offset | Size | Field  | Type | Description
  +0x00  | 4    | dwType | uint | ...
```

Use decompiler PRE comments at block-start addresses for context and algorithm steps. Keep them concise. Use disassembly EOL comments at instruction addresses for hex and numeric constants or raw structure offsets.

Use actual multiline strings for plate comments. Do not pass literal `\n` sequences.

## Step 5: Verify

Call `analyze_function_completeness` once comments are set.

Acceptable unfixable deductions include documented phantom variables, API-mandated `void *` parameters, and standard API parameter names that do not perfectly match strict Hungarian notation.

If fixable deductions exceed 10 points, address them and verify again before reporting completion.

## Optional Dynamic Cross-Check

For leaf functions with ambiguous semantics, such as hash algorithms, CRC/checksum variants, or bit-packing routines, use available dataflow or emulation MCP tools to falsify the static interpretation before marking the function done.

Skip dynamic checks for non-leaf functions, heap or syscall side effects, or functions that already score as good enough.

## Output

Report completion in this form:

```text
DONE: FunctionName
Changes: brief summary
Score: N% with any unfixable deductions noted
```
