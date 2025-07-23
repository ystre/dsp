#include "tracy_alloc.hh"

#include <dsp/profiler.hh>

/**
 * @brief   Must be in a separate compilation unit for a visible callstack.
 */
void alloc::operator()() {
    DSP_PROFILING_ZONE("func");
    m_xs.push_back(1);
}
