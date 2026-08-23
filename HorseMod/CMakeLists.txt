set(TARGET HorseMod)

file(GLOB_RECURSE SCVI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI/*.h"
)

set(HORSEMOD_DETERMINISTIC_SOURCES
    horselib/deterministic/Config.cpp
    horselib/deterministic/InputTimeline.cpp
    horselib/deterministic/PresentationJournal.cpp
    horselib/deterministic/ReplayCoordinator.cpp
    horselib/deterministic/SimulationSession.cpp
    horselib/deterministic/SnapshotStore.cpp
)

add_library(HorseDeterministicCore STATIC
    ${HORSEMOD_DETERMINISTIC_SOURCES}
)
target_include_directories(HorseDeterministicCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
)
target_compile_features(HorseDeterministicCore PUBLIC cxx_std_20)
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
    winhttp shell32 ws2_32 advapi32 ole32 propsys uuid
)
target_compile_definitions(HorseReplayLauncher PRIVATE
    HORSEMOD_VERSION="${HORSEMOD_VERSION}"
    HORSEMOD_VERSION_MAJOR=${HORSEMOD_VERSION_MAJOR}
    HORSEMOD_VERSION_MINOR=${HORSEMOD_VERSION_MINOR}
    HORSEMOD_VERSION_PATCH=${HORSEMOD_VERSION_PATCH}
)

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

target_compile_definitions(${TARGET} PRIVATE
    HORSE_ENABLE_GEKKONET=0
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

add_dependencies(${TARGET} HorseReplayLauncher)
