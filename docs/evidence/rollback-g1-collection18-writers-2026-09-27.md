# G1 collection18 writer receipts — 2026-09-27

Implemented the next writer-observation dependency in the existing callback owner and observer. The retained recursive-append RED now passes. New same-address and same-size destruction REDs were retained before their fixes. **This is a bounded known-writer diagnostic, not a complete collection18 entry-state witness or G1 recovery.** The full suite still stops at the original finish-dispatch receiver-replacement RED; no live run is admitted.

Astra performed all production/test edits without delegation in the existing dirty checkout and build directory. The [entry audit](rollback-g1-collection18-prebroadcast-2026-09-27.md), [prior blocker](rollback-g1-collection18-implementation-blocker-2026-09-27.md) and kind2/kind3 evidence were reused and remain unchanged. This checkpoint supersedes the earlier decision to leave the writer dependency unimplemented.

## Native contract and additional investigation

The [complete new native MCP transcript](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/fb196e4fe040f4ff9a5ec5b4464acd82a61d1ef31aadbb1ae27b092695ac5e30.json) explicitly targets the existing `SoulcaliburVI.exe`. It includes disassembly, decompilation, callers/xrefs, bounded operand searches, comments, save and verified readback. No second import, database script, direct project edit or snapshot was used.

Current game SHA256 is `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`. Each of the twelve new 48-byte entry signatures was compared with the executable's PE section bytes: [first ten](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/92834e9601a7451e4afd140e14c031065fd8090f9af177c3470d17005cd6b6ee.json), [two row helpers](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/946f53a7c89ea4e7c38568282d9503d472c82a34d3f66838e8fcaea9b1dcad3e.json). The startup path validates all signatures before installing any observer detour. Per-site mismatch tests exercise that gate.

| Native entry | Verified effect/order and observation boundary |
| --- | --- |
| `14043D210` | RCX collection, RDX output-handle pointer, R8 receiver, R9 callable; returns the original output pointer. Allocates a temporary callable, compacts, writes count at `14043D32C`, grows if needed, transfers into the row, then publishes the handle. Positive recursion prevents compaction but does not prevent append. |
| Existing `1403CA7A0` | RemoveAll remains owned by the existing wrapper. Positive recursion can destroy/free callback storage and disable the row during broadcast. Nonrecursive removal calls range removal and capacity shrink. RAX is retained uninterpreted, never treated as a Boolean result. |
| `140399DF0` | Compaction can remove stale rows and shrink backing; returns without compaction at positive recursion. Receipt means potential mutation entry, not proof that its branch wrote. |
| `1403A1910` | Destroys the destination, copies tail rows over the same addresses, decrements count and optionally shrinks. Removal does not preserve insertion order. |
| `142050940`, `1403A1C50` | Growth/capacity helpers write capacity before calling the backing allocator. Probes start before those writes. |
| `142050570` | Moves/reallocates stride-0x40 backing; reallocation can return the same address. There is no native generation counter in the inspected header. |
| `1403AE520`, `140912DF0` | Collection destruction frees rows/backing. The hub destructor visits the embedded collections, including hub+0x810. Its entry invalidates before those visits. |
| `1403A22D0` | Clears a single row, including delegate destruction, heap release and disabling its unit count. |
| `1417DFEA0` | One-argument virtual copy (+0x50) rebuilds destination through `140399420`, preserving the source delegate handle. Equality at +0x58 is read-only, not this copy operation. |
| `140399420` | Calls the old delegate destructor at `140399453` **before** checking equal unit count. Equal-size replacement can destroy the delegate without reallocating. This additional gap was reproduced and fixed after the first writer implementation. |
| `1403A1A70` | Changes stride-0x10 callback backing, including same-address heap reuse. Used by temporary construction and copy paths as well as other native delegate families. |

Collection18 BeginPlayG supplies hub+0x810 to the binder at `1403B54E4`; EndPlayG removes this collection before actor EndPlay. Dispatch `140400A00` reaches the sole existing executor owner with return PC `140400A9A`. Callback `1403C5360` references point to that registration and unwind data. The hub constructor initializes 41 collections. Operand searches for +0x810/+0x7E8 were not truncated, but other native types use those offsets: those searches are **not** a proof that every alias or virtual mutation path is covered.

Targeted plate comments were saved and verified for the binder, range removal, copy virtual and equal-size resize. The binder's old inline-storage wording was corrected: three 16-byte units cannot fit a two-unit inline buffer. No type/prototype changes were made.

## Implemented production boundary

[CallbackExecutorDetour](../../HorseMod/horselib/deterministic/DeterministicHookSet.FrameAndStage.inl) calls the new observer before and after its original trampoline. There is no second hook at `141D38300` or `1403FCA60`. Existing unrelated/input-filter behavior remains in the actual production body used by the fixture.

[Collection18 receipts](../../HorseMod/horselib/deterministic/NativeReplayVfxCompletionObservation.Collection18.inl) select the first exact dispatch call and record hub indexed identity, collection/header/backing values, thread and observer ordering. Active writer tracking begins while observer admission is open, so a writer already executing when selection occurs can be retained and paired. Known mutation entries invalidate the selected occurrence before calling native. Recursion/reentry, overlap, header/layout/identity failure and overflow fail closed as evidence. Native callbacks and writers always forward exactly once with arguments, return values and LastError preserved.

The fixed bounds are 32 simultaneously tracked writers, 32 retained writer receipts and a 64-row/count-capacity association bound. There is no callback-row or descriptor semantic copy yet. Row association uses the captured range only. After invalidation, a row writer outside that range is conservatively retained as association-unknown; it is not followed to new backing or declared unrelated. This can include a binder's temporary storage. Unknown association is visible, not a proven selected-row mutation.

Return bookkeeping accesses only owned receipt metadata. It does not dereference the descriptor, collection, row, receiver, binder output or returned allocation. ClosePhase inside a selected call remains incomplete even after returns drain; selected-call pairing continues through the cutoff. A missing native return, capacity overflow or persistence failure cannot produce a complete receipt set.

Receipt sequence numbers order observer events only. They are not native allocation generations. Delegate handles, UObject serials and address equality also do not establish collection generation. Every selected occurrence reports `entry_state_witness_implemented=false`, `snapshot_complete=false`, and false writer-coverage/allocation-generation/receiver-undo/native-completion claims. Thus selection prevents the existing overall observation phase from being represented as complete, even for the stable/no-writer control.

[Runner retention](../../tools/deterministic_qualification/replay_control.py) independently validates the new version, reservation, proof-denial fields, bounds, pair sequence uniqueness/order, parent nesting and correlations. `vfx_collection18_writer_receipts_complete` means the bounded retained receipts paired successfully; it does not mean writer coverage or state completeness. Older 3 MiB sidecars remain readable under their existing schema.

Total observer reservation is now **4 MiB**, increased from 3 MiB. The compile-time bound charges simultaneous records, active tokens, producer/registration/collection18 metadata, serialization scratch, 22 pinned detour/trampoline reserves and 8 KiB overhead. No event-path heap allocation was added. The existing host accounting and runner diagnostic reservation include this charge; the production ceiling remains 1 GiB.

## Tests, exact sources and build

The fixture compiles the actual startup/exports/completion path and unchanged production callback-owner body. It substitutes external native services and allocated memory, not the observer, serializer, generation counter or validity predicate. These are native-contract simulations; they do not execute the game's writer code.

| Check | Evidence/result |
| --- | --- |
| Retained recursive append regression | Existing RED from the prior report; GREEN in this checkpoint, without rerunning the unchanged old failure first. |
| New same-address replacement RED | [Receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ac91a84e1e14714037aa85ed9687f8276386cfc84d72f462e8dd5a6d9278a228.json), [raw](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/f315c0cb94de282a708e87824603ecd728c44ca3f55ab088f3e80cde716b7a49.log). Child exit0 and native replacement/forwarding; missing production receipt fails. Source `fa3c66f78bc8f2f412f519ed0ffe0df8c2c36e24860f832764f27eb973a1a447`. |
| Additional same-size destruction RED | [Receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/28c9daee2841013f7aa5d4ec6c67179fc6bcdaf36ce16f0b8d3657d300190a31.json), [raw](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/b76744cbb199b2f1c4fd9add0f6e1055b5f779e16bd2397d379155219e1c8cdf.log). Native resize forwards and destroys; required writer site absent. Source `0fe267ba6c5a3ef143144ac0ff49ecaa9f580a67f15bb0c13256c10e1b7ef07f`. |
| Final affected file | [122 passed: 14 unit, 108 native-contract](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/9c9006254be93fd975d065e443b60052f8bfe0b8c75be118c64f741969a99aca.json), [raw](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/49d9a2c0db5990a7e79d6ad1b207ab4daf3a28cad6e00356721a1013a6336452.log). |
| Build/native checkpoint | [Pass](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a923f2ea4a07b66863e73baf9b0409ad1402acecaef927be44bf83863e67458d.json): both CTests and required shipped physics/native checks. |
| Required full suite | [RED, 21 passed then first failure](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/acd8abc1ed59461650461ff9c41bd281ff0b8ff12f463a9e5c442e3d3a44ce16.json), [raw](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c96cbd8cd292452aa71d7bf3cda947671d41dc0de1308e707c82055a0aeed9e2.log). `test_vfx_finish_dispatch_revalidates_live_completion_rows`: receiver child exit125, same-count replacement still reaches native snapshot. No retry or exclusion. |

New cases cover recursive append, tail replacement at the same address, same-handle virtual copy, removal, executor reentry, active/history overflow, unknown callable/layout, unreadable header, header over-capacity, mismatched indexed hub identity, backing reuse, destruction, preexisting/cross-thread writers, closure inside a writer, and native row/output-page revocation before return. Twelve signature corruptions reject installation. Retention tampering rejects forged coverage/generation/undo/completion, duplicate sequences, invalid parent, overflow, reservation and invalidation claims. These hub/header tests are not the still-missing listener/allocation/material graph tests.

Commands used the existing runner only:

```text
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_writer_receipts --layer native-contract
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py::test_collection18_writer_receipts[row-resize-sites17] --layer native-contract
python tools/replay_test.py local --test tools/deterministic_qualification/tests/test_vfx_completion_observation.py --layer all
python tools/replay_test.py build
python tools/replay_test.py local --group all
```

The [source/evidence index](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/bd7833eafc3b615c3ab67c8593aeeaf95b866b537ea0277603e34e77562a70de.json) retains exact source receipts and a [normalized review diff](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/6dbf7bcfcec1fa15d726da1c613d86dd51d0dad93b796dbe076fe706be41bca0.diff). [RED/early artifacts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/93b97a81dde59756e36220d0876dc118ae7a2f75a845e4ee4b07f5b0ca95d15a.json) and [final fixture artifacts](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/7ee7a5657e50cccf68eb62b90f6b9bf7c62f5b3bf1f69b686a8d6cbb8d87e08e.json) include raw sidecars, executables and exact generated production extracts. Eight source paths changed from the prior checkpoint; no recovery/state-policy source changed. No edits occurred during tests or build.

- Workspace: `46f9ad627065dd2f9b65aca4202aef6b79f1a014aec935599e503cf3fee68f7f`.
- Build source snapshot: `1512d9c3afe08e0b744f0cc23efebe0d5c5b931ee776fbe194edbb5acd0fd19b`.
- Runtime DLL: `b652b2606536b9d058bff07468a2e617a38bf4106d0ba907acfce60a1e5d67b1`.
- Observer/framework DLLs unchanged: `190adbc89d9e6c1d4f96805671b5bc2364ee1230791470054da2d9b45f48f380` / `ca1b3d151efac51c70a4162abe59109d7973e35548bb5d79ed91122098bd132f`.
- State policy unchanged: version4, `b2307c93d691bfd6572328d2f10ed1b003fff416def914bc1612f37c38de457d`.

## Remaining dependency and admission

Known writer entry observation is implemented; it is not a universal alias census. The current selector does not validate/copy descriptor semantics or listeners, bind receiver world versus descriptor-selected character/root, inventory trace controllers and both material arrays, or establish collection/callback allocation generation. Writer-free receipts cannot authorize those claims. No incomplete descriptor/material observer was added.

The next falsifiable dependency is a supported-layout closure at the existing boundary: bind captured row storage and finalized one-argument callback identity to all routes that can replace it, including direct/virtual copy or destruction outside an observed parent and reconstruction at a reused hub/row address. The native constructor and aliased/virtual paths must be resolved before a positive entry-state witness; existing known-writer cases provide a regression boundary. A negative case must execute the native mutation route through production and invalidate before the first affected read, with original forwarding preserved. If a route has no resolvable owner, it remains unsupported evidence rather than a fabricated generation.

After that closure, the prior audit still requires two listeners selecting one target, listener order/handles, both trace-controller arrays and fallback material sources. Descriptor start configuration is not history or B. Manager-task handoff and native/application/render/GPU completion remain separate. No changed-consumer recovery claim is possible without real mutation, complete B and 120 independently matching continuation ticks.

| Dimension | Current result |
| --- | --- |
| Observer validity | Known-writer diagnostic and affected controls GREEN; full suite RED at the original G1 consumer defect. Full entry-state witness remains absent. |
| Simulation | No new native game execution or continuation comparison. Fixture native services are simulations only. |
| Recovery | G1/G2 open. Complete B and existing guards unchanged. No site11 termination, PendingKill revival, callback suppression or receipt completeness counted as recovery. |
| Coherence | No new normal-renderer observation. |
| Performance | No game or observer-overhead measurement. Historical incompatible 55.202 TPS failure remains. Local test/build duration is not rollback cost. |
| Deployment | No diagnostic profile/full admission, deployment, live run or unchanged408/600. All six prior states and clean journal verified; no game/test/build process remains, Steam9240 running. |

Debris remains exact/Unresolved. No public DLL API change. The [baseline](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/4d68a88bfd49eecaae1eb1f5fac629543dfe8b9c997530619b9e5e1f20e8f2eb.json) preserves the initial checkout, process, journal and status bytes. Older September27 status content is preserved when adding this checkpoint.

The [final receipt](../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/9511e75c5c1f7a243293942a33fb07465ff1c8a0b6fb07222d216f1425cc37e2.json) verifies current build/source compatibility, all six file states, unchanged clean journal, Steam9240 and absence of game/owned test/build processes. It retains raw read-only status and exact before/after status bytes; no game was present to map DLLs.
