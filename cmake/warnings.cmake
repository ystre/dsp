# Compiler warnings, applied through the `proj_warnings` interface target.
# Same set as nova-cpp's `novaCompilerWarnings.cmake` (GCC only: the project
# targets GCC 16). `-Werror` unless `PROJ_WERROR=OFF`.

option(PROJ_WERROR "Treat warnings as errors" ON)

add_library(proj_warnings INTERFACE)
target_compile_options(proj_warnings INTERFACE
    -Wall
    -Wextra
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wuseless-cast
    $<$<BOOL:${PROJ_WERROR}>:-Werror>
)
