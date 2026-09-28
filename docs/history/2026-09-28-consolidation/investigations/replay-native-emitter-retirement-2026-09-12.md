> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# Native CPU emitter retirement during a retained seek

## Failure and required invariant

Candidate replay-2fc1b85afaca4e31bafd865b770dc6c9 (runtime54f7d0ab / observerf6026e1d) completed the first exact205 checkpoint landing, then reached208 after a fresh B205 capture and A205 publication. CPU settlement rejected component21c545240c0, ordinal1: the prepared root21cf356c200 was absent from the observed image and its slot was null. Complete B remained retained. Cleanup completed with zero games; no independent control was launched. [Retained report and raw/source links](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/equal-checkpoint-native-emitter-retirement-failure.json).

This is native simulation/lifecycle ownership. A null slot cannot establish that native cleanup finished, and stale C allocation records cannot be freed or read after deletion. Reconstructing a live emitter at C would change native completion and peer behavior. The existing private B graph must survive until undo or explicit commit.

## Native evidence

- 141F78870 evaluates emitter completion, notifies other emitters through virtual178, invokes the completed emitter's virtual0(this,1), then clears its slot. If it deletes an emitter it also retires the component's render scene data. Native completion and render retirement remain unmodified.
- Sprite vtable143949B60 points to141F908C0; mesh143949D88 points to141F90870. Disassembly verifies RCX=this, EDX=flags and RAX=this. Both invoke141F8F9E0 and free the root if flags bit0 is set. Mesh first frees its material-override backing. Production hooks verify exact entry bytes for both variants.
- 141F8F9E0 destroys attached extensions and frees particle/index/instance/burst/duration backing. The existing image admission rejects nonempty attached extensions; mesh override storage is admitted only when empty. No new payload category is admitted here.
- 141F7DA40 also has a whole-array destruction path. That path is not admitted merely because the per-emitter destructor ran: the transaction still requires the same array address, count, capacity and ordinal at settlement.

## Bounded implementation and evidence

The existing CPU prepared owner registers its C root during execution. Entry and return witnesses distinguish Live, Destroying, Destroyed and Invalid. Hooks always call the native destructor exactly once. No freed root is read after its return. Unknown flags, type, thread, slot replacement, missing witnesses and array changes still reject.

A captured null C member plus completed destruction permits settlement only after validating component/asset leases, completed particle work and the private B graph. Native deletion already owns C retirement; stale allocation records are discarded without freeing them. Undo restores B into the null slot; commit preserves null C and transfers B for native retirement. Reopening remains retryable. The encompassing render transaction, GPU drain, deferred retirement and complete B publication remain required.

Runtimeaf7f6077 / observer6f6dddb6 builds and passes both C++ suites, the shipped-native physics fixture,151 focused Python tests and bootstrap. The new production-method fixture makes C memory inaccessible before settlement, verifies complete B recovery, and rejects missing/in-flight witnesses, changed slots/extents, pending particle work and mutated B. These are local ownership checks, not live continuation proof. The same repeated205->208 live experiment is running; its independent120-tick result remains pending.

The two detours reserve2MiB total in host-owned accounting. Per-emitter witness/list storage is charged through sizeof(Prepared); no checkpoint image, GPU readback or new GPU wait is added. Actual capture/restore/completion latency and cleanup must come from the assembled run. Hook removal requires no active destructor and no registered execution owner.

Remaining unknowns: whether this exact native deletion explains the live null slot (resolved by the guarded repeated-seek experiment), and whether undo after such deletion recovers B with120 independent ticks (requires a cancellation after resimulation). Whole-array destruction, slot rebinding and unaudited emitter payloads remain unsupported; no full-seeker completion is claimed.

## Live recovery result

Runtimec34aaf43 / observer5a906c11 now demonstrates the terminal path and complete undo: candidate5520fa06e5f941a4a6d4b8563ddc9714 records native deleting-destructor completion for ordinals1 and4, settles C208, fails deliberately after11 CPU participants, reopens without native allocation frees, drains again and recovers B210.120 ticks/480 callbacks/Lux poses and20 active HUD observations match native365a58ae6d5a483a91be0d418852400d. Both cleanups complete/zero games, no diagnostic GPU maps. [Retained result](../../../../build_cmake_LessEqual421__Shipping__Win64/replay-tests/native-emitter-complete-B-recovery-stage.json). Whole-array destruction and slot rebinding remain guarded unsupported cases. Capture125127us and publication-with-B183526us are partial costs only.
