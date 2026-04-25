@echo off
setlocal

set GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64
set GAME_EXE=%GAME_DIR%\SoulcaliburVI.exe

rem Close Soul Calibur VI if it's running
echo Checking if Soul Calibur VI is running...
taskkill /IM "SoulcaliburVI.exe" /F >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Soul Calibur VI was closed.
    timeout /t 2 /nobreak >nul
) else (
    echo Soul Calibur VI is not running.
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
