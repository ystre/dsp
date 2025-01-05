find_package(GTest REQUIRED)

include(CTest)
include(GoogleTest)

function(test_file TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})

    target_link_libraries(${TARGET_NAME} PRIVATE
        dsp

        GTest::gtest_main
        GTest::gtest
        GTest::gmock
    )

    code_analysis(${TARGET_NAME} PRIVATE)

    gtest_discover_tests(
        ${TARGET_NAME}
        DISCOVERY_MODE PRE_TEST
    )
endfunction()

test_file(test-router router.cc)
