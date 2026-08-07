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
        PATCH_COMMAND git apply --whitespace=nowarn
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-correction-flush.patch
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-authored-delay-prefix.patch
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-authored-delay-prefix-ready.patch
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-pre-gameplay-network-poll.patch
            ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-external-disconnect-authority.patch
    )
    FetchContent_MakeAvailable(GekkoNet)
    # FetchContent does not rerun PATCH_COMMAND for an already-populated
    # dependency. Keep existing build trees honest as well as fresh clones.
    file(READ "${gekkonet_SOURCE_DIR}/GekkoLib/include/gekkonet.h"
        _horsemod_gekkonet_header)
    if(NOT _horsemod_gekkonet_header MATCHES "gekko_flush_corrections")
        execute_process(
            COMMAND git apply --whitespace=nowarn
                ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-correction-flush.patch
            WORKING_DIRECTORY ${gekkonet_SOURCE_DIR}
            RESULT_VARIABLE _horsemod_gekkonet_patch_result
        )
        if(NOT _horsemod_gekkonet_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply the required Gekko terminal-flush patch")
        endif()
    endif()
    if(NOT _horsemod_gekkonet_header MATCHES
            "gekko_prime_local_delay_prefix")
        execute_process(
            COMMAND git apply --whitespace=nowarn
                ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-authored-delay-prefix.patch
            WORKING_DIRECTORY ${gekkonet_SOURCE_DIR}
            RESULT_VARIABLE _horsemod_gekkonet_prefix_patch_result
        )
        if(NOT _horsemod_gekkonet_prefix_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply the required Gekko authored-delay-prefix patch")
        endif()
    endif()
    if(NOT _horsemod_gekkonet_header MATCHES
            "gekko_delay_prefix_ready")
        execute_process(
            COMMAND git apply --whitespace=nowarn
                ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-authored-delay-prefix-ready.patch
            WORKING_DIRECTORY ${gekkonet_SOURCE_DIR}
            RESULT_VARIABLE _horsemod_gekkonet_prefix_ready_patch_result
        )
        if(NOT _horsemod_gekkonet_prefix_ready_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply the required Gekko bilateral delay-prefix patch")
        endif()
    endif()
    if(NOT _horsemod_gekkonet_header MATCHES
            "external_disconnect_detection")
        execute_process(
            COMMAND git apply --whitespace=nowarn
                ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-external-disconnect-authority.patch
            WORKING_DIRECTORY ${gekkonet_SOURCE_DIR}
            RESULT_VARIABLE _horsemod_gekkonet_disconnect_patch_result
        )
        if(NOT _horsemod_gekkonet_disconnect_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply the required Gekko external-disconnect-authority patch")
        endif()
    endif()
    file(READ "${gekkonet_SOURCE_DIR}/GekkoLib/src/game_session.cpp"
        _horsemod_gekkonet_game_session)
    if(NOT _horsemod_gekkonet_game_session MATCHES
            "Poll-only startup must progress actor/session synchronization")
        execute_process(
            COMMAND git apply --whitespace=nowarn
                ${CMAKE_CURRENT_SOURCE_DIR}/patches/gekkonet-pre-gameplay-network-poll.patch
            WORKING_DIRECTORY ${gekkonet_SOURCE_DIR}
            RESULT_VARIABLE _horsemod_gekkonet_poll_patch_result
        )
        if(NOT _horsemod_gekkonet_poll_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply the required Gekko pre-gameplay poll patch")
        endif()
    endif()
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
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ReplayTraceWriteBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ReplayScrub.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/ReplayScrubDiag.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackController.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackCacheInjection.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackDiag.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackEndToEndHarness.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackFaultInject.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackFrameZeroRngBaseline.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoAdapter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoGameplayInputBridge.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoRuntimeCore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackGekkoSessionStart.hpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackAudioPresentation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackProtocolV2.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackProductionRuntime.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackP2PHarness.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackReplayInputScript.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackReplaySelectionAvailabilityPolicy.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackReplaySelectionAvailabilityHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackReplayForkRuntime.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSidecar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSideEffectLedger.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSnapshot.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSnapshotStore.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStepHarness.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStateHash.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSummaryConsensus.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackSteamP2PTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockTransportObserveHook.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockInviteFallback.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockTransportObserveModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackStockTransportSurface.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackUdpRuntime.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib/RollbackUiNavigationDriver.hpp
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

    add_executable(RollbackGekkoRuntimeCoreSelfTest
        ${CMAKE_SOURCE_DIR}/tools/rollback_gekko_runtime_core_selftest.cpp
    )
    target_include_directories(RollbackGekkoRuntimeCoreSelfTest PRIVATE
        ${CMAKE_SOURCE_DIR}/HorseMod/horselib
    )
    target_compile_definitions(RollbackGekkoRuntimeCoreSelfTest PRIVATE
        HORSE_ENABLE_GEKKONET=1
    )
    target_compile_features(RollbackGekkoRuntimeCoreSelfTest PRIVATE cxx_std_20)
    target_link_libraries(RollbackGekkoRuntimeCoreSelfTest PRIVATE GekkoNet)
    if(MSVC)
        set_property(TARGET RollbackGekkoRuntimeCoreSelfTest PROPERTY
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

add_executable(RollbackRouteManagerSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_route_manager_selftest.cpp
)
target_include_directories(RollbackRouteManagerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_compile_features(RollbackRouteManagerSelfTest PRIVATE cxx_std_17)
target_link_libraries(RollbackRouteManagerSelfTest PRIVATE ws2_32 bcrypt)
if(MSVC)
    set_property(TARGET RollbackRouteManagerSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackBetaConfigSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_beta_config_selftest.cpp
)
target_include_directories(RollbackBetaConfigSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackBetaConfigSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackFaultInjectSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_fault_inject_selftest.cpp
)
target_include_directories(RollbackFaultInjectSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackFaultInjectSelfTest PROPERTY
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

if(HORSEMOD_ENABLE_GEKKONET AND TARGET GekkoNet)
    add_executable(RollbackEndToEndSelfTest
        ${CMAKE_SOURCE_DIR}/tools/rollback_end_to_end_selftest.cpp
    )
    target_include_directories(RollbackEndToEndSelfTest PRIVATE
        ${CMAKE_SOURCE_DIR}/HorseMod/horselib
    )
    target_compile_definitions(RollbackEndToEndSelfTest PRIVATE
        HORSE_ENABLE_GEKKONET=1
        GEKKONET_STATIC=1
    )
    target_include_directories(RollbackEndToEndSelfTest PRIVATE
        ${gekkonet_SOURCE_DIR}/GekkoLib/include
    )
    target_link_libraries(RollbackEndToEndSelfTest PRIVATE GekkoNet)
    if(MSVC)
        set_property(TARGET RollbackEndToEndSelfTest PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
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

add_executable(RollbackLuxMoveStateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_lux_move_state_selftest.cpp
)
target_include_directories(RollbackLuxMoveStateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLuxMoveStateSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackFloatingPointEnvironmentSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_floating_point_environment_selftest.cpp
)
target_include_directories(RollbackFloatingPointEnvironmentSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackFloatingPointEnvironmentSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackBattleCameraSnapshotSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_battle_camera_snapshot_selftest.cpp
)
target_include_directories(RollbackBattleCameraSnapshotSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackBattleCameraSnapshotSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackPresentationSemanticSnapshotSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_presentation_semantic_snapshot_selftest.cpp
)
target_include_directories(RollbackPresentationSemanticSnapshotSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackPresentationSemanticSnapshotSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackCharaPresentationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_chara_presentation_selftest.cpp
)
target_include_directories(RollbackCharaPresentationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackCharaPresentationSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackSnapshotStoreSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_snapshot_store_selftest.cpp
)
target_include_directories(RollbackSnapshotStoreSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackSnapshotStoreSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackSideEffectLedgerSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_side_effect_ledger_selftest.cpp
)
target_include_directories(RollbackSideEffectLedgerSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackSideEffectLedgerSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackAudioPresentationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_audio_presentation_selftest.cpp
)
target_include_directories(RollbackAudioPresentationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackAudioPresentationSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackGameplayCrtSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_gameplay_crt_selftest.cpp
)
target_include_directories(RollbackGameplayCrtSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackGameplayCrtSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackFixtureEvidenceSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_fixture_evidence_selftest.cpp
)
target_include_directories(RollbackFixtureEvidenceSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackFixtureEvidenceSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(ReplayMatrixBankRingSelfTest
    ${CMAKE_SOURCE_DIR}/tools/replay_matrix_bank_ring_selftest.cpp
)
target_include_directories(ReplayMatrixBankRingSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET ReplayMatrixBankRingSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(ReplayInputPairAuthoritySelfTest
    ${CMAKE_SOURCE_DIR}/tools/replay_input_pair_authority_selftest.cpp
)
target_include_directories(ReplayInputPairAuthoritySelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET ReplayInputPairAuthoritySelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackMotionDecodeScratchSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_motion_decode_scratch_selftest.cpp
)
target_include_directories(RollbackMotionDecodeScratchSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_compile_features(RollbackMotionDecodeScratchSelfTest PRIVATE cxx_std_17)
if(MSVC)
    set_property(TARGET RollbackMotionDecodeScratchSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackMotionDecodeTraceWindowSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_motion_decode_trace_window_selftest.cpp
)
target_include_directories(RollbackMotionDecodeTraceWindowSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackMotionDecodeTraceWindowSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackMotionPoseResidueSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_motion_pose_residue_selftest.cpp
)
target_include_directories(RollbackMotionPoseResidueSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_compile_features(RollbackMotionPoseResidueSelfTest PRIVATE cxx_std_17)
if(MSVC)
    set_property(TARGET RollbackMotionPoseResidueSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackProtocolV2SelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_protocol_v2_selftest.cpp
)
target_include_directories(RollbackProtocolV2SelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_link_libraries(RollbackProtocolV2SelfTest PRIVATE bcrypt)
if(MSVC)
    set_property(TARGET RollbackProtocolV2SelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackProtocolV2Benchmark
    ${CMAKE_SOURCE_DIR}/tools/rollback_protocol_v2_benchmark.cpp
)
target_include_directories(RollbackProtocolV2Benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_link_libraries(RollbackProtocolV2Benchmark PRIVATE bcrypt)
if(MSVC)
    set_property(TARGET RollbackProtocolV2Benchmark PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackUdpRuntimeSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_udp_runtime_selftest.cpp
)
target_include_directories(RollbackUdpRuntimeSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_link_libraries(RollbackUdpRuntimeSelfTest PRIVATE ws2_32 bcrypt)
if(MSVC)
    set_property(TARGET RollbackUdpRuntimeSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackSteamP2PTransportSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_steam_p2p_transport_selftest.cpp
)
target_include_directories(RollbackSteamP2PTransportSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
target_link_libraries(
    RollbackSteamP2PTransportSelfTest PRIVATE ws2_32 bcrypt)
if(MSVC)
    set_property(TARGET RollbackSteamP2PTransportSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackSummaryConsensusSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_summary_consensus_selftest.cpp
)
target_include_directories(RollbackSummaryConsensusSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackSummaryConsensusSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackUiNavigationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_ui_navigation_selftest.cpp
)
target_include_directories(RollbackUiNavigationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackUiNavigationSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackOnlineStageStateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_online_stage_state_selftest.cpp
)
target_include_directories(RollbackOnlineStageStateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackOnlineStageStateSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackStockInviteFallbackSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_stock_invite_fallback_selftest.cpp
)
target_include_directories(RollbackStockInviteFallbackSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackStockInviteFallbackSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackLaunchContractSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_launch_contract_selftest.cpp
)
target_include_directories(RollbackLaunchContractSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackLaunchContractSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackFrameZeroRngBaselineSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_frame_zero_rng_baseline_selftest.cpp
)
target_include_directories(RollbackFrameZeroRngBaselineSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackFrameZeroRngBaselineSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackReplayOracleSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_replay_oracle_selftest.cpp
)
target_include_directories(RollbackReplayOracleSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackReplayOracleSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackReplayInputSidecarSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_replay_input_sidecar_selftest.cpp
)
target_include_directories(RollbackReplayInputSidecarSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackReplayInputSidecarSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackProductionSummarySelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_production_summary_selftest.cpp
)
target_include_directories(RollbackProductionSummarySelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackProductionSummarySelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackBattleSceneStateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_battle_scene_state_selftest.cpp
)
target_include_directories(RollbackBattleSceneStateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackBattleSceneStateSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackProductionActiveGuardSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_production_active_guard_selftest.cpp
)
target_include_directories(RollbackProductionActiveGuardSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackProductionActiveGuardSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackPeerLivenessSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_peer_liveness_selftest.cpp
)
target_include_directories(RollbackPeerLivenessSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackPeerLivenessSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackRoundCoordinatorSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_round_coordinator_selftest.cpp
)
target_include_directories(RollbackRoundCoordinatorSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)

add_executable(RollbackNativeTerminalGateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_native_terminal_gate_selftest.cpp
)
target_include_directories(RollbackNativeTerminalGateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)

add_executable(RollbackNativePreNewRoundGateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_native_pre_new_round_gate_selftest.cpp
)
target_include_directories(RollbackNativePreNewRoundGateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)

add_executable(RollbackNativeSimulationIterationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_native_simulation_iteration_selftest.cpp
)
target_include_directories(RollbackNativeSimulationIterationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)

add_executable(RollbackInstallOnceCallbackGateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_install_once_callback_gate_selftest.cpp
)
target_include_directories(RollbackInstallOnceCallbackGateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackInstallOnceCallbackGateSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(ReplayTraceWriteBufferSelfTest
    ${CMAKE_SOURCE_DIR}/tools/replay_trace_write_buffer_selftest.cpp
)
target_include_directories(ReplayTraceWriteBufferSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)

add_executable(RollbackVtableSlotPatchSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_vtable_slot_patch_selftest.cpp
)
target_include_directories(RollbackVtableSlotPatchSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackVtableSlotPatchSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(PeImportSlotResolverSelfTest
    ${CMAKE_SOURCE_DIR}/tools/pe_import_slot_resolver_selftest.cpp
)
target_include_directories(PeImportSlotResolverSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET PeImportSlotResolverSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackVfxPresentationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_vfx_presentation_selftest.cpp
)
target_include_directories(RollbackVfxPresentationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackVfxPresentationSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackScheduledBarrierSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_scheduled_barrier_selftest.cpp
)
target_include_directories(RollbackScheduledBarrierSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackScheduledBarrierSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackHistoricalCameraArgsSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_historical_camera_args_selftest.cpp
)
target_include_directories(RollbackHistoricalCameraArgsSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackHistoricalCameraArgsSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackStageWindSnapshotSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_stage_wind_snapshot_selftest.cpp
)
target_include_directories(RollbackStageWindSnapshotSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackStageWindSnapshotSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_link_options(RollbackStageWindSnapshotSelfTest PRIVATE
        /STACK:4194304)
endif()

add_executable(RollbackStageWindAuthoritySelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_stage_wind_authority_selftest.cpp
)
target_include_directories(RollbackStageWindAuthoritySelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackStageWindAuthoritySelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    target_link_options(RollbackStageWindAuthoritySelfTest PRIVATE
        /STACK:4194304)
endif()

add_executable(RollbackModuleUnloadSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_module_unload_selftest.cpp
)
target_include_directories(RollbackModuleUnloadSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackModuleUnloadSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(ReplayAnimationPresentationSelfTest
    ${CMAKE_SOURCE_DIR}/tools/replay_animation_presentation_selftest.cpp
)
target_include_directories(ReplayAnimationPresentationSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET ReplayAnimationPresentationSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(LuxBattleCharaClipFrameLayoutSelfTest
    ${CMAKE_SOURCE_DIR}/tools/lux_battle_chara_clip_frame_layout_selftest.cpp
)
target_include_directories(LuxBattleCharaClipFrameLayoutSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET LuxBattleCharaClipFrameLayoutSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackCharaAnimationStateSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_chara_animation_state_selftest.cpp
)
target_include_directories(RollbackCharaAnimationStateSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackCharaAnimationStateSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackRootMotionTraceContractSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_root_motion_trace_contract_selftest.cpp
)
target_include_directories(RollbackRootMotionTraceContractSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackRootMotionTraceContractSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackPoseProducerTraceContractSelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_pose_producer_trace_contract_selftest.cpp
)
target_include_directories(RollbackPoseProducerTraceContractSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackPoseProducerTraceContractSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(RollbackReplaySelectionAvailabilitySelfTest
    ${CMAKE_SOURCE_DIR}/tools/rollback_replay_selection_availability_selftest.cpp
)
target_include_directories(RollbackReplaySelectionAvailabilitySelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/HorseMod/horselib
)
if(MSVC)
    set_property(TARGET RollbackReplaySelectionAvailabilitySelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(HORSEMOD_ROLLBACK_SELFTEST_TARGETS)

function(horsemod_register_rollback_selftest target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "Cannot register missing rollback self-test target: ${target}")
    endif()

    cmake_parse_arguments(
        HORSEMOD_SELFTEST
        "REQUIRES_GEKKO;PERFORMANCE"
        ""
        ""
        ${ARGN}
    )
    if(HORSEMOD_SELFTEST_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown rollback self-test registration arguments for ${target}: "
            "${HORSEMOD_SELFTEST_UNPARSED_ARGUMENTS}")
    endif()

    target_compile_features(${target} PRIVATE cxx_std_17)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)

    set(_horsemod_selftest_labels rollback)
    if(HORSEMOD_SELFTEST_PERFORMANCE)
        list(APPEND _horsemod_selftest_labels rollback-performance)
    else()
        list(APPEND _horsemod_selftest_labels rollback-fast)
    endif()
    if(HORSEMOD_SELFTEST_REQUIRES_GEKKO)
        list(APPEND _horsemod_selftest_labels requires-gekko)
    endif()
    set_tests_properties(${target} PROPERTIES
        LABELS "${_horsemod_selftest_labels}"
        TIMEOUT 240
    )

    set(
        HORSEMOD_ROLLBACK_SELFTEST_TARGETS
        "${HORSEMOD_ROLLBACK_SELFTEST_TARGETS};${target}"
        PARENT_SCOPE
    )
endfunction()

set(_horsemod_rollback_selftests
    LuxBattleCharaClipFrameLayoutSelfTest
    ReplayAnimationPresentationSelfTest
    ReplayTraceWriteBufferSelfTest
    RollbackCharaAnimationStateSelfTest
    RollbackPoseProducerTraceContractSelfTest
    RollbackRootMotionTraceContractSelfTest
    RollbackMotionDecodeScratchSelfTest
    RollbackMotionDecodeTraceWindowSelfTest
    RollbackMotionPoseResidueSelfTest
    ReplayMatrixBankRingSelfTest
    ReplayInputPairAuthoritySelfTest
    RollbackReplayInputSidecarSelfTest
    RollbackReplaySelectionAvailabilitySelfTest
    RollbackProductionSummarySelfTest
    RollbackInputCacheAdapterSelfTest
    RollbackTransportSelfTest
    RollbackRouteManagerSelfTest
    RollbackBetaConfigSelfTest
    RollbackFaultInjectSelfTest
    RollbackOnlineSessionSelfTest
    RollbackLiveTransportSelfTest
    RollbackLivePeerPipelineSelfTest
    RollbackLiveActivationSelfTest
    RollbackLiveActivationExecutorSelfTest
    RollbackLiveBoundarySelfTest
    RollbackStockTransportSelfTest
    RollbackStockTransportObserveSelfTest
    RollbackLiveOnlineCaptureSelfTest
    RollbackSnapshotSelfTest
    RollbackLuxMoveStateSelfTest
    RollbackFloatingPointEnvironmentSelfTest
    RollbackBattleCameraSnapshotSelfTest
    RollbackPresentationSemanticSnapshotSelfTest
    RollbackCharaPresentationSelfTest
    RollbackSnapshotStoreSelfTest
    RollbackSideEffectLedgerSelfTest
    RollbackAudioPresentationSelfTest
    RollbackGameplayCrtSelfTest
    RollbackFixtureEvidenceSelfTest
    RollbackProtocolV2SelfTest
    RollbackUdpRuntimeSelfTest
    RollbackSteamP2PTransportSelfTest
    RollbackSummaryConsensusSelfTest
    RollbackUiNavigationSelfTest
    RollbackOnlineStageStateSelfTest
    RollbackStockInviteFallbackSelfTest
    RollbackLaunchContractSelfTest
    RollbackFrameZeroRngBaselineSelfTest
    RollbackReplayOracleSelfTest
    RollbackBattleSceneStateSelfTest
    RollbackProductionActiveGuardSelfTest
    RollbackPeerLivenessSelfTest
    RollbackRoundCoordinatorSelfTest
    RollbackNativeTerminalGateSelfTest
    RollbackNativePreNewRoundGateSelfTest
    RollbackNativeSimulationIterationSelfTest
    RollbackInstallOnceCallbackGateSelfTest
    RollbackVtableSlotPatchSelfTest
    PeImportSlotResolverSelfTest
    RollbackVfxPresentationSelfTest
    RollbackScheduledBarrierSelfTest
    RollbackHistoricalCameraArgsSelfTest
    RollbackStageWindSnapshotSelfTest
    RollbackStageWindAuthoritySelfTest
    RollbackModuleUnloadSelfTest
)
foreach(rollback_selftest IN LISTS _horsemod_rollback_selftests)
    horsemod_register_rollback_selftest(${rollback_selftest})
endforeach()
horsemod_register_rollback_selftest(
    RollbackProtocolV2Benchmark
    PERFORMANCE
)

if(HORSEMOD_ENABLE_GEKKONET AND TARGET GekkoNet)
    set(_horsemod_gekkonet_selftests
        RollbackGekkoSelfTest
        RollbackGekkoGameplayInputSelfTest
        RollbackGekkoUdpSelfTest
        RollbackGekkoRuntimeCoreSelfTest
        RollbackEndToEndSelfTest
    )
    foreach(rollback_selftest IN LISTS _horsemod_gekkonet_selftests)
        horsemod_register_rollback_selftest(
            ${rollback_selftest}
            REQUIRES_GEKKO
        )
    endforeach()
endif()

# Rollback self-tests are easy to add and easy to forget in a separate runner.
# Fail configuration if any target matching the rollback/replay self-test naming
# contract was not registered above. CTest is the sole executable inventory
# consumed by the Python validation runners.
get_property(
    _horsemod_directory_targets
    DIRECTORY
    PROPERTY BUILDSYSTEM_TARGETS
)
foreach(_horsemod_target IN LISTS _horsemod_directory_targets)
    if(_horsemod_target MATCHES "^(Rollback|Replay).+SelfTest$")
        if(NOT _horsemod_target IN_LIST HORSEMOD_ROLLBACK_SELFTEST_TARGETS)
            message(FATAL_ERROR
                "Rollback self-test target is not registered with CTest: "
                "${_horsemod_target}")
        endif()
    endif()
endforeach()

add_custom_target(HorseModRollbackSelfTests
    DEPENDS ${HORSEMOD_ROLLBACK_SELFTEST_TARGETS}
)
add_custom_target(HorseModRollbackSelfTestsRun
    COMMAND ${CMAKE_CTEST_COMMAND}
        --test-dir ${CMAKE_BINARY_DIR}
        -C $<CONFIG>
        --output-on-failure
        -L rollback
    DEPENDS ${HORSEMOD_ROLLBACK_SELFTEST_TARGETS}
    USES_TERMINAL
    COMMENT "Running the registered rollback self-tests"
)

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
target_link_libraries(${TARGET} PRIVATE dxgi d3d11 delayimp ws2_32 bcrypt)
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
