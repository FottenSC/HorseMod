@echo off
rem ============================================================================
rem build_and_deploy.bat
rem
rem Build HorseMod and copy the resulting DLL into the live SoulCalibur VI
rem mods directory so the game picks it up on its next launch.
rem
rem This is the day-to-day dev iteration entry point — edit code, run this,
rem alt-tab into the game (or relaunch via run_game.bat).
rem
rem Layered design:
rem   build_horse_mod.bat   <- pure build, produces HorseMod.dll
rem   build_and_deploy.bat  <- this file: calls the build, then copies
rem   run_game.bat          <- kill game, build_and_deploy, relaunch
rem
rem ----------------------------------------------------------------------------
rem CONFIGURE YOUR GAME PATH HERE if your install isn't on E:\SteamLibrary.
rem GAME_DIR is the folder containing SoulcaliburVI.exe.  MOD_NAME is the
rem subdirectory under ue4ss\Mods\ that the game's UE4SS install scans.
rem ============================================================================

setlocal enabledelayedexpansion

set REPO_ROOT=%~dp0
if not "%REPO_ROOT:~-1%"=="\" set REPO_ROOT=%REPO_ROOT%\

set BUILD_DIR=build_cmake_LessEqual421__Shipping__Win64
set BUILT_DLL=%REPO_ROOT%%BUILD_DIR%\HorseMod\HorseMod.dll

set GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64
set MOD_NAME=HorseLab
set DEPLOY_DIR=%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls

rem ---- Build (delegate to the pure build script) ---------------------------
call "%REPO_ROOT%build_horse_mod.bat"
if !ERRORLEVEL! NEQ 0 (
    echo [build_and_deploy] build failed (exit !ERRORLEVEL!^)
    exit /b !ERRORLEVEL!
)

if not exist "%BUILT_DLL%" (
    echo [build_and_deploy] expected DLL not found at %BUILT_DLL%
    exit /b 1
)

rem ---- Deploy --------------------------------------------------------------
if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"

echo Copying main.dll to game directory...
copy /Y "%BUILT_DLL%" "%DEPLOY_DIR%\main.dll"
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo [build_and_deploy] copy failed — is SoulcaliburVI.exe still running?
    echo [build_and_deploy] close the game and re-run, or use run_game.bat
    echo [build_and_deploy] which kills the game first.
    exit /b !ERRORLEVEL!
)

echo Copied successfully to %DEPLOY_DIR%\main.dll
echo.
exit /b 0
