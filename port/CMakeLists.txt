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
)

if(WIN32)
    add_executable(nier WIN32 ${NIER_SOURCES})
else()
    add_executable(nier ${NIER_SOURCES})
endif()

rexglue_setup_target(nier)
