@echo off
setlocal

set "GAME_DIR=E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64"
set "GAME_EXE=%GAME_DIR%\SoulcaliburVI.exe"
set "DWMAPI_DLL=%GAME_DIR%\dwmapi.dll"
set "DWMAPI_DLL_DISABLED=%GAME_DIR%\dwmapi.dll.DISABLED"

rem Close the game first so file operations are not blocked.
taskkill /IM "SoulcaliburVI.exe" /F >nul 2>&1

rem Ensure UE4SS proxy DLL is enabled.
if exist "%DWMAPI_DLL_DISABLED%" (
    if not exist "%DWMAPI_DLL%" ren "%DWMAPI_DLL_DISABLED%" "dwmapi.dll"
)

rem Launch the game with UE4SS enabled.
start "" "%GAME_EXE%"
