# Micro-benchmarks: every `*.bench.cpp` next to a module's sources is compiled
# into `bench-<module>` (Google Benchmark). The `bench` target builds them
# all; `ci/micro-bench.sh` runs every `bench-*` executable and stores JSON
# output for tools/bench/compare.py.

option(PROJ_BUILD_BENCH "Build micro-benchmarks" ON)

if(PROJ_BUILD_BENCH)
    find_package(benchmark REQUIRED)
    add_custom_target(bench)
endif()

function(add_bench_target MODULE_NAME)
    if(NOT PROJ_BUILD_BENCH)
        return()
    endif()

    set(target "bench-${MODULE_NAME}")
    file(GLOB_RECURSE BENCH_SOURCES CONFIGURE_DEPENDS "*.bench.cpp")
    add_executable("${target}" ${BENCH_SOURCES})

    target_link_libraries(${target} PRIVATE
        ${MODULE_NAME}
        proj_warnings

        benchmark::benchmark_main
    )
    add_dependencies(bench "${target}")
endfunction()
