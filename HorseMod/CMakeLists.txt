set(TARGET HorseMod)

file(GLOB_RECURSE SCVI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI/*.h"
)

add_library(${TARGET} SHARED
    dllmain.cpp
    ${SCVI_HEADERS}
)

target_include_directories(${TARGET} PRIVATE
    .
    ${CMAKE_CURRENT_SOURCE_DIR}/horselib
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SoulCaliburVI
)
target_link_libraries(${TARGET} PUBLIC UE4SS)

# Copy the DLL to the game's mod directory after building (optional, adjust path as needed)
# add_custom_command(TARGET ${TARGET} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET}> "path/to/game/Binaries/Win64/Mods/${TARGET}/dlls/"
# )