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

option(BUILD_GUI "Build the Dear ImGui / SDL3 graphical simulator" ON)

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
# sim_gui - new visual front-end built on the same sim_core
# ---------------------------------------------------------------------------
if(BUILD_GUI)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SDL3 IMPORTED_TARGET sdl3)
    pkg_check_modules(OPENGL IMPORTED_TARGET gl)
  endif()

  if(TARGET PkgConfig::SDL3 AND TARGET PkgConfig::OPENGL)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
    set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)

    add_executable(sim_gui
        gui/main.cpp
        ${IMGUI_SOURCES})

    target_include_directories(sim_gui PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends)

    target_link_libraries(sim_gui PRIVATE
        sim_core
        PkgConfig::SDL3
        PkgConfig::OPENGL)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(sim_gui PRIVATE -Wall -Wextra)
    endif()

    message(STATUS "sim_gui enabled (Dear ImGui 1.93.0 + SDL3 + OpenGL)")
  else()
    message(WARNING
      "SDL3 or OpenGL development files not found via pkg-config; "
      "sim_gui will not be built. Use -DBUILD_GUI=OFF to silence this warning.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
set(SIM_TARGETS "sim_cli")
if(TARGET sim_gui)
  string(APPEND SIM_TARGETS " sim_gui")
endif()

message(STATUS "Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "Targets       : ${SIM_TARGETS}")
message(STATUS "Binaries      : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
