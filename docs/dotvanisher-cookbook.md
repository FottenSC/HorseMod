# DotVanisher Cookbook: How The Spectator Timeout Patch Works

DotVanisher is a standalone UE4SS C++ mod for Soulcalibur VI. It exists to fix
one narrow failure mode: spectators can be kicked out while a match is still
loading, even though the two players continue into the match normally.

The mod does not include HorseMod's UI, replay tools, hitbox tools, or runtime
feature set. The only shipped runtime behavior is a detour around the host's
watch-queue timeout tick.

Source entry points:

- Runtime implementation: `DotVanisher/dllmain.cpp`
- Build target: `DotVanisher/CMakeLists.txt`
- Local build script: `DotVanisher/build_dotvanisher.bat`
- Local deploy script: `DotVanisher/build_and_deploy_dotvanisher.bat`
- Thunderstore package script: `DotVanisher/build_release_thunderstore_dotvanisher.bat`

## The Problem It Targets

Soulcalibur VI has a host-side watch/spectator queue. While a match is loading,
spectators may sit in a pending-watch state. The vanilla game tracks that pending
state with a timeout timer. If the timer expires before the pending spectators
finish loading into the watch session, the host forces the watch flow to end for
those spectators.

That timeout can be too aggressive on slow storage or heavier stages. From the
players' point of view, the match is fine. From the spectators' point of view,
they get dropped because the host concluded that the pending watch queue took too
long.

DotVanisher does not remove the timeout permanently. It gives the pending watch
queue a bounded 90 second grace window. During that grace window, it keeps the
vanilla timeout timer from accumulating enough time to trip the cleanup path.
After the grace window expires, the mod stops interfering and lets the vanilla
cleanup logic run.

## The Ingredients

The implementation is intentionally small:

- UE4SS loads the DLL and calls the exported `start_mod()` function.
- `DotVanisher::Mod` registers as a `RC::CppUserModBase`.
- `on_unreal_init()` installs a single PolyHook 2 x64 detour.
- The detour targets the recovered SC6 function at
  `SoulcaliburVI.exe + 0x2E613A0`, named in the comments as
  `HandleHostTickWatchEventQueues`.
- The detour reads three recovered fields from the host system state:
  - `pHostSysState + 0xB0`: pending watch spectator list pointer
  - `pHostSysState + 0xB8`: pending watch spectator count
  - `pHostSysState + 0xC0`: host watch timeout timer
- Before forwarding to the original function, it may rewrite the timeout timer.

The build target links against `UE4SS` and `bcrypt`. `UE4SS` provides the C++
mod interface and logging surface. `bcrypt` is used only for SHA-256 binary
verification.

## Load And Install Flow

UE4SS finds the mod DLL through the normal C++ mod layout:

```text
<game>/Binaries/Win64/ue4ss/Mods/DotVanisher/
  enabled.txt
  dlls/
    main.dll
```

At load time, UE4SS calls:

```cpp
extern "C" RC::CppUserModBase* start_mod()
```

DotVanisher returns a new `DotVanisher::Mod`. Once Unreal initialization reaches
the UE4SS callback, `Mod::on_unreal_init()` calls:

```cpp
WatchTimeoutHook::instance().install();
```

Installation does four important things:

1. Resolves the loaded `SoulcaliburVI.exe` module with `GetModuleHandleW`.
2. Computes the hook address as `image_base + 0x2E613A0`.
3. Verifies that the game binary and target prologue match the expected build.
4. Installs a `PLH::x64Detour` from the vanilla function to
   `WatchTimeoutHook::detour`.

The hook keeps the trampoline address returned by PolyHook. Every detoured call
still ends by calling the original function through that trampoline.

## Binary Verification

DotVanisher is patching a raw executable address, so the install path is
deliberately conservative. Before hooking, it verifies three things:

- The executable file size is exactly `71,737,344` bytes.
- The executable SHA-256 equals the hard-coded expected hash.
- The first 32 bytes at the target function match the expected prologue.

If any of those checks fail, the hook is disabled. This is important because
`0x2E613A0` is not a symbolic import or reflected Unreal function. It is an RVA
recovered from the current SC6 binary. If the game executable changes, or if
another hook has already modified the same prologue, the old address or
instruction assumptions may no longer be safe.

The mod logs explicit errors for unsupported file size, unsupported SHA-256,
failure to read the target bytes, or prologue mismatch. In all of those cases,
DotVanisher fails closed by leaving the vanilla game behavior untouched.

## The Detour Recipe

The detoured function has this recovered shape:

```cpp
using OriginalFn = bool(__fastcall*)(uint8_t* pHostSysState, float flDeltaSeconds);
```

The detour itself is short:

1. Call `maybe_suppress_timeout(pHostSysState, flDeltaSeconds)`.
2. Resolve the original function from the PolyHook trampoline.
3. Call the original function with the same arguments.
4. Return the original function's result.

DotVanisher therefore does not replace the watch-queue tick. It only adjusts one
field immediately before the vanilla function continues.

The key behavior lives in `maybe_suppress_timeout()`.

## Reading The Watch State

Each tick, DotVanisher tries to read:

```text
watch_list   = *(uintptr_t*)(pHostSysState + 0xB0)
watch_count  = *(int64_t*)(pHostSysState + 0xB8)
timer_before = *(float*)(pHostSysState + 0xC0)
```

All reads use structured-exception-handling wrappers:

- `read_uintptr_seh`
- `read_i64_seh`
- `read_float_seh`
- `read_bytes_seh`

The write uses `write_float_seh`.

Those wrappers matter because this is injected code reading recovered native
memory. If a pointer is stale, the expected layout is wrong, or the host state is
temporarily invalid, the mod should not crash the process. A failed read or write
clears the current epoch state, logs a warning once, and leaves the vanilla
timeout untouched.

## When The Mod Refuses To Interfere

DotVanisher only touches the timer when the watch state looks like a real pending
spectator queue.

It does nothing when:

- `pHostSysState` is null.
- The recovered fields cannot be read safely.
- `watch_count` is `0`.
- `watch_count` is negative.
- `watch_count` is greater than `16`.
- The 90 second grace window has already expired.

The `16` spectator limit is a sanity bound, not a gameplay feature. If the field
contains a value outside that range, DotVanisher assumes either the offset is
wrong or the state is not the expected pending-watch list. It then lets the
vanilla function run normally.

## Epochs: How DotVanisher Knows A Wait Is New

The mod groups pending-watch time into an "epoch". An epoch starts when there is
a non-zero pending watch count and either:

- there was no previous epoch,
- the `pHostSysState` pointer changed, or
- the pending `watch_list` pointer changed.

The epoch key is therefore:

```text
(host_state_pointer, watch_list_pointer)
```

When a new epoch starts, DotVanisher records:

- `m_epoch_start_ms`: `GetTickCount64()` at the start of the wait
- `m_epoch_host_state`: the host state pointer
- `m_epoch_watch_list`: the pending spectator list pointer
- `m_epoch_last_count`: the latest pending spectator count
- `m_epoch_max_delta`: the largest positive frame delta seen
- `m_epoch_max_timer_before`: the largest positive timer value observed before
  the mod rewrote it

When the pending count drops to zero, the epoch ends. The mod logs an epoch
summary and clears the state. Null state, read failure, out-of-range counts, and
uninstall also clear the epoch.

This is intentionally based on the state/list identity rather than only on
`watch_count`. A count changing from 2 to 1 is still the same pending-watch wait.
A different list pointer means the host is likely processing a different watch
queue instance.

## The 90 Second Grace Window

The constant is:

```cpp
constexpr uint64_t kGraceMs = 90'000;
```

For the first 90 seconds of an epoch, DotVanisher writes a compensated value into
the timeout timer:

```cpp
timer_write = -min(valid_delta_seconds, 120.0f)
```

Invalid, non-finite, or negative frame deltas are treated as zero.

The practical effect is that the vanilla tick sees a timer value that has been
offset just before it runs. The compensation is shaped like `-flDeltaSeconds`,
which strongly suggests the original function adds the current frame delta to the
timer during its own tick. By writing the negative delta first, DotVanisher keeps
the vanilla accumulation near zero while spectators are still pending.

The `120.0f` cap is a defensive bound. If the game ever reports a giant positive
delta, DotVanisher will not write an arbitrarily large negative float into the
host state.

Once `elapsed_ms >= 90,000`, DotVanisher stops writing the timer. It logs one
"grace expired" warning for that epoch and lets the vanilla function continue
normally. That preserves the original game's ability to clean up a genuinely
stuck watch queue.

## What It Does Not Patch

DotVanisher does not:

- Change player match loading.
- Change explicit spectator leave, cancel, or end handling.
- Remove the host watch timeout forever.
- Add an ImGui menu or settings UI.
- Depend on HorseMod at runtime.
- Patch broad online matchmaking behavior.
- Replace the original watch-queue function.

The mod only offsets the host watch timeout timer while pending spectators exist,
and only for a bounded grace period per pending-watch epoch.

## Logging And Diagnostics

The mod logs through `RC::Output::send`.

Expected healthy install messages include:

- target binary verified
- spectator timeout hook installed
- spectator timeout epoch started
- spectator timeout epoch ended

Important warning/error messages include:

- unsupported executable size
- unsupported executable SHA-256
- target prologue mismatch
- failed to read watch state fields
- unreasonable watch count
- failed to reset watch timeout timer
- spectator timeout grace expired

The epoch logs are useful when debugging spectator load behavior. They include
the state pointer, watch-list pointer, count, elapsed time, maximum observed
delta, and maximum observed timer value. If spectators still disconnect, the
first question is whether the epoch started at all. If it did start, check
whether it ended normally, hit a read/write failure, or expired after 90 seconds.

## Failure Behavior

DotVanisher is designed to fail closed:

- If the binary is not the expected SC6 build, it does not hook.
- If the prologue has already been modified, it does not hook.
- If the trampoline is missing, the detour returns `false` rather than guessing.
- If recovered state reads fail, it leaves the timer alone.
- If the pending count looks impossible, it leaves the timer alone.
- If writing the timer fails, it clears state and leaves future vanilla behavior
  intact.

That means the worst expected behavior is "the original spectator timeout still
happens", not "the match flow is replaced by custom logic".

## Build And Package Flow

`DotVanisher/CMakeLists.txt` declares a shared library named `DotVanisher`:

```cmake
add_library(DotVanisher SHARED dllmain.cpp)
target_link_libraries(DotVanisher PUBLIC UE4SS bcrypt)
```

The root `E:\myMods\CMakeLists.txt` adds `DotVanisher` as a subdirectory, so the
standalone build script configures the larger `E:\myMods` CMake tree and builds
only the `DotVanisher` target:

```bat
DotVanisher\build_dotvanisher.bat
```

The resulting DLL is expected at:

```text
E:\myMods\build_cmake_LessEqual421__Shipping__Win64\DotVanisher\DotVanisher.dll
```

For local testing, the deploy script copies that DLL to:

```text
E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\DotVanisher\dlls\main.dll
```

The Thunderstore script builds the same DLL, stages this layout:

```text
manifest.json
README.md
icon.png
mod/
  enabled.txt
  dlls/
    main.dll
```

and emits:

```text
E:\myMods\dist\DotVanisher-thunderstore-<VERSION>.zip
```

The package depends on `Thunderstore-unreal_shimloader-1.1.7`, which provides
the UE4SS loading path for mod-manager installs.

## Quick Mental Model

Think of DotVanisher as a small shim around one timer field:

```text
UE4SS loads DotVanisher
  -> DotVanisher verifies the exact SC6 binary
  -> PolyHook detours HandleHostTickWatchEventQueues
  -> each host watch tick reads pending spectator state
  -> if pending spectators exist, start or continue an epoch
  -> for up to 90 seconds, write the timeout timer back near zero
  -> call the original SC6 watch tick every time
  -> when pending spectators clear, log and reset
  -> if the wait exceeds 90 seconds, stop suppressing vanilla cleanup
```

The important part is that the original function still runs. DotVanisher's
entire intervention is a bounded pre-call adjustment to the timeout timer.
