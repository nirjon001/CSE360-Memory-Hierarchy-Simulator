cmake_minimum_required(VERSION 3.20)
project(memory_hierarchy_simulator
        VERSION 1.0.0
        DESCRIPTION "CSE360 memory hierarchy simulator - CLI and GUI front-ends"
        LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_GUI "Build the Raylib pyramid-dashboard simulator (sim_gui)" ON)
option(BUILD_IMGUI_GUI "Build the legacy Dear ImGui / SDL3 dashboard (sim_gui_imgui)" OFF)

# ---------------------------------------------------------------------------
# Shared core: cache math, hierarchy, trace handling, orchestration helpers.
# The original .c sources are intentionally compiled with the C++ compiler:
# they are valid C++17 as-is, so no source file had to be changed.
# ---------------------------------------------------------------------------
set(SIM_CORE_SOURCES
    src/cache.c
    src/hierarchy.c
    src/trace.c
    src/sim_runner.c)

set_source_files_properties(${SIM_CORE_SOURCES} PROPERTIES LANGUAGE CXX)

add_library(sim_core STATIC ${SIM_CORE_SOURCES})
target_include_directories(sim_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(sim_core PUBLIC _GNU_SOURCE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sim_core PRIVATE -Wall -Wextra)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # GCC's inliner warns about level_amat()'s recursive bound at -O3 even
    # though i+1 is guarded by i != nlevels-1. The core source stays untouched.
    target_compile_options(sim_core PRIVATE -Wno-array-bounds)
  endif()
endif()

# ---------------------------------------------------------------------------
# sim_cli - the original terminal program (src/main.c), unchanged behaviour
# ---------------------------------------------------------------------------
set(SIM_CLI_SOURCES src/main.c)
set_source_files_properties(${SIM_CLI_SOURCES} PROPERTIES LANGUAGE CXX)

add_executable(sim_cli ${SIM_CLI_SOURCES})
target_link_libraries(sim_cli PRIVATE sim_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(sim_cli PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# sim_gui - Raylib pyramid dashboard, built on the same sim_core.
# Raylib is fetched at configure time when it is not installed system-wide.
# ---------------------------------------------------------------------------
if(BUILD_GUI)
  include(FetchContent)

  # A previous ImGui configure may have cached OPENGL_VERSION from pkg-config
  # (e.g. "26.2.5"); Raylib expects one of its own enum values and would fail.
  unset(OPENGL_VERSION CACHE)
  unset(OPENGL_VERSION)

  # Keep Raylib's own extras out of the build.
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

  # Wayland-only environments have no DISPLAY; point GLFW at Wayland there.
  # On a normal X11 desktop Raylib's own defaults are left alone.
  if(DEFINED ENV{WAYLAND_DISPLAY} AND NOT DEFINED ENV{DISPLAY})
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
  endif()

  find_package(raylib 5.0 QUIET)

  if(NOT raylib_FOUND)
    message(STATUS "Raylib not found system-wide; fetching v5.5 with FetchContent...")
    FetchContent_Declare(raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG 5.5
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(raylib)
  endif()

  add_executable(sim_gui gui/main.cpp)
  target_link_libraries(sim_gui PRIVATE sim_core raylib)

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sim_gui PRIVATE -Wall -Wextra)
  endif()

  message(STATUS "sim_gui enabled (Raylib 5.5 pyramid dashboard)")
endif()

# ---------------------------------------------------------------------------
# sim_gui_imgui - the previous dashboard, kept as an optional target so no
# work is lost. Off by default; enable with -DBUILD_IMGUI_GUI=ON.
# ---------------------------------------------------------------------------
if(BUILD_IMGUI_GUI)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
    pkg_check_modules(GLPKG IMPORTED_TARGET gl)
  endif()

  if(TARGET PkgConfig::SDL3 AND TARGET PkgConfig::GLPKG)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)

    add_executable(sim_gui_imgui
        gui/imgui_main.cpp
        ${IMGUI_SOURCES})

    target_include_directories(sim_gui_imgui PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends)

    target_link_libraries(sim_gui_imgui PRIVATE
        sim_core
        PkgConfig::SDL3
        PkgConfig::GLPKG)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(sim_gui_imgui PRIVATE -Wall -Wextra)
    endif()

    message(STATUS "sim_gui_imgui enabled (legacy Dear ImGui + SDL3 + OpenGL)")
  else()
    message(WARNING
      "SDL3 or OpenGL development files not found; sim_gui_imgui skipped. "
      "Use -DBUILD_IMGUI_GUI=OFF to silence this warning.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
set(SIM_TARGETS "sim_cli")
if(TARGET sim_gui)
  string(APPEND SIM_TARGETS " sim_gui")
endif()
if(TARGET sim_gui_imgui)
  string(APPEND SIM_TARGETS " sim_gui_imgui")
endif()

message(STATUS "Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "Targets       : ${SIM_TARGETS}")
message(STATUS "Binaries      : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
