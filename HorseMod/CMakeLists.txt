set(TARGET HorseMod)

option(HORSEMOD_ENABLE_EBTRACER
    "Enable the native chara+0x16EB transition tracer" OFF)
option(HORSEMOD_ENABLE_GEKKONET
    "Enable the GekkoNet rollback session dependency" ON)

if(HORSEMOD_ENABLE_GEKKONET)
    include(FetchContent)
    if(NOT DEFINED HORSEMOD_GEKKONET_GIT_TAG)
        set(HORSEMOD_GEKKONET_GIT_TAG
            "02c447c9d6a8a478070b2c162d15370dfad34482")
    endif()
    # Keep GekkoNet transport-free here; Horse owns send/receive integration.
    # These normal variables are honored by GekkoNet's option() calls without
    # leaking generic cache switches like BUILD_SHARED_LIBS to sibling targets.
    set(NO_ASIO_BUILD ON)
    set(BUILD_DOCS OFF)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(
        GekkoNet
        GIT_REPOSITORY https://github.com/HeatXD/GekkoNet.git
        GIT_TAG ${HORSEMOD_GEKKONET_GIT_TAG}
    )
    FetchContent_MakeAvailable(GekkoNet)
    if(TARGET GekkoNet AND MSVC)
        set_property(TARGET GekkoNet PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

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
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/GameImGui/Toast.hpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/OnlineRules.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/LuxBattleLauncherStartHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/HasSubProviderEntryHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ReplayDebugTrace.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ReplayScrub.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ReplayScrubDiag.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackCacheInjection.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackDiag.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackEndToEndHarness.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackFaultInject.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoAdapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoGameplayInputBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoSession.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoTransportBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoUdpAdapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackHgCpuSnapshot.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackInputCacheAdapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackInputLogProbe.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackInputHistory.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLab.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLiveActivationGate.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLiveActivationExecutor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLiveBoundary.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLiveBoundaryHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLivePeerPipeline.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackLiveTransportQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackOnlineSession.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSnapshot.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStepHarness.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStateHash.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockTransportObserveHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockTransportObserveModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockTransportSurface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackTransport.hpp
)

set(HORSEMOD_IMGUIFILEDIALOG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuiFileDialog/ImGuiFileDialog.cpp
)

set(HORSEMOD_VERSION_FILE "${CMAKE_SOURCE_DIR}/VERSION")
set(HORSEMOD_VERSION "dev")
if(EXISTS "${HORSEMOD_VERSION_FILE}")
    file(READ "${HORSEMOD_VERSION_FILE}" HORSEMOD_VERSION)
    string(STRIP "${HORSEMOD_VERSION}" HORSEMOD_VERSION)
endif()
set(HORSEMOD_VERSION_MAJOR 0)
set(HORSEMOD_VERSION_MINOR 0)
set(HORSEMOD_VERSION_PATCH 0)
if(HORSEMOD_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(HORSEMOD_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(HORSEMOD_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(HORSEMOD_VERSION_PATCH "${CMAKE_MATCH_3}")
endif()

add_library(${TARGET} SHARED
    dllmain.cpp
    ${SCVI_HEADERS}
    ${HORSELIB_GAMEIMGUI_HEADERS}
    ${HORSELIB_HEADERS}
    ${HORSEMOD_IMGUIFILEDIALOG_SOURCES}
)

add_executable(HorseReplayLauncher WIN32
    tools/HorseReplayLauncher.cpp
    ${CMAKE_SOURCE_DIR}/release_resources/HorseReplayLauncher.rc
)
set_target_properties(HorseReplayLauncher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(HorseReplayLauncher PRIVATE
    ${CMAKE_SOURCE_DIR}/release_resources
)
target_link_libraries(HorseReplayLauncher PRIVATE
    winhttp
    shell32
    ws2_32
    advapi32
    ole32
    propsys
    uuid
)
target_compile_definitions(HorseReplayLauncher PRIVATE
    HORSEMOD_VERSION="${HORSEMOD_VERSION}"
    HORSEMOD_VERSION_MAJOR=${HORSEMOD_VERSION_MAJOR}
    HORSEMOD_VERSION_MINOR=${HORSEMOD_VERSION_MINOR}
    HORSEMOD_VERSION_PATCH=${HORSEMOD_VERSION_PATCH}
)

if(HORSEMOD_ENABLE_GEKKONET AND TARGET GekkoNet)
    add_executable(RollbackGekkoSelfTest
        ${CMAKE_SOURCE_DIR}/tools/rollback_gekkonet_selftest.cpp
    )
    target_include_directories(RollbackGekkoSelfTest PRIVATE
        ${CMAKE_SOURCE_DIR}/HorseMod/horselib
    )
    target_compile_definitions(RollbackGekkoSelfTest PRIVATE
        HORSE_ENABLE_GEKKONET=1
    )
    target_link_libraries(RollbackGekkoSelfTest PRIVATE GekkoNet)
    if(MSVC)
        set_property(TARGET RollbackGekkoSelfTest PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    add_executable(RollbackGekkoGameplayInputSelfTest
        ${CMAKE_SOURCE_DIR}/tools/rollback_gekko_gameplay_input_selftest.cpp
    )
    target_include_directories(RollbackGekkoGameplayInputSelfTest PRIVATE
        ${CMAKE_SOURCE_DIR}/HorseMod/horselib
    )
    target_compile_definitions(RollbackGekkoGameplayInputSelfTest PRIVATE
        HORSE_ENABLE_GEKKONET=1
    )
    target_link_libraries(RollbackGekkoGameplayInputSelfTest PRIVATE GekkoNet)
    if(MSVC)
        set_property(TARGET RollbackGekkoGameplayInputSelfTest PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    add_executable(RollbackGekkoUdpSelfTest
        ${CMAKE_SOURCE_DIR}/tools/rollback_gekko_udp_selftest.cpp
    )
    target_include_directories(RollbackGekkoUdpSelfTest PRIVATE
        ${CMAKE_SOURCE_DIR}/HorseMod/horselib
    )
    target_compile_definitions(RollbackGekkoUdpSelfTest PRIVATE
        HORSE_ENABLE_GEKKONET=1
    )
    target_link_libraries(RollbackGekkoUdpSelfTest PRIVATE GekkoNet ws2_32)
    if(MSVC)
        set_property(TARGET RollbackGekkoUdpSelfTest PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

add_executable(RollbackInputCacheAdapterSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_input_cache_adapter_selftest.cpp
)
target_include_directories(RollbackInputCacheAdapterSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackInputCacheAdapterSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackTransportSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_transport_selftest.cpp
)
target_include_directories(RollbackTransportSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackTransportSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackOnlineSessionSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_online_session_selftest.cpp
)
target_include_directories(RollbackOnlineSessionSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackOnlineSessionSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLiveTransportSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_live_transport_selftest.cpp
)
target_include_directories(RollbackLiveTransportSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLiveTransportSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLivePeerPipelineSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_live_peer_pipeline_selftest.cpp
)
target_include_directories(RollbackLivePeerPipelineSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLivePeerPipelineSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackEndToEndSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_end_to_end_selftest.cpp
)
target_include_directories(RollbackEndToEndSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackEndToEndSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLiveActivationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_live_activation_selftest.cpp
)
target_include_directories(RollbackLiveActivationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLiveActivationSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLiveActivationExecutorSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_live_activation_executor_selftest.cpp
)
target_include_directories(RollbackLiveActivationExecutorSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLiveActivationExecutorSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLiveBoundarySelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_live_boundary_selftest.cpp
)
target_include_directories(RollbackLiveBoundarySelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLiveBoundarySelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackStockTransportSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_stock_transport_selftest.cpp
)
target_include_directories(RollbackStockTransportSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackStockTransportSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackStockTransportObserveSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_stock_transport_observe_selftest.cpp
)
target_include_directories(RollbackStockTransportObserveSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackStockTransportObserveSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLiveOnlineCaptureSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_live_online_capture_selftest.cpp
)
target_include_directories(RollbackLiveOnlineCaptureSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLiveOnlineCaptureSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackSnapshotSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_snapshot_selftest.cpp
)
target_include_directories(RollbackSnapshotSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackSnapshotSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(_horsemod_enable_ebtracer ${HORSEMOD_ENABLE_EBTRACER})
set(_horsemod_enable_gekkonet ${HORSEMOD_ENABLE_GEKKONET})

target_compile_definitions(${TARGET} PRIVATE
    HORSE_ENABLE_EBTRACER=$<BOOL:${_horsemod_enable_ebtracer}>
    HORSE_ENABLE_GEKKONET=$<BOOL:${_horsemod_enable_gekkonet}>
    HORSEMOD_VERSION="${HORSEMOD_VERSION}"
)

set(HORSEMOD_REPLAY_FUNCTION_MAP
    "${CMAKE_CURRENT_SOURCE_DIR}/debug/SoulcaliburVI_function_map.tsv"
)

target_include_directories(${TARGET} PRIVATE
    .
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuiFileDialog
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
target_link_libraries(${TARGET} PRIVATE dxgi d3d11 delayimp ws2_32)
if(HORSEMOD_ENABLE_GEKKONET AND TARGET GekkoNet)
    target_link_libraries(${TARGET} PRIVATE GekkoNet)
endif()

# ---------------------------------------------------------------------------
# Delay-load DirectX / XInput so they don't register in the Windows loader's
# import-resolution pass when HorseMod.dll is just SITTING IN THE PROCESS.
#
# Why this matters
# ----------------
# When the user disables HorseMod via enabled.txt-rename (UE4SS skips
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
    # Emit HorseMod.map (symbol -> RVA) next to the DLL so crash-dump
    # faulting addresses can be resolved to a function without a debugger
    # (see tools/crash_dump_parse.py).
    /MAP
)

if(EXISTS "${HORSEMOD_REPLAY_FUNCTION_MAP}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HORSEMOD_REPLAY_FUNCTION_MAP}"
            "$<TARGET_FILE_DIR:${TARGET}>/SoulcaliburVI_function_map.tsv"
        COMMENT "Copying replay function map next to HorseMod.dll"
    )
endif()

add_dependencies(${TARGET} HorseReplayLauncher)

# Copy the DLL to the game's mod directory after building (optional, adjust path as needed)
# add_custom_command(TARGET ${TARGET} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET}> "path/to/game/Binaries/Win64/Mods/${TARGET}/dlls/"
# )
