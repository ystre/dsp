# Phase 1 has no runtime dependencies beyond the standard library; test and
# benchmark frameworks are found in cmake/test.cmake and cmake/bench.cmake.
#
# Phase 2 adds liburing and nova-cpp (`exp` branch, consumed as source for its
# C++26 reflection-based configuration, as in o1).
