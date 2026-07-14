@echo off
rem ============================================================================
rem build_and_deploy_dotvanisher.bat
rem
rem Build DotVanisher and copy it into the live Soulcalibur VI UE4SS mod folder.
rem ============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
if not "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR%\"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"

set "BUILD_DIR=%REPO_ROOT%\build_cmake_LessEqual421__Shipping__Win64"
set "BUILT_DLL=%BUILD_DIR%\DotVanisher\DotVanisher.dll"

set GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64
set MOD_NAME=DotVanisher
set MOD_DIR=%GAME_DIR%\ue4ss\Mods\%MOD_NAME%
set DEPLOY_DIR=%MOD_DIR%\dlls

call "%SCRIPT_DIR%build_dotvanisher.bat"
if !ERRORLEVEL! NEQ 0 (
    echo [build_and_deploy_dotvanisher] build failed (exit !ERRORLEVEL!^)
    exit /b !ERRORLEVEL!
)

if not exist "%BUILT_DLL%" (
    echo [build_and_deploy_dotvanisher] expected DLL not found at %BUILT_DLL%
    exit /b 1
)

if not exist "%MOD_DIR%" (
    mkdir "%MOD_DIR%"
    if !ERRORLEVEL! NEQ 0 (
        echo [build_and_deploy_dotvanisher] failed to create mod directory: %MOD_DIR%
        exit /b !ERRORLEVEL!
    )
)

if not exist "%DEPLOY_DIR%" (
    mkdir "%DEPLOY_DIR%"
    if !ERRORLEVEL! NEQ 0 (
        echo [build_and_deploy_dotvanisher] failed to create dll directory: %DEPLOY_DIR%
        exit /b !ERRORLEVEL!
    )
)

if not exist "%MOD_DIR%\enabled.txt" (
    type nul > "%MOD_DIR%\enabled.txt"
    if !ERRORLEVEL! NEQ 0 (
        echo [build_and_deploy_dotvanisher] failed to create enabled.txt in %MOD_DIR%
        exit /b !ERRORLEVEL!
    )
)

echo Copying main.dll to game directory...
copy /Y "%BUILT_DLL%" "%DEPLOY_DIR%\main.dll"
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo [build_and_deploy_dotvanisher] copy failed - is SoulcaliburVI.exe still running?
    echo [build_and_deploy_dotvanisher] close the game and re-run.
    exit /b !ERRORLEVEL!
)

echo Copied successfully to %DEPLOY_DIR%\main.dll
echo.
echo [build_and_deploy_dotvanisher] finished at %DATE% %TIME%
exit /b 0
