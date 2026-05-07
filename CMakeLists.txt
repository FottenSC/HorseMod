cmake_minimum_required(VERSION 3.22)
project(MyMods)

# ---------------------------------------------------------------------------
# Dev-iteration knobs.  These shave dozens of seconds off `build_horse_mod.bat`
# without affecting release builds (which set neither flag).
#
# MYMODS_FAST_DEV   ON  -> turn off LTO on UE4SS so HorseMod link is fast.
# FETCHCONTENT_BASE_DIR    -> persist cloned deps OUTSIDE the repo entirely,
#                             at %LOCALAPPDATA%\HorseMod\fetched_deps.  This
#                             survives any repo cleanup (`git clean -fdx`,
#                             OneDrive resync, manual `rm -rf .fetched_deps`)
#                             that previously caused a full re-clone +
#                             3-minute UE4SS rebuild.  Re-clone happens
#                             because git checkout sets fresh mtimes on every
#                             cloned header, and ninja's depfile sees those
#                             mtimes as input changes for every UE4SS .obj.
#                             Per-user cache; portable across machines (each
#                             user's LOCALAPPDATA resolves locally).
# FETCHCONTENT_UPDATES_DISCONNECTED  ON  -> once a dep is populated, never
#                             contact the remote again.  Kills the per-configure
#                             `git fetch` that iconfontcppheaders triggers
#                             (its declared GIT_TAG is `main`, a moving branch).
# FETCHCONTENT_FULLY_DISCONNECTED    ON when deps already populated -> bypass
#                             the populate step entirely.  Without this, every
#                             nuked-build-dir rebuild causes:
#                                cmake -B regenerates `<dep>-populate-gitinfo.txt`
#                                with new mtime → ExternalProject's download stamp
#                                (older mtime) is now stale → it RM-RFs `<dep>-src/`
#                                and re-clones from network → fresh mtimes on every
#                                dep header → ninja re-builds every UE4SS .obj that
#                                #includes those headers (~360 of them) → 3-min
#                                rebuild for what should have been a fast incremental.
#                             FULLY_DISCONNECTED skips all populate logic, leaving
#                             `<dep>-src/` mtimes alone.  Gated on a sentinel
#                             (glaze-src exists) so a first-time clean build still
#                             populates normally; subsequent build-dir wipes reuse.
# ---------------------------------------------------------------------------
option(MYMODS_FAST_DEV "Disable LTO on UE4SS for fast dev iteration" OFF)

# Resolve out-of-repo cache dir.  Fallback to <repo>/.fetched_deps if
# LOCALAPPDATA is unset (non-Windows host running CMake against this tree —
# unsupported config, but don't crash configure).
if(DEFINED ENV{LOCALAPPDATA})
    set(_horsemod_fc_base "$ENV{LOCALAPPDATA}/HorseMod/fetched_deps")
else()
    set(_horsemod_fc_base "${CMAKE_SOURCE_DIR}/.fetched_deps")
endif()
set(FETCHCONTENT_BASE_DIR "${_horsemod_fc_base}"
    CACHE PATH "Persistent clone location for FetchContent dependencies" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON
    CACHE BOOL "Skip FetchContent update step when source already populated" FORCE)

# Sentinel: glaze-src is the first FetchContent dep in deps/third/CMakeLists.
# If its CMakeLists.txt exists, the cache is populated → skip all populate.
if(EXISTS "${_horsemod_fc_base}/glaze-src/CMakeLists.txt")
    set(FETCHCONTENT_FULLY_DISCONNECTED ON
        CACHE BOOL "Bypass populate step (deps already cloned)" FORCE)
    message(STATUS "FetchContent: deps already populated, skipping populate step")
else()
    set(FETCHCONTENT_FULLY_DISCONNECTED OFF
        CACHE BOOL "First-time build: populate deps normally" FORCE)
    message(STATUS "FetchContent: first-time build, populating deps from remote")
endif()

add_subdirectory(RE-UE4SS)
add_subdirectory(HorseMod)

# Apply the dev-iteration LTO opt-out AFTER UE4SS has been declared so the
# target exists.  We deliberately don't edit RE-UE4SS/UE4SS/CMakeLists.txt —
# that's vendored upstream code we want to keep clean for git-pull updates.
if(MYMODS_FAST_DEV AND TARGET UE4SS)
    set_property(TARGET UE4SS PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
    message(STATUS "MYMODS_FAST_DEV: LTO disabled on UE4SS")
endif()
