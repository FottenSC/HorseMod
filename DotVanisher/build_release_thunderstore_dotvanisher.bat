@echo off
rem ============================================================================
rem build_release_thunderstore_dotvanisher.bat
rem
rem Produces a Thunderstore-format zip for the standalone DotVanisher mod.
rem
rem Output: ..\dist\DotVanisher-thunderstore-<VERSION>.zip
rem
rem Default mode only builds the local Thunderstore-format zip. It does not
rem upload anything. Pass --publish explicitly to publish to Thunderstore.
rem
rem Thunderstore / SC6 shimloader layout:
rem   <zip>\manifest.json
rem   <zip>\README.md
rem   <zip>\icon.png
rem   <zip>\mod\enabled.txt
rem   <zip>\mod\dlls\main.dll
rem ============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_NAME=%~nx0"
set "SCRIPT_DIR=%~dp0"
if not "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR%\"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"

set "PUBLISH_TO_THUNDERSTORE=0"
if /I "%THUNDERSTORE_PUBLISH%"=="1" set "PUBLISH_TO_THUNDERSTORE=1"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--publish" (
    set "PUBLISH_TO_THUNDERSTORE=1"
    shift
    goto parse_args
)
if /I "%~1"=="/publish" (
    set "PUBLISH_TO_THUNDERSTORE=1"
    shift
    goto parse_args
)
if /I "%~1"=="--no-publish" (
    set "PUBLISH_TO_THUNDERSTORE=0"
    shift
    goto parse_args
)
if /I "%~1"=="/no-publish" (
    set "PUBLISH_TO_THUNDERSTORE=0"
    shift
    goto parse_args
)
if /I "%~1"=="--help" (
    echo Usage: %SCRIPT_NAME% [--publish^|--no-publish]
    echo.
    echo Default: build the Thunderstore zip only.
    echo --publish: also upload to Thunderstore after packaging.
    exit /b 0
)
echo [dotvanisher.thunderstore] unknown argument: %~1
echo [dotvanisher.thunderstore] usage: %SCRIPT_NAME% [--publish^|--no-publish]
exit /b 2

:args_done

set "BUILD_DIR=%REPO_ROOT%\build_cmake_LessEqual421__Shipping__Win64"
set "BUILT_DLL=%BUILD_DIR%\DotVanisher\DotVanisher.dll"
set "DIST_DIR=%REPO_ROOT%\dist"
set "STAGE_DIR=%DIST_DIR%\stage_dotvanisher_thunderstore"
set "VERSION_FILE=%SCRIPT_DIR%VERSION"
set "README_SRC=%SCRIPT_DIR%release_resources\README.md"
set "ICON_SRC=%SCRIPT_DIR%release_resources\icon.png"
if not exist "%ICON_SRC%" set "ICON_SRC=%REPO_ROOT%\release_resources\icon.png"
set "PUBLISH_SCRIPT=%REPO_ROOT%\tools\publish_thunderstore.ps1"
set "THUNDERSTORE_ENV=%REPO_ROOT%\.env"
set "THUNDERSTORE_REPOSITORY=https://thunderstore.io"
set "THUNDERSTORE_NAMESPACE=Fotten"
set "THUNDERSTORE_PACKAGE=DotVanisher"
set "THUNDERSTORE_COMMUNITY=soulcalibur-vi"
set "THUNDERSTORE_CATEGORY=tools"
set "SHIMLOADER_DEP=Thunderstore-unreal_shimloader-1.1.7"

if not exist "%VERSION_FILE%" (
    echo [dotvanisher.thunderstore] VERSION file missing at %VERSION_FILE%
    exit /b 1
)
set /p VERSION=<"%VERSION_FILE%"
if "%VERSION%"=="" (
    echo [dotvanisher.thunderstore] VERSION file is empty
    exit /b 1
)
echo [dotvanisher.thunderstore] version: %VERSION%

if not exist "%README_SRC%" (
    echo [dotvanisher.thunderstore] missing README: %README_SRC%
    exit /b 1
)
if not exist "%ICON_SRC%" (
    echo [dotvanisher.thunderstore] missing icon: %ICON_SRC%
    echo [dotvanisher.thunderstore] add DotVanisher\release_resources\icon.png or restore release_resources\icon.png.
    exit /b 1
)

if "%PUBLISH_TO_THUNDERSTORE%"=="1" (
    if not exist "%PUBLISH_SCRIPT%" (
        echo [dotvanisher.thunderstore] missing publish helper: %PUBLISH_SCRIPT%
        exit /b 1
    )
    echo [dotvanisher.thunderstore] publish mode enabled
    echo [dotvanisher.thunderstore] checking published Thunderstore version ...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%PUBLISH_SCRIPT%" ^
        -CheckOnly ^
        -RepoRoot "%REPO_ROOT%" ^
        -LocalVersion "%VERSION%" ^
        -RepositoryUrl "%THUNDERSTORE_REPOSITORY%" ^
        -Namespace "%THUNDERSTORE_NAMESPACE%" ^
        -PackageName "%THUNDERSTORE_PACKAGE%" ^
        -Community "%THUNDERSTORE_COMMUNITY%" ^
        -Category "%THUNDERSTORE_CATEGORY%"
    if !ERRORLEVEL! NEQ 0 (
        echo [dotvanisher.thunderstore] version gate failed (exit !ERRORLEVEL!^)
        exit /b !ERRORLEVEL!
    )
) else (
    echo [dotvanisher.thunderstore] local package mode; skipping Thunderstore version gate
)

set MYMODS_FAST_DEV_UNSAFE=
echo [dotvanisher.thunderstore] building DotVanisher (Shipping / Win64, LTO on) ...
call "%SCRIPT_DIR%build_dotvanisher.bat"
if !ERRORLEVEL! NEQ 0 (
    echo [dotvanisher.thunderstore] build failed (exit !ERRORLEVEL!^)
    exit /b !ERRORLEVEL!
)
if not exist "%BUILT_DLL%" (
    echo [dotvanisher.thunderstore] expected DLL not found at %BUILT_DLL%
    exit /b 1
)

if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
mkdir "%STAGE_DIR%\mod\dlls"

copy /Y "%BUILT_DLL%" "%STAGE_DIR%\mod\dlls\main.dll" >nul
if !ERRORLEVEL! NEQ 0 (
    echo [dotvanisher.thunderstore] failed to copy DLL into stage
    exit /b 1
)
type nul > "%STAGE_DIR%\mod\enabled.txt"

copy /Y "%README_SRC%" "%STAGE_DIR%\README.md" >nul
copy /Y "%ICON_SRC%"   "%STAGE_DIR%\icon.png"  >nul
if !ERRORLEVEL! NEQ 0 (
    echo [dotvanisher.thunderstore] failed to copy package metadata
    exit /b 1
)

set "MANIFEST=%STAGE_DIR%\manifest.json"
> "%MANIFEST%" echo {
>>"%MANIFEST%" echo     "name": "DotVanisher",
>>"%MANIFEST%" echo     "version_number": "%VERSION%",
>>"%MANIFEST%" echo     "website_url": "https://github.com/FottenSC/HorseMod",
>>"%MANIFEST%" echo     "description": "Small SoulCalibur VI UE4SS mod that reduces false spectator disconnects during slow match loads.",
>>"%MANIFEST%" echo     "dependencies": ["%SHIMLOADER_DEP%"]
>>"%MANIFEST%" echo }

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
set "OUTPUT=%DIST_DIR%\DotVanisher-thunderstore-%VERSION%.zip"
if exist "%OUTPUT%" del "%OUTPUT%"

rem Create ZIP entries with forward slashes. NodeJS-based Thunderstore mod
rem managers treat backslash ZIP entries as literal filenames.
powershell -NoProfile -Command ^
    "Add-Type -AssemblyName System.IO.Compression;" ^
    "Add-Type -AssemblyName System.IO.Compression.FileSystem;" ^
    "$src = '%STAGE_DIR%';" ^
    "$out = '%OUTPUT%';" ^
    "$zip = [IO.Compression.ZipFile]::Open($out, [IO.Compression.ZipArchiveMode]::Create);" ^
    "try {" ^
    "  Get-ChildItem -Path $src -Recurse -File | ForEach-Object {" ^
    "    $rel = $_.FullName.Substring($src.Length + 1).Replace('\', '/');" ^
    "    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal);" ^
    "  }" ^
    "} finally {" ^
    "  $zip.Dispose();" ^
    "}"
if !ERRORLEVEL! NEQ 0 (
    echo [dotvanisher.thunderstore] zip creation failed
    exit /b !ERRORLEVEL!
)

echo.
echo [dotvanisher.thunderstore] OK: %OUTPUT%
for %%I in ("%OUTPUT%") do echo [dotvanisher.thunderstore] size: %%~zI bytes
echo [dotvanisher.thunderstore] contents:
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::OpenRead('%OUTPUT%').Entries | ForEach-Object { '  ' + $_.FullName }"

rmdir /S /Q "%STAGE_DIR%"

if not "%PUBLISH_TO_THUNDERSTORE%"=="1" (
    echo.
    echo [dotvanisher.thunderstore] package built locally; not publishing.
    echo [dotvanisher.thunderstore] run "%SCRIPT_NAME% --publish" to upload this release.
    exit /b 0
)

echo.
echo [dotvanisher.thunderstore] publishing to Thunderstore ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PUBLISH_SCRIPT%" ^
    -RepoRoot "%REPO_ROOT%" ^
    -LocalVersion "%VERSION%" ^
    -PackageZip "%OUTPUT%" ^
    -EnvPath "%THUNDERSTORE_ENV%" ^
    -RepositoryUrl "%THUNDERSTORE_REPOSITORY%" ^
    -Namespace "%THUNDERSTORE_NAMESPACE%" ^
    -PackageName "%THUNDERSTORE_PACKAGE%" ^
    -Community "%THUNDERSTORE_COMMUNITY%" ^
    -Category "%THUNDERSTORE_CATEGORY%"
if !ERRORLEVEL! NEQ 0 (
    echo [dotvanisher.thunderstore] publish failed (exit !ERRORLEVEL!^)
    exit /b !ERRORLEVEL!
)

echo.
echo [dotvanisher.thunderstore] done.
exit /b 0
