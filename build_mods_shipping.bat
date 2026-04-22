@echo off
setlocal

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

rem Set the build directory
set BUILD_DIR=build_cmake_LessEqual421__Shipping__Win64

rem Setup MSVC developer environment for Ninja and correct toolset resolution
call "E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat"

rem Generate and Build the project with Ninja
cmake -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=LessEqual421__Shipping__Win64
cmake --build "%BUILD_DIR%"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully!
echo.

rem ===== CONFIGURE YOUR GAME PATH HERE =====
set GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64
set MOD_NAME=HorseLab
set GAME_EXE=%GAME_DIR%\SoulcaliburVI.exe

if defined GAME_DIR (
    echo Copying main.dll to game directory...
    if not exist "%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls" mkdir "%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls"
    copy /Y "%CD%\%BUILD_DIR%\HorseMod\HorseMod.dll" "%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls\main.dll"
    echo Copied successfully to %GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls\main.dll
)

echo Your mod DLLs are located in:
echo %CD%\%BUILD_DIR%\HorseMod
echo.
echo Launching Soul Calibur VI...
start "" "%GAME_EXE%"
timeout /t 3 /nobreak
