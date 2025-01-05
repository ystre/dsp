add_executable(svc
    main.cc
    handler.cc
)

target_link_libraries(svc PRIVATE dsp)
code_analysis(svc PRIVATE)

add_executable(sim sim.cc)
target_link_libraries(sim PRIVATE dsp Boost::program_options)
code_analysis(sim PRIVATE)
