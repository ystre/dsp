cmake_minimum_required(VERSION 3.16)

project("dsp-super" LANGUAGES CXX)

option(BUILD_TESTS "Build tests" ON)
option(RUNTIME_ASSERTIONS "Enable asserts in release build" OFF)
option(NOVA_EXPERIMENTAL_FEATURE_SET "Experimental Nova" OFF)
option(LTO "Link Time Optimization" FALSE)
option(PROFILING "Enable profiling" OFF)
option(PROFILING_TRACE_LOGS "Enable trace logs, else Tracy Profiler" OFF)
option(MALLOC_PROFILING "Enable profiling malloc and their friends" OFF)

if(RUNTIME_ASSERTIONS)
    add_compile_definitions(NOVA_RUNTIME_ASSERTIONS)
endif()

find_package(nova REQUIRED)
include(novaCompilerWarnings)
include(novaCompilerSettings)

include(cmake/dependencies.cmake)

if(NOT PROFILING_CALLSTACK_DEPTH)
    set(PROFILING_CALLSTACK_DEPTH 20)
endif()

if(PROFILING)
    add_compile_definitions(
        TRACY_ENABLE
        TRACY_DELAYED_INIT
        TRACY_MANUAL_LIFETIME
        DSP_PROFILING
        DSP_PROFILING_CALLSTACK_DEPTH=${PROFILING_CALLSTACK_DEPTH}
    )

    if(MALLOC_PROFILING)
        add_compile_definitions(DSP_MALLOC_PROFILING)
    endif()

    if(PROFILING_TRACE_LOGS)
        add_compile_definitions(DSP_PROFILING_TRACE_LOGS)
    endif()

    add_subdirectory(deps/tracy)
endif()

add_subdirectory(libdsp)
add_subdirectory(svc)
add_subdirectory(dsp-tools)
