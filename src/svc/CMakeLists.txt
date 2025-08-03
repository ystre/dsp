add_executable(svc
    main.cc
    handler.cc
)

target_link_libraries(svc PRIVATE dsp dsp-profiler)
target_include_directories(svc PRIVATE "${DSP_DEPS_INCLUDE_DIR}")
code_analysis(svc PRIVATE)

add_executable(sim sim.cc)
target_link_libraries(sim PRIVATE dsp Boost::program_options)
code_analysis(sim PRIVATE)

install(TARGETS svc)
