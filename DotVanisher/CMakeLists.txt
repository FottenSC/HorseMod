set(TARGET DotVanisher)

add_library(${TARGET} SHARED
    dllmain.cpp
)

target_include_directories(${TARGET} PRIVATE .)

target_link_libraries(${TARGET} PUBLIC
    UE4SS
    bcrypt
)

target_link_options(${TARGET} PRIVATE
    /MAP
)
