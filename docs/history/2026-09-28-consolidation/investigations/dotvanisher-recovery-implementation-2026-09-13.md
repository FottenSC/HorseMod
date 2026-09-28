> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# DotVanisher watch-assignment recovery implementation

Implemented `0.2.0-dev` for a successful assignment received before the assigned peer's native route exists. The existing host retirement timer remains separate. This change does not repair pre-assignment connection timeouts or BattleSync loading completion, and it does not establish the cause of the reported host incident.

Source: `DotVanisher/WatchRecovery.inl`, `WatchRecoveryState.hpp`, `WatchRecoverySites.hpp`, integrated by `dllmain.cpp`. See `DotVanisher/README.md` for supported ownership, lifecycle and deployment limits. The build script now also stops on configure failure instead of attempting a potentially stale build.

## Native boundaries

Eleven guarded detours cover request submission, assignment, acknowledgment, native state update, explicit reset, start/finish, destruction, incoming end/reset and replacement watch request. Two further prologue guards cover route-service resolution and lookup. All recovery behavior remains disabled until the entire set is installed. Partial installation keeps pass-through trampolines valid until process exit.

The native event-7 acknowledgment (`142E54120`, now `HandleLuxorWatchAssignmentAcknowledgment`) dispatches sender `(4,status)` and returns true. It is delivered without delay. The assignment message `0x0C` reaches `142E6C540` separately; only status zero, an existing peer and a missing route qualify for deferral. The request must have entered an empty queue on the observed owner thread while the connection is ready.

Only values are retained. Replayed assignment data is passed to the original response handler using a stack read archive; complete assembly proves that this handler reads only mode `+0x14` and cursor `+0x18`, consumes six bytes, and never releases that archive. Neither dispatcher nor archive destructor receives the borrowed object. Native shared pairs obtained during lookup are released within that callback using the two distinct observed refcount modes.

Native state-update processing runs before retry, so its timeout/failure wins. Retry never extends the supplied request timeout or 20-second cap. Duplicate retained assignments cannot reset it. Cancellation and session teardown discard pending values; unrelated queue activity requests native resolution at the next original-thread update instead of silently losing an already-consumed assignment. Generation checks run after native probes, and pending state is consumed before a native completion can reenter the mod.

Foreign-thread replay is deliberately unsupported: retained values wait for the original owner thread or a terminal lifecycle callback. If the original thread never returns, the mechanism cannot promise completion within its deadline. No native allocation/reference is retained while waiting. Peer/route/session identity changes disqualify replay into replacement state. Runtime scheduling, re-entry and endpoint coverage still require later live qualification.

[Retained native evidence](../../../investigations/evidence/native/dotvanisher-recovery-implementation-native-2026-09-13.json) includes the complete response assembly, typed acknowledgment, queue/reset/finish/update bodies and exact prologues. Ghidra acknowledgment annotation changes were saved successfully after one save attempt encountered another active transaction.

## Offline validation completed

- Built and linked the DLL successfully using the existing compile/link commands and existing dependency libraries. Only DotVanisher build outputs were rebuilt; shared dependency builds/configuration were not invoked. CMake source/dependency declarations were updated for subsequent normal builds.
- `DotVanisher/test_offline.bat` compiled with MSVC `/W4 /WX` and passed the standalone fake-native adapter scenarios. The executable neither loads the mod/UE4SS nor runs Soulcalibur; its fake image reserves data address space with one committed page, without executable code.
- Scenarios exercise both acknowledgment orderings, ready fast path, late route, duplicate/deadline handling, rejection, explicit cancellation/end, competing queue work, new request/session, native failure first, destruction inside update, route/session identity change, peer departure, original-thread enforcement, re-entry, callback reentrancy, malformed archive admission and atomic/non-atomic strong/weak reference release.
- `verify_native_layout.py` passed against the executable file: exact SHA/size, eleven detour prologues, two helper prologues, lookup/acquisition vtable slots and both native timer constants.
- DLL export inspection confirms `start_mod` and `uninstall_mod`. This is not a runtime load/ABI test.
- Hermes performed a bounded implementation review. Reentrant generation handling, ambiguous queue handling and thread affinity findings were corrected; no further concrete defect was identified in the supported single-thread, single-request path.

Built DLL: `E:\myMods\build_cmake_LessEqual421__Shipping__Win64\DotVanisher\DotVanisher.dll`.

SHA-256: `288B2AFB8538CBB60DBF4ABC2EAFBB8B194694AAF426FF8799A96C36E423E9C4`.

No live game test, process attachment, deployment, or release packaging was performed. This is a tested offline development candidate for the admission-ordering failure class, not a certified general loading fix.
