add_library(libadmin STATIC
    exposition.cpp
    http.cpp
)
set_target_properties(libadmin PROPERTIES OUTPUT_NAME admin)
target_include_directories(libadmin PUBLIC "${PROJECT_SOURCE_DIR}/libadmin")
target_link_libraries(libadmin PRIVATE proj_warnings)

add_test_target(libadmin)
