# Ghidra Autolabel Runner

Experimental queue tooling for long-running Ghidra MCP documentation passes.

The runner does not try to invent names by itself. Static heuristics can find
good candidates, but function names, variable names, and structs still need an
LLM or RE analyst to inspect the evidence. The tool's job is to keep that work
continuous and auditable:

1. Discover candidate functions from the MCP.
2. Snapshot decompile, variables, comments, xrefs, and completeness data.
3. Let an agent produce an explicit JSON edit plan.
4. Validate and dry-run the plan by default.
5. Apply the plan only with `--apply`, then save the Ghidra program.

This avoids the dangerous failure mode where a background loop fills the
database with confident but unverified labels.

## Quick Start

```powershell
python tools\ghidra_autolabel\ghidra_autolabel.py schema
python tools\ghidra_autolabel\ghidra_autolabel.py scan --limit 50
python tools\ghidra_autolabel\ghidra_autolabel.py claim
python tools\ghidra_autolabel\ghidra_autolabel.py snapshot 0x1403e8840
```

Snapshots are written under `docs/investigations/ghidra-autolabel/`.

## Edit Plan Format

Create a JSON file like this:

```json
{
  "program": "SoulcaliburVI.exe",
  "address": "0x1403e8840",
  "function_name": "ALuxHUD_BeginPlay_LoadReplayAndArcadeWidgets",
  "prototype": "void ALuxHUD_BeginPlay_LoadReplayAndArcadeWidgets(ALuxHUD_Partial * pHud)",
  "calling_convention": "__fastcall",
  "variables": {
    "pWidgetClass": { "type": "void *" }
  },
  "comments": {
    "plate": "One-line summary.\n\nAlgorithm:\n1. ...",
    "decompiler": [
      { "address": "0x1403e884d", "comment": "Load and cache widget classes." }
    ],
    "disassembly": [
      { "address": "0x1403e8871", "comment": "+0x390 replay icon widget class" }
    ]
  },
  "structs": [
    {
      "name": "ALuxHUD_Partial",
      "fields": [
        { "name": "paddingBeforeWidgetClassCache", "type": "byte[912]" },
        { "name": "replayIconWidgetClass", "type": "void *" }
      ]
    }
  ]
}
```

Dry-run validation:

```powershell
python tools\ghidra_autolabel\ghidra_autolabel.py apply-plan .\plan.json
```

Apply and save:

```powershell
python tools\ghidra_autolabel\ghidra_autolabel.py apply-plan .\plan.json --apply --save
```

## Continuous Loop

For an overnight run, use the loop mode to keep producing fresh evidence
bundles. A supervising Codex/Claude session can then consume the newest
snapshot, write a plan, apply it, and continue.

```powershell
python tools\ghidra_autolabel\ghidra_autolabel.py loop --limit 200 --sleep 20
```

The loop records claimed/completed state in
`docs/investigations/ghidra-autolabel/queue.jsonl`.

## Rules Baked In

- The malware/IOC tools are denied unless this script is edited deliberately.
- `set_function_prototype` is run before comments because it wipes plate
  comments.
- Comments are applied in one `batch_set_comments` call.
- Plan application is dry-run unless `--apply` is present.
- Every MCP call includes `program=SoulcaliburVI.exe` by default.

