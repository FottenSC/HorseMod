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
rem   <mod payload, layout dictated by the SC6 community's shimloader>
rem
rem Mod payload — the SC6 Thunderstore community uses unreal-shimloader
rem (Thunderstore-unreal_shimloader) to redirect UE4SS' Mods directory into
rem a profile-local sandbox.  At RUNTIME, shimloader translates UE4SS path
rem lookups under <game>\Binaries\Win64\Mods\... into the corresponding
rem files in the user's profile under <profile>\shimloader\mod\....
rem
rem At INSTALL time, TMM / r2modmanPlus / Gale apply SC6-specific routing
rem rules that take the contents of the zip's `mod\` directory and drop
rem them into the profile's shimloader mod sandbox.  So a zip entry of:
rem
rem   <zip>\mod\dlls\main.dll
rem
rem ends up on disk at:
rem
rem   <profile>\shimloader\mod\<author>-<package>\dlls\main.dll
rem
rem (i.e. the `mod\` prefix is stripped and the install lands under the
rem Thunderstore namespace folder `<author>-<package>`).  manifest.json,
rem README.md, and icon.png stay at the zip root — Thunderstore requires
rem those at the top level for the package metadata pipeline.
rem
rem THE LAYOUT THIS SCRIPT SHIPS:
rem
rem   <zip>\mod\enabled.txt
rem   <zip>\mod\dlls\main.dll
rem   <zip>\manifest.json
rem   <zip>\README.md
rem   <zip>\icon.png
rem
rem History (so this comment doesn't regress)
rem ------------------------------------------
rem v0.25.0 shipped the payload as `shimloader\mod\HorseLab\...` at the
rem zip root, on the (wrong) theory that mod managers strip the
rem `shimloader\mod\` prefix.  They don't — TMM extracted that wrapper
rem verbatim and the mod silently never loaded for users who didn't
rem already have a stale leftover layout from a manual install.
rem
rem A subsequent attempt flattened the zip to just `enabled.txt` +
rem `dlls\main.dll` at the root, expecting TMM to land the contents at
rem <profile>\shimloader\mod\<author>-<package>\.  That dropped loose
rem DLLs at the package root and lost the `dlls\` directory.  Wrong.
rem
rem A subsequent attempt shipped under `UE4SS\Mods\HorseLab\...` on the
rem theory that TMM would strip `UE4SS\Mods\` and land content at
rem `<profile>\shimloader\mod\HorseLab\...`.  That is what shimloader's
rem own bundled Lua mods do — but for third-party packages the SC6 TMM
rem install rules instead route the `mod\` directory directly into the
rem profile sandbox under the package's own namespace folder.  Verified
rem manually in v0.25.1 — the layout above is what actually works.
rem
rem (Manual / GitHub-release users get a different zip — see
rem build_release_github.bat — that drops files directly under
rem ue4ss\Mods\HorseLab\ since they don't go through shimloader.)
rem
rem Zipping note
rem ------------
rem We use [System.IO.Compression.ZipFile]::CreateFromDirectory rather
rem than PowerShell's Compress-Archive because the latter writes ZIP
rem entries with WINDOWS BACKSLASHES (`mod\dlls\main.dll`) which violate
rem the ZIP spec.  NodeJS-based extractors (TMM, r2modman, Gale) treat
rem backslash-paths as literal filenames and silently flatten the
rem structure.  CreateFromDirectory uses forward slashes and produces a
rem zip every Thunderstore-ecosystem extractor accepts.
rem
rem Version comes from the VERSION file at the repo root.  manifest.json's
rem version_number field is generated from VERSION at build time.  The
rem shimloader dependency version is pinned in SHIMLOADER_DEP below; bump
rem it when the SC6 community publishes a new shimloader release.
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

rem Pinned dependency on the SC6 community's unreal-shimloader package.
rem Format: Author-Name-Version, matching the package URL on
rem https://thunderstore.io/c/soulcalibur-vi/p/Thunderstore/unreal_shimloader/
set SHIMLOADER_DEP=Thunderstore-unreal_shimloader-1.1.4

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
rem build_horse_mod.bat defaults to LTO ON (the only safe state without also
rem deploying our locally-built UE4SS.dll), so we don't need to set anything
rem extra here.  Just be sure MYMODS_FAST_DEV_UNSAFE isn't lingering.
set MYMODS_FAST_DEV_UNSAFE=
echo [release.thunderstore] building HorseMod (Shipping / Win64, LTO on) ...
call "%REPO_ROOT%build_horse_mod.bat"
if !ERRORLEVEL! NEQ 0 (
    echo [release.thunderstore] build failed (exit !ERRORLEVEL!^)
    exit /b !ERRORLEVEL!
)
if not exist "%BUILT_DLL%" (
    echo [release.thunderstore] expected DLL not found at %BUILT_DLL%
    exit /b 1
)

rem ---- Stage Thunderstore zip layout ---------------------------------------
rem Mod payload goes under `mod\` — see header comment for the routing
rem rationale.  SC6 TMM install rules take the zip's `mod\` directory and
rem drop it under `<profile>\shimloader\mod\<author>-<package>\` on disk,
rem which is what shimloader rewrites UE4SS's runtime lookups to.
if exist "%STAGE_DIR%" rmdir /S /Q "%STAGE_DIR%"
mkdir "%STAGE_DIR%\mod\dlls"

copy /Y "%BUILT_DLL%" "%STAGE_DIR%\mod\dlls\main.dll" >nul
if !ERRORLEVEL! NEQ 0 (
    echo [release.thunderstore] failed to copy DLL into stage
    exit /b 1
)
type nul > "%STAGE_DIR%\mod\enabled.txt"

rem manifest.json / README.md / icon.png stay at the zip root —
rem Thunderstore (and the upload pipeline) require those at the top level.
copy /Y "%REPO_ROOT%release_resources\README.md" "%STAGE_DIR%\README.md" >nul
copy /Y "%ICON_SRC%"                              "%STAGE_DIR%\icon.png"  >nul

rem ---- Generate manifest.json ----------------------------------------------
rem Thunderstore manifest fields:
rem   name            - lowercase package name, ASCII + underscore only
rem   version_number  - SemVer (must match the eventual upload's filename)
rem   website_url     - source / homepage
rem   description     - short blurb (max 250 chars)
rem   dependencies    - other Thunderstore packages this mod requires.
rem                     We depend on unreal-shimloader, which itself bundles
rem                     UE4SS for SC6 and provides the path-redirection that
rem                     translates UE4SS's runtime lookups under
rem                     <game>\Binaries\Win64\Mods\<author>-<package>\... to
rem                     the corresponding files in the user's profile.
rem                     Pinned version is in SHIMLOADER_DEP at the top of
rem                     this script.
set MANIFEST=%STAGE_DIR%\manifest.json
> "%MANIFEST%" echo {
>>"%MANIFEST%" echo     "name": "HorseMod",
>>"%MANIFEST%" echo     "version_number": "%VERSION%",
>>"%MANIFEST%" echo     "website_url": "https://github.com/FottenSC/HorseMod",
>>"%MANIFEST%" echo     "description": "SoulCalibur VI training overlay: hitboxes, hurtboxes, free-fly camera, slow-motion, reset-position override.",
>>"%MANIFEST%" echo     "dependencies": ["%SHIMLOADER_DEP%"]
>>"%MANIFEST%" echo }

rem ---- Zip via PowerShell --------------------------------------------------
rem
rem IMPORTANT — we use [System.IO.Compression.ZipFile]::CreateFromDirectory
rem instead of PowerShell's friendlier Compress-Archive cmdlet because
rem Compress-Archive writes ZIP entry names with WINDOWS BACKSLASHES
rem (`dlls\main.dll`).  The ZIP spec mandates forward slashes; some
rem extractors silently cope by treating backslashes as path separators
rem too, but Thunderstore Mod Manager / r2modman / Gale (all NodeJS-based)
rem do NOT — they treat `dlls\main.dll` as a single literal filename and
rem strip the prefix, dropping `main.dll` directly into the install root
rem and losing the `dlls\` directory entirely.  UE4SS then can't find the
rem DLL because it expects `Mods\<mod>\dlls\main.dll` and the file is at
rem `Mods\<mod>\main.dll`.  This bit us in v0.25.0 — the install
rem APPEARED to extract correctly when inspected manually, but UE4SS
rem silently never loaded the mod.
rem
rem CreateFromDirectory uses forward slashes, includes explicit directory
rem entries, and produces a zip every Thunderstore-ecosystem extractor
rem handles correctly.  See the diff that introduced this comment for the
rem full debug trail.
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
set OUTPUT=%DIST_DIR%\HorseMod-thunderstore-%VERSION%.zip
if exist "%OUTPUT%" del "%OUTPUT%"

rem We can't use CreateFromDirectory directly on .NET Framework because it
rem still uses the platform's native path separator (backslash on Windows)
rem despite docs claiming spec-compliance.  Manually iterate and explicitly
rem replace `\` with `/` in each entry name to guarantee NodeJS-based
rem extractors (TMM, r2modman, Gale) can parse the directory structure.
powershell -NoProfile -Command ^
    "Add-Type -AssemblyName System.IO.Compression;" ^
    "Add-Type -AssemblyName System.IO.Compression.FileSystem;" ^
    "$src = '%STAGE_DIR%';" ^
    "$out = '%OUTPUT%';" ^
    "$zip = [IO.Compression.ZipFile]::Open($out, [IO.Compression.ZipArchiveMode]::Create);" ^
    "try {" ^
    "  Get-ChildItem -Path $src -Recurse -File | ForEach-Object {" ^
    "    $rel = $_.FullName.Substring($src.Length + 1).Replace('\', '/');" ^
    "    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(" ^
    "      $zip, $_.FullName, $rel, [IO.Compression.CompressionLevel]::Optimal);" ^
    "  }" ^
    "} finally {" ^
    "  $zip.Dispose();" ^
    "}"
if !ERRORLEVEL! NEQ 0 (
    echo [release.thunderstore] zip creation failed
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
