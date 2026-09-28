# DotVanisher 1.1.0

This build adds bounded recovery when a **successful watch assignment arrives before its existing player's native route is registered**. It waits on the same connection attempt and resumes the original response handler when the route becomes available. It does not create routes, resend requests, synthesize acknowledgments, or extend the native request deadline.

The mod must run on the endpoint receiving the watch assignment. A room host who is spectating can be that endpoint. It does not patch unmodified remote clients, and it is not a verified fix for every loading-dots failure. BattleSync loading-data/completion failures and pre-assignment request timeouts are outside this change.

The older host battle-end retirement grace remains separate. Its list is populated at battle end; the original description treating it as general slow-load protection was too broad.

## Recovery lifetime

- Admission requires one watch request submitted to an empty native request queue, a ready connection, and the same thread observed executing that connection's state-update task.
- Acknowledgment is delivered to native code immediately, regardless of assignment deferral. Rejected requests cancel recovery.
- The deadline is measured from submission, capped at the native request's supplied timeout and 20 seconds. Duplicate deferred assignments cannot restart it.
- Only the decoded player ID and scalar peer/route/session identities are retained. No packet, native object, delegate, or shared reference survives a callback.
- Native failure/timeout processing runs before recovery on each owner update. Explicit cancel/end, reset, session finish/start, destruction and a replacement watch request invalidate the attempt.
- Unrelated queue activity stops further waiting but preserves the consumed assignment until the original owner update can return it to native handling. It is not silently discarded.
- A missing player resumes the native failure branch. A changed peer/route/session identity never receives the old assignment. Native calls are followed by generation checks to reject reentrant cancellation or replacement.
- Cross-thread recovery is unsupported. If the state-update task moves threads, the value-only assignment is retained for the original thread or a terminal lifecycle callback; it is never replayed on an unverified thread. If that thread does not return, this mechanism cannot guarantee recovery. No native allocation is held in that case.

The synthetic read archive used for replay is restricted to `142E6C540`, whose complete assembly accesses only mode `+0x14` and cursor `+0x18`, reads six bytes and does not release the archive. It is never passed to the packet dispatcher or archive destructor.

## Build and offline checks

`build_dotvanisher.bat` builds the DLL. `test_offline.bat` builds and runs a standalone executable with fake native objects/callbacks. It does not load the mod, UE4SS or Soulcalibur. It covers acknowledgment ordering, deadlines, duplicates, rejection, cancellation, re-entry, native failure, teardown, thread affinity, reentrant probes and both native shared-reference release modes.

`python DotVanisher/verify_native_layout.py <path-to-SoulcaliburVI.exe>` reads the executable file, validates its exact size/hash, all eleven detour prologues, both helper prologues, route-service vtable entries and timeout constants. It never accesses a live process.

The implementation uses the existing fixed SC6 binary identity. A hook/dependency mismatch disables recovery. Partial hook installation leaves any installed entries as pass-through; their trampolines remain allocated through process exit. **Restart is required to disable/remove the mod**, including after UE4SS mod-object removal.

Version `1.1.0` is packaged for Thunderstore with offline validation only. No live test or game deployment has been performed. Successful compilation and mock tests do not establish online compatibility, thread behavior or that the user's reported incident reaches this branch.
