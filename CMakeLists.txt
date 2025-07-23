cmake_minimum_required(VERSION 3.16)

include(cmake/version.cmake)
dsp_extract_version()

project("dsp" VERSION ${DSP_VERSION} LANGUAGES CXX)
message(STATUS "DSP version ${DSP_VERSION}")

option(BUILD_TESTS "Build tests" ON)
option(RUNTIME_ASSERTIONS "Enable asserts in release build" OFF)
option(NOVA_EXPERIMENTAL_FEATURE_SET "Experimental Nova" OFF)
option(LTO "Link Time Optimization" FALSE)
option(PROFILING "Enable Tracy Profiler" OFF)
option(MALLOC_PROFILING "Enable profiling malloc and their friends" OFF)

if(RUNTIME_ASSERTIONS)
    add_compile_definitions(NOVA_RUNTIME_ASSERTIONS)
endif()

include(cmake/compiler-warnings.cmake)
include(cmake/dependencies.cmake)
include(cmake/settings.cmake)

if(PROFILING)
    add_compile_definitions(
        TRACY_ENABLE
        TRACY_DELAYED_INIT
        TRACY_MANUAL_LIFETIME
        DSP_PROFILING
    )

    if(MALLOC_PROFILING)
        add_compile_definitions(DSP_MALLOC_PROFILING)
    endif()

    add_subdirectory(deps/tracy)
endif()

add_subdirectory(deps/nova-cpp/include)
add_subdirectory(include)
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(unit-tests)
endif()
