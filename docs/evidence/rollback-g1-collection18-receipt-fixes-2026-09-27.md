# Collection18 receipt defect fixes — 2026-09-27

Coding checkpoint complete; **G1/G2 remain open**. GPT-6 Astra made the production/test edits without delegation. This supersedes the diagnostic limitations identified in the two retained read-only reviews, not the preceding native audit or recovery blockers. The latest instruction confines this handoff to receipt fixes; the next G1 implementation contract belongs to root. No deployment or game run occurred.

## Changes and limits

- Known writer hooks now track active calls from installation, including calls spanning observer startup. Preexisting writers are retained by entry sequence, so recycling a lower active slot cannot detach a child from its parent.
- Version 2 publishes independent retained/attempted/omitted counts, active-tracking overflow, pending counts, observation-start and close sequences. Overflow flags may describe omitted writers. Overflow remains incomplete evidence.
- The reader validates hub and collection identity, row association, callable classification, nesting, parent ancestry, preexisting overlap and closure intervals. Missing, malformed, wrong-run or staged publications clear previous completeness. Historical version 1 remains retainable but incomplete.
- The existing native-contract fixture now includes the verified `14043D25A -> 1403A1A70` stack-local stride-16 call before compaction. Ordinary append therefore consumes an additional receipt and conservatively records unknown row association. Native forwarding remains exactly once.
- A first-invalid/second-valid occurrence case proves the first witness is retained. A `ReadProcessMemory` attempt counter with a revoked-page positive control checks the exercised return windows. **The older page-revocation-only cases did not prove absence of attempted guarded reads.** Source inspection and the new counter support the narrower current claim; this is not a general memory-access or lifetime proof.
- Alternate observer-header overrides are rejected before local execution and before integration-cache lookup. A cached current-source pass cannot qualify an alternate header.

Only seven production/test/tooling files changed from the preceding build; the [exact diff/source and RED receipt index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/07e28aaa56750a73620d35be18f7008188aefee582b0a4361c7a164acf626123.json) identifies each. The observer remains bounded at **4 MiB**, including fixed metadata and 22 hook reserves, within the existing 1 GiB accounting. No hook entry, public DLL API, state policy, complete-B guard or debris policy changed. There is no writer exclusion, allocation generation, receiver lease, entry-state snapshot, undo or completion proof. Selecting collection18 still makes the overall observer phase incomplete, even when its retained known-writer pairs are complete.

## RED and GREEN receipts

Each defect experiment stopped at its first failure. The immutable manifests include the precise command, source snapshot and raw log; compiled RED executable identities and cache receipts are also retained in the index above.

| RED case | Manifest | Raw log |
| --- | --- | --- |
| Writer spans startup | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/32ce208514945988667baa3c95c8616cc8a7e91ec88c2597cd49b2a3737fe8e7.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/35fd108ccf5ea6f2ed3bcb6846dfa5c1201c072965f469457f2394829885443c.log) |
| Recycled parent slot | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/49b1f82c0664e97baf8f8f3080470d5276ba54e2f6505b7622144737a287a181.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/79d85df3ee944aa1a3f4c3b1a25035570df6415ef249e1f6b2383e222250d684.log) |
| Omitted recursive writer | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cacf6c9c728187426bcdb4ece933431115fdfbf2aff7096d0ed679bb8615f85d.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/ee5295404f12283966813cc033b8d0f9e1e48f2d5a3055b305d628e8a8170601.log) |
| Stale phase completeness | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/9abc1816634ce6a268dd236bf4e68cc2e213f80d7cf770260014330cf1366d14.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/0e19e008928e833c0bec7db59cb90a32083b1979d8edfa56fda30de83baa40fd.log) |
| Deleted receipt suffix | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/e65bfea7012f811987f3ca574fbb2b1acdbe1fa42a8c239dcbc6728642d0d3b8.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f039b339385e0154b9c482ea4ef578432266d863b73bf7d9f4d286554311533a.log) |
| Wrong writer object | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/bfd7898fe1981e77185a0cbde75dd29989be9db137e687b43d3d1c7634ac355c.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/85d6c460d05c94cb9966796d22b184418656abea06a8a3b645e66fed1de6c306.log) |
| Impossible return interval | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/0ca1a5b7f6fdca0ef6a2ff04c19842d3b70d596648c73a71f218dafbb138b43a.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c9487d4cfde843406c2dcd2ed69cddd3376353f112affe4e5deff2e04e55c5cf.log) |
| Alternate source qualification | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3a46c3dc4a68c573e12b5d5ce12543113f125c782410dff5d71ac83b76ad3eb5.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/1b9728f563330deb7fecc7faf563b6ba753cba59f4de9a74f07480c4e0507a92.log) |
| Missing native scratch call | [receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/feed98b55f9f08183ebf3989f3631771f4aaecc4fc9dd8d249a6d5ac05555de4.json) | [RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/3ab11eb10203fb19f73d205eedfabc9da2e5496043dd91262c1e3616ce24d03c.log) |

The alternate-source RED log includes the controlled subprocess stub's `1 passed` text in the failure traceback; that is not another successful test. The stub now emits no misleading test summary. Some early pytest temporary sidecars were cleaned by pytest before separate artifact copying. Their raw failure logs, exact source snapshots and compiled executable/cache identities are retained; no missing sidecar was recreated and relabeled as original. [Available fixture artifacts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/25fba39acd6fe62e99d734f60f9303bfbe1b5315165afaaab365ec450bfc3b6c.json) include the GREEN sidecars and generated source fragments.

Final affected selection, after the build: **209 passed** (127 native-contract, 68 workflow, 14 unit), [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/c34bae28e436d4ccde72f6b6c662ec1503dc48af4feb1a10bfce45a816b1c466.json). Command: `python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py --test tools/deterministic_qualification/tests/test_replay_workflow.py --layer all`. This includes overflow/unknown, same-address replacement, recursion/removal/reentry, original forwarding and corrupt-publication controls. The manifest links its immutable raw GREEN log.

`python tools/replay_test.py build` passed: [manifest](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/45716867cbe746284956fe4141e001c1b0185f51239f7384eddd5514d38b0220.json), [build log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/86477058eda8342cd9f2c67fa4d498a1fd54ce194fce377e2d417616bd6e077a.log), [required native checks](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4a651a5225914d0aecb1bff68a20f5f1d0aabbc9aebeb39e6b4ff9ff201d05bb.log), both CTests passed. Source remained frozen during build/test execution. Existing physics continuation controls do not establish changed-consumer G1 recovery.

## Identity, disposition and handoff

The [final independent audit](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/50bd7d1d9fd8ece6b517b4413df8076ff1640b1861d07beeab8a5bbdfa767315.json) retains binaries, exact source/build identities, checkout inventory, six installed states, journal and processes. Workspace `1ca340d793cc6201977c089fa82259e12f57f538f5f9adffa1556be5e5f40124`; reconstructible build snapshot `31d3f7e5ffcd703e617175b8bb779d50ad8347f5a7edfc60e9b941aa2ea3fce0`; runtime `edd074cc81f8ab6732641936e99b6752645c0f6cf38143e754d16d3098e0287f`. Observer/framework and state-policy identities are unchanged and recorded in the manifest. Game identity remains `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`.

[Read-only status](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9570f8c3fa50ba9b4cf8c85b07c974ee65537129cb65d156b976b0a3d3fd4bd8.log) reports current build/local PASS, incompatible historical live FAIL, G1/G2 open and no validated resolving live profile. The unchanged full-suite same-count receiver RED was not rerun or excluded. A full passing suite remains required before live admission. Root owns the next bounded contract; this checkpoint introduces no new recovery route.

All six installed/configuration states match the baseline, including three absences. Journal SHA-256 `e5310921978707b1bf5c8b2651d816e4d2e8bde80516d517b321d32790bfbc4d` remains clean. No game or owned build/test process remains; Steam PID 9240 is running. No candidate DLL was mapped in a game. Prior status bytes (`600d3d2c6dd96d53f90329b2104c050e6b8d38012bcb4b48472c892947e04c51`, 13,136 bytes) are preserved as the exact suffix beneath the new checkpoint.

| Dimension | Result |
| --- | --- |
| Observer validity | Scoped receipt/reader regressions GREEN; full writer coverage and entry-state witness unproved |
| Simulation | No new live simulation result |
| Recovery | No changed-consumer complete-B/application/render/GPU recovery; G1/G2 open |
| Coherence | No new renderer qualification |
| Performance | No new measurement; incompatible historical 55.202 TPS failure remains |

Debris remains exact/Unresolved. No required callback suppression, PendingKill revival, in-flight resource release or expected-state installation was introduced.
