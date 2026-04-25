@echo off
rem ============================================================================
rem build_release_github.bat
rem
rem Produces a GitHub-Releases-ready zip of HorseMod for SoulCalibur VI.
rem
rem Output: dist\HorseMod-v<VERSION>.zip
rem
rem The zip layout is "drop into ue4ss\Mods\":
rem
rem   HorseLab\
rem     enabled.txt
rem     dlls\
rem       main.dll
rem   README.md
rem
rem End user extracts the zip into
rem   <Steam>\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\ue4ss\Mods\
rem and the layout merges into the right place.
rem
rem Version comes from the VERSION file at the repo root (single-line semver,
rem e.g. "0.10.0").  Edit that file to bump the release version.
rem ============================================================================

setlocal enabledelayedexpansion

set REPO_ROOT=%~dp0
if not "%REPO_ROOT:~-1%"=="\" set REPO_ROOT=%REPO_ROOT%\

set BUILD_DIR=build_cmake_LessEqual421__Shipping__Win64
set BUILT_DLL=%REPO_ROOT%%BUILD_DIR%\HorseMod\HorseMod.dll
set DIST_DIR=%REPO_ROOT%dist
set STAGE_DIR=%DIST_DIR%\stage_github

rem ---- Read VERSION file ----------------------------------------------------
if not exist "%REPO_ROOT%VERSION" (
    echo [release.github] VERSION file missing at %REPO_ROOT%VERSION
    echo [release.github] create it with a single line, e.g. ^"0.10.0^"
    exit /b 1
)
set /p VERSION=<"%REPO_ROOT%VERSION"
if "%VERSION%"=="" (
    echo [release.github] VERSION file is empty
    exit /b 1
)
echo [release.github] version: %VERSION%

rem ---- Build the mod (delegates to the existing build script) --------------
echo [release.github] building HorseMod (Shipping / Win64) ...
call "%REPO_ROOT%build_horse_mod.bat"
if !ERRORLEVEL! NEQ 0 (
    echo [release.github] build failed (exit !ERRORLEVEL!^)
    exit /b !ERRORLEVEL!
)

if not exist "%BUILT_DLL%" (
    echo [release.github] expected DLL not found at %BUILT_DLL%
    exit /b 1
)

rem ---- Stage release contents ----------------------------------------------
if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
mkdir "%STAGE_DIR%\HorseLab\dlls"

copy /Y "%BUILT_DLL%" "%STAGE_DIR%\HorseLab\dlls\main.dll" >nul
if !ERRORLEVEL! NEQ 0 (
    echo [release.github] failed to copy DLL into stage
    exit /b 1
)

rem enabled.txt — UE4SS reads it to decide whether to load the mod.  Empty
rem file is fine; the rename to "enabled.txt.DISABLED" is the documented
rem off-switch.
type nul > "%STAGE_DIR%\HorseLab\enabled.txt"

rem README.md (release-facing instructions)
copy /Y "%REPO_ROOT%release_resources\README.md" "%STAGE_DIR%\README.md" >nul

rem ---- Zip via PowerShell --------------------------------------------------
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
set OUTPUT=%DIST_DIR%\HorseMod-v%VERSION%.zip
if exist "%OUTPUT%" del "%OUTPUT%"

powershell -NoProfile -Command ^
    "Compress-Archive -Path '%STAGE_DIR%\*' -DestinationPath '%OUTPUT%' -Force"
if !ERRORLEVEL! NEQ 0 (
    echo [release.github] Compress-Archive failed
    exit /b !ERRORLEVEL!
)

rem Quick sanity report — list what's in the zip and its size on disk.
rem Single-line PowerShell so cmd's `^` line-continuation behaviour
rem doesn't smuggle stray carets into the command string.
echo.
echo [release.github] OK: %OUTPUT%
for %%I in ("%OUTPUT%") do echo [release.github] size: %%~zI bytes
echo [release.github] contents:
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::OpenRead('%OUTPUT%').Entries | ForEach-Object { '  ' + $_.FullName }"

rem ---- Cleanup staging dir (zip is the deliverable) ------------------------
rmdir /S /Q "%STAGE_DIR%"

echo.
echo [release.github] done.
exit /b 0
