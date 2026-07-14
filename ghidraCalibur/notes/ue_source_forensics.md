# SC6 UE Source-Baseline Forensics

Run the reproducible comparison after the forensic worktrees exist:

```powershell
python E:\myMods\ghidraCalibur\tools\ue_version_forensics.py `
  --release-root E:\DevShitPosts\UnrealEngine-forensics\worktrees\release-4.17.2 `
  --staging-pre-root E:\DevShitPosts\UnrealEngine-forensics\worktrees\staging-pre-4.18 `
  --staging-start-root E:\DevShitPosts\UnrealEngine-forensics\worktrees\staging-4.17-start `
  --sc6-executable E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe `
  --corroboration E:\myMods\ghidraCalibur\forensics\ue_corroboration.json `
  --output-dir E:\myMods\ghidraCalibur\forensics
```

The current `staging-4.17` ref is not a valid candidate: it moved to 4.18. The tool instead compares the first and last historical 4.17 staging snapshots with final `4.17.2-release`.

`ue_discriminators.json` records source-only strings which are retained in one baseline but not the other, plus the matching SC6 binary offsets. `ue_corroboration.json` records independent read-only Ghidra/layout evidence for a marker. A baseline requires at least three independently corroborated positive markers and a two-marker lead. Anything weaker remains inconclusive and must be corroborated with read-only Ghidra control-flow, reflection, layout, or serializer evidence before applying any UE type.
