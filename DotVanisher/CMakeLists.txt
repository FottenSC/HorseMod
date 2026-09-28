set(TARGET DotVanisher)

add_library(${TARGET} SHARED
    dllmain.cpp
    WatchRecovery.inl
    WatchRecoveryState.hpp
    WatchRecoverySites.hpp
)

# Keep the native adapter/ABI guards explicit in the build graph as well as
# compiler include dependencies (including after an isolated offline build).
set_property(SOURCE dllmain.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/WatchRecovery.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/WatchRecoveryState.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/WatchRecoverySites.hpp"
)

target_include_directories(${TARGET} PRIVATE .)

target_link_libraries(${TARGET} PUBLIC
    UE4SS
    bcrypt
)

target_link_options(${TARGET} PRIVATE
    /MAP
)
