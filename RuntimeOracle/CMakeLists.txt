set(TARGET RuntimeOracle)

set(RUNTIME_ORACLE_SOURCE_DIR
    "E:/DevShitPosts/caliburRE/ue4ss/RuntimeOracle"
)

add_library(${TARGET} SHARED
    "${RUNTIME_ORACLE_SOURCE_DIR}/RuntimeOracleDllMain.cpp"
    "${RUNTIME_ORACLE_SOURCE_DIR}/RuntimeOracleHook.cpp"
)

target_link_libraries(${TARGET} PUBLIC UE4SS)

target_link_options(${TARGET} PRIVATE
    /MAP
)
