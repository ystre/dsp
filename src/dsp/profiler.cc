#include "dsp/profiler.hh"

#include <nova/log.hh>

#include <dlfcn.h>

#include <cstddef>
#include <cstdlib>

namespace dsp {

void start_profiler() {
    tracy::StartupProfiler();
    nova::topic_log::info("dsp", "Profiler has been started");
}

void stop_profiler() {
    tracy::ShutdownProfiler();
    nova::topic_log::info("dsp", "Profiler has been stopped");
}

} // namespace dsp

#ifndef DSP_MALLOC_PROFILING

void* operator new(std::size_t n) {
    void* ptr = malloc(n);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    TracySecureAllocS(ptr, n, DSP_PROFILING_CALLSTACK_DEPTH);
#pragma GCC diagnostic pop
    return ptr;
}

void operator delete(void* ptr) noexcept {
    TracySecureFreeS(ptr, DSP_PROFILING_CALLSTACK_DEPTH);
    free(ptr);
}

void operator delete(void* ptr, [[maybe_unused]] std::size_t n) noexcept {
    TracySecureFreeS(ptr, DSP_PROFILING_CALLSTACK_DEPTH);
    free(ptr);
}

#else

constexpr auto AllocDlFailure = 66;
constexpr auto CallocUnsupported = 67;
constexpr auto ReallocUnsupported = 68;

extern "C" void* malloc(size_t n) {
    using malloc_sig = void*(*)(size_t);
    static malloc_sig sys_malloc = nullptr;

    if (sys_malloc == nullptr) {
        // FIXME: In case of error, dlsym calls into `malloc`.
        //        https://github.com/bminor/glibc/blob/master/dlfcn/dlerror.c#L162
        sys_malloc = reinterpret_cast<malloc_sig>(dlsym(RTLD_NEXT, "malloc"));
        if (sys_malloc == nullptr) {
            exit(AllocDlFailure);
        }
    }

    void* ptr = sys_malloc(n);
    TracySecureAlloc(ptr, n);           // TODO(feat): Use callstacks.
    return sys_malloc(n);
}

extern "C" void* calloc(size_t n, size_t size) {
    exit(CallocUnsupported);        // TODO: Figure out what to do with `calloc` and Tracy.
    using calloc_sig = void*(*)(size_t, size_t);
    static calloc_sig sys_calloc = nullptr;

    if (sys_calloc == nullptr) {
        sys_calloc = reinterpret_cast<calloc_sig>(dlsym(RTLD_NEXT, "calloc"));
        if (sys_calloc == nullptr) {
            exit(AllocDlFailure);
        }
    }

    return sys_calloc(n, size);
}

extern "C" void* realloc(void* ptr, size_t new_size) {
    exit(ReallocUnsupported);       // TODO: Figure out what to do with `realloc` and Tracy.
    using realloc_sig = void*(*)(void*, size_t);
    static realloc_sig sys_realloc = nullptr;

    if (sys_realloc == nullptr) {
        sys_realloc = reinterpret_cast<realloc_sig>(dlsym(RTLD_NEXT, "realloc"));
        if (sys_realloc == nullptr) {
            exit(AllocDlFailure);
        }
    }

    return sys_realloc(ptr, new_size);
}

extern "C" void free(void* ptr) {
    using free_sig = void(*)(void*);
    static free_sig sys_free = nullptr;

    if (sys_free == nullptr) {
        sys_free = reinterpret_cast<free_sig>(dlsym(RTLD_NEXT, "free"));
        if (sys_free == nullptr) {
            exit(AllocDlFailure);
        }
    }

    TracySecureFree(ptr);               // TODO(feat): Use callstacks.
    sys_free(ptr);
}

#endif // DSP_MALLOC_PROFILING
