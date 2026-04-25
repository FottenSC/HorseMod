set(TARGET HorseMod)

file(GLOB_RECURSE SCVI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI/*.h"
)

# ----------------------------------------------------------------------------
# Source layout
#
# horselib/GameImGui/ is header-only (like the rest of horselib).  We list
# the headers explicitly so MSVC / Ninja include them in the IDE view.
# ----------------------------------------------------------------------------
set(HORSELIB_GAMEIMGUI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/GameImGui.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/DX11State.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/PresentHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/WndProcHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/GamepadInput.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/XInputHook.hpp
)

# Other header-only horselib files that we want IDE-visible.  Most of
# horselib/*.hpp is pulled in transitively via dllmain.cpp's #includes,
# but listing the ones the user is likely to edit separately keeps them
# a click away in VS Solution Explorer.
set(HORSELIB_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ModSettings.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ResetOverride.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/SetStartPositionHook.hpp
)

add_library(${TARGET} SHARED
    dllmain.cpp
    ${SCVI_HEADERS}
    ${HORSELIB_GAMEIMGUI_HEADERS}
    ${HORSELIB_HEADERS}
)

target_include_directories(${TARGET} PRIVATE
    .
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI
)

# ----------------------------------------------------------------------------
# Link dependencies.
#
# UE4SS already re-exports (PUBLIC): fmt, ImGui, PolyHook_2, d3d11, ... — we
# inherit all of those via linking to UE4SS.  We still list dxgi explicitly
# because the Windows SDK does NOT auto-link it in every toolchain and our
# PresentHook calls D3D11CreateDeviceAndSwapChain, which pulls in dxgi symbols
# on the LNK side.
# ----------------------------------------------------------------------------
target_link_libraries(${TARGET} PUBLIC UE4SS)
# dxgi + d3d11 for the Present hook / back-buffer RTV.
# XInput is pulled in via #pragma comment(lib, "Xinput.lib") inside
# FreeCamera.hpp and GameImGui/GamepadInput.hpp — same convention.
# delayimp provides the delay-load stubs (see below).
target_link_libraries(${TARGET} PRIVATE dxgi d3d11 delayimp)

# ---------------------------------------------------------------------------
# Delay-load DirectX / XInput so they don't register in the Windows loader's
# import-resolution pass when HorseMod.dll is just SITTING IN THE PROCESS.
#
# Why this matters
# ----------------
# When the user disables HorseLab via enabled.txt-rename (UE4SS skips
# start_mod()), or sets the disable_gameimgui.txt kill switch, no HorseMod
# code runs — but the DLL is still mapped into SC6's process.  Windows'
# normal loader reads HorseMod.dll's import table and pulls in every DLL
# listed there, even if nothing ever calls into them.  ImGui's DX11 backend
# imports d3dcompiler_47.dll (for runtime shader compilation), which SC6
# itself does NOT load.  Its presence in the process triggers Steam Input's
# anti-tamper layer (or similar) to defensively unbind PS4→XInput
# emulation — game stops seeing the controller.
#
# With /DELAYLOAD, these DLLs appear in the import table but are NOT fetched
# by the loader.  They load lazily on first function call.  When GameImGui
# is disabled, no call ever happens, and d3dcompiler_47.dll never touches
# the process — Steam Input has nothing to react to.
#
# The runtime cost is a handful of nanoseconds per first-call resolution.
# The DLLs themselves are always present on modern Windows, so the error
# path (DLL-not-found) is effectively unreachable.
# ---------------------------------------------------------------------------
target_link_options(${TARGET} PRIVATE
    /DELAYLOAD:d3d11.dll
    /DELAYLOAD:dxgi.dll
    /DELAYLOAD:d3dcompiler_47.dll
    /DELAYLOAD:xinput1_4.dll
)

# Copy the DLL to the game's mod directory after building (optional, adjust path as needed)
# add_custom_command(TARGET ${TARGET} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET}> "path/to/game/Binaries/Win64/Mods/${TARGET}/dlls/"
# )
