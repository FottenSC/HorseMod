cmake_minimum_required(VERSION 3.22)
project(MyMods)

# ---------------------------------------------------------------------------
# Dev-iteration knobs.  These shave dozens of seconds off `build_horse_mod.bat`
# without affecting release builds (which set neither flag).
#
# MYMODS_FAST_DEV   ON  -> turn off LTO on UE4SS so HorseMod link is fast.
# FETCHCONTENT_BASE_DIR    -> persist cloned deps OUTSIDE the build tree so a
#                             nuked/rebuilt build dir doesn't re-clone ~30 MB
#                             of git history (glaze/glfw/imgui/.../fmt) every
#                             time.  Lives at <repo>/.fetched_deps.
# FETCHCONTENT_UPDATES_DISCONNECTED  ON  -> once a dep is populated, never
#                             contact the remote again.  Kills the per-configure
#                             `git fetch` that iconfontcppheaders triggers
#                             (its declared GIT_TAG is `main`, a moving branch).
# ---------------------------------------------------------------------------
option(MYMODS_FAST_DEV "Disable LTO on UE4SS for fast dev iteration" OFF)

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.fetched_deps"
    CACHE PATH "Persistent clone location for FetchContent dependencies" FORCE)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON
    CACHE BOOL "Skip FetchContent update step when source already populated" FORCE)

add_subdirectory(RE-UE4SS)
add_subdirectory(HorseMod)

# Apply the dev-iteration LTO opt-out AFTER UE4SS has been declared so the
# target exists.  We deliberately don't edit RE-UE4SS/UE4SS/CMakeLists.txt —
# that's vendored upstream code we want to keep clean for git-pull updates.
if(MYMODS_FAST_DEV AND TARGET UE4SS)
    set_property(TARGET UE4SS PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
    message(STATUS "MYMODS_FAST_DEV: LTO disabled on UE4SS")
endif()
