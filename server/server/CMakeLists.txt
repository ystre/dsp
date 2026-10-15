add_executable(server main.cpp)
set_target_properties(server PROPERTIES OUTPUT_NAME "${PROJ_NAME}-server")
target_link_libraries(server PRIVATE libproto libadmin proj_identity proj_warnings)

install(TARGETS server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime)
