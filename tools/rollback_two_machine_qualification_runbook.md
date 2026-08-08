# Physical rollback release qualification

Physical qualification is trace-derived and candidate-bound. Operator-authored
counters and schema-1 per-machine reports are not evidence. All commands must
use the exact candidate DLL, `trace=true` beta profile, and
`rollback_physical_case_matrix.json` frozen into the candidate manifest.

Protocol, snapshot, candidate, trace, physical-manifest, and per-machine
report versions are read from and validated against the immutable candidate;
operators must not transcribe them from this runbook. Normal rendering is
mandatory; `lux-no-render` is never release evidence.

## Initialize

Create the candidate manifest only after the repository and recursive
submodules are clean. Candidate creation validates the trusted-golden schema
and oracle contract before writing anything.

```powershell
python tools\rollback_two_client_acceptance_run.py `
  --write-candidate-manifest reports\rollback_beta\candidate.json `
  --built-dll build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll `
  --require-clean-candidate

python tools\rollback_two_machine_qualification.py `
  --initialize reports\rollback_two_machine\physical.json `
  --candidate-manifest reports\rollback_beta\candidate.json `
  --host-machine machine-a --guest-machine machine-b `
  --host-endpoint steam:<HOST_STEAM_ID64> `
  --guest-endpoint steam:<GUEST_STEAM_ID64>
```

Initialization derives commit, DLL, protocol, snapshot, replay/golden/profile
inventory, matrix, and tool hashes from the candidate. Steam endpoints are bound to the inverse
runtime Steam ID64 values. Machine names remain explicit operator attestations;
the tooling does not claim hardware-backed machine identity.

## Run one segment on each machine

Tag the runtime request with the same run, case, segment, seed, protocol, and
snapshot on both machines. The runner derives the schedule SHA-256 from the
candidate-bound matrix policy and seed; operators cannot supply it. The case's
runtime profile is the exact name in `rollback_physical_case_matrix.json`.

```powershell
python tools\rollback_physical_case_run.py `
  --candidate-manifest reports\rollback_beta\candidate.json `
  --dll build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll `
  --beta-profile tools\rollback_beta_steam.ini `
  --tag-request <ROLLBACK_REQUEST.txt> `
  --trace <CURRENT_PROCESS_TRACE.jsonl> `
  --report <HOST_OR_GUEST_REPORT.json> `
  --case clean --segment active --role host `
  --run-id <RUN_ID> --seed 0x5C6B5001 --wait
```

The runner accepts only one PID/start marker and requires an activation event,
at least two production status events, and a final qualification terminal
event. It derives time, frames, fault/stall/route counters, transitions,
identity, failures, consumed replay identity, and shutdown state from JSONL.
Representative-content SHA-256 values come from the runtime-validated consumed
input sidecar and are never operator-supplied tags.

## Ingest and finalize

```powershell
python tools\rollback_two_machine_qualification.py `
  --record-segment reports\rollback_two_machine\physical.json `
  --host-trace <HOST_TRACE.jsonl> --host-report <HOST_REPORT.json> `
  --guest-trace <GUEST_TRACE.jsonl> --guest-report <GUEST_REPORT.json>

python tools\rollback_two_machine_qualification.py `
  --finalize-case reports\rollback_two_machine\physical.json `
  --case clean
```

Repeat the segment IDs specified by the matrix. Disconnect/reconnect is two
segments with distinct nonzero lobby, session-contract, and epoch identities
(`fail-closed`, then `clean-lobby-recovery`). Content coverage uses the three
trusted golden IDs plus `lowest-sha-remaining-corpus`. Long soak requires 3,600
seconds and 216,000 confirmed frames.

## Validate and release

```powershell
python tools\rollback_two_machine_qualification.py `
  --validate reports\rollback_two_machine\physical.json

python tools\rollback_two_client_acceptance_run.py `
  --beta-release-gate `
  --candidate-manifest reports\rollback_beta\candidate.json `
  --local-qualification-report <LOCAL_REPORT.json> `
  --release-qualification-manifest reports\rollback_two_machine\physical.json
```

Validation reopens every copied trace and report and reproduces all claims.
Any modified trace, report, profile, matrix, candidate, DLL, helper, missing
path, duplicate path, schema mismatch, or host/guest identity mismatch fails
closed.
