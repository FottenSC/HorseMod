@echo off
rem ============================================================================
rem build_release_thunderstore.bat
rem
rem Produces a Thunderstore-format zip of HorseMod for SoulCalibur VI.
rem
rem Output: dist\HorseMod-thunderstore-<VERSION>.zip
rem
rem Thunderstore expects a flat zip with these files at the root:
rem
rem   manifest.json
rem   README.md
rem   icon.png   (256x256 — see release_resources\icon.png)
rem   <mod payload, layout up to the community>
rem
rem Mod payload (matches the GitHub release layout so users on either path
rem get the same on-disk shape after install):
rem
rem   HorseLab\
rem     enabled.txt
rem     dlls\
rem       main.dll
rem
rem Version comes from the VERSION file at the repo root.  manifest.json's
rem version_number field is generated from VERSION at build time.
rem
rem Icon: drop a 256x256 PNG into release_resources\icon.png BEFORE running
rem this script.  If the file is missing, the script aborts with an error
rem (Thunderstore rejects packages without an icon, so failing fast is
rem better than producing an unuploadable zip).
rem ============================================================================

setlocal enabledelayedexpansion

set REPO_ROOT=%~dp0
if not "%REPO_ROOT:~-1%"=="\" set REPO_ROOT=%REPO_ROOT%\

set BUILD_DIR=build_cmake_LessEqual421__Shipping__Win64
set BUILT_DLL=%REPO_ROOT%%BUILD_DIR%\HorseMod\HorseMod.dll
set DIST_DIR=%REPO_ROOT%dist
set STAGE_DIR=%DIST_DIR%\stage_thunderstore
set ICON_SRC=%REPO_ROOT%release_resources\icon.png

rem ---- Read VERSION file ----------------------------------------------------
if not exist "%REPO_ROOT%VERSION" (
    echo [release.thunderstore] VERSION file missing at %REPO_ROOT%VERSION
    echo [release.thunderstore] create it with a single line, e.g. ^"0.10.0^"
    exit /b 1
)
set /p VERSION=<"%REPO_ROOT%VERSION"
if "%VERSION%"=="" (
    echo [release.thunderstore] VERSION file is empty
    exit /b 1
)
echo [release.thunderstore] version: %VERSION%

rem ---- Icon presence check (Thunderstore requires it) ----------------------
if not exist "%ICON_SRC%" (
    echo [release.thunderstore] missing icon: %ICON_SRC%
    echo [release.thunderstore] place a 256x256 PNG at that path and re-run.
    echo [release.thunderstore] Thunderstore rejects packages without an icon.
    exit /b 1
)

rem ---- Build the mod -------------------------------------------------------
echo [release.thunderstore] building HorseMod (Shipping / Win64) ...
call "%REPO_ROOT%build_horse_mod.bat"
if !ERRORLEVEL! NEQ 0 (
    echo [release.thunderstore] build failed (exit %ERRORLEVEL%^)
    exit /b !ERRORLEVEL!
)
if not exist "%BUILT_DLL%" (
    echo [release.thunderstore] expected DLL not found at %BUILT_DLL%
    exit /b 1
)

rem ---- Stage Thunderstore zip layout ---------------------------------------
if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
mkdir "%STAGE_DIR%\HorseLab\dlls"

rem Mod payload (mirror of the GitHub release content)
copy /Y "%BUILT_DLL%" "%STAGE_DIR%\HorseLab\dlls\main.dll" >nul
if !ERRORLEVEL! NEQ 0 (
    echo [release.thunderstore] failed to copy DLL into stage
    exit /b 1
)
type nul > "%STAGE_DIR%\HorseLab\enabled.txt"

rem README.md and icon.png at the zip root (Thunderstore convention)
copy /Y "%REPO_ROOT%release_resources\README.md" "%STAGE_DIR%\README.md" >nul
copy /Y "%ICON_SRC%"                              "%STAGE_DIR%\icon.png"  >nul

rem ---- Generate manifest.json ----------------------------------------------
rem Thunderstore manifest fields:
rem   name            - lowercase package name, ASCII + underscore only
rem   version_number  - SemVer (must match the eventual upload's filename)
rem   website_url     - source / homepage
rem   description     - short blurb (max 250 chars)
rem   dependencies    - other Thunderstore packages this mod requires.
rem                     We depend on UE4SS being installed but Thunderstore
rem                     doesn't currently host UE4SS as a managed package
rem                     for SC6, so we leave this empty and explain in
rem                     README that UE4SS is a prerequisite.
set MANIFEST=%STAGE_DIR%\manifest.json
> "%MANIFEST%" echo {
>>"%MANIFEST%" echo     "name": "HorseMod",
>>"%MANIFEST%" echo     "version_number": "%VERSION%",
>>"%MANIFEST%" echo     "website_url": "https://github.com/FottenSC/HorseMod",
>>"%MANIFEST%" echo     "description": "SoulCalibur VI training overlay: hitboxes, hurtboxes, free-fly camera, slow-motion, reset-position override.",
>>"%MANIFEST%" echo     "dependencies": []
>>"%MANIFEST%" echo }

rem ---- Zip via PowerShell --------------------------------------------------
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
set OUTPUT=%DIST_DIR%\HorseMod-thunderstore-%VERSION%.zip
if exist "%OUTPUT%" del "%OUTPUT%"

powershell -NoProfile -Command ^
    "Compress-Archive -Path '%STAGE_DIR%\*' -DestinationPath '%OUTPUT%' -Force"
if !ERRORLEVEL! NEQ 0 (
    echo [release.thunderstore] Compress-Archive failed
    exit /b !ERRORLEVEL!
)

rem Quick sanity report (single-line PowerShell — see github script for
rem the explanation of why we don't use cmd `^` line continuation here).
echo.
echo [release.thunderstore] OK: %OUTPUT%
for %%I in ("%OUTPUT%") do echo [release.thunderstore] size: %%~zI bytes
echo [release.thunderstore] contents:
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::OpenRead('%OUTPUT%').Entries | ForEach-Object { '  ' + $_.FullName }"

rem ---- Cleanup staging dir (zip is the deliverable) ------------------------
rmdir /S /Q "%STAGE_DIR%"

echo.
echo [release.thunderstore] done.
echo [release.thunderstore] upload via https://thunderstore.io/c/^<community^>/upload/
exit /b 0
