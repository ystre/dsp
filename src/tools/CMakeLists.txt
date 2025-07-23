function(add_tool NAME)
    add_executable(${NAME} ${NAME}.cc ${ARGN})
    target_link_libraries(${NAME} PRIVATE dsp Boost::program_options)
    code_analysis(${NAME} PRIVATE)
endfunction()

add_tool(tcp-client)
add_tool(kafka-client)

add_tool(tracy-sandbox tracy_alloc.cc)
target_link_libraries(tracy-sandbox PRIVATE dsp-profiler)
target_include_directories(tracy-sandbox PRIVATE "${DSP_DEPS_INCLUDE_DIR}")
