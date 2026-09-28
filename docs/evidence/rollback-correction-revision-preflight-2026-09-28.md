# Rolling correction revision preflight — 2026-09-28

This is a local input-adapter checkpoint. It does not change physics admission or qualify live rollback.

## Defect and fix

A schedule could contain a later arrival with the same or an older expected source revision. `ReplayCorrectionSchedule::Create` accepted it, leaving the rolling host to fail only when that arrival was reached, after the retained run had started. The qualification wire parser and grouping step likewise accepted the malformed order.

The schedule now requires strictly increasing arrival ticks and expected revisions. The wire parser and grouping step apply the same rule before handing the request to the host. `BeginRollingSchedule` checks that the first expected revision matches its retained checkpoint before acquiring the rolling window; a mismatch returns `GenerationMismatch`. Later revision IDs need only increase: `Sc6ReplayInputSource` allocates IDs from its session counter, which can advance beyond an installed older revision after recovery. Runtime `Admit` still checks each arrival against the actual installed revision.

## Retained tests

| Check | Receipt | Result |
| --- | --- | --- |
| Native schedule rejected duplicate/decreasing revision, before fix | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cf16b6ef8ba4e0256a01e9ea65bae73a2a23c5405a433a3caed1ecb6d8b7d598.json) | Failed at new production-boundary assertion. |
| Qualification wire rejected duplicate/decreasing revision, before fix | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3a5e5ff8d4c723ab707b38f0e9c3f765275d2f73f38adaeac955185207bba8d9.json) | Failed at new wire preflight assertion. |
| Native grouping rejected duplicate revision across arrivals, before fix | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3ea2ab56f07f95137cf5c7f3046cf801308d360a673365129e6af3c8273303e1.json) | Failed at new grouping assertion. |
| Final focused unit and native-contract cases, including revision assignment | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ea0ae96dfe9163e15bba74b7adf3b96d1b90d4b14ca3bd4448f3d6e34a629751.json) | Three passed. |
| Final native build and two CTests | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c5432dfaccae2eed216dd791518a32d2ff4025fadbbc355523e66a3593547926.json) | Passed. |
| Independent changed-free-body dispatch regression | [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/8d731ab122c1f4e95ee77952100b709c9bdda226edc7356d1a02fb366423f0ea.json), [raw failure](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/77db1d4bf28f7e3a44a4daf05c268f4c1d41a4270ba535ff25de7f018f70f00a.log) | Still RED, fixture exit 136 at the physics start wrapper. |

The focused passes do not replace the full local suite required for live admission.

## Remaining input contract

The authored local adapter copies overrides and keeps immutable revision handles; it has no device one-time sampling or prediction policy. The out-of-window admission predicate fails closed, but a complete recoverable late-input/session-reset contract and live continuation are still unproved. G10 remains open. G0 remains RED, so there was no deployment or live correction.
