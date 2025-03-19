add_library(dsp-headers INTERFACE)

file(GLOB_RECURSE DSP_HEADER_FILES "${CMAKE_CURRENT_LIST_DIR}/*.hh")

target_sources(dsp-headers
    PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    FILES
    ${DSP_HEADER_FILES}
)

target_link_libraries(dsp-headers INTERFACE
    project_warnings
)

target_include_directories(dsp-headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<INSTALL_INTERFACE:include>
)

code_analysis(dsp-headers INTERFACE)

add_library(dsp INTERFACE)

target_link_libraries(dsp INTERFACE
    dsp-headers
    dsp-tcp

    nova
    nova-deps

    RdKafka::rdkafka++
    prometheus-cpp::core
    prometheus-cpp::pull
)
