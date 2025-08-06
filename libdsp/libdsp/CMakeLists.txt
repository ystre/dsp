if(PROFILING)
    add_library(dsp-profiler profiler.cc)

    target_link_libraries(dsp-profiler PRIVATE
        dsp-headers
        nova

        spdlog::spdlog_header_only
        Tracy::TracyClient
    )

    target_include_directories(dsp-profiler PUBLIC "${DSP_DEPS_INCLUDE_DIR}")
    code_analysis(dsp-profiler PRIVATE)
else()
    add_library(dsp-profiler INTERFACE)
endif()

add_library(dsp-tcp tcp.cc)
target_link_libraries(dsp-tcp PRIVATE
    dsp-headers
    dsp-profiler
    nova

    Boost::headers
    spdlog::spdlog_header_only
)

code_analysis(dsp-tcp PRIVATE)

add_library(dsp INTERFACE)

target_link_libraries(dsp INTERFACE
    dsp-headers
    dsp-tcp

    # nova
    # nova-deps

    RdKafka::rdkafka++
    prometheus-cpp::core
    prometheus-cpp::pull
)

file(GLOB_RECURSE DSP_HEADER_FILES "${CMAKE_CURRENT_LIST_DIR}/*.hh")

target_sources(dsp
    PUBLIC
    FILE_SET dsp_headers
    TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    FILES
    ${DSP_HEADER_FILES}
)

# target_include_directories(dsp-headers INTERFACE
    # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    # $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
# )

install(TARGETS
    dsp
    dsp-tcp
    dsp-profiler
    project_warnings
    nova
    nova-deps

    EXPORT DspTargets
    FILE_SET dsp_headers
)

install(EXPORT DspTargets
    FILE DspTargets.cmake
    NAMESPACE Dsp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dsp"
)
