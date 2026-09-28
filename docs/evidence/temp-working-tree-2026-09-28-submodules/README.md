# Temporary submodule working-tree checkpoint

The parent temporary checkpoint includes these patches because Git commits do not capture uncommitted submodule contents. No extra submodule commits were created. The original submodule worktrees remain unchanged.

Each patch captures all tracked changes against the base commit in `manifest.json`, including binary changes if present. Neither submodule had non-ignored untracked files. Reverse-application checks passed against the current worktrees.

After checking out the parent checkpoint and initializing its submodules, restore the edits from the repository root:

```powershell
git -C third_party/GekkoNet apply ../../docs/evidence/temp-working-tree-2026-09-28-submodules/GekkoNet.patch
git -C tools/BlueprintToCpp apply ../../docs/evidence/temp-working-tree-2026-09-28-submodules/BlueprintToCpp.patch
```

Apply only to clean submodules at the recorded base commits; the current workspace already contains these edits.
