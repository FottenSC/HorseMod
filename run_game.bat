@echo off
setlocal

set GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64
set GAME_EXE=%GAME_DIR%\SoulcaliburVI.exe

rem Close Soul Calibur VI if it's running
echo Checking if Soul Calibur VI is running...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = Get-Process -Name 'SoulcaliburVI' -ErrorAction SilentlyContinue; if (-not $p) { exit 1 }; $closed = $false; foreach ($proc in $p) { if ($proc.MainWindowHandle -ne 0) { $closed = $proc.CloseMainWindow() -or $closed } }; if (-not $closed) { Write-Host 'Soul Calibur VI is running but has no visible window; leaving it open to avoid a UE crash dialog.'; exit 2 }; $deadline = (Get-Date).AddSeconds(30); do { Start-Sleep -Milliseconds 250; $alive = Get-Process -Name 'SoulcaliburVI' -ErrorAction SilentlyContinue } while ($alive -and (Get-Date) -lt $deadline); if ($alive) { Write-Host 'Soul Calibur VI did not close after 30 seconds; leaving it running to avoid a UE crash dialog.'; exit 2 }; exit 0"
set CLOSE_RESULT=%ERRORLEVEL%
if "%CLOSE_RESULT%"=="0" (
    echo Soul Calibur VI was closed cleanly.
    timeout /t 2 /nobreak >nul
) else if "%CLOSE_RESULT%"=="1" (
    echo Soul Calibur VI is not running.
) else (
    echo.
    echo Close Soul Calibur VI manually, then rerun this script.
    exit /b %CLOSE_RESULT%
)
echo.

rem Build HorseMod and deploy the DLL into the game's mods directory.
rem build_horse_mod.bat is now pure-build only (no copy); the deploy step
rem moved into build_and_deploy.bat so the release scripts can call the
rem pure build without producing redundant deploy noise.
echo Running build + deploy script...
call "%~dp0build_and_deploy.bat"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build script failed. Game will not be launched.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Launching Soul Calibur VI...
start "" "%GAME_EXE%"
timeout /t 3 /nobreak
