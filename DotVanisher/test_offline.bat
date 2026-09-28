@echo off
setlocal
rem Standalone fake-native adapter tests. Never loads SC6, UE4SS, or DotVanisher.dll.
set "DOT_TEST_DIR=%~dp0..\build_cmake_LessEqual421__Shipping__Win64\DotVanisher\offline-tests"
if not exist "%DOT_TEST_DIR%" mkdir "%DOT_TEST_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%
call "E:\ProgramFiles\vsStudioCommunity\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /utf-8 /Zc:preprocessor /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /Fo"%DOT_TEST_DIR%\watch_recovery_selftest.obj" /Fe"%DOT_TEST_DIR%\watch_recovery_selftest.exe" "%~dp0watch_recovery_selftest.cpp"
if errorlevel 1 exit /b %ERRORLEVEL%
"%DOT_TEST_DIR%\watch_recovery_selftest.exe"
exit /b %ERRORLEVEL%
