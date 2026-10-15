cmake_minimum_required(VERSION 3.28)

if(CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

include(cmake/project.cmake)
project("${PROJ_NAME}" VERSION ${PROJ_VERSION} LANGUAGES CXX)
message(STATUS "${PROJ_NAME} version ${PROJ_VERSION} (metric prefix: ${PROJ_METRIC_PREFIX})")

include(GNUInstallDirs)
include(cmake/sanitizers.cmake)
include(settings.cmake)
include(dependencies.cmake)
include(cmake/warnings.cmake)
include(cmake/git-version.cmake)
include(cmake/test.cmake)
include(cmake/bench.cmake)

project_git_hash(PROJ_GIT_HASH)

# Identity baked into every binary (see project.env).
add_library(proj_identity INTERFACE)
target_compile_definitions(proj_identity INTERFACE
    PROJ_NAME="${PROJ_NAME}"
    PROJ_METRIC_PREFIX="${PROJ_METRIC_PREFIX}"
    PROJ_VERSION="${PROJ_VERSION}"
    PROJ_GIT_HASH="${PROJ_GIT_HASH}"
)

add_subdirectory(libproto)
add_subdirectory(libadmin)
add_subdirectory(server)
add_subdirectory(loadgen)
