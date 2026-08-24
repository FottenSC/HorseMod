set(TARGET HorseMod)

if(DEFINED HORSEMOD_ENABLE_GEKKONET)
    message(WARNING
        "HORSEMOD_ENABLE_GEKKONET is a retired rollback option and is ignored")
    unset(HORSEMOD_ENABLE_GEKKONET CACHE)
endif()
option(HORSEMOD_BUILD_PINNED_GEKKONET
    "Build the pinned GekkoNet integration after offline qualification" OFF)
set(HORSEMOD_GEKKONET_GIT_COMMIT
    "f21698d85367c8d97585cf7610897316c873681c"
    CACHE STRING "Immutable FottenSC/GekkoNet commit")
if(HORSEMOD_BUILD_PINNED_GEKKONET)
    include(FetchContent)
    set(NO_ASIO_BUILD ON)
    set(BUILD_DOCS OFF)
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(
        GekkoNet
        GIT_REPOSITORY https://github.com/FottenSC/GekkoNet.git
        GIT_TAG ${HORSEMOD_GEKKONET_GIT_COMMIT}
        GIT_SHALLOW FALSE
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

set(HORSEMOD_DETERMINISTIC_SOURCES
    horselib/deterministic/CallbackTopology.cpp
    horselib/deterministic/CandidateCheckpoint.cpp
    horselib/deterministic/CandidateGameStateAdapter.cpp
    horselib/deterministic/Config.cpp
    horselib/deterministic/HgCpuStream.cpp
    horselib/deterministic/HgCpuCoverageProbe.cpp
    horselib/deterministic/HgCpuRuntimeDiagnostics.cpp
    horselib/deterministic/MoveDispatchState.cpp
    horselib/deterministic/NativeReplayMaterializer.cpp
    horselib/deterministic/InputTimeline.cpp
    horselib/deterministic/NativeBatchTimeline.cpp
    horselib/deterministic/NativeCandidateRegions.cpp
    horselib/deterministic/OnlineCoordinator.cpp
    horselib/deterministic/ParticlePresentation.cpp
    horselib/deterministic/PresentationJournal.cpp
    horselib/deterministic/ReplayCoordinator.cpp
    horselib/deterministic/ReplaySeekPlanner.cpp
    horselib/deterministic/Sc6ReplayNativeBridge.cpp
    horselib/deterministic/SimulationSession.cpp
    horselib/deterministic/SnapshotStore.cpp
    horselib/deterministic/StageBreakListenerDiagnostics.cpp
)

add_library(HorseDeterministicCore STATIC
    ${HORSEMOD_DETERMINISTIC_SOURCES}
)
target_include_directories(HorseDeterministicCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
)
target_compile_features(HorseDeterministicCore PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(HorseDeterministicCore PUBLIC bcrypt)
endif()
if(MSVC)
    set_property(TARGET HorseDeterministicCore PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(HORSELIB_GAMEIMGUI_HEADERS
    horselib/GameImGui/GameImGui.hpp
    horselib/GameImGui/DX11State.hpp
    horselib/GameImGui/PresentHook.hpp
    horselib/GameImGui/Toast.hpp
    horselib/GameImGui/WndProcHook.hpp
    horselib/GameImGui/GamepadInput.hpp
    horselib/GameImGui/XInputHook.hpp
)

set(HORSELIB_HEADERS
    horselib/ModSettings.hpp
    horselib/ResetOverride.hpp
    horselib/SetStartPositionHook.hpp
    horselib/OnlineRules.hpp
    horselib/LuxBattleLauncherStartHook.hpp
    horselib/HasSubProviderEntryHook.hpp
)

set(HORSELIB_SOURCES
    horselib/deterministic/DeterministicHookSet.cpp
    horselib/deterministic/Sc6CandidateCheckpointCapture.cpp
    horselib/deterministic/Sc6ReplayRuntime.cpp
)

set(HORSEMOD_IMGUIFILEDIALOG_SOURCES
    third_party/ImGuiFileDialog/ImGuiFileDialog.cpp
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

set(HORSEMOD_SOURCE_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE HORSEMOD_SOURCE_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

add_library(${TARGET} SHARED
    dllmain.cpp
    ${SCVI_HEADERS}
    ${HORSELIB_GAMEIMGUI_HEADERS}
    ${HORSELIB_HEADERS}
    ${HORSELIB_SOURCES}
    ${HORSEMOD_IMGUIFILEDIALOG_SOURCES}
)

add_library(ReplayQualificationMod SHARED EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/ReplayQualificationMod.cpp
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/ReplayPayloadImporter.cpp
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/ReplaySceneNavigator.cpp
)
target_include_directories(ReplayQualificationMod PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod
)
target_compile_features(ReplayQualificationMod PRIVATE cxx_std_20)
target_compile_definitions(ReplayQualificationMod PRIVATE
    REPLAY_QUALIFICATION_SOURCE_COMMIT="${HORSEMOD_SOURCE_COMMIT}"
)
target_link_libraries(ReplayQualificationMod PRIVATE UE4SS)
if(MSVC)
    set_property(TARGET ReplayQualificationMod PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(DeterministicCoreSelfTest
    ${CMAKE_SOURCE_DIR}/tools/deterministic_core_selftest.cpp
)
target_link_libraries(DeterministicCoreSelfTest PRIVATE HorseDeterministicCore)
target_compile_features(DeterministicCoreSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET DeterministicCoreSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(NAME DeterministicCoreSelfTest COMMAND $<TARGET_FILE:DeterministicCoreSelfTest>)
set_tests_properties(DeterministicCoreSelfTest PROPERTIES
    LABELS "deterministic;fast"
    TIMEOUT 60
)

add_executable(NativeCandidateRegionsSelfTest
    ${CMAKE_SOURCE_DIR}/tools/native_candidate_regions_selftest.cpp
)
target_link_libraries(NativeCandidateRegionsSelfTest PRIVATE HorseDeterministicCore)
target_compile_features(NativeCandidateRegionsSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET NativeCandidateRegionsSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(
    NAME NativeCandidateRegionsSelfTest
    COMMAND $<TARGET_FILE:NativeCandidateRegionsSelfTest>
)

add_executable(OnlineCoordinatorSelfTest
    ${CMAKE_SOURCE_DIR}/tools/online_coordinator_selftest.cpp
)
target_link_libraries(OnlineCoordinatorSelfTest PRIVATE HorseDeterministicCore)
target_compile_features(OnlineCoordinatorSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET OnlineCoordinatorSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(
    NAME OnlineCoordinatorSelfTest
    COMMAND $<TARGET_FILE:OnlineCoordinatorSelfTest>
)
set_tests_properties(OnlineCoordinatorSelfTest PROPERTIES
    LABELS "deterministic;online;fast"
    TIMEOUT 60
)
set_tests_properties(NativeCandidateRegionsSelfTest PROPERTIES
    LABELS "deterministic;fast"
    TIMEOUT 60
)

add_executable(DeterministicSchemaExporter
    ${CMAKE_SOURCE_DIR}/tools/deterministic_schema_exporter.cpp
)
target_link_libraries(DeterministicSchemaExporter PRIVATE HorseDeterministicCore)
target_compile_features(DeterministicSchemaExporter PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET DeterministicSchemaExporter PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set(HORSEMOD_GENERATED_SCHEMA
    "${CMAKE_CURRENT_BINARY_DIR}/generated/deterministic_contract.json")
add_custom_command(
    OUTPUT "${HORSEMOD_GENERATED_SCHEMA}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND $<TARGET_FILE:DeterministicSchemaExporter> "${HORSEMOD_GENERATED_SCHEMA}"
    DEPENDS DeterministicSchemaExporter
    VERBATIM
)
add_custom_target(DeterministicSchema ALL DEPENDS "${HORSEMOD_GENERATED_SCHEMA}")

target_compile_definitions(${TARGET} PRIVATE
    HORSE_ENABLE_GEKKONET=$<BOOL:${HORSEMOD_BUILD_PINNED_GEKKONET}>
    HORSEMOD_SOURCE_COMMIT="${HORSEMOD_SOURCE_COMMIT}"
    HORSEMOD_VERSION="${HORSEMOD_VERSION}"
)

target_include_directories(${TARGET} PRIVATE
    .
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuiFileDialog
)

target_link_libraries(${TARGET} PUBLIC UE4SS)
target_link_libraries(${TARGET} PRIVATE
    HorseDeterministicCore dxgi d3d11 delayimp ws2_32 bcrypt
)
if(HORSEMOD_BUILD_PINNED_GEKKONET AND TARGET GekkoNet)
    target_link_libraries(${TARGET} PRIVATE GekkoNet)
endif()

target_link_options(${TARGET} PRIVATE
    /DELAYLOAD:d3d11.dll
    /DELAYLOAD:dxgi.dll
    /DELAYLOAD:d3dcompiler_47.dll
    /DELAYLOAD:xinput1_4.dll
    /MAP
)

set(HORSEMOD_REPLAY_FUNCTION_MAP
    "${CMAKE_CURRENT_SOURCE_DIR}/debug/SoulcaliburVI_function_map.tsv"
)
if(EXISTS "${HORSEMOD_REPLAY_FUNCTION_MAP}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HORSEMOD_REPLAY_FUNCTION_MAP}"
            "$<TARGET_FILE_DIR:${TARGET}>/SoulcaliburVI_function_map.tsv"
        COMMENT "Copying replay function map next to HorseMod.dll"
    )
endif()
