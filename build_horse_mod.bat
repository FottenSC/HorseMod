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
rem ============================================================================

setlocal

rem Set the build directory
set BUILD_DIR=build_cmake_LessEqual421__Shipping__Win64

rem Setup MSVC developer environment for Ninja and correct toolset resolution
call "E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat"

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
cmake -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=LessEqual421__Shipping__Win64
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
