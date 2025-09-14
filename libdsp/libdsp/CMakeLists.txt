file(GLOB_RECURSE DSP_HEADER_FILES "*.hpp")

add_library(dsp-headers INTERFACE)
target_sources(dsp-headers INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${LIBDSP_PROJECT_ROOT}"
    FILES "${DSP_HEADER_FILES}"
)

target_include_directories(dsp-headers INTERFACE
    "$<BUILD_INTERFACE:${LIBDSP_PROJECT_ROOT}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(dsp-tcp tcp.cpp)
target_link_libraries(dsp-tcp PUBLIC
    dsp-headers
    dsp-profiler
    nova::nova

    Boost::headers
    spdlog::spdlog_header_only
)

code_analysis(dsp-tcp PRIVATE)

if(PROFILING)
    add_library(dsp-profiler profiler.cpp)

    target_link_libraries(dsp-profiler PRIVATE
        dsp-headers
        nova::nova

        spdlog::spdlog_header_only
        Tracy::TracyClient
    )

    target_include_directories(dsp-profiler PUBLIC "${DSP_DEPS_INCLUDE_DIR}")
    code_analysis(dsp-profiler PRIVATE)
else()
    add_library(dsp-profiler INTERFACE)
endif()

add_library(dsp INTERFACE)
target_link_libraries(dsp INTERFACE
    dsp-headers
    dsp-tcp
    dsp-profiler

    nova::nova

    RdKafka::rdkafka++
    prometheus-cpp::core
    prometheus-cpp::pull
)

code_analysis(dsp INTERFACE)

include(test.cmake)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_test_target(router)

    find_package(benchmark REQUIRED)

    # add_bench_target(serializer)
endif()
