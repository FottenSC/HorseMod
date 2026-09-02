set(TARGET HorseMod)

if(DEFINED HORSEMOD_ENABLE_GEKKONET)
    message(WARNING
        "HORSEMOD_ENABLE_GEKKONET is a retired rollback option and is ignored")
    unset(HORSEMOD_ENABLE_GEKKONET CACHE)
endif()
option(HORSEMOD_BUILD_PINNED_GEKKONET
    "Build the pinned GekkoNet integration after offline qualification" OFF)
option(HORSEMOD_BUILD_OBSERVER_PROBE
    "Build the observer-only online accessor qualification surface" ON)
set(HORSEMOD_GEKKONET_GIT_COMMIT
    "f21698d85367c8d97585cf7610897316c873681c"
    CACHE STRING "Immutable FottenSC/GekkoNet commit")
if(HORSEMOD_BUILD_PINNED_GEKKONET)
    set(HORSEMOD_GEKKONET_SOURCE_DIR
        "${CMAKE_SOURCE_DIR}/third_party/GekkoNet")
    if(NOT EXISTS "${HORSEMOD_GEKKONET_SOURCE_DIR}/GekkoLib/CMakeLists.txt")
        message(FATAL_ERROR
            "Pinned GekkoNet submodule is unavailable; run git submodule update --init")
    endif()
    find_package(Git REQUIRED)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${HORSEMOD_GEKKONET_SOURCE_DIR}"
        OUTPUT_VARIABLE HORSEMOD_GEKKONET_ACTUAL_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE HORSEMOD_GEKKONET_REV_PARSE_RESULT
    )
    if(NOT HORSEMOD_GEKKONET_REV_PARSE_RESULT EQUAL 0
        OR NOT HORSEMOD_GEKKONET_ACTUAL_COMMIT STREQUAL
            HORSEMOD_GEKKONET_GIT_COMMIT)
        message(FATAL_ERROR
            "GekkoNet must be exactly ${HORSEMOD_GEKKONET_GIT_COMMIT}; found ${HORSEMOD_GEKKONET_ACTUAL_COMMIT}")
    endif()
    set(NO_ASIO_BUILD ON CACHE BOOL "Exclude GekkoNet ASIO transport" FORCE)
    set(BUILD_DOCS OFF CACHE BOOL "Disable GekkoNet docs" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build GekkoNet statically" FORCE)
    add_subdirectory(
        "${HORSEMOD_GEKKONET_SOURCE_DIR}/GekkoLib"
        "${CMAKE_BINARY_DIR}/third_party/GekkoNet"
        EXCLUDE_FROM_ALL)
    if(TARGET GekkoNet AND MSVC)
        set_property(TARGET GekkoNet PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()

file(GLOB_RECURSE SCVI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI/*.h"
)

set(HORSEMOD_DETERMINISTIC_SOURCES
    horselib/deterministic/AudioPresentation.cpp
    horselib/deterministic/BattleAudioSelectorState.cpp
    horselib/deterministic/CanonicalHashTimeline.cpp
    horselib/deterministic/CallbackTopology.cpp
    horselib/deterministic/CandidateCheckpoint.cpp
    horselib/deterministic/CandidateGameStateAdapter.cpp
    horselib/deterministic/Config.cpp
    horselib/deterministic/FloatingPointEnvironment.cpp
    horselib/deterministic/HgCpuStream.cpp
    horselib/deterministic/HgCpuCoverageProbe.cpp
    horselib/deterministic/HgCpuRuntimeDiagnostics.cpp
    horselib/deterministic/MoveDispatchState.cpp
    horselib/deterministic/MotionBankSnapshot.cpp
    horselib/deterministic/SecondaryEventState.cpp
    horselib/deterministic/CharaAnimationState.cpp
    horselib/deterministic/NativeReplayMaterializer.cpp
    horselib/deterministic/InputTimeline.cpp
    horselib/deterministic/NativeBatchTimeline.cpp
    horselib/deterministic/NativeCandidateRegions.cpp
    horselib/deterministic/NativeAudioPresentationController.cpp
    horselib/deterministic/NativePresentationJournal.cpp
    horselib/deterministic/OnlineCoordinator.cpp
    horselib/deterministic/ParticlePresentation.cpp
    horselib/deterministic/PresentationJournal.cpp
    horselib/deterministic/ReplayCoordinator.cpp
    horselib/deterministic/ReplaySeekPlanner.cpp
    horselib/deterministic/Sc6ReplayNativeBridge.cpp
    horselib/deterministic/Sc6OnlineContractObserver.cpp
    horselib/deterministic/Sc6OnlineObserverProbe.cpp
    horselib/deterministic/Sc6StageCatalog.cpp
    horselib/deterministic/SimulationSession.cpp
    horselib/deterministic/SnapshotStore.cpp
    horselib/deterministic/StageBreakListenerDiagnostics.cpp
    horselib/deterministic/StageBreakPresentationIdentity.cpp
    horselib/deterministic/StagePresentation.cpp
    horselib/deterministic/StageWindGraphTransaction.cpp
    horselib/deterministic/StageWindTopology.cpp
    horselib/deterministic/SteamP2PTransport.cpp
    horselib/deterministic/UcrtRandBroker.cpp
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(HORSEMOD_PRODUCTION_CANDIDATE_MANIFEST
    "${CMAKE_SOURCE_DIR}/docs/investigations/deterministic-production-candidate-manifest.json")
set(HORSEMOD_GENERATED_CANDIDATE_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ProductionCandidateManifest.generated.hpp")
add_custom_command(
    OUTPUT "${HORSEMOD_GENERATED_CANDIDATE_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/generate_production_candidate_manifest.py"
        "${HORSEMOD_PRODUCTION_CANDIDATE_MANIFEST}"
        "${HORSEMOD_GENERATED_CANDIDATE_HEADER}"
    DEPENDS
        "${HORSEMOD_PRODUCTION_CANDIDATE_MANIFEST}"
        "${CMAKE_SOURCE_DIR}/tools/generate_production_candidate_manifest.py"
    VERBATIM
)
add_custom_target(ProductionCandidateManifest
    DEPENDS "${HORSEMOD_GENERATED_CANDIDATE_HEADER}")
set(HORSEMOD_PRODUCTION_REGION_MANIFEST
    "${CMAKE_SOURCE_DIR}/docs/investigations/deterministic-production-region-manifest.json")
set(HORSEMOD_GENERATED_REGIONS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ProductionRegions.generated.hpp")
add_custom_command(
    OUTPUT "${HORSEMOD_GENERATED_REGIONS_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/generate_production_regions.py"
        "${HORSEMOD_PRODUCTION_REGION_MANIFEST}"
        "${HORSEMOD_GENERATED_REGIONS_HEADER}"
    DEPENDS
        "${HORSEMOD_PRODUCTION_REGION_MANIFEST}"
        "${CMAKE_SOURCE_DIR}/tools/generate_production_regions.py"
    VERBATIM
)
add_custom_target(ProductionRegions
    DEPENDS "${HORSEMOD_GENERATED_REGIONS_HEADER}")

add_library(HorseDeterministicCore STATIC
    ${HORSEMOD_DETERMINISTIC_SOURCES}
)
add_dependencies(HorseDeterministicCore ProductionCandidateManifest)
add_dependencies(HorseDeterministicCore ProductionRegions)
target_include_directories(HorseDeterministicCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(HorseDeterministicCore PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(HorseDeterministicCore PUBLIC bcrypt)
endif()
if(MSVC)
    set_property(TARGET HorseDeterministicCore PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    # UE4SS uses named single-config shipping profiles that CMake does not
    # classify as Release. Keep the deterministic hot path optimized in every
    # non-Debug profile instead of silently compiling snapshot capture with
    # MSVC's unoptimized defaults.
    target_compile_options(HorseDeterministicCore PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:/O2>"
        "$<$<NOT:$<CONFIG:Debug>>:/Ob2>"
    )
    target_compile_definitions(HorseDeterministicCore PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:NDEBUG>"
    )
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
    horselib/deterministic/Sc6PresentationSink.cpp
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
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/OnlineRoomAutomation.cpp
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/ReplayPayloadImporter.cpp
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/ReplaySceneNavigator.cpp
)
target_include_directories(ReplayQualificationMod PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
add_dependencies(ReplayQualificationMod ProductionRegions)
target_compile_features(ReplayQualificationMod PRIVATE cxx_std_20)
target_compile_definitions(ReplayQualificationMod PRIVATE
    REPLAY_QUALIFICATION_SOURCE_COMMIT="${HORSEMOD_SOURCE_COMMIT}"
)
target_link_libraries(ReplayQualificationMod PRIVATE UE4SS HorseDeterministicCore)
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

add_executable(SteamP2PTransportSelfTest
    ${CMAKE_SOURCE_DIR}/tools/steam_p2p_transport_selftest.cpp
)
target_link_libraries(SteamP2PTransportSelfTest PRIVATE HorseDeterministicCore)
target_compile_features(SteamP2PTransportSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET SteamP2PTransportSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(
    NAME SteamP2PTransportSelfTest
    COMMAND $<TARGET_FILE:SteamP2PTransportSelfTest>
)
add_executable(Sc6OnlineContractObserverSelfTest
    ${CMAKE_SOURCE_DIR}/tools/sc6_online_contract_observer_selftest.cpp
)
target_link_libraries(Sc6OnlineContractObserverSelfTest
    PRIVATE HorseDeterministicCore)
target_compile_features(Sc6OnlineContractObserverSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET Sc6OnlineContractObserverSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(
    NAME Sc6OnlineContractObserverSelfTest
    COMMAND $<TARGET_FILE:Sc6OnlineContractObserverSelfTest>
)
add_executable(Sc6OnlineObserverProbeSelfTest
    ${CMAKE_SOURCE_DIR}/tools/sc6_online_observer_probe_selftest.cpp
)
target_link_libraries(Sc6OnlineObserverProbeSelfTest
    PRIVATE HorseDeterministicCore)
target_compile_features(Sc6OnlineObserverProbeSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET Sc6OnlineObserverProbeSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(
    NAME Sc6OnlineObserverProbeSelfTest
    COMMAND $<TARGET_FILE:Sc6OnlineObserverProbeSelfTest>
)
set_tests_properties(SteamP2PTransportSelfTest PROPERTIES
    LABELS "deterministic;online;fast"
    TIMEOUT 60
)
set_tests_properties(OnlineCoordinatorSelfTest PROPERTIES
    LABELS "deterministic;online;fast"
    TIMEOUT 60
)
set_tests_properties(Sc6OnlineContractObserverSelfTest PROPERTIES
    LABELS "deterministic;online;fast"
    TIMEOUT 60
)
set_tests_properties(Sc6OnlineObserverProbeSelfTest PROPERTIES
    LABELS "deterministic;online;observer-only;fast"
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

set(HORSEMOD_GENERATED_RELEASE_IDENTITIES
    "${CMAKE_CURRENT_BINARY_DIR}/generated/ProductionReleaseIdentities.generated.hpp")
add_custom_target(ProductionReleaseIdentities
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/generate_compiled_release_identities.py"
        "${CMAKE_SOURCE_DIR}"
        "${HORSEMOD_GENERATED_SCHEMA}"
        "${HORSEMOD_GENERATED_RELEASE_IDENTITIES}"
    DEPENDS
        "${HORSEMOD_GENERATED_SCHEMA}"
        "${CMAKE_SOURCE_DIR}/tools/generate_compiled_release_identities.py"
    BYPRODUCTS "${HORSEMOD_GENERATED_RELEASE_IDENTITIES}"
    VERBATIM
)
add_library(HorseProductionRelease STATIC
    horselib/deterministic/ProductionOnlineAllowlist.cpp
    horselib/deterministic/ProductionReleaseLoader.cpp
)
add_dependencies(HorseProductionRelease ProductionReleaseIdentities)
target_include_directories(HorseProductionRelease PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(HorseProductionRelease PUBLIC cxx_std_20)
target_link_libraries(HorseProductionRelease PUBLIC HorseDeterministicCore bcrypt)
if(MSVC)
    set_property(TARGET HorseProductionRelease PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
target_link_libraries(OnlineCoordinatorSelfTest PRIVATE HorseProductionRelease)
add_executable(ProductionReleaseLoaderSelfTest
    ${CMAKE_SOURCE_DIR}/tools/production_release_loader_selftest.cpp)
target_link_libraries(ProductionReleaseLoaderSelfTest PRIVATE
    HorseProductionRelease)
target_compile_features(ProductionReleaseLoaderSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET ProductionReleaseLoaderSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(NAME ProductionReleaseLoaderSelfTest
    COMMAND $<TARGET_FILE:ProductionReleaseLoaderSelfTest>
        "${HORSEMOD_GENERATED_SCHEMA}"
        "${HORSEMOD_PRODUCTION_CANDIDATE_MANIFEST}"
        "${HORSEMOD_PRODUCTION_REGION_MANIFEST}")
set_tests_properties(ProductionReleaseLoaderSelfTest PROPERTIES
    LABELS "deterministic;release;fast" TIMEOUT 60)

target_compile_definitions(${TARGET} PRIVATE
    HORSE_ENABLE_GEKKONET=$<BOOL:${HORSEMOD_BUILD_PINNED_GEKKONET}>
    HORSE_ENABLE_OBSERVER_PROBE=$<BOOL:${HORSEMOD_BUILD_OBSERVER_PROBE}>
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
    HorseDeterministicCore HorseProductionRelease dxgi d3d11 delayimp ws2_32 bcrypt
)
if(HORSEMOD_BUILD_PINNED_GEKKONET AND TARGET GekkoNet)
    add_library(HorseGekkoRollback STATIC
        horselib/deterministic/GekkoRollbackSession.cpp)
    target_include_directories(HorseGekkoRollback PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/horselib)
    target_compile_features(HorseGekkoRollback PUBLIC cxx_std_20)
    target_link_libraries(HorseGekkoRollback PUBLIC
        HorseDeterministicCore GekkoNet)
    if(MSVC)
        set_property(TARGET HorseGekkoRollback PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    target_link_libraries(${TARGET} PRIVATE HorseGekkoRollback)

    add_executable(GekkoRollbackSessionSelfTest
        ${CMAKE_SOURCE_DIR}/tools/gekko_rollback_session_selftest.cpp)
    target_link_libraries(GekkoRollbackSessionSelfTest PRIVATE
        HorseGekkoRollback)
    target_compile_features(GekkoRollbackSessionSelfTest PRIVATE cxx_std_20)
    if(MSVC)
        set_property(TARGET GekkoRollbackSessionSelfTest PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    add_test(NAME GekkoRollbackSessionSelfTest
        COMMAND $<TARGET_FILE:GekkoRollbackSessionSelfTest>)
    set_tests_properties(GekkoRollbackSessionSelfTest PROPERTIES
        LABELS "deterministic;online;fast" TIMEOUT 60)
endif()

# Dirty-tree iteration intentionally builds only the two runtime DLLs, the
# exported contract, and the eight deterministic self-tests. Configuration is
# kept separate so source identity is frozen only when explicitly requested.
set(HORSEMOD_ITERATION_TARGETS
    ${TARGET}
    ReplayQualificationMod
    DeterministicSchema
    DeterministicCoreSelfTest
    NativeCandidateRegionsSelfTest
    OnlineCoordinatorSelfTest
    SteamP2PTransportSelfTest
    Sc6OnlineContractObserverSelfTest
    Sc6OnlineObserverProbeSelfTest
    ProductionReleaseLoaderSelfTest
)
if(TARGET GekkoRollbackSessionSelfTest)
    list(APPEND HORSEMOD_ITERATION_TARGETS GekkoRollbackSessionSelfTest)
endif()
add_custom_target(DeterministicIterationTargets
    DEPENDS ${HORSEMOD_ITERATION_TARGETS})

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
