cmake_minimum_required(VERSION 3.24)

if(CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

project("svc" LANGUAGES CXX)

set(DSP_SVC_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}")
add_subdirectory(svc)
