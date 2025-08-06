cmake_minimum_required(VERSION 3.16)

set(ROOT_DIR "${CMAKE_HOME_DIRECTORY}/..")

include(cmake/version.cmake)
dsp_extract_version()

project("dsp" VERSION ${DSP_VERSION} LANGUAGES CXX)
message(STATUS "DSP version ${DSP_VERSION}")

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

include("${ROOT_DIR}/cmake/compiler-warnings.cmake")
include("${ROOT_DIR}/cmake/dependencies.cmake")
include("${ROOT_DIR}/cmake/settings.cmake")
# include("${ROOT_DIR}/cmake/profiling.cmake")

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

    add_subdirectory("${ROOT_DIR}/deps/tracy" tracy)
endif()

add_subdirectory("${ROOT_DIR}/deps/nova-cpp/include" nova)

include(GNUInstallDirs)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(libdsp)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/DspConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dsp"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DspConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DspConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dsp"
)
