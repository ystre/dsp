set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules are used; skip the dependency scanning CMake enables for C++26.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All executables (binaries, tests, benchmarks) in one place: `<build>/bin`.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Portable default for images; use `-DPROJ_MARCH=native` for local tuning runs.
set(PROJ_MARCH "x86-64-v3" CACHE STRING "Value for -march")
add_compile_options(-march=${PROJ_MARCH})

# Release builds keep debug info; `ci/package.sh` splits it into a separate
# archive (baldr-cpp convention).
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")

# Fully static libstdc++/libgcc: the runtime image is distroless (glibc only).
option(PROJ_STATIC_RUNTIME "Statically link libstdc++ and libgcc" ON)
if(PROJ_STATIC_RUNTIME AND SANITIZERS STREQUAL "")
    add_link_options(-static-libstdc++ -static-libgcc)
endif()
