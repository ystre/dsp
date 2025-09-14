file(GLOB_RECURSE DSP_RT_HEADER_FILES "*.hpp")

add_library(dsp-svc-headers INTERFACE)
target_sources(dsp-svc-headers INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${DSP_RT_PROJECT_ROOT}"
    FILES "${DSP_RT_HEADER_FILES}"
)

target_include_directories(dsp-svc-headers INTERFACE
    "$<BUILD_INTERFACE:${DSP_SVC_PROJECT_ROOT}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_executable(svc
    main.cpp
    handler.cpp
)

target_link_libraries(svc PRIVATE dsp dsp-profiler dsp-svc-headers)
target_include_directories(svc PRIVATE "${DSP_DEPS_INCLUDE_DIR}")
code_analysis(svc PRIVATE)

add_executable(sim sim.cpp)
target_link_libraries(sim PRIVATE dsp Boost::program_options dsp-svc-headers)
code_analysis(sim PRIVATE)

install(TARGETS svc)
