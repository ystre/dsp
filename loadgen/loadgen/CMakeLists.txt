add_executable(loadgen main.cpp)
set_target_properties(loadgen PROPERTIES OUTPUT_NAME "${PROJ_NAME}-loadgen")
target_link_libraries(loadgen PRIVATE libproto libadmin proj_identity proj_warnings)

install(TARGETS loadgen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
