@echo off
rem ============================================================================
rem build_dotvanisher.bat
rem
rem Pure build script for the standalone DotVanisher UE4SS C++ mod.
rem Output: ..\build_cmake_LessEqual421__Shipping__Win64\DotVanisher\DotVanisher.dll
rem ============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
if not "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR%\"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"

set "BUILD_DIR=%REPO_ROOT%\build_cmake_LessEqual421__Shipping__Win64"

call "E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat"

set SCCACHE_ARGS=
if defined MYMODS_USE_SCCACHE (
    where sccache >nul 2>nul
    if !ERRORLEVEL! EQU 0 (
        echo [build] MYMODS_USE_SCCACHE set + sccache on PATH - enabling launcher
        echo [build]   WARNING: requires /Z7 instead of /Zi; will fail with C1041 otherwise
        set SCCACHE_ARGS=-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache
    ) else (
        echo [build] MYMODS_USE_SCCACHE set but sccache not on PATH - skipping
    )
)

set FAST_DEV_ARGS=-DMYMODS_FAST_DEV=OFF
if defined MYMODS_FAST_DEV_UNSAFE (
    echo [build] MYMODS_FAST_DEV_UNSAFE set - disabling LTO on UE4SS
    echo [build]   You MUST deploy build_cmake_*/.../bin/UE4SS.dll to the game
    echo [build]   manually, or DotVanisher will crash at LoadLibrary time.
    set FAST_DEV_ARGS=-DMYMODS_FAST_DEV=ON
)

cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=LessEqual421__Shipping__Win64 %FAST_DEV_ARGS% %SCCACHE_ARGS%
cmake --build "%BUILD_DIR%" --target DotVanisher --parallel %NUMBER_OF_PROCESSORS%

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully!
echo.
echo Your mod DLL is located at:
echo %BUILD_DIR%\DotVanisher\DotVanisher.dll
echo.
exit /b 0
