> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Latest G1 checkpoint - selected C18 material-slot live receipt - 2026-09-27

Root continued the Ghidra investigation and saved the selected helper/material-call binding in the existing SoulcaliburVI.exe program. Astra Xhigh with Daybreak explicitly disabled implemented two observation-only hooks for the provider's material getter and lower vector setter. Root reviewed the code and real PolyHook patch sizes, built the project with both CTests passing, corrected one stale test-only PolyHook stub after an initial full-suite failure, and passed the required full 1,179-test local suite (378 unit, 545 native-contract, 256 workflow).

One bounded active-combat C18 diagnostic at frame 172 completed the selected Start/primary BaseColor helper receipt and four linked material calls: slot 0 returned MID 1671439268928 and entered the lower setter; slot 1 returned MID 1671439302336 and entered the lower setter. Both setters received FName 23156 and the helper's exact four color bits. These pointers match entry-time shape rows for the selected provider, but the receipt does not prove that either vector row changed, what source supplied the color, a native tick, a material ownership lease, provider body occupancy, or native/render/GPU completion. The ordinary provider census remains invalid. See the [live receipt and immutable logs](../../../evidence/rollback-g1-c18-mid-slot-live-2026-09-27.md), [Astra implementation handoff](../../../evidence/rollback-g1-c18-mid-slot-implementation-astra-2026-09-27.md), and [Ghidra checkpoint](../../../evidence/rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md).

Observer validity: the selected call interval and getter-to-setter pairing completed; entry shape is comparison evidence only. Simulation: no independent corrected-history comparison. Recovery: no changed-consumer complete-B seven-tick pass; G1/G2 remain open. Coherence: no normal-render qualification. Ownership: no selected MID lease, actual row-change receipt or completion proof. Performance: no rollback-update measurement. The live stage passed bounded capture/cleanup only. Deployment was restored, zero game processes remained, Steam PID 9240 continued, and post-run preflight was ready. Next: test the exact selected MIDs' pre/post BaseColor row values, then resolve the chosen color source, selected readers/body branch and operation completion before any exclusion, retirement or rollback admission.

The complete prior status follows byte-for-byte (46131 bytes, SHA-256 `c4a2c05cf3d468faabbc688c907db627dc85f992460bd3aac39a2c5be8db39ee`).

---
# Latest G1 checkpoint - selected C18 Start/helper live correlation - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled added a bounded observation-only C18 broadcast-to-Start/helper receipt through existing hook owners. Its production-boundary RED was retained; 42 focused and 105 adjacent tests passed. Root reviewed the receipt, built the project with both CTests passing, and passed the full 1,158-test local suite. One bounded active-combat C18 diagnostic then observed the selected callback at frame 172 calling Start once and the primary BaseColor helper once. Start's component equaled the entry-time trace root; the nested helper's actor/provider equaled entry row 0. The exact MID slot write, color source, native tick, body occupancy and resource/GPU completion remain open. See the [live evidence](../../../evidence/rollback-g1-c18-start-helper-live-2026-09-27.md) and [updated Ghidra checkpoint](../../../evidence/rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md).

Observer validity: the bounded selected call interval completed and its raw/parsed receipts agree; ordinary provider generation remains invalid. Simulation: no independent corrected-history comparison. Recovery: no changed-consumer complete-B pass; G1/G2 remain open. Coherence: no normal-render qualification. Ownership: no selected MID lease or resource/GPU completion. Performance: no rollback-update measurement. Cleanup restored the journaled deployment, left zero game processes, kept Steam PID 9240 and post-run preflight ready. Next: observe the exact material getter slot return/MID vector write (provider vtable targets now resolved in Ghidra) and source asset, then trace selected readers/body/lifecycle before any exclusion or recovery admission.

The complete prior status follows byte-for-byte (44254 bytes, SHA-256 `3d785f76534d15a29ff2564e191c338c12f23bb7695f0e22fa366da698e5eec4`).

---
# Latest G1 checkpoint - selected trace feedback Ghidra investigation - 2026-09-27

Root continued actual reverse engineering in the existing SoulcaliburVI.exe Ghidra program and saved verified function names, prototypes, variable types/names, comments and a reflected 0x28-byte vector-parameter struct. Native StartTrace has distinct primary existing-entry and secondary new-entry color writes. The vector setter updates MID rows and proxy tasks; MID reset and republish are separate operations. The trace VFX finish callback changes retained slot, attachment activation and weak membership. Material replacement can update a native physics body, conditional on that selected provider having a valid body. The retained frame-172 C18 receipt identifies raw packed Life 18 and entry-time fighter/manager/trace-root pointers; a successful Start would use the existing-entry branch and BaseColor key. It does not prove Start reached the helper or identify its MID or body occupancy. The native MID getter is resolved; two generic Lux setters query only its success bit before overwriting a caller-supplied vector. Selected BaseColor asset readers and exact MID binding remain open. See the [Ghidra checkpoint](../../../evidence/rollback-g1-lux-unreal-feedback-ghidra-2026-09-27.md) and [ordered investigation plan](../../../rollback-g1-lux-unreal-feedback-ghidra-plan-2026-09-27.md).

Implementation/testing: no production edit, build or new live run at this checkpoint; the prior 1,145-test local pass remains the latest. Observer validity: the earlier 145-MID zero-serial sample is entry-shape evidence and ordinary provider census remains invalid. Simulation: no new comparison. Recovery: no changed-consumer complete-B pass; G1 and G2 remain open. Coherence: no normal-render qualification. Performance: no qualifying measurement. Next: establish whether Start ran, then bind any primary BaseColor call to its provider, MID and tick; resolve its readers and actual body branch before any state exclusion or material retirement.

# Latest G1 checkpoint - MID resource completion investigation - 2026-09-27

The 145-occurrence C18 sample, project build, both CTests and full 1,145-test local pass remain the latest implemented and tested state. No B recovery or live rollback was performed after that diagnostic. Astra Xhigh with Daybreak disabled reviewed the material ownership architecture and found no justified production change yet. The existing complete-B and C-only fail-closed guards remain in force.

Root traced the observed material instances through their native proxy allocation, vector and refresh tasks, BeginDestroy/FinishDestroy, deferred resource retirement and material replacement. The ordinary MID destruction fence is a render-thread task fence; the material tasks request the any-thread route and have no verified prerequisite into that fence. A delayed proxy-release batch can clear its count by submitting another render task, so a zero batch count is not release completion. Neither the sampled pointers nor the fences prove operation-scoped ownership, resource/GPU completion or a complete native-memory bound. See the [Ghidra investigation](../../../evidence/rollback-g1-mid-proxy-lifecycle-2026-09-27.md). Ghidra work was read-only in the existing Soulcalibur program.

Observer validity: the separate live diagnostic sampled all 145 ordered instances, but the ordinary material census remains invalid. Simulation: no independent corrected-history comparison. Recovery: no changed-consumer complete-B pass; G1 and G2 remain open. Coherence: no normal-render qualification. Performance: no qualifying measurement. Next: establish and test a material-operation completion dependency and the remaining producer/reset/replacement ownership; have Astra implement only an evidenced production contract, then root builds, tests and runs the shortest matching live experiment. Do not treat an empty delayed-release batch or submitted render fence as settlement.

The complete prior status follows byte-for-byte (40151 bytes, SHA-256 `5cb7718c84a95424d2e53abd3421003b4b0ec7082af6c5c11be44590a2546e24`).

---
# Latest G1 checkpoint - C18 145-shape live sample - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled compacted the existing C18 material shape capture to 192 ordered occurrences without expanding its 4 MiB observer reservation or weakening overflow rejection. The production-boundary RED reproduced 145 matched / 129 omitted at limit 16; selected GREEN checks passed 115 native-contract and 16 workflow cases. Root's project build and both CTests passed, then the full ordinary local suite passed **1,145 tests** (378 unit, 511 native-contract, 256 workflow). See the [implementation evidence](../../../evidence/rollback-g1-c18-shape-capacity-2026-09-27.md).

One bounded active-combat `c18-diagnostic` used the retained replay, startup flush and no async loading thread. It armed at frame 170, selected and returned C18 at frame 172, and the separate version 4 zero-serial diagnostic sampled all **145 ordered MID occurrences with zero omissions and matching entry copies**. The ordinary material provider census still rejected `entry_invalidated`; the sampled rows do not prove a generation, ownership, native byte bound, writer exclusion, resource/GPU completion or rollback admission. The stage passed bounded observation/cleanup only, not B recovery. See the [live evidence](../../../evidence/rollback-g1-c18-shape-capacity-live-2026-09-27.md) for the immutable report, raw log/sidecar, exact game/runtime/framework/observer/replay identities and source archive. Cleanup restored the journal, left zero game processes, retained Steam PID 9240 and post-run preflight was ready.

Observer validity: local suite passed and the separate live zero-serial shape diagnostic sampled this entry; ordinary provider census remains invalid. Simulation: no independent corrected-history comparison. Recovery: no changed-consumer complete-B pass; G1/G2 remain open. Coherence: no normal-render qualification. Performance: no qualifying measurement; historical incompatible failure remains. Next: root investigates the native owner, writer and completion paths for the observed MID proxy field and two nonempty vector backings in existing Ghidra. Require a production-boundary complete-B ownership/execution contract before another live recovery attempt. This diagnostic does not qualify G3 as a live rollback gate.

The complete prior status follows byte-for-byte (37,707 bytes, SHA-256 `90803468900590a248c65720b532786c3a8ba8252351a44505c847ed8b02d56a`).

---
# Latest G1 checkpoint - C18 TraceEventHandler receiver identified - 2026-09-27

The bounded combat C18 diagnostic now selects an active-combat callback at frame172 after arming at frame170. Its latest retained run `replay-9d8009d431bf49f4811e21e9dc6863ba` measured weak listener row0, indexed object310145/serial8261, vtable RVA `326B2E0`; the separate zero-serial diagnostic still rejected `listener_receiver_vtable`, ordinary material census remained invalid, and no ownership or recovery was proved. The stage passed only bounded observation/cleanup. See the [immutable report](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/e23c3f12c6f811052b6d5362cbe4a814e1c47420fe2118664a39184bd5cdc2aa.json), [raw sidecar](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/c42d6b6ff000cc68dfa15a60d0b8fb986ccb579f44dc05bffa154d8628168179.json), and [stage manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/47310991ecde96942e551a7aad2ae54f3ffbc01fee86d5b2212a3e51a06a0eb0.json). Cleanup left zero game processes and kept Steam PID9240.

Root verified in the existing Soulcalibur Ghidra program that UE class `LuxBattleTraceEventHandler` registers a 0x388-byte actor whose constructor installs table `14326B2E0`. Its BeginPlay binds that handler as the collection18 weak receiver; the callback uses its world to find the BattleManager and separately selects an `ALuxBattleChara` target. Root corrected and saved/read back the directly relevant constructor, BeginPlay and callback names/prototypes/comments. The prior base-Chara receiver assumption was disproved by the live table; the selected fighter remains a separate Chara identity. See [TraceEventHandler implementation evidence](../../../evidence/rollback-g1-c18-trace-event-handler-2026-09-27.md) and [rejected-type diagnostic evidence](../../../evidence/rollback-g1-c18-rejected-type-sample-2026-09-27.md).

Astra Xhigh with Daybreak explicitly disabled has now corrected only the receiver class check and versioned receipt reader to `326B2E0`, retaining the selected-fighter, BattleManager, VFX, indexed weak-serial, callback-forwarding, complete-B and C-only guards. The production-boundary RED reproduced the old rejection, then 104 selected native-contract and 12 workflow cases passed. This final receiver correction is **not yet project-built or live-tested**. The preceding diagnostic-only source passed the project build/two CTests and full ordinary local suite (1,127 tests); that receipt does not certify the new receiver source.

Next: root builds this final source, runs both CTests and the full ordinary local suite, checks preflight/journal and exact mapped identities, then runs one shortest combat C18 diagnostic with the same retained replay/startup interventions. Stop at its first failed predicate, investigate, and retry only after a specific change. G1 and G2 remain open: no changed-consumer complete-B recovery, lifetime/ownership, coherent rendering or performance qualification has been demonstrated. G3 cannot be inferred from selected tests or this observer-only run.
# Latest G1 checkpoint - zero-serial C18 live rejection - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled added a separate, fail-closed zero-serial C18 shape diagnostic and an ordinary-forward `c18-diagnostic` runner stage. The ordinary provider census, weak delegate serial rule, C-only retirement veto, historical Request/Prepare rejection and complete-B guards remain. Root built the final source with both CTests passing and ran the full ordinary local suite: **PASS 1,034 tests** (378 unit, 428 native-contract, 228 workflow). A first broad run stopped at a post-exit opt-in compatibility RED after 597 passes; Astra fixed that exact runner defect before the full green rerun. See the [implementation evidence](../../../evidence/rollback-g1-c18-zero-serial-diagnostic-2026-09-27.md) for immutable tests and source receipts.

One bounded live `c18-diagnostic` run used the retained replay, startup flush and no async loading thread. It selected and returned a C18 callback, but the separate receipt rejected at `entry_pointer_or_header` before listener/material traversal. The indexed hub serial was zero, ordinary provider census remained invalid, and zero detail rows do **not** establish empty native state. The stage pass means bounded observation and cleanup only. Root verified mapped runtime/framework/UCRT identities, game/replay hashes, zero remaining games, Steam PID 9240, a clean journal with all seven prior file states restored, and ready preflight afterward. See the [live evidence](../../../evidence/rollback-g1-c18-zero-serial-live-2026-09-27.md) for the immutable stage, raw log and sidecar.

Ghidra confirms native callback collection capacity one can coexist with null heap storage: the first entry remains inline. The current diagnostic conservatively rejects that form, but the live receipt did not publish which entry check failed. The next bounded production-boundary experiment is inline-capacity handling with distinct entry failure reasons; do not retry the unchanged live diagnostic. G1 and G2 remain open; status still reports G3 pass and no validated resolving live profile.

Observer validity: full local pass; live selected return bound correctly but material entry sampling failed closed. Simulation: no independent comparison. Recovery: no changed-consumer B pass. Coherence: unmeasured. Performance: no qualifying measurement; the historical 55.202 TPS failure remains incompatible.

The complete prior status follows byte-for-byte (32,032 bytes, SHA-256 `fae0e6b02b6c7657d107b03a206e2aa1f4cb26bc9943d8028238d177ea98b93c`).

---
# Latest G1 checkpoint - material shape live diagnostic - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled added an observation-only C18 material-memory shape census with a production-boundary RED/GREEN. Root built it and ran the full ordinary local suite: **PASS 1,008 tests**, including both CTests. The [census evidence](../../../evidence/rollback-g1-material-memory-shape-census-2026-09-27.md) and [live diagnostic evidence](../../../evidence/rollback-g1-material-memory-shape-live-2026-09-27.md) link immutable manifests, raw logs, exact runtime/observer/framework/game/replay identities and the relevant source. No rollback admission or destructive permission changed.

Two bounded owned live probes stopped at their first failures. The first failed historical scheduler `membership_counts` at target 303/current 304 and did not retain a VFX sidecar. The second used one instrumentation change to retain that sidecar, then failed `application_scheduler_lifetime_preflight_failed`. Its selected C18 occurrence had an indexed hub with serial zero, so the ordinary provider census invalidated entry and produced no material shape rows. Zero rows do not mean empty native state. Root verified mapped DLL identities, complete deployment journal restoration, zero remaining game processes, and the existing Steam client still running. No unchanged probe was retried.

The existing Ghidra program confirms object serial allocation is lazy; observing serial zero is compatible with a live indexed object. The next slice is a separate zero-serial diagnostic receipt with strict pointer and topology validation, without granting provider census validity, material ownership, completion or admission. A bounded ordinary-forward diagnostic must retain the sidecar before historical scheduler preparation. The ordinary C18 serial requirements and complete-B guards remain intact.

Observer validity: full local checks pass, but the selected live entry failed closed before provider traversal. Simulation: no independent comparison qualified. Recovery: no changed-consumer B pass; G1 and G2 remain open. Coherence: unmeasured. Performance: no passing measurement; the earlier 55.202 TPS failure remains incompatible.

The complete prior status follows byte-for-byte (29,686 bytes, SHA-256 `68895ecc0e93fb17d564b9c8078363024da1200484e903064a33f1174765dbbb`).

---
# Latest G1 checkpoint - trace material ownership observations and full local suite - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled wrote two bounded G1 slices in the existing checkout. The first makes C-only hidden-trace, particle and added-trace retirement fail closed when material producer coverage is absent; it retains complete B/C owners and prevents later render-command/release transitions after the sticky failure. The second adds process-pinned observations at the verified trace Start `1408D8C40` before its first state write and the nested raw-MID helper `1408D5840` before provider lookup. Six native entries now belong to the existing material guard. Entry/return, same-thread reentry, concurrent calls, partial startup and overflow are recorded; required native calls and callbacks still forward. These observations are **not** a producer exclusion, material lifetime lease, B undo, resource/GPU completion receipt or positive recovery. `HistoricalRestoreSupported()` remains false at Request and direct Prepare, and C-only retirement remains vetoed. The opt-in positive-admission regression remains RED by design. See [C-only veto](../../../evidence/rollback-g1-material-c-only-veto-2026-09-27.md) and [trace producer observation](../../../evidence/rollback-g1-trace-material-producer-observation-2026-09-27.md).

Root investigated the existing `SoulcaliburVI.exe` Ghidra program and verified both Win64 entry signatures against the source arrays. The [material native evidence](../../../evidence/rollback-g1-material-task-cpu-guard-2026-09-27.md) now records the MID `+0xF0` proxy creation, BeginDestroy/FinishDestroy fences and physical deletion, the deferred array/task callback, and the shared mesh GetMaterial/SetMaterial raw lookup and replacement path. The adjacent `+0xF8/+0x100` proxy ownership, provider replacement exclusion, transaction-linked deferred/resource/GPU completion and complete-B material reconciliation remain unresolved. A controlled late-entry case proves that an ordinary Clear inspection can be stale after its lock is released; entry hooks alone cannot grant destructive permission.

Root ran `python tools/replay_test.py build`: **PASS**, including both CTests; [build manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cbc4140878f871b84aa71fbf51c374298d9576cf708d21f5d62730e586b971bd.json) and [raw build console](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/0b696d05c1c74cbe9aa4269f98e194490041bac41f2ea575b696a4621159abc2.log). The subsequent full ordinary `python tools/replay_test.py local` passed **1,001 tests** (378 unit, 405 native-contract, 218 workflow); [local manifest](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/3e29c51f85970e87c241acdb46b3947ea8686fd23d193e4c703ff33fb7fc3a6a.json) and [raw local console](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/267c86d9ed33773375b2a8aabf63bb1f10c2e6aa02ac59f1c5fe24c8fc25477e.log). Current built SHA-256 identities: HorseMod `3b5dbd186585a532717f04ec550d27fbc7b33db712c74aab09cc8ce08306dcc4`, observer `fcced1a4ec66e5469f839c3d6f14ed9244cc05ca25de2f98d320e0802817867a`, framework `ca1b3d151efac51c70a4162abe59109d7973e35548bb5d79ed91122098bd132f`.

Observer validity: full local/G3 PASS for this source and binaries. Simulation, required native lifecycle, changed-consumer complete-B recovery, coherent normal rendering and performance: **not measured in a game for this source**. The retained historical 55.202 TPS live run is incompatible and remains a performance failure, not a recovery pass. `python tools/replay_test.py status` still reports G1 and G2 open, with a recorded clean deployment journal only. Independent process inventory found no Soulcalibur game and Steam PID 9240 still running. No deployment or live run occurred in this checkpoint.

Next: close the selected provider replacement/MID destruction and resource-completion ownership from the native paths above with a production-boundary RED/GREEN and complete-B reconciliation before reconsidering historical admission. Only then use the shortest representative active-combat changed-consumer run and independently matching continuation; do not repeat the unchanged 408 or 600-cycle campaigns.

# Latest G1 checkpoint - finish dispatch containment and full local suite - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled added transaction-scoped live-row containment at the existing VFX finish Dispatch owner. It uses the prepared manager allocation and captured ordered rows; the shared host predicate revalidates indexed receivers, both FName words, native function routes and trace-root membership. Unsupported protected calls write a bounded receipt and terminate before this dispatcher's native snapshot; admitted and unrelated calls still forward once. Startup installation is independent of optional diagnostics. The formerly RED same-count receiver-serial and FName-Number cases now pass, along with 29 adjacent containment/forwarding cases. This is terminal containment, not a receiver lease or B recovery. See [the finish-dispatch evidence](../../../evidence/rollback-g1-finish-dispatch-architecture-2026-09-27.md).

Root stopped the first build on a compiler macro collision, then Astra fixed it. Root stopped the adjacent material selection on a stale startup fixture extractor, then Astra updated the existing extractor. The rebuilt DLLs and both CTests pass. The broad local suite then passed 985 tests (378 unit, 389 native-contract, 218 workflow); a final build bound the same source state. The material guard [checkpoint](../../../evidence/rollback-g1-material-task-cpu-guard-2026-09-27.md) remains fail-closed for historical Request and Prepare until producer exclusion and proxy/resource/GPU lifetime are proved. Status reports current build/local receipts and G3 pass; G1 and the remaining gates stay open. Its receiver blocker label still names the former RED test, whose new result proves only narrow containment. No validated resolving live profile, game deployment, independent native continuation, coherent B recovery, visual check or current performance run occurred.

Observer validity: full local suite passes, G1 still open. Simulation: no new native game or independent control. Recovery: material and receiver unsupported paths contain failure; complete-B recovery unproved. Coherence: unmeasured. Performance: no new live measurement; historical incompatible 55.202 TPS remains. Cleanup: runner reports unknown; deployment journal is recorded clean only and Steam remains running.

The complete prior status follows byte-for-byte (22951 bytes, SHA-256 eb4283e2f297b152c60c2bc361030ee55b1f04c918299274c46d7586bd5f7913).

---
# Latest G1 checkpoint - material-task containment and finish-receiver audit - 2026-09-27

Astra Xhigh with Daybreak explicitly disabled added a process-pinned CPU material-task guard, then corrected two production-boundary defects found by independent Astra review. A builder could enter after a clear receipt and before C-only native retirement; corrected capture could repeatedly queue Finish without terminal failure. The first failures are retained as RED logs. Historical Request and direct Prepare now reject UnsupportedContent before ownership acquisition until native producer exclusion and proxy lifetime are proved. Corrected-capture material failure is sticky across historical restore, capture and seek, while complete B, provisional checkpoints and in-flight owners stay pinned. Ordinary capture remains independent. See [the material guard evidence](../../../evidence/rollback-g1-material-task-cpu-guard-2026-09-27.md).

Root's revised production-boundary selections passed, including cancellation, timeout and late callbacks. The existing build directory compiled, both CTests passed, and 34 selected native-contract cases passed against runtime ae92170ee242da47ebe9acd3d94187f491b3ad85218fd73c81c88b11e381912a. The retained build/local manifests and reconstructible source archives are linked in the evidence. The receiver [native/architecture audit](../../../evidence/rollback-g1-finish-dispatch-architecture-2026-09-27.md) confirms that a dispatch-entry live-row veto can contain the two same-count REDs, but does not provide a before-effects receiver lease or positive B recovery. No full suite, game deployment, rolling campaign, visual assessment or performance measurement ran. G0-G10 remain open; no validated resolving live profile exists.

Observer validity: 34 selected local native-contract cases pass, finish-receiver cases still RED and G1 open. Simulation: no new native game or independent-control execution. Recovery: material failures retain owners in local fixtures; coherent complete-B recovery remains unproved. Coherence: unmeasured. Performance: no new live measurement; historical incompatible 55.202 TPS remains. Cleanup: runner reports unknown; deployment journal is recorded clean, not live-verified.

The complete prior status follows byte-for-byte (20582 bytes, SHA-256 7b09412d3c998008c1fd07fc28b6d8baa38da57ab93e33d983172e6996ff7de0).

---
# Latest G1 checkpoint - trace material publication admission - 2026-09-27

The trace-state publication preflight now rejects an effective material provider on captured A or B trace meshes, including private and freshly projected children. Astra Xhigh wrote the bounded, guarded native-layout check with Daybreak disabled. Root's production-boundary regression changed from RED to GREEN, five adjacent native-contract cases passed, and the build/native checks including both CTests passed. The [material-admission evidence](../../../evidence/rollback-g1-trace-material-admission-2026-09-27.md) links immutable logs, reconstructible source and exact identities.

This closes only the known admission gap for captured providers. Start-created or later-replaced providers, MID vector B undo, proxy ownership, independent vector/refresh task completion and the same-count finish-receiver cases remain unresolved. No full suite, game, deployment or rolling campaign ran. G1/G2 and all later gates remain open; no validated resolving live profile exists.

Observer validity: five selected local cases pass, G1 open. Simulation: no new native game or independent control. Recovery: unsupported publication rejects while fixture B remains unchanged; complete B unproved. Coherence: unmeasured. Performance: no new live measurement; historical 55.202 TPS remains incompatible.

The complete prior status follows byte-for-byte (19080 bytes, SHA-256 1cc22db6fa427083ccdf07aa918288ba45eb643478087e9e6eda9d0e9bd55ebe).

---
# Latest G1 checkpoint - collection18 multi-bucket census - 2026-09-27

The collection18 observer now validates the native 16-bucket sparse-registry collision route using the recovered hash mixer. A production-boundary RED at `registry_hash_layout` became four selected GREEN cases; the affected observer module passed 144 tests. Root's build/native checks and both CTests passed. The [multi-bucket evidence](../../../evidence/rollback-g1-collection18-multibucket-2026-09-27.md) links immutable logs and source/binary identities. Astra Xhigh wrote the code with Daybreak disabled.

This receipt is observation only. G1 same-count finish-receiver replacement and before-effects lease remain unresolved. Full B undo, application/render/GPU settlement, Start-created material providers and live coherence remain unproved. The known full-suite G1 RED was not rerun unchanged, and no game/deployment occurred. G1/G2 stay open with no validated resolving live profile.

Observer validity: scoped 144 tests pass, full G1 open. Simulation: no new game run. Recovery: no changed-consumer B pass. Coherence: unmeasured. Performance: no new live measurement; historical 55.202 TPS failure remains incompatible.

# Latest G1 checkpoint - collection18 material-provider census - 2026-09-27

The existing collection18 owner now records a bounded entry-visible material-provider census for one-bucket world registries. The production-boundary missing-receipt RED became 2 selected GREEN cases, then the affected observer module passed 142 local tests. Root's build/native checks and both CTests passed. See [the provider-census evidence](../../../evidence/rollback-g1-collection18-provider-census-2026-09-27.md) for retained logs, source/binary identities, Ghidra contract and limits.

This receipt is observation only. The exact native hash mixer is now recovered for the next multi-bucket test. Start-created trace births, MID class, receiver lease, complete B undo and application/render/GPU settlement remain unproved. The full-suite G1 same-count receiver RED was not rerun unchanged, and no live game/deployment occurred. G1/G2 remain open; there is no validated resolving live profile.

Observer validity: selected local cases pass, full-suite G1 open. Simulation: no new game run. Recovery: no changed-consumer B pass. Coherence: unmeasured. Performance: no new live measurement; historical 55.202 TPS failure remains incompatible.

# Latest G1 checkpoint — finish-dispatch fixture route — 2026-09-27

The controlled fixture now invokes the existing production completion-observer Dispatch wrapper, and its unchanged, empty and unrelated forwarding controls pass. Separate serial-only and FName-Number same-count live replacements still reach the native snapshot after captured-row admission; both required G1 cases remain RED. See the [finish fixture checkpoint](../../../evidence/rollback-g1-finish-fixture-route-2026-09-27.md) for retained logs, current source/build identities and limits.

Root independently ran the three selected native-contract controls and build/native checks, including both CTests. The full suite was not rerun into its known G1 RED. No game/deployment or 408/600 campaign ran. G1/G2 remain open; there is no receiver lease, complete B undo, or application/render/GPU settlement proof. The [native/source investigation](../../../evidence/rollback-g1-finish-material-native-contract-2026-09-27.md) identifies the selected trace-material provider and the next ownership questions. Steam PID 9240 remains running; the deployment journal is clean.

Observer validity: scoped fixture routing passes, overall G1 incomplete. Simulation: no new game run. Recovery: both changed-live cases RED, complete B unproved. Coherence: unmeasured. Performance: unmeasured; the incompatible historical 55.202 TPS failure remains.

The complete prior status follows byte-for-byte (15,134 bytes, SHA-256 `a5b40c3e040ac370fa7152c7fbb2cde22502bbc2a68c1aa24e2a641dcb4a9e2b`).

---

# Latest coding checkpoint — collection18 receipt fixes — 2026-09-27

**Coding handoff complete; G1/G2 remain open.** The verified review defects are
fixed: writers spanning startup, recycled-slot ancestry, omitted-writer counts,
closure/identity validation and stale reader completeness. The fixture includes
the native stack-local stride-16 call and measures attempted guarded reads in
selected return windows. Alternate headers cannot qualify current source,
including through cached integration passes. See the
[receipt-fix checkpoint](../../../evidence/rollback-g1-collection18-receipt-fixes-2026-09-27.md)
for immutable RED/GREEN logs, source/binary identities and exact limits.

Final affected local selection: **209 passed** (127 native-contract, 68 workflow,
14 unit). Build and required native checks passed, including both CTests.
Read-only status identifies build/local as current PASS. The full suite remains
blocked by the retained same-count completion-receiver RED; it was not rerun
unchanged. No live profile is admitted and no game/deployment was run.

Observer validity: scoped receipts pass; coverage, allocation generations,
exclusion, leases, complete B and native completion remain unproved. Selected
collection18 still makes the overall phase incomplete. Simulation: no new live
result. Recovery: no changed-consumer recovery. Coherence: no new qualification.
Performance: unmeasured; incompatible historical 55.202 TPS failure remains.
The fixed observer reservation remains 4 MiB within existing 1 GiB accounting.
Debris remains exact/Unresolved; existing complete-B and lifetime guards remain.

Six installed states are unchanged; journal is clean; no game or owned build/test
process remains; Steam PID 9240 is running. Root owns the next bounded G1
implementation contract. This pass ends at the receipt-fix coding checkpoint.

The complete prior status follows byte-for-byte (13,136 bytes, SHA-256
`600d3d2c6dd96d53f90329b2104c050e6b8d38012bcb4b48472c892947e04c51`).

---

# Rollback checkpoint — 2026-09-27

Tooling is implemented and locally validated. **Local rollback remains unqualified;
G0–G10 are open, and live deployment is blocked by G1.** The
[ordered rollback plan](../../../rollback-netcode-local-readiness-plan-2026-09-22.md) still
governs native work. Networking and the original 408/600 qualification campaigns
were not advanced by this tooling project.

## Latest G1 checkpoint — collection18 writer receipts implemented

The next writer-observation dependency is implemented at the existing callback
owner and observer. The retained recursive-append regression is GREEN. Additional
same-address replacement and same-size delegate destruction regressions failed
before their fixes. The [writer checkpoint](../../../evidence/rollback-g1-collection18-writers-2026-09-27.md)
retains native evidence, exact source/executable/sidecar receipts and the limits of
this diagnostic. The earlier entry audit and implementation-blocker report remain
unchanged as prior evidence.

Known writer entries invalidate the selected occurrence before native execution;
original calls still forward exactly once. Bounded receipts include preexisting
and cross-thread writers, recursion/reentry, close-inside, unknown/overflow and
revoked return-side storage. No borrowed pointers are dereferenced on return.
The fixed observer reservation is 4 MiB, accounted within the 1 GiB production
ceiling. There is no new executor hook, public DLL API, state-policy or debris change.

[122 affected tests pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/9c9006254be93fd975d065e443b60052f8bfe0b8c75be118c64f741969a99aca.json)
(14 unit, 108 native-contract). [Build and mandatory native checks pass](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a923f2ea4a07b66863e73baf9b0409ad1402acecaef927be44bf83863e67458d.json).
The [full suite stopped at its first failure](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/acd8abc1ed59461650461ff9c41bd281ff0b8ff12f463a9e5c442e3d3a44ce16.json)
after 21 passes: the existing G1 finish-dispatch receiver replacement still reaches
native effects after admission. It was not retried, excluded or marked expected.
G1/G2 and full-suite admission remain open; no live diagnostic profile was authorized.

The receipt is not a writer census, allocation generation, lease, complete B or
completion proof. A selected collection18 occurrence keeps the overall observer
phase incomplete. Descriptor/listener/world/target/controller/material entry
state is still unimplemented. The next dependency is supported-layout closure of
remaining aliased/virtual replacement and reused-hub paths before adding that
entry witness; the report specifies the falsifiable production boundary.

Workspace: `46f9ad627065dd2f9b65aca4202aef6b79f1a014aec935599e503cf3fee68f7f`.
Build source: `1512d9c3afe08e0b744f0cc23efebe0d5c5b931ee776fbe194edbb5acd0fd19b`.
Runtime: `b652b2606536b9d058bff07468a2e617a38bf4106d0ba907acfce60a1e5d67b1`.
Observer/framework and state-policy identities remain as recorded below. Prior
build/local receipts describe their own source checkpoints, not these new hooks.

Observer validity: affected diagnostic GREEN, full suite RED. Simulation: no new
game execution; controlled native-contract fixtures only. Recovery: G1/G2 open,
no changed-consumer recovery or independent120-tick continuation. Coherence:
unmeasured. Performance: unmeasured; historical incompatible55.202 TPS failure
remains. Complete B and existing guards are preserved, debris exact/Unresolved.
No deployment/live/408/600 run occurred. All six prior states match the clean
journal; no game/test/build remains and Steam9240 is running. Earlier September27
sections below are preserved byte-for-byte as historical checkpoints.

## Latest G1 checkpoint — collection18 implementation blocked

The complete observation-only witness is **not implemented**. A new regression
through the actual existing callback owner forwards unrelated, input-filter and
collection18 calls exactly once, performs a controlled recursive append, and
revokes borrowed pages before return. It reaches the intended **RED: no
collection18 witness records that mutation**. The old observer's closed phase
cannot establish coverage of an unobserved writer.

The [implementation checkpoint](../../../evidence/rollback-g1-collection18-implementation-blocker-2026-09-27.md)
retains the RED, raw sidecar/executable, exact production extracts, source snapshots,
limited additional getter evidence and the precise next writer-observation contract.
No partial production observer or competing hook was added. Remaining generation,
replacement, listener/target and both-array material cases are explicitly unfinished.
The completed audit below and kind2/kind3 evidence remain unchanged.

[Build/native checks](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/a4f8cd6860e92fb535343efd4a20293b33b1451973e8b9485f70b607ccfed795.json)
pass both CTests and required shipped native checks. [Two existing observer controls](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/cb6ae507d736c70165c49edf183b9f65b6ccfa76e9c26812a37862ab41dd4018.json)
pass; this is not GREEN for the new regression and cannot qualify G3. Only the two
existing test/fixture files changed among retained source entries. Current workspace
is `d4442e317134640cbe78b02c59d84bb85db7e165acef7f9b981050c6234b7394`, with build
snapshot `6bb349e8a0e69eb6d2d44c9bc569c1b9a8b5379c8f8f1ec72b86378a066f1abf`.
Runtime/observer/framework hashes and state policy remain unchanged. The older
858-case receipt below describes the earlier source checkpoint, not this new test.

Observer validity: new required RED; existing controls pass. Simulation: no new
native game execution. Recovery: G1/G2 remain open, including the original
finish-dispatch RED; no changed-consumer recovery. Coherence: unmeasured.
Performance: no new measurement; historical incompatible 55.202 TPS failure remains.
Complete B, guards, 1 GiB production ceiling and debris exact/Unresolved are unchanged.
No live profile or full-suite admission exists. No deployment or 408/600 run occurred;
the clean journal and six prior states are preserved, no game is running, and Steam9240
remains running. Earlier September27 sections below are retained byte-for-byte as
prior checkpoints; this section is the latest result.

## G1 collection18 boundary audit — September 27

Astra verified the existing callback hook at `141D38300` can observe the
`140400A00` broadcast before its listener effects, but **recovery admission remains
NO-GO**. The listener supplies world context; the descriptor selects the affected
character/trace manager. Native recursion permits registration/removal, and the
complete affected material set includes both retained trace arrays. Collection
allocation generations, writer exclusion, trace-MID B recovery and the manager-task
to application/render/GPU settlement transition remain unowned.

The [native/source audit and smallest falsifiable next step](../../../evidence/rollback-g1-collection18-prebroadcast-2026-09-27.md)
retain exact assembly, source/binary identities, corrected/saved native comments
and the proposed single-occurrence diagnostic at the existing hook. No production
or test edits, build/suite rerun, deployment, live test, public API or debris-policy
change occurred. The completed kind2/kind3 correlation was reused. A diagnostic
value copy is not a lease, complete B or changed-consumer recovery.

Fresh read-only status confirms the build/local receipts below remain current;
the historical live failure remains incompatible at 55.202 TPS. G1/G2 stay open,
the full suite retains its G1 RED, and no resolving live profile is established.
All six prior deployment/configuration states were independently verified; no game,
build or test process was active and Steam PID 9240 remained running. The
[pre-audit September 27 status bytes](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/objects/376288e84c5b11c3e384040df948af82d0d385b6a5b32d84afff855e78bbc6af.md)
are retained; the existing status content below is preserved.

## Current identities and checks

[Final build/native evidence](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ad66ef08f96a692afeb65a3f4fe77474823ee56a9f06e161021df4c31064a76b.json)
passes both CTests and mandatory shipped native checks.
[Final local checkpoint](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/639efff35afd037abd74f81730728bad2f494affdfa70db595a4db516a3f92ee.json)
passes 858 cases: 378 unit, 216 workflow and 264 compiled native-contract cases,
with the one documented G1 regression excluded. This selection cannot qualify G3.
Readiness verifies both receipts as current. Exact/affected selection and layer
filters now keep development runs narrow; duplicate baseline runs were removed.
The [test-workflow checkpoint](rollback-test-workflow-2026-09-27.md) retains the
coverage audit and measured savings. Broad-suite wall time remains about 7.8 minutes.

- Workspace: `f441246ddeb1f8f4cb696a0f61f6e5163438dab379b4b2a5b7fd78480ee3b971`.
- Build source snapshot: `a96a0ae414a66b5b36f94e0595034c358b78e6a0d9bbd3e254eb39d3221dcc39`.
- Runtime DLL: `961c5c2837d86372236ed5e124d8186b055f6737125152ab8f9d03c9001fd50f`.
- Observer DLL: `190adbc89d9e6c1d4f96805671b5bc2364ee1230791470054da2d9b45f48f380`.
- Framework DLL: `ca1b3d151efac51c70a4162abe59109d7973e35548bb5d79ed91122098bd132f`.
- State policy: version 4, `b2307c93d691bfd6572328d2f10ed1b003fff416def914bc1612f37c38de457d`.

Built identities are separate from installed or mapped identities. Full game,
replay, dependency and test-binary identities are in the linked receipts.

## Gates, blocker and next experiment

| Gate | Current disposition |
| --- | --- |
| G0 Baseline | Open: build passes; unresolved prerequisite regression remains |
| G1 Consumer/state policy | Open: same-count VFX receiver replacement reaches native effects; positive lease/before-effects proof unresolved |
| G2 A617→C624 | Open: new short profile blocked on compatible G1 proof |
| G3 Full integration | Open: full suite retains the G1 RED; scoped selection is not a substitute |
| G4 Rolling 30 | Open: no new qualifying live evidence |
| G5 Rolling 600 | Open: no new qualifying live evidence |
| G6 Changed-input lifecycle | Open: distributed schedules need verified native consumption and independent lifecycle differences |
| G7 Recovery | Open: changed-VFX complete-B recovery and continuation unproved |
| G8 Epoch/reset | Open: no new qualifying evidence |
| G9 Coherence/ownership/cost | Open: runtime timing boundaries, coherent output and complete ownership/cost remain unqualified |
| G10 Network readiness | Open: preceding gates and local adapter acceptance remain required |

The blocking production-boundary test is
`test_consumer_host.py::test_vfx_finish_dispatch_revalidates_live_completion_rows`:
[retained full-suite RED](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/ad6a4379d509907728f3a50af1b09d58c0dee0003d70caa791ba1211788ddd34.json).
It needs a separately scoped native consumer/lifecycle fix, then the full suite.
**Next live experiment: undefined.** No validated resolving profile establishes
G1 lease/before-effects ownership. Do not retry an unchanged 408/600 campaign.

The latest historical live attempt still fails resumed performance at **55.202 TPS**.
Its scoped 120-tick simulation and prerequisite B recovery pass; changed-VFX
recovery was not exercised and coherence remains incomplete. It is incompatible
with the newly built candidate, not current qualification.

## Deployment and handoff

[September 26 preflight](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/evidence/manifests/39435b45861b9a8935f9f2f87992bdb335ae915d26bcda520811d58048edaf25.json)
was blocked on G1 and verified all six deployment/configuration states. Current
read-only status still records a clean journal; no game process was observed on
September 27, and Steam PID 9240 remains running.
No candidate deployment, game launch, configuration change or Steam termination
occurred during this tooling work. New mapped identities and native telemetry
remain unverified live.

Use `python tools/replay_test.py status` for read-only current readiness. Detailed
changes, immutable evidence, benchmarks and limitations are in the
[tooling handoff](rollback-tooling-validation-2026-09-26.md) and
[tooling command guide](../../../rollback-tooling.md). Production memory remains limited
to 1 GiB; complete-B undo and native/GPU completion/retirement guards remain required.

The previous status is preserved byte-for-byte in
[the status archive](rollback-status-history-2026-09-26.md), SHA-256
`9607462ce946e16386647597b53f190fcf97d82335a0149f36d963ddcb72717a`.
Older campaigns and links remain there; they are historical evidence.
