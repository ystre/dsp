cmake_minimum_required(VERSION 3.16)

include(cmake/version.cmake)
dsp_extract_version()

project("dsp" VERSION ${DSP_VERSION} LANGUAGES CXX)
message(STATUS "DSP version ${DSP_VERSION}")

option(BUILD_TESTS "Build tests" ON)
option(RUNTIME_ASSERTIONS "Enable asserts in release build" OFF)
option(NOVA_EXPERIMENTAL_FEATURE_SET "Experimental Nova" OFF)
option(LTO "Link Time Optimization" FALSE)

if(RUNTIME_ASSERTIONS)
    add_compile_definitions(NOVA_RUNTIME_ASSERTIONS)
endif()

include(cmake/compiler-warnings.cmake)
include(cmake/dependencies.cmake)
include(cmake/settings.cmake)

include(deps/nova-cpp/cmake/targets.cmake)
include(cmake/targets.cmake)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(unit-tests)
endif()
