/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Profiling
 *
 * The provided `start_profiler()` and `stop_profiler()` are defined regardless
 * of compilation flags/defintions. If `DSP_PROFILING` is not defined, their
 * defintions are empty.
 */

#pragma once

#ifdef DSP_PROFILING

#if !defined(TRACY_DELAYED_INIT) || !defined(TRACY_MANUAL_LIFETIME)
static_assert(false, "Tracy's lifetime must be managed manually,"
                     " define TRACY_DELAYED_INIT and TRACY_MANUAL_LIFETIME."
                     " See DSP User's Guide for more information.");
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <tracy/public/tracy/Tracy.hpp>
#pragma GCC diagnostic pop

#ifndef DSP_PROFILING_CALLSTACK_DEPTH
#define DSP_PROFILING_CALLSTACK_DEPTH 20
#endif

// https://stackoverflow.com/questions/154136/why-use-apparently-meaningless-do-while-and-if-else-statements-in-macros
//
// "The do ... while and if ... else are there to make it so that a semicolon
// after your macro always means the same thing."
#define DSP_PROFILING_ZONE(name)                                \
    do {                                                        \
        _Pragma("GCC diagnostic push")                          \
        _Pragma("GCC diagnostic ignored \"-Wold-style-cast\"")  \
        _Pragma("GCC diagnostic ignored \"-Wuseless-cast\"")    \
        ZoneScopedN(name);                                      \
        _Pragma("GCC diagnostic pop")                           \
    } while (0)

#endif // DSP_PROFILING

namespace dsp {

#ifdef DSP_PROFILING
void start_profiler();
void stop_profiler();
#else
inline void start_profiler() {}
inline void stop_profiler() {}
#endif // DSP_PROFILING

} // namespace dsp
