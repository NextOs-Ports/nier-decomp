# nier - ReXGlue Recompiled Project
#
# SDK boilerplate lives in cmake/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(nier LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/rexglue.cmake)

# Sources
set(NIER_SOURCES
    src/main.cpp
    src/timing_probe.cpp
)

if(WIN32)
    add_executable(nier WIN32 ${NIER_SOURCES})
else()
    add_executable(nier ${NIER_SOURCES})
endif()

rexglue_setup_target(nier)

# Optional protocol-only backend for CPU boot diagnostics. It produces no image.
set(REXGLUE_PLUGIN_SOURCE "" CACHE PATH "Private ReXGlue source matching the SDK")
if(REXGLUE_PLUGIN_SOURCE)
    add_library(rexgpu-null SHARED
        src/diagnostic_null_gpu.cpp
        ${REXGLUE_PLUGIN_SOURCE}/src/graphics/graphics_system.cpp
        ${REXGLUE_PLUGIN_SOURCE}/src/graphics/command_processor.cpp
        ${REXGLUE_PLUGIN_SOURCE}/src/graphics/register_file.cpp
        ${REXGLUE_PLUGIN_SOURCE}/src/graphics/registers.cpp
        ${REXGLUE_PLUGIN_SOURCE}/src/graphics/flags.cpp)
    target_link_libraries(rexgpu-null PRIVATE rex::runtime)
    target_include_directories(rexgpu-null PRIVATE ${REXGLUE_PLUGIN_SOURCE}/thirdparty/renderdoc)
    target_link_options(rexgpu-null PRIVATE -Wl,--no-undefined)
    rexglue_apply_target_settings(rexgpu-null)
endif()

option(NIER_THINLTO "ThinLTO with the approved four-job link limit" OFF)
if(NIER_THINLTO)
    target_compile_options(nier PRIVATE -flto=thin)
    target_compile_options(nier_recomp PRIVATE -flto=thin)
    target_link_options(nier PRIVATE -fuse-ld=lld -flto=thin
        "-Wl,--thinlto-jobs=4"
        "-Wl,--thinlto-cache-dir=${CMAKE_CURRENT_BINARY_DIR}/thinlto-cache")
endif()
if(CMAKE_CROSSCOMPILING)
    set(REXGLUE_HOST_TOOL "" CACHE FILEPATH "Host rexglue code generator")
    if(REXGLUE_HOST_TOOL)
        set_target_properties(rex::rexglue PROPERTIES IMPORTED_LOCATION_RELEASE "${REXGLUE_HOST_TOOL}"
            IMPORTED_LOCATION "${REXGLUE_HOST_TOOL}")
    endif()
endif()

# The SDK helper provides $ORIGIN but only stages the shared runtime on macOS.
if(UNIX AND NOT APPLE)
    add_custom_command(TARGET nier POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:nier>)
endif()

option(NIER_BUILD_LIFECYCLE_PROBE "Build synthetic SDL event integration check" OFF)
if(NIER_BUILD_LIFECYCLE_PROBE)
    add_executable(nier_lifecycle_probe tests/lifecycle_probe.cpp)
    target_include_directories(nier_lifecycle_probe PRIVATE src)
    target_link_libraries(nier_lifecycle_probe PRIVATE rex::runtime)
endif()
