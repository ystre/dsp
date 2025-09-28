function(add_tool NAME)
    add_executable(${NAME} ${NAME}.cpp ${ARGN})
    target_link_libraries(${NAME} PRIVATE dsp Boost::program_options)
    code_analysis(${NAME} PRIVATE)
endfunction()

add_tool(tcp-client)
add_tool(kafka-client)
add_tool(ws-server)
add_tool(ws-client)

# FIXME: Protoc version mismatch due to distro managed Protobuf version.
# add_subdirectory(protos)
# add_tool(grpc-server)
# add_tool(grpc-client)
# target_link_libraries(grpc-server PRIVATE proto-service)
# target_link_libraries(grpc-client PRIVATE proto-service)

add_tool(tracy-sandbox tracy_alloc.cpp)
target_link_libraries(tracy-sandbox PRIVATE dsp-profiler)
target_include_directories(tracy-sandbox PRIVATE "${DSP_DEPS_INCLUDE_DIR}")
