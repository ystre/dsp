add_library(libproto INTERFACE)
target_include_directories(libproto INTERFACE "${PROJECT_SOURCE_DIR}/libproto")

add_test_target(libproto)
add_bench_target(libproto)
