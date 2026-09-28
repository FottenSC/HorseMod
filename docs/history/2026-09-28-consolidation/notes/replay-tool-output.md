> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Replay command output

Replay commands now default to compact summaries. Complete reports, raw native/test logs and original console transcripts remain on disk. Successful summaries link one immutable console transcript, which contains the immutable qualification-manifest link. Failures additionally show available coordinates, cleanup and evidence links. Diagnostic rejection messages without an error code do not automatically become the failure cause.

```powershell
python tools/replay_test.py preflight --profile changed-rolling30-proposed
python tools/replay_test.py local --group runner --output json
python tools/replay_test.py status
python tools/replay_test.py status --report E:/myMods/build_cmake_LessEqual421__Shipping__Win64/replay-tests/combat-restore-stage.json --output json
python tools/replay_test.py build --output full
```

`--output compact|json|full` only selects presentation. Compact is bounded to 2 KiB for success and 4 KiB for failure/unknown, including newlines. JSON emits one versioned compact document with `schema_version: 1`; optional fields are omitted when unavailable. Full exposes the original detailed console output. Help retains argparse formatting. Truncation is explicit and complete logs remain linked. Formatting does not change experiment profiles, selected tests, return codes, control compatibility or acceptance results.

`status` only reads existing reports, deployment-journal identity and bounded native-log sections. It does not acquire deployment locks, launch, recover, retain evidence or scan Steam. With no report argument it selects the newest stage/executor report and excludes it if it belongs to a different active run. Explicit report selection inspects that historical report. Unavailable/incomplete data is reported as unknown; it is never promoted to a pass. A journal marked active is recorded ownership, not proof that a process is alive.

Native-log inspection reads at most 64 KiB of header and 256 KiB of tail. Run identity must match. A failure extracted from the tail is labelled `bounded log window`; an authoritative comparator failure takes precedence. Later recovery failures remain separate. Status returns successfully when a report path was selected, including failed/unknown experiment results; ordinary experiment commands retain their existing exit codes.

Validation and retained output-reduction measurements: [reporting evidence](../../../evidence/replay-reporting-2026-09-22.json). No live game campaign is required for formatting changes.


## Native diagnostics and read-only inspection

Newly retained capture logs have a versioned diagnostic index linked to their immutable log hash. It records capture, preparation, publication, execution, completion, retirement, recovery and cleanup predicates in log order, with available run/traversal identities and byte offsets. Adjacent duplicate messages are coalesced; repeated ticks in different traversals stay distinct. Broader recognition is diagnostic metadata and does not change acceptance classification. The existing capacity-failure classification is preserved.

```powershell
python tools/replay_test.py inspect --report E:/path/to/immutable-manifest.json
python tools/replay_test.py inspect --report E:/path/to/immutable-manifest.json --participant ground --tick 617 --output json
python tools/replay_test.py inspect --report E:/path/to/immutable-manifest.json --offset 123456 --output json
python tools/replay_test.py status --output json > previous-status.json
python tools/replay_test.py status --since previous-status.json --output json
python -m tools.deterministic_qualification.runner --output json replay-compare-trajectory --reference reference.json --candidate candidate.json --report comparison.json
```

`inspect` returns one matching primary diagnostic per page, its bounded surrounding context, and a continuation offset when more remains. Participant and tick filters combine. Offsets address capture logs concatenated in manifest order: reuse the same report and filters when continuing. Indexed reads verify index and raw-log hashes before trusting coordinates. Verification may read the whole indexed log, but does not copy it or emit its contents.

Older logs are scanned in windows of at most 1 MiB per request, including identity lookbehind. A partial search says `no match in scanned range`; this is not proof that the complete log has no failure. Legacy windows do not verify the whole-log hash. Unlabelled messages without a run marker in the contiguous window are excluded and reported as unknown. Missing/corrupt indexes, missing logs and oversized partial lines are explicit diagnostics. Inspection returns 1 for unavailable/unverified evidence and 0 for a valid read, independently of the experiment result. It never launches, recovers, deploys or retains anything.

`status --since` compares a saved JSON status summary and reports changed semantic fields. Elapsed time alone is ignored. JSON retains the current fields so the next comparison can use it as a baseline; different run identities produce `new run`. Missing identity or an invalid baseline produces `unknown`. File redirection in the example is performed by the shell, not by status.

Standalone runner commands use the same formatter and retain complete console transcripts and exact published report snapshots. Trajectory comparisons retain scope, limitations and first differing boundary/fields. `--output` works before or after the subcommand; conflicting repeated values reject before work begins. Nested handlers produce one summary. `full` preserves the detailed stdout/stderr streams while retaining their transcript. Existing command exit codes remain unchanged.

Extension validation: 273 selected runner, reporting, fidelity, profile, evidence, deployment/recovery and source-retention tests passed. Actual CLI formatter replay reduced retained preflight output by97.95% and native-success output by92.31%. The latest immutable native log independently confirmed preparation code7 at target617, line279772. [Retained extension evidence](../../../evidence/replay-reporting-extension-2026-09-22.json). No live campaign ran.
