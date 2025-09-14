cmake_minimum_required(VERSION 3.24)

if(CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

include(version.cmake)
set(DSP_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/libdsp/dsp.hpp")
dsp_extract_version("${DSP_VERSION_FILE}")

project("dsp" VERSION ${DSP_VERSION} LANGUAGES CXX)
message(STATUS "DSP version ${DSP_VERSION}")

include(CTest)
find_package(nova REQUIRED)
include(novaCompilerWarnings)
include(novaCompilerSettings)

set(LIBDSP_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}")
add_subdirectory(libdsp)
