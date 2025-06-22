add_executable(tcp-client tcp-client.cc)
target_link_libraries(tcp-client PRIVATE dsp Boost::program_options)
code_analysis(tcp-client PRIVATE)

add_executable(kafka-client kafka-client.cc)
target_link_libraries(kafka-client PRIVATE dsp Boost::program_options)
code_analysis(kafka-client PRIVATE)

add_executable(grpc-server grpc-server.cc)
target_link_libraries(grpc-server PRIVATE proto-service dsp Boost::program_options)
code_analysis(grpc-server PRIVATE)

add_executable(grpc-client grpc-client.cc)
target_link_libraries(grpc-client PRIVATE proto-service dsp Boost::program_options)
code_analysis(grpc-client PRIVATE)
