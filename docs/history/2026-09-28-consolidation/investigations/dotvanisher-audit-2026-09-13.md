> Archived investigation/checkpoint. Its results and instructions apply to its recorded sources; see [current status](../../../rollback-status.md) for active work.

# DotVanisher code audit — 2026-09-13

Assessment (corrected after tracing list producers): high confidence that the timer intervention suppresses the specific native host battle-end watcher-retirement timeout during an ordinary first pending episode. Its observed producers are battle-result and battle-end-to-lobby handlers, not initial watch admission. The previous assessment of moderate confidence in end-to-end spectator recovery was too broad and is withdrawn. No defensible general recovery probability is available. See [the follow-up investigation](dotvanisher-general-fix-2026-09-13.md) for independent request deadlines, assignment/route failures, and the repair boundary.

Source reviewed: `DotVanisher/dllmain.cpp`, CMake integration, build/deploy/release scripts, and UE4SS mod removal. Native evidence was read directly through Ghidra MCP from the existing SoulcaliburVI.exe program. Hermes independently challenged the findings. No mod source was changed, deployed, or tested in a live online session.

## Native mechanism verified

The target at VA `0x142E613A0` is currently named `ProcessLuxorHostSysEventQueues` in Ghidra. DotVanisher's older name refers to the same address.

- Native ABI takes the state pointer in RCX and float delta in XMM1 and returns bool, matching the hook.
- `+0xB0` is the intrusive watch-list sentinel pointer; `+0xB8` is its 64-bit count; `+0xC0` is the float elapsed timer.
- At `0x142E61A04`, native code checks the count. Zero count reaches the timer reset at `0x142E61F3B`.
- At `0x142E61A12`, native code adds delta to the timer. At `0x142E61A1B`, it compares against the float at `0x143E8A474`. Its bytes are `00 00 F0 41`, i.e. 30.0f. `JBE` bypasses timeout cleanup; values strictly greater than 30 enter it.
- The timeout path queues watch-end callbacks, flushes transport, emits event opcode 10, clears the count and frees watch nodes. At `0x142E61EB8` onward, it resets links on the existing sentinel rather than replacing the sentinel pointer.

For finite positive deltas up to 120 seconds, the hook's `-delta` write and native `+delta` cancel. The hook forwards the original arguments and return value and writes only the watch timer. Deferred and periodic callback processing still execute. This supports narrow timeout suppression; it does not establish unchanged behavior for every downstream networking state.

The grace is 90 seconds of wall-clock suppression, followed by the native 30 seconds of accumulated tick time. Under steady ticking, continuously pending watchers therefore reach cleanup at approximately 120 seconds, not at 90 seconds. This agrees with a grace window before native cleanup is allowed, but should be stated plainly to users.

## Findings and limitations

### P2 — Grace ownership can outlive the actual pending episode

`dllmain.cpp:520` clears the epoch only when the pre-call sample sees zero count. Lines 540–542 otherwise distinguish episodes using host and sentinel addresses. The native cleanup retains the sentinel, and the hook performs no post-call observation or explicit session-lifecycle reset.

Consequently, if the list is cleared and refilled between pre-call samples, a new episode inherits the earlier episode's deadline. An already expired deadline means the next episode receives no fresh grace. New spectators joining a continuously nonempty queue also share its old deadline. This observation gap follows from the code; its frequency in real room/match transitions was not measured. Fixing it should use a verified watch/session lifecycle boundary, not assume sentinel identity is an episode identifier.

### P2 — Build can mask configuration failure

`build_dotvanisher.bat:39–40` invokes `cmake --build` without checking the configure command's exit status. Only the build's status is tested. If an earlier Ninja configuration remains usable after a failed configure, it can report success with stale settings; deploy and packaging scripts trust that result. Check configure failure before starting the build.

### Operational limitation — Uninstall requires process restart

The module is pinned with `GET_MODULE_HANDLE_EX_FLAG_PIN`, and `uninstall_mod` deletes only the UE4SS mod object. Its default destructor intentionally leaves the singleton hook active until process teardown. Disabling/reloading through UE4SS therefore does not remove the intervention. This is a deliberate in-flight-code protection, not an established crash defect; document restart-required disabling.

### Compatibility and evidence limits

- Protection runs on the host watch path. Installing only on a spectator does not alter the remote host's timer.
- Exact executable size, SHA-256 and target prologue checks reject unsupported builds or an existing target hook. This is a strong guard, but compatibility is intentionally restricted.
- The built DLL depends on UE4SS C++ exports and layout. Local export availability passed; compatibility with every manually installed UE4SS or the framework bundled by the release's shimloader dependency remains unverified.
- The current UE4SS.log contained no DotVanisher lines, and no dedicated DotVanisher tests were found in the searched test tooling. This is absence of retained validation in those locations, not proof the mod has never run.
- A finite delta above 150 seconds can exceed the native threshold despite the 120-second compensation cap on a newly admitted episode. This is an extreme edge case, not a normal-frame defect.

## Checks completed

- Built target `DotVanisher` successfully in the existing Shipping/Win64 CMake directory; no deployment or packaging.
- Installed EXE size: 71,737,344 bytes.
- Installed EXE SHA-256: `F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553`, matching the source guard.
- All 32 expected target-prologue bytes match both the installed EXE and Ghidra bytes.
- DLL exports `start_mod` and `uninstall_mod`.
- All 17 UE4SS named imports exist in the locally installed UE4SS.dll. This is an import-resolution check, not a complete C++ ABI or runtime load test.

The next useful runtime evidence is a bounded host/spectator comparison that distinguishes timeout-driven watch-end from other failure paths, followed by repeated watch attempts, overlapping joins, explicit cancel, room exit/re-entry and restart cleanup. A successful DLL build alone cannot establish those behaviors.
