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
target_link_libraries(dsp-tcp PUBLIC
    dsp-headers
    dsp-profiler
    nova

    Boost::headers
    spdlog::spdlog_header_only
)

code_analysis(dsp-tcp PRIVATE)
