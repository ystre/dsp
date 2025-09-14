#include "tracy_alloc.hpp"

#include <libdsp/profiler.hpp>

#include <memory>

/**
 * @brief   Must be in a separate compilation unit for a visible callstack.
 */
void alloc::operator()() {
    DSP_PROFILING_ZONE("func");
    m_xs.push_back(std::make_shared<int>(1));
}
