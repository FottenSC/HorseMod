> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Rollback tooling implementation and validation — 2026-09-26

This tooling work does not qualify rollback or authorize deployment. The existing
G1 receiver-lifecycle regression remains a separate blocker. No game was launched
or deployment/configuration changed by this tooling implementation.

## Delivered scope

- Structured outcomes and a named, versioned historical adapter. The retained
  55.202-TPS experiment reports scoped simulation success, expected prerequisite
  rejection/recovery, resumed-performance failure, unexercised changed-VFX
  recovery, incomplete coherence, and successful cleanup. Historical evidence
  is interpreted without rewriting it.
- Read-only project readiness, immutable evidence indexing and offline rebuilding.
  Build/local/live records are separate; missing identities or payloads cannot
  qualify a gate. Scoped diagnostics cannot replace G0–G10 requirements.
- Monotonic orchestration spans, fixed native rolling telemetry through existing
  owners, explicit aborted/missing/terminal samples, separate resumed TPS and
  optional-witness overhead comparison. Native interval boundaries remain
  unverified live while the prerequisite regression is open.
- Versioned experiment contracts and execution-derived coverage. The short
  A617→C624 protocol authors the same corrected prefix independently in candidate
  and control, reaches A through native forward execution, and explicitly omits
  preceding rolling transactions. It cannot replace the 408-cycle regression.
- Compressed content-addressed source retention with legacy ZIP reading and
  deterministic export. Existing archives are preserved. Compiled fixtures cache
  preprocessing, toolchain/flags and link dependencies; tests still execute.
- A distributed-schedule generator that requires positive native sample-consumption
  receipts, freezes the earliest eligible samples and ages 1/4/7, and fails when
  proof is missing. It does not infer lifecycle changes from input edits.

Commands and format contracts are in [rollback-tooling.md](../../../rollback-tooling.md).

## Retained regressions and prerequisite blocker

All evidence links below are immutable manifests containing retained raw-log links.

| Observation | Retained evidence | Disposition |
| --- | --- | --- |
| Full integration: live VFX receiver replaced with unchanged count reaches the native snapshot; expected failfast, observed exit 125 | [G1 RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ad6a4379d509907728f3a50af1b09d58c0dee0003d70caa791ba1211788ddd34.json) | Separate native lifecycle defect; test preserved; deployment blocked |
| New protocol accepted unsupported capture/fault combinations | [decoder RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/bc38e8cec951fb69577799353720bfd84ec15e03cce587f9db0e736b8819d380.json) | Python/native validation tightened together |
| Full native self-test overflowed its stack after test hosts acquired fixed telemetry arrays | [native probe](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e2a3f4ac4baf7cf7f7fb147e7e665f3b2455e910aa9aff6ca28820ae7cea85f0.json) | Test hosts allocate the real telemetry object off-stack; production remains fixed and accounted |
| Extracted rolling transition fixture omitted telemetry owner inputs | [fixture RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/de599cab51cd51e693047d75950545c09bd11bf1851d7a17ae4011621f87f45e.json) | Fixture models the additional observation inputs; production behavior unchanged |
| Historical audit manifest lacked a run summary | [index RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/b709e7b66c3f68e63e72ca28cf8c79e89ef245ce5927e46de20b3593c1587acc.json), [regression RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0abc444c4cd3d72124c357e3ad12ba735b79925ccc73ed86e3900126487441b8.json) | Versioned audit adapter preserves an unknown, non-certifying result |
| Environment-supplied compiler optimization flag did not invalidate fixture cache | [cache RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/65e1b40c37cc4267245fa62d3e3ad4b0be473bd9d6db466dbb9fdb966b2c1bf4.json) | CL, _CL_, LINK and _LINK_ recorded after toolchain setup |
| Full historical status omitted structured outcomes | [reporting RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/638e36da3751a3f07d7a62f163307e110d9968377349b7ddf63b5200ceb69895.json) | Shared read-only manifest interpretation supplies all formats |
| Checkpoint fixture omitted an existing consumer-abort predicate | [fixture RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/7f8bc298397fba2037b710370b50cd833bdbf5a5a0fce66c2d260223dd9d4ec3.json) | Fixture tests both consumer and particle aborts through the extracted production prefix |
| Explicit failed measurement retained a successful aggregate | [finalization RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/2e64136c2fd28ad37c254c745ef412654d1cae88aacd6729ae1c7d48d8fac2b9.json) | Failed outcomes set the aggregate before publication; preflight remains blocked |
| Live compatibility did not require external game/replay/dependency hashes | [identity RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/68e1f00d20b96daa5418640a7f2b2b74e29d73e0e089f0a09bdfc626ee397940.json) | All required identities must be present and current; incomplete identities are unverified |
| Copied cache metadata could identify a different compilation recipe | [cache RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a5d6c86a71c4f9b50ee5c0d4c8a465d02c54f2d35a0d4a15ca87d741bba65eab.json) | Lookup verifies the stored recipe against the current key before reuse |
| CLI exception wrapper hid argument-validation rationale | [CLI RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/b8c9308d42e9b782ed8be02bbb239ce4d99ce3d555bb605dfe7da8939c809ea7.json) | Parser text and existing exit code 2 preserved |
| Pause-trigger fixture omitted existing consumer-task mode | [fixture RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/9727de2bb00afba86942dc0fd26fced38789c0bde563a21fe9ccb80462bcc648.json) | Tests the actual earlier boundary required by that mode |
| Ground-motion extractor included an adjacent telemetry method | [fixture RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/95795d738d9fc4e1224b4ab9ba8efc29ad0c38952c79fc695b04e5cd46b2f731.json) | Extraction ends at the function boundary; assertions unchanged |
| Ground application fixture omitted existing G1 consumer interfaces | [fixture RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c15d2b73d54b9d86085627b66e809c5000854516034d3324cee599668361293b.json) | Models pending/completed consumer paths and checks that neither admits a new ground update |
| Component-dispatch fixture omitted the existing execution-scope header | [fixture RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f11f914ca33c06fd456224923c7d8453907ea2b596515207a4824da30ba11c5d.json) | Uses the actual header; native enforcement assertions unchanged |
| Default `status --since` compacted readiness twice and failed to render | [CLI RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ea314165d8700053275a031176a91cf7814c3ba1a29226436d14c487c9ae0fff.json) | Compact gate values survive repeated formatting; actual CLI regressions cover schema versions 1 and 2 |

The original timing-only failure remains at
[its original manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f0a0bc72716807e87246a43a8a59d44e8ea82bfbc45847d4fbcb24673659b831.json).
An offline audit exercised `status --report` in compact, JSON and full formats,
checked all six scoped conclusions, and verified that the original manifest hash
was unchanged:
[three-format audit](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/696d39b46df6bf37383427b9e592fca93ceb219d32be9f70d672d5dc98df1c07.json).

## Validation checkpoint

The final frozen-source `tooling-integration` selection passes **851 tests**, with
exactly **one deselected**: the retained G1 test above. It cannot satisfy G3. The full
suite's first unexpected failure is preserved; no live run may bypass it.
[Final integration receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0e80615f71c541d93c5a88cf8e2a814761973e71c2ffdb5ff22f463f8d1fcae8.json),
[raw test log](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/0062b38271ea7fe8d65752410a6ca46f4783c185b69ca9dd0e75f948162327cc.log).
Orchestration wall time was 468.39 seconds, with 461.91 seconds of fixture/test
execution. Build and integration receipts have the same workspace fingerprint.

The final frozen-source build passes both CTests and the mandatory shipped native
physics checks:
[build and native receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a7333e65284e60cb0d84f680931c6a6620c755909cd0d620ac18aceadbc72e64.json).
Workspace fingerprint is
`7b5ba830080e480770ee3cd2d8b4dc752fa60664939047f92f88926714e09d4b`;
source snapshot is
`e96f5641b93561b39aa66038c07d79c81586417d16760aff1066953a6ca815f4`.
State policy remains version 4, SHA-256
`b2307c93d691bfd6572328d2f10ed1b003fff416def914bc1612f37c38de457d`.
Full runtime/observer/framework/game and native-input hashes are in that receipt.

The focused tooling selection passes **39 tests**, including the production timer,
protocol decoder and repaired ground/dispatch fixtures:
[retained pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0a7661f1a777488adc0699aac5f9b4f4284394764448258a570a0a1f39e083fe.json).

Final reporting checks pass **79 tests**, including the new CLI regression:
[reporting pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/92f59f935bc36255450191a030f1d7c0b616cc610f043c71697d11fe22eb1d08.json).
An actual default-status audit verified that build/local success does not hide the
historical resumed-performance failure. Both compact baseline versions report
`no change`. All **22,728 evidence/retention file metadata records**, the index hash
and deployment journal hash remain unchanged by status:
[read-only CLI audit](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f86765e0a209939502d27142820e64b29214851630d4c0a38e32dbe96a603d0f.json).
Offline index rebuilding passed with **914 entries and no errors**; subsequent
ordinary finalizations add their own entries:
[index rebuild](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e1c6483ad87db2fb0661d6dda6f18fc56ccab8eabf585ea68e4de7689845aa3b.json).

## Measured fixture reuse

One cold/warm pair per group used unchanged inputs and separate new cache
directories. Both runs executed every requested test. All cacheable compilations
were reused on the warm runs; preprocessing and dependency/hash verification still
ran. The retained benchmark asserts identical source identities, test counts and
cache keys, with zero warm compilations:
[benchmark audit](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/1d2b2eb81f34c02f43f71a50f42c7bf2f70c29cb557dc58aab62edf99e5ff53c.json).

| Group | Test results, both runs | Cold / warm wall time | Cold / warm compiler and dependency time | Warm cache |
| --- | --- | --- | --- | --- |
| Runner | 220 passed | 72.61 / 56.50 s | 22.38 / 12.04 s | 6 hits, 0 compilations |
| VFX observation | 83 passed | 21.38 / 17.28 s | 4.10 / 2.08 s | 1 hit, 0 compilations |

Runner wall time fell by 16.11 seconds (22.2%); VFX by 4.10 seconds (19.2%).
These are single-pair local measurements, not guaranteed speedups or runtime
rollback costs. The runner cold run also retained a new workspace snapshot;
the warm run verified the existing snapshot. Compiler/dependency durations isolate
the cache effect; nested durations are not summed as wall time.

The first runner warm attempt correctly missed one compilation because absolute
temporary source names changed preprocessed `__FILE__` bytes. That failed the
zero-recompilation requirement despite passing all tests:
[initial warm receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3dc3e60568a4929a5d6d4634a89119b6f8e251a956860b5c9827dc7cdf9804e4.json).
The fixture now compiles the same generated source by its relative filename.
The cache still compares actual compiler-observed bytes. Miss reasons remain
explicit: cold entries reported `no matching compilation`; warm entries verified
preprocessing, toolchain and link inputs.
The benchmark predates the final two status-rendering tests; its linked source
identities and 220-test workload are preserved. That later Python-only formatting
fix does not change any fixture compilation input.

## Measured storage

Seeding the verified build snapshot reused **127,820,108 uncompressed bytes** and
stored **zero new payload bytes**. The compressed source database occupied
**32,358,400 bytes**. The operation took 1.752 seconds, including 0.740 seconds of
payload verification. These are measurements of this snapshot, not a storage target:
[seed receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/9bd6168352699d54dfee9a3d767242f5c7a2ca251eaca71ec987df8e2c9e0c75.json).

Two portable exports produced identical **33,513,584-byte** ZIPs with SHA-256
`a6ebbeec40f173989b07c90e4dff50342afb700ddeee3a650ee3a32bf3fdfa68`:
[first export receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/1bec0b960869225e359f5718ad8fd9add81e9c846bba65a30ce1e375f184f25b.json),
[second export console](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/1eaeb3302be78dc4b742edf392dd3e1a87bc8e08c42a8a9f169784d4d8af77a0.log).
Export wall times were 4.26 and 4.44 seconds. Historical archives were neither
converted nor deleted. These exports record the stated intermediate source
snapshot; the final build receipt identifies the final source snapshot.

The final build snapshot contains 17,347 files and **127,831,718 uncompressed
bytes**, all reused at final publication with **zero new payload bytes**. At that
checkpoint the source database was **32,862,208 bytes**. Final portable export is
**33,516,135 bytes**, SHA-256
`fa1826f9304fad566bc12867b0c3f1efb4f45b0e2ca4880de011bb30d506ceaf`.
Exporting twice to the same destination verified identical content without another
archive:
[first final export](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/b1114eac5e82ccd50fee85e5c09197e7f542d28b8f3c37952017ba799ec854c5.json),
[repeat export](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/f7b205e443d0f1d6a78765db02e48173d6daeb80ea530b6ee34d49b306a72518.json).

## Live admission and deployment disposition

Final-source preflight of `short-boundary617-624` is **blocked** by the missing
compatible G1 gate. The 15 environment/file checks pass: the deployment journal is
clean, all six recorded file/configuration states match, no game process is present,
Steam PID 9240 is running, required files are verified, and disk space is sufficient:
[preflight and deployment receipt](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/39435b45861b9a8935f9f2f87992bdb335ae915d26bcda520811d58048edaf25.json).

No game was launched, candidate DLL deployed, configuration changed, or Steam
process terminated. Installed DLLs remain the recorded predecessor; built candidate
DLL identities are separate. New mapped-DLL receipts and runtime telemetry are
therefore unverified, not inferred from build success.

The distributed-profile generator was also exercised against retained stock
evidence. It rejected missing positive native sample-consumption proof in the first
third and wrote no profile:
[bounded generation result](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/2fc1bac80297de1d4e341a76376f4c1aea20ebaf6d64f90fa3fb5d2d79c8e652.json).
Distributed schedules remain ungenerated until that prerequisite exists. This
negative result is not evidence that gameplay contains no eligible input.

## Native limitations and next work

Required build/native checks pass on the implemented telemetry, but native timer
boundaries, sample-consumption witnesses, optional-witness overhead, and the short
boundary experiment are not demonstrated live. The existing driver has no next
checkpoint after its final transaction; that sample is labeled
`terminal_no_next_checkpoint`, never hidden in a successful cost distribution.

G1 lease/before-effects proof has no established resolving live profile. The next
authorized tooling diagnostic remains gated on a separately scoped lifecycle fix,
the full integration prerequisite, and compatible G1 proof where required. Matching
short gameplay observations would not establish accumulated ownership-history
equivalence. The original 408/600 qualification requirements remain unchanged.
