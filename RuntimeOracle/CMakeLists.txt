set(TARGET RuntimeOracle)

set(RUNTIME_ORACLE_SOURCE_DIR
    "E:/DevShitPosts/caliburRE/ue4ss/RuntimeOracle"
)

set(RUNTIME_ORACLE_SOURCES
    "${RUNTIME_ORACLE_SOURCE_DIR}/RuntimeOracleDllMain.cpp"
    "${RUNTIME_ORACLE_SOURCE_DIR}/RuntimeOracleHook.cpp"
)

if(EXISTS "${RUNTIME_ORACLE_SOURCE_DIR}/RuntimeOracleDllMain.cpp"
   AND EXISTS "${RUNTIME_ORACLE_SOURCE_DIR}/RuntimeOracleHook.cpp")
    add_library(${TARGET} SHARED
        ${RUNTIME_ORACLE_SOURCES}
    )

    target_link_libraries(${TARGET} PUBLIC UE4SS)

    target_link_options(${TARGET} PRIVATE
        /MAP
    )
else()
    message(STATUS
        "RuntimeOracle external sources not found; skipping RuntimeOracle")
endif()
