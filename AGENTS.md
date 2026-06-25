# HorseMod — SC6 reverse-engineering project

Always think longer term short term fixes are not a good solution.

Repo layout:
- `HorseMod/` — C++ ASI mod (PolyHook2 + custom hooks) for Soulcalibur VI
- `RE-UE4SS/` — UE4SS integration
- `dump/` — UE header/asset dumps used as RE inputs. a full dump can be found at `C:\Users\prest\Documents\SoulcaliburModding\SCVI Sound Tools\dump` move the files you end up using to the repo dump folder
- `E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\UE4SS.log` — Can be used to check ue4ss logs

## Replay testing

Run a strict replay seek test after changes touching ReplayScrub, replay hooks, timeline generation, seek/restore behavior, GameMode presence handling, clocks, MoveVM/hit state, RNG, input history, or no-render gates. Build and deploy first, then start with `E:\myMods\ReplayExample\REPLAY_12744704008398858106.bin` using `E:\myMods\tools\replay_seek_test_run.py --kill-game --launch-game --allow-unknown-presence --start-replay <replay> --timeline-generation-mode lux-no-render --case-preset watch --watch-frames 600 --wait --analyze --strict --min-resume-tick-rate 58 --resume-tick-window 120 --max-seek-validation-seconds 0.5`. For timeline-generation changes, compare `normal` vs `lux-no-render` oracle data first; fast generation is valid only with zero mismatches.

The reverse-engineering work runs through `bethington/ghidra-mcp` MCP tools. 
The MCP is the authoritative interface for Ghidra — never edit `.gpr` files or invoke Ghidra scripts directly. 
The bridge auto-discovers tools from `/mcp/schema`; if a tool name isn't in the schema it doesn't exist.

## Ghidra MCP conventions

The MCP enforces conventions in the tool layer (auto-fix / warn / reject tiers). Don't fight them — they exist because the codebase outgrew prompt-only discipline.

**Tool inventory and dynamic groups**: 

`/mcp/schema` is authoritative. Don't guess tool names from training data. 
For Ghidra comment work, load the `comment` group and use native comment tools: 
`batch_set_comments`, `set_plate_comment`, `set_decompiler_comment`, `set_disassembly_comment`, and `get_plate_comment`. 
`set_bookmark` is only for bookmarks and audit breadcrumbs; do not treat it as a substitute for plate, PRE, EOL, or disassembly comments.

If `check_tools` reports a tool callable but the Codex client cannot invoke that endpoint, state that as a client/tool-exposure problem and stop before claiming the Ghidra comments were updated.

**Allowed Ghidra MCP tool groups by task**:
- Default allowed non-malware groups: `analysis`, `comment`, `datatype`, `documentation`, `function`, `listing`, `program`, `symbol`, `xref`
- Function cleanup: `function`, `comment`, `analysis`
- Comment audits: `comment`, `analysis`, `listing`, `xref`
- Struct/type recovery: `datatype`, `xref`, `function`, `symbol`, `analysis`
- Global/data labeling: `symbol`, `datatype`, `listing`, `xref`
- Caller/callee tracing: `xref`, `function`, `listing`
- Documentation transfer and binary comparison: `documentation`, `function`, `listing`
- Program navigation and persistence: `program`, `listing`

Load these groups as needed with `load_tool_group`. The `malware` group remains excluded by default; use it only when the user explicitly requests malware, IOC, or anti-analysis work.
When doing Ghidra reverse-engineering work, opportunistically improve clear function names, variable names, variable types, labels, and structs that are directly relevant to the current analysis.

**Naming**:
- Functions: PascalCase, verb-first. `GetPlayerHealth`, not `playerHealth` or `SKILLS_GetLevel`. Module prefixes (`UPPERCASE_`) are accepted and validated separately.
- Globals: `g_` + Hungarian (`g_dwCount`, `g_pMain`, `g_szPath`).
- Labels: snake_case.
- Struct fields are auto-prefixed on `add_struct_field` / `modify_struct_field` / `create_struct` — pass the logical name (`count`, `next`, `health`); the tool stamps the prefix (`dwCount`, `pNext`, `wHealth`).

**Hungarian quick-ref**:
```
b:byte  c:char  f:bool  n:int/short  dw:uint/DWORD  w:ushort  l:long
fl:float  d:double  ll:longlong  qw:ulonglong  ld:float10  h:HANDLE
p:void*/ptr  pb:byte*  pw:ushort*  pdw:uint*  pn:int*  pp:void**
sz:char*(local)  lpsz:char*(param)  wsz:wchar_t*  lpcsz:const char*(param)
ab:byte[N]  aw:ushort[N]  ad:uint[N]  an:int[N]
g_:global prefix (g_dwCount, g_pMain)  pfn:func_ptr (PascalCase, no g_)
Struct pointers: p+StructName (pUnit, pInventory, ppItem for double ptr)
```

**Type normalization**: `undefined1`→byte, `undefined2`→ushort, `undefined4`→uint/int/float/ptr (by usage), `undefined8`→double/longlong. Use Ghidra builtins (`dword`, `byte`, `ushort`) not Windows aliases (`DWORD`, `BYTE`) when calling `set_local_variable_type`.

## Workflow ordering (load-bearing)

`set_function_prototype` **wipes plate comments**. Do all structural work first:

1. Rename function + set prototype (parallel)
2. Type audit + variable rename (`get_function_variables` → `set_local_variable_type` → `rename_variables`)
3. Plate + PRE + EOL comments in one `batch_set_comments` call
4. `analyze_function_completeness` — if fixable deductions > 10 points, address and re-verify

Never rename a variable with a Hungarian prefix (`dw`, `n`, `b`, `p`, `sz`, ...) while its type is still `undefined*`. 
Resolve the type first; if undeterminable, use a descriptive name without a type prefix (`questBits` not `dwQuestBits`).

Phantoms (`extraout_*`, `in_*` with undefined types) are decompiler artifacts. Note them in plate-comment Special Cases — don't retry type-setting.

## Gotchas

- **Plate-comment newlines**: passing `\n` literally produces the text `\n` in the comment. Use actual multi-line strings.
- **Register-only variables**: when `set_local_variable_type` fails for a register var, document via `set_decompiler_comment` PRE_COMMENT (`"nIterator: int - loop counter (register-only)"`). The completeness scorer excludes these.
- **Struct access without a struct**: for raw `*(ptr+0x10)` access where no matching struct exists, add EOL comments at each access (`/* +0x10: flags */`) — satisfies the scorer without forcing struct creation.
- `/run_script_inline`, `/run_ghidra_script`, and `run_script` may appear callable in the MCP `program` group. Project policy still forbids using them for Ghidra database edits unless the user explicitly overrides this rule. Use native MCP tools instead.

## Skills available

- `ghidra-doc-function` — full V5 doc workflow (classify → rename → type → comment → verify) for a single function
- `ghidra-investigate-type` — discover/define a struct from generic-pointer parameters (`int*`/`void*`) and apply it across all callers

