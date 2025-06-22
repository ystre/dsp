set(proto-message-def "${CMAKE_CURRENT_SOURCE_DIR}/message.proto")
set(proto-message-src "${CMAKE_CURRENT_BINARY_DIR}/message.pb.cc")
set(proto-message-header "${CMAKE_CURRENT_BINARY_DIR}/message.pb.h")
set(grpc-message-src "${CMAKE_CURRENT_BINARY_DIR}/message.grpc.pb.cc")
set(grpc-message-header "${CMAKE_CURRENT_BINARY_DIR}/message.grpc.pb.h")

add_custom_target(proto-debug
  COMMAND ${CMAKE_COMMAND} -E echo
  "protoc = $<TARGET_FILE_DIR:protobuf::protoc>"
  "protoc = ${PROTOC_PROGRAM}"
  "protoc = ${Protobuf_PROTOC_EXECUTABLE}"
)

add_library(proto-service
    service.proto
)

target_link_libraries(proto-service PRIVATE
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
)

target_include_directories(proto-service PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

protobuf_generate(TARGET proto-service)

protobuf_generate(
    TARGET proto-service
    LANGUAGE grpc
    PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
)
