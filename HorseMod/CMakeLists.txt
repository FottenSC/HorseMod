set(TARGET HorseMod)

# Remove retired integration settings from the reused build cache.
unset(HORSEMOD_ENABLE_GEKKONET CACHE)
unset(HORSEMOD_BUILD_PINNED_GEKKONET CACHE)
unset(HORSEMOD_BUILD_OBSERVER_PROBE CACHE)
unset(HORSEMOD_GEKKONET_GIT_COMMIT CACHE)

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
    horselib/deterministic/ParticlePresentation.cpp
    horselib/deterministic/PresentationJournal.cpp
    horselib/deterministic/ReplayCoordinator.cpp
    horselib/deterministic/ReplaySeekPlanner.cpp
    horselib/deterministic/Sc6ReplayNativeBridge.cpp
    horselib/deterministic/Sc6ReplayWorldState.cpp
    horselib/deterministic/Sc6ReplayVfxState.cpp
    horselib/deterministic/Sc6ReplayCpuEmitterState.cpp
    horselib/deterministic/Sc6ReplayObjectLease.cpp
    horselib/deterministic/Sc6ReplaySchedulerState.cpp
    horselib/deterministic/Sc6ReplaySchedulerState.Storage.cpp
    horselib/deterministic/Sc6StageCatalog.cpp
    horselib/deterministic/SimulationSession.cpp
    horselib/deterministic/SnapshotStore.cpp
    horselib/deterministic/StageBreakListenerDiagnostics.cpp
    horselib/deterministic/StageBreakPresentationIdentity.cpp
    horselib/deterministic/StagePresentation.cpp
    horselib/deterministic/StageWindGraphTransaction.cpp
    horselib/deterministic/StageWindTopology.cpp
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
    horselib/GameImGui/ReplayOutputWindow.hpp
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
    horselib/deterministic/Sc6ReplayExecutor.cpp
    horselib/deterministic/Sc6ReplayVfxHandlerState.hpp
    horselib/deterministic/ReplayVfxHandlerStorage.hpp
    horselib/deterministic/Sc6ReplayHost.cpp
    horselib/deterministic/Sc6ReplayParticleCopy.cpp
    horselib/deterministic/Sc6ReplayWorld.cpp
    horselib/deterministic/Sc6ReplayTaskGroup.cpp
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

# Compile the bounded equality shader offline; never invoke a shader compiler
# during a rollback update. The generated bytecode is a retained build input.
find_program(HORSEMOD_FXC fxc REQUIRED)
set(HORSEMOD_EQUALITY_SHADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ReplayGpuImageEqualityShader.hpp")
set(HORSEMOD_EXPAND_SHADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ReplayGpuImageExpandShader.hpp")
add_custom_command(OUTPUT "${HORSEMOD_EQUALITY_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${HORSEMOD_FXC}" /nologo /T cs_5_0 /E main /O3
        /Fh "${HORSEMOD_EQUALITY_SHADER}" /Vn replay_gpu_image_equality_shader
        "${CMAKE_CURRENT_SOURCE_DIR}/horselib/deterministic/ReplayGpuImageEquality.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/horselib/deterministic/ReplayGpuImageEquality.hlsl"
    VERBATIM)
add_custom_command(OUTPUT "${HORSEMOD_EXPAND_SHADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${HORSEMOD_FXC}" /nologo /T cs_5_0 /E main /O3
        /Fh "${HORSEMOD_EXPAND_SHADER}" /Vn replay_gpu_image_expand_shader
        "${CMAKE_CURRENT_SOURCE_DIR}/horselib/deterministic/ReplayGpuImageExpand.hlsl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/horselib/deterministic/ReplayGpuImageExpand.hlsl"
    VERBATIM)
add_custom_target(ReplayGpuImageEqualityShader DEPENDS "${HORSEMOD_EQUALITY_SHADER}" "${HORSEMOD_EXPAND_SHADER}")
add_dependencies(${TARGET} ReplayGpuImageEqualityShader)

add_library(ReplayQualificationMod SHARED EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_mod/ReplayQualificationMod.cpp
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
add_dependencies(DeterministicCoreSelfTest ReplayGpuImageEqualityShader)
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

add_executable(ReplayQualificationHealthSelfTest
    ${CMAKE_SOURCE_DIR}/tools/replay_qualification_health_selftest.cpp
)
target_include_directories(ReplayQualificationHealthSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/tools
)
target_compile_features(ReplayQualificationHealthSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET ReplayQualificationHealthSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(NAME ReplayQualificationHealthSelfTest
    COMMAND $<TARGET_FILE:ReplayQualificationHealthSelfTest>)
set_tests_properties(ReplayQualificationHealthSelfTest PROPERTIES
    LABELS "deterministic;fast"
    TIMEOUT 60
)

add_executable(NativeCandidateRegionsSelfTest
    horselib/deterministic/Sc6ReplayExecutor.cpp
    horselib/deterministic/Sc6ReplayTaskGroup.cpp
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/deps/first/Unreal/src/VersionedContainer/Flags412.cpp
    ${CMAKE_SOURCE_DIR}/tools/native_candidate_regions_selftest.cpp
)
target_link_libraries(NativeCandidateRegionsSelfTest PRIVATE HorseDeterministicCore)
target_include_directories(NativeCandidateRegionsSelfTest PRIVATE
    ${CMAKE_SOURCE_DIR}/RE-UE4SS/deps/first/Unreal/include)
if(WIN32)
    target_link_libraries(NativeCandidateRegionsSelfTest PRIVATE d3d11)
endif()
target_compile_features(NativeCandidateRegionsSelfTest PRIVATE cxx_std_20)
if(MSVC)
    set_property(TARGET NativeCandidateRegionsSelfTest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
add_test(
    NAME NativeCandidateRegionsSelfTest
    COMMAND $<TARGET_FILE:NativeCandidateRegionsSelfTest>
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
    HorseDeterministicCore dxgi d3d11 delayimp bcrypt
)
set(HORSEMOD_ITERATION_TARGETS
    ${TARGET}
    ReplayQualificationMod
    DeterministicSchema
    DeterministicCoreSelfTest
    ReplayQualificationHealthSelfTest
    NativeCandidateRegionsSelfTest
)
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
