# Collection18 implementation checkpoint — 2026-09-27

**Decision: keep the new production-boundary regression RED; do not ship a partial collection18 observer.** The requested complete witness is not implemented. No production code, public API, native hooks, recovery guards or state policy changed. This is a test-only implementability checkpoint, not a recovery result or a proof that a future witness is impossible. Astra performed the edits without delegation.

The [completed audit](rollback-g1-collection18-prebroadcast-2026-09-27.md) is unchanged. Its descriptor/listener/Start analysis and the completed kind2/kind3 correlation were reused. The additional native reads below address previously unresolved getter dependencies only.

## New executable evidence

The existing [VFX fixture](../../tools/replay_vfx_completion_observation_selftest.cpp) now compiles the actual `DeterministicHookSet::CallbackExecutorDetour` body and its two input-array helpers, extracted unchanged from production. It continues using the actual startup block, observer, DLL exports, completion block and runner retention function. There is no substitute fixture observer, generation counter, admission predicate or recovery implementation.

The new [native-contract regression](../../tools/deterministic_qualification/tests/test_vfx_completion_observation.py) is `test_collection18_recursive_append_requires_writer_evidence`. Its external native callee models the already verified positive-recursion append behavior of `14043D210`. It is controlled native dependency code, **not execution of that game function** and not live proof of a callback census.

The test establishes, before its failing assertion:

- Unrelated and input-filter calls each forward once through the actual owner. The input-filter path retains its real before/after input observations.
- A synthetic native call reaches that same owner with the real return PC `400A9A` relative to the synthetic image; `_ReturnAddress` is not overridden.
- The collection18 call forwards once. The callee appends from count 2 to 3 at recursion depth 1 without entering the observer's different byte-bound registration hook.
- The callee revokes access to the borrowed descriptor, hub, row and callable pages before returning. The production hook returns without touching those pages and drains its in-flight count.
- The real observer closes its existing phase with `complete=true`, but emits **no collection18 witness**. The assertion requiring an invalidated collection18 record fails. Existing phase completeness is scoped to existing observation; it never implied coverage of this unobserved writer.

This is a narrow missing-observation regression. Its two initial rows do not establish a validated receiver/world/target/material graph, actual listener invocation order, or a positive collection18 case. Tests for two listeners selecting one target, generation mismatch, same-address replacement, removal/reentry, unknown/unreadable/over-capacity rows and material outside the selected new trace have **not** been completed. They remain required before a complete implementation; no fixture grants their outcomes.

| Receipt | Result and scope |
| --- | --- |
| [Semantic RED](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/93e3bf82e6038c5a982f59090f8e1c2e36fa73320dd3c29f36f03911f6a50213.json), [raw log](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/5d05e634630c94a6ecc940343c5f6cffd0b3c34cb6b929a50a119d4e6764ad1f.log) | Child exits 0; forwarding and page-revocation checks pass; pytest fails at missing production witness. Stopped at that first semantic failure; no unchanged retry. |
| [RED attachments](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/dc6da94fa797b6c0b6ea1023ab5174635cff95189a4b03b648ee0779e366420b.json) | Immutable sidecar, executable, extracted production bodies and supplemental native MCP reads. |
| [Existing observer controls](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cb6ae507d736c70165c49edf183b9f65b6ccfa76e9c26812a37862ab41dd4018.json) | Two native-contract cases pass: ordinary forwarding and the existing full-capacity phase. This is **not** GREEN for the new regression. |
| [Build/native checkpoint](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a4f8cd6860e92fb535343efd4a20293b33b1451973e8b9485f70b607ccfed795.json) | Build, both CTests and required shipped native checks pass. Runtime/observer/framework hashes remain unchanged. |

The first fixture attempt had a [compile setup error](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/5e5c01dd17a575edf59e6d244682025a0a1e5621efd192b74c11762f47ec19de.json): including real `Types.hpp` also requires the existing build's generated headers. Adding that include directory was the specific change preceding the semantic RED. The compile error is not counted as the requested regression failure.

Commands used the existing runner and native-contract layer:

```text
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_recursive_append_requires_writer_evidence --layer native-contract
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_completion_observes_true_entries_without_changing_native_calls --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_vfx_shared_capacity_full_phase_and_forwarding --layer native-contract
python tools/replay_test.py build
```

## Why an entry copy is insufficient for the requested witness

The existing observer installs ten native hooks. Its collection writer pair is byte-bound registration `1403A3B70` and `RemoveAll 1403CA7A0`. `HubWriterBefore` selects only its active disable-hub occurrence. The collection18 one-argument binder `14043D210` has no observation owner. Merely calling a new copier from the existing callback executor would not make that writer visible.

An entry copy followed by return bookkeeping cannot distinguish a stable collection from an intervening unobserved append/replacement. Re-reading borrowed storage at return is prohibited and would fault in the new regression. Even a permitted matching byte comparison would not prove absence of same-address replacement between reads. The existing observer lock protects metadata; recursion and delegate handles supply neither an allocation generation nor a complete writer receipt.

The requested contract therefore needs writer observation covering the selected occurrence from before its first read through native return, including invalidation during observer phase closure. The current contract does not supply this. Reporting successful generation/mutation validation from the copied values would be false. Marking every occurrence permanently unknown would avoid a false pass but would not implement the requested complete witness. Neither option was added to production.

**Smallest next falsifiable step:** establish the bounded writer-observation contract for `14043D210` and the already owned `1403CA7A0`, including the native paths that replace/reuse a callback row at the same address. Verify signature, ownership and notification ordering before wiring it into the existing observer. The regression above is the first required check for that integration. Its success alone would still leave the other required negative cases and receiver/material coverage open. No second hook at `141D38300` is needed or permitted. A writer receipt would invalidate evidence, not block callbacks or become a lease.

## Additional native read dependencies

The retained supplemental MCP transcript explicitly selected the existing `SoulcaliburVI.exe`. It made no database changes, imports, snapshots or scripts.

- Provider table `1438829C0` slot `+628` points to `141DC8A90`: material count comes from skeletal asset `mesh+910`, then asset `+A8`; null asset returns zero.
- Slot `+4E8` points to `141DC8220`: material selection first uses a non-null override from mesh `+808` / count `+810`, otherwise skeletal asset `+A0` with **0x30-byte** rows and count `+A8`. An override-only inventory would miss affected fallback materials. These getters only read in the inspected native code; an observer need not invoke them.
- The inspected retained receiver table `14326B8D8` slot `+138` points to `141C204E0`. It checks receiver/outer flags and the outer's indexed object flags, requires its immediate outer to be a `ULevel` through `141ACED50`, then reads level `+C0`. The framework's `AActor::GetWorld` outer-chain helper is not this implementation. This does not establish the runtime table or world of any unobserved collection18 listener; those still require validation.

These reads narrow a future implementation. They do not establish synchronized receiver/world/registry generations, controller allocation generations, material coverage through new trace creation, or complete B. Both `+418` and `+428` trace arrays remain required by the retained audit.

## Source, cleanup and claims

The [baseline receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/033abd1ec9386112a84585466b712aa1ccf3fb7c910406d83aee96b09b7d5210.json) retains the dirty checkout inventory, process commands, journal, six prior file states and prior status/audit bytes. Comparing all entries of the prior and new build snapshots finds exactly two changed source paths: the existing Python test file and its existing C++ fixture. [Review diff](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/bd5977bdcd1815482747e6d4109b0fdbcf4bc9ede632b4c2606e48220108002f.diff) normalizes line endings for readability; snapshots retain exact bytes.

New workspace fingerprint: `d4442e317134640cbe78b02c59d84bb85db7e165acef7f9b981050c6234b7394`. RED source snapshot: `4811989ca4ae71556b44a0cc715df0ebd662f811edc7db55874b7d60011cd8ae`. Build snapshot: `6bb349e8a0e69eb6d2d44c9bc569c1b9a8b5379c8f8f1ec72b86378a066f1abf`. All include reconstructible untracked test contents.

| Dimension | Checkpoint |
| --- | --- |
| Observer validity | New required collection18 regression RED. Two existing controls pass; complete witness and remaining cases unimplemented. |
| Simulation | No new game execution or continuation evidence. |
| Recovery | G1/G2 open; original finish-dispatch RED remains. No changed-consumer complete-B/application/render/GPU recovery. |
| Coherence | No new normal-renderer observation or qualification. |
| Performance | No new game/rollback/observer overhead measurement. Incompatible historical 55.202 TPS failure remains. |
| Memory/ownership | No new production allocation; existing observer reservation and 1 GiB ceiling unchanged. No ownership/exclusion/completion claim. |
| Deployment | No live profile, full-suite admission, deployment or 408/600 run. Six prior states preserved, clean journal, no game; Steam9240 retained. |

No test was deleted, deselected globally or marked expected-failure to obtain a pass. Read-only status reports the latest two-case selection as current; that does not resolve the retained new RED or qualify G3. The older 858-case receipt belongs to the earlier source checkpoint. Complete B, existing guards, debris exact/Unresolved and all prior primary evidence remain unchanged.
