# gitVersion.cmake
#
# Provides:
#   `project_git_hash(<out_var>)`     - short git commit hash of the current
#                                      checkout.
#   `project_git_branch(<out_var>)`   - current branch name (e.g. "master").
#   `project_git_dirty(<out_var>)`    - "dirty" if the working tree has
#                                      uncommitted changes, "clean" otherwise.
#
# All three fall back to "unknown" when git or the repository itself isn't
# available (e.g. a source tarball build).

function(project_git_hash out_var)
    find_package(Git QUIET)

    set(hash "unknown")
    if(Git_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE exit_code
        )
        if(exit_code EQUAL 0 AND NOT result STREQUAL "")
            set(hash "${result}")
        endif()
    endif()

    set(${out_var} "${hash}" PARENT_SCOPE)
endfunction()

function(project_git_branch out_var)
    find_package(Git QUIET)

    set(branch "unknown")
    if(Git_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE exit_code
        )
        if(exit_code EQUAL 0 AND NOT result STREQUAL "")
            set(branch "${result}")
        endif()
    endif()

    set(${out_var} "${branch}" PARENT_SCOPE)
endfunction()

function(project_git_dirty out_var)
    find_package(Git QUIET)

    set(dirty "unknown")
    if(Git_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE exit_code
        )
        if(exit_code EQUAL 0)
            if(result STREQUAL "")
                set(dirty "clean")
            else()
                set(dirty "dirty")
            endif()
        endif()
    endif()

    set(${out_var} "${dirty}" PARENT_SCOPE)
endfunction()
