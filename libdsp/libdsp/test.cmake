function(add_test_target MODULE_NAME)
    set(target "test-${MODULE_NAME}")
    set(source_file "${MODULE_NAME}.test.cpp")
    string(REPLACE "-" "_" source_file ${source_file})
    add_executable("${target}" ${source_file})

    target_link_libraries(${target} PRIVATE
        dsp

        GTest::gtest_main
        GTest::gtest
        GTest::gmock
    )

    code_analysis(${target} PRIVATE)

    gtest_discover_tests(
        ${target}
        DISCOVERY_MODE PRE_TEST
    )
endfunction()

function(add_bench_target MODULE_NAME)
    set(target "bench-${MODULE_NAME}")
    set(source_file "${MODULE_NAME}.bench.cpp")
    string(REPLACE "-" "_" source_file ${source_file})
    add_executable("${target}" ${source_file})

    target_link_libraries(${target} PRIVATE dsp benchmark::benchmark)
    code_analysis(${target} PRIVATE)

    add_test(NAME ${target} COMMAND ${target} CONFIGURATIONS Release)
    set_property(TEST ${target} PROPERTY LABELS bench)
endfunction()
