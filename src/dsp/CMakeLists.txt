add_library(dsp-tcp tcp.cc)
target_link_libraries(dsp-tcp PUBLIC
    dsp-headers
    nova

    Boost::headers
    spdlog::spdlog_header_only
)

code_analysis(dsp-tcp PRIVATE)
