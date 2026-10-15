# Reads the project identity from `project.env` (the single source of truth,
# see that file) into CMake variables:
#
#   PROJ_NAME, PROJ_METRIC_PREFIX, PROJ_VENDOR, PROJ_VERSION
#
# Included before `project()`, so it must not rely on anything `project()`
# sets up.

function(_proj_read_env file)
    file(STRINGS "${file}" lines REGEX "^[A-Z_]+=")
    foreach(line IN LISTS lines)
        string(REGEX MATCH "^([A-Z_]+)=(.*)$" _ "${line}")
        set("PROJENV_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" PARENT_SCOPE)
    endforeach()
endfunction()

_proj_read_env("${CMAKE_CURRENT_LIST_DIR}/../project.env")

foreach(key PROJECT_NAME METRIC_PREFIX VENDOR)
    if(NOT DEFINED PROJENV_${key} OR PROJENV_${key} STREQUAL "")
        message(FATAL_ERROR "project.env: '${key}' is missing or empty")
    endif()
endforeach()

set(PROJ_NAME          "${PROJENV_PROJECT_NAME}")
set(PROJ_METRIC_PREFIX "${PROJENV_METRIC_PREFIX}")
set(PROJ_VENDOR        "${PROJENV_VENDOR}")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" PROJ_VERSION LIMIT_COUNT 1)
