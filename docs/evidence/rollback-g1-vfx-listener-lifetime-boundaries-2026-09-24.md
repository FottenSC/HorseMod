# G1 VFX listener lifetime boundaries — read-only audit

Read-only queries against the existing `SoulcaliburVI.exe` Ghidra program, image base `140000000`, game SHA-256 `f8904e4b04bca3b47bc52a683f6190365d2eb89ee8f44f8072759e9c5e04a553`; plus current source inspection. No Ghidra database edit, project source edit, build, or live run in this audit.

## Exact native manager boundaries

- `InitializeLuxVfxInstanceManagerDefaults` `14085ED90` installs manager vtable `143356F68` and initializes the `+0x388` weak-listener TArray header (pointer/count/capacity) to zero. Its class size is `0x480`.
- Vtable slot `+0` points at deleting destructor `140895660`, which calls body `140862C90` before optional object free. `140862C90` frees the `+0x388` listener backing when non-null, then calls the base destructor. The decompiled body has no visible shared-lock acquisition around that free. This is an explicit lifetime route outside GC reachability.
- `ALuxVFxInstanceManagerEndPlay` `140899740`, vtable slot `+0x4D0`, destroys each live primary particle or ground component, calls `DispatchMulticastScriptDelegateAndPruneInvalidEntries` `1403D74D0` through manager `+0x388` with its slot ID, then compacts the corresponding manager record. EndPlay itself does not clear `+0x388`; later destruction frees its backing. A shutdown broadcast is therefore expected, but attributing the six late observed entries to EndPlay requires a stack witness.
- `ALuxVFxInstanceManager_OnParticleSystemFinished` `14089F870` and ground finish `14089F5F0` also enter `1403D74D0` with manager `+0x388` before removing their respective records. The reflected particle-finish thunk `142413250` invokes manager vslot `+0x5F8`.
- The dispatcher `1403D74D0` copies rows before calling listeners; for each copied row it resolves the weak receiver and named function, then invokes virtual `ProcessEvent`. Invalid rows can be pruned from the live array. Earlier listener callbacks may change later listeners while the dispatcher iterates its snapshot. A pre-snapshot table comparison alone cannot guarantee later receiver lifetime or callback execution.

## Mod publication boundary

`Sc6ReplayVfxState::table_offsets` is `{0x388,0x458,0x468}` (`Sc6ReplayVfxState.hpp:442`). `PreparedManager::Write` (`Sc6ReplayVfxState.cpp:2878`) copies an `Array` header to `image->manager_ + table_offsets[a-2]` for `a>=2`, so it explicitly republishes `+0x388`. `Publish` and `Undo` call this writer after checks; `ValidateBinding` checks the current thread, indexed manager generation, object leases, battle-manager identity, and epoch. This is a mod writer distinct from native registration and dispatch-pruning. The checks do not by themselves prove native worker quiescence through the write or receiver lifetime through later dispatch.

## Observed slice and remaining gap

The retained A210/B217 diagnostic reports 122 records and zero drops at its in-run status checkpoint. Its first 122 durable entries are 61 complete pairs (16 registrations,45 dispatches) on one thread for manager index310120/serial8362. The listener rows progress empty → one → two. Later shutdown calls overflow the final sidecar. See [live receipt](rollback-g1-vfx-observer-capacity-live-2026-09-24.json) and [Astra assessment](rollback-g1-vfx-ownership-assessment-2026-09-24.json).

The direct native registration helper census identifies `1408CDBB0` as the only inspected `AddUniqueWeakNamedCallback` caller targeting manager `+0x388`; it does not enumerate reflected binding, generic delegate mutation, direct array writes, or destruction. See [writer census](rollback-g1-debris-callback-writer-census-2026-09-24.md). No shared lock has been identified across registration, mod header publication, destructor free, dispatch copying, weak resolution, and `ProcessEvent`. The next bounded test must establish the execution owner or shared synchronization and lifetime rule for each of those exact routes. Do not turn the post-admission mutation RED green with an entry comparison alone, suppress native callbacks, or call process termination recovery.
