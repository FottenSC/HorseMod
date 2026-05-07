@echo off
rem ============================================================================
rem build_horse_mod.bat
rem
rem Pure build script — produces HorseMod.dll and exits.  Does NOT copy the
rem DLL anywhere.  For a build-and-deploy-to-game workflow run
rem build_and_deploy.bat instead.
rem
rem Output:  build_cmake_LessEqual421__Shipping__Win64\HorseMod\HorseMod.dll
rem
rem Why split build from deploy?
rem ---------------------------
rem * The release scripts (build_release_github.bat / build_release_thunder-
rem   store.bat) only need the artefact in the build dir.  The previous
rem   combined script tried to copy into the game dir as a side effect of
rem   every build, which spammed "file in use" warnings whenever the game
rem   was running and added a redundant copy to the release pipeline.
rem * Tooling (CI, scheduled rebuilds, sanity-only "does it still compile?"
rem   runs) wants the build but not the deploy.
rem
rem Callers:
rem   build_and_deploy.bat        (interactive dev: build + copy to game)
rem   run_game.bat                (relaunch helper: build + copy + relaunch)
rem   build_release_github.bat    (release: build + zip into dist/)
rem   build_release_thunderstore.bat   (release: build + zip into dist/)
rem
rem Dev-iteration speed-ups (auto-applied; harmless if missing):
rem   * sccache as compiler launcher (only if sccache.exe is on PATH).
rem     Free win on rebuild-after-clean.  Install: `winget install sccache`
rem     or `cargo install sccache`.  Skipped silently if not found.
rem
rem MYMODS_FAST_DEV (LTO opt-out) is OPT-IN only — see warning below.
rem ============================================================================
rem
rem !! IMPORTANT — MYMODS_FAST_DEV / LTO-off is unsafe with this deploy flow !!
rem
rem We DO NOT deploy our locally-built UE4SS.dll to the game.  build_and_
rem deploy.bat copies only HorseMod.dll -> <game>\...\HorseLab\dlls\main.dll.
rem The game keeps using its installed UE4SS.dll from the official release
rem (LTO ON).  HorseMod.dll, however, was just LINKED against our build-tree
rem UE4SS.lib.
rem
rem When LTO state differs between the UE4SS that HorseMod LINKED against
rem (LTO off if MYMODS_FAST_DEV=ON) and the UE4SS the game LOADS at runtime
rem (LTO on, from the release), MSVC's inline-vs-out-of-line decisions for
rem templated/inlined methods in UE4SS headers diverge.  HorseMod ends up
rem with __declspec(dllimport) entries for symbols the deployed DLL never
rem exports, and the game CRASHES on boot at LoadLibrary("main.dll") time.
rem
rem To safely use MYMODS_FAST_DEV=1 you'd also need to deploy
rem build_cmake_*/LessEqual421__Shipping__Win64/bin/UE4SS.dll into the game's
rem ue4ss/ folder so it matches what HorseMod was linked against.  We don't
rem do that automatically because it would clobber the user's UE4SS install.
rem
rem TL;DR: leave MYMODS_FAST_DEV unset.  The other speedups (the new
rem FETCHCONTENT_BASE_DIR + FETCHCONTENT_UPDATES_DISCONNECTED) already
rem cut configure from ~50s to ~2s without any ABI risk.
rem ============================================================================

setlocal enabledelayedexpansion

rem Set the build directory
set BUILD_DIR=build_cmake_LessEqual421__Shipping__Win64

rem Setup MSVC developer environment for Ninja and correct toolset resolution
call "E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat"

rem ---- Optional: sccache compile-output cache ------------------------------
rem  CMake picks up CMAKE_<LANG>_COMPILER_LAUNCHER from the environment at
rem  configure time.  We set it via -D so it sticks in the cache instead of
rem  silently dropping when CMake is re-run without the env var.
rem
rem  Currently OPT-IN ONLY (set MYMODS_USE_SCCACHE=1) because the upstream
rem  msvc-compatible.cmake bakes /Zi into Shipping_FLAGS (separate PDB file).
rem  Sccache wrapping cl.exe breaks the /FS mspdbsrv coordination protocol —
rem  result is fatal C1041 "cannot open program database" errors during
rem  parallel compiles.  To enable sccache, /Zi must be replaced with /Z7
rem  (embedded debug info, no shared PDB).  Until that conversion is in
rem  place, leave MYMODS_USE_SCCACHE unset.
set SCCACHE_ARGS=
if defined MYMODS_USE_SCCACHE (
    where sccache >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        echo [build] MYMODS_USE_SCCACHE set + sccache on PATH — enabling launcher
        echo [build]   WARNING: requires /Z7 instead of /Zi; will fail with C1041 otherwise
        set SCCACHE_ARGS=-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache
    ) else (
        echo [build] MYMODS_USE_SCCACHE set but sccache not on PATH — skipping
    )
) else (
    rem sccache disabled by default — see comment above
    rem set MYMODS_USE_SCCACHE=1 to enable (after /Zi -^> /Z7 conversion)
)

rem ---- LTO control ---------------------------------------------------------
rem  Default: LTO ON (matches the release UE4SS the game actually runs).
rem  Opt-in to LTO-off only if you ALSO deploy your locally-built UE4SS.dll
rem  alongside HorseMod.dll — see the big warning at the top of this file.
set FAST_DEV_ARGS=-DMYMODS_FAST_DEV=OFF
if defined MYMODS_FAST_DEV_UNSAFE (
    echo [build] MYMODS_FAST_DEV_UNSAFE set — disabling LTO on UE4SS
    echo [build]   You MUST deploy build_cmake_*/.../bin/UE4SS.dll to the game
    echo [build]   manually, or HorseMod will crash at LoadLibrary time.
    set FAST_DEV_ARGS=-DMYMODS_FAST_DEV=ON
)

rem Generate and Build the project with Ninja.
rem
rem --target HorseMod: build only OUR mod + its link-time prerequisites (UE4SS,
rem   the third-party deps UE4SS uses, patternsleuth's Rust crate).  Without
rem   this flag, ninja would also rebuild UVTD.exe, KismetDebuggerMod.dll,
rem   EventViewerMod.dll and the proxy DLL — none of which we care about and
rem   which collectively cost ~minutes per clean build.
rem
rem --parallel: use all logical cores.  Ninja defaults are usually good, but
rem   `NUMBER_OF_PROCESSORS` forces the ceiling explicitly (helps if a tool
rem   in the chain has its own lower default).
cmake -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=LessEqual421__Shipping__Win64 %FAST_DEV_ARGS% %SCCACHE_ARGS%
cmake --build "%BUILD_DIR%" --target HorseMod --parallel %NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully!
echo.
echo Your mod DLL is located at:
echo %CD%\%BUILD_DIR%\HorseMod\HorseMod.dll
echo.
exit /b 0
