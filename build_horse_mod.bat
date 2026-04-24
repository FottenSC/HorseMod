@echo off
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

rem ===== CONFIGURE YOUR GAME PATH HERE =====
set GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64
set MOD_NAME=HorseLab

if defined GAME_DIR (
    echo Copying main.dll to game directory...
    if not exist "%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls" mkdir "%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls"
    copy /Y "%CD%\%BUILD_DIR%\HorseMod\HorseMod.dll" "%GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls\main.dll"
    echo Copied successfully to %GAME_DIR%\ue4ss\Mods\%MOD_NAME%\dlls\main.dll
)

echo.
echo Your mod DLLs are located in:
echo %CD%\%BUILD_DIR%\HorseMod
echo.
exit /b 0