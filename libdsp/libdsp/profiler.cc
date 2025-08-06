#include <libdsp/profiler.hh>
#include <nova/log.hh>

#include <fmt/core.h>

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <map>
#include <mutex>

#include <dlfcn.h>
#include <unistd.h>

namespace dsp {

using malloc_sig = void*(*)(size_t);
using calloc_sig = void*(*)(size_t, size_t);
using realloc_sig = void*(*)(void*, size_t);
using free_sig = void(*)(void*);

malloc_sig g_sys_malloc = nullptr;
calloc_sig g_sys_calloc = nullptr;
realloc_sig g_sys_realloc = nullptr;
free_sig g_sys_free = nullptr;

/**
 * @brief   Return the address of a symbol in a shared object.
 */
template <typename F>
auto load_function(const std::string& name) -> F {
    static constexpr auto MallocLinkFailure = 66;

    // FIXME: In case of error, dlsym calls into `malloc`.
    //        https://github.com/bminor/glibc/blob/master/dlfcn/dlerror.c#L162
    auto* func = reinterpret_cast<F>(dlsym(RTLD_NEXT, name.c_str()));
    if (func == nullptr) {
        exit(MallocLinkFailure);
    }
    return func;
}

template <typename T>
class sys_allocator {
public:
    using value_type = T;

    sys_allocator() = default;
    template <typename U> sys_allocator(const sys_allocator<U>&) {}
    template <typename U> struct rebind { using other = sys_allocator<U>; };

    auto allocate(std::size_t n) -> T* {
        if (g_sys_malloc == nullptr) {
            g_sys_malloc = load_function<malloc_sig>("malloc");
        }

        return static_cast<T*>(g_sys_malloc(n * sizeof(T)));
    }

    void deallocate(T* ptr, [[maybe_unused]] std::size_t n) {
        if (g_sys_free == nullptr) {
            g_sys_free = load_function<free_sig>("free");
        }

        g_sys_free(ptr);
    }

};

thread_local bool t_allocation_lock { false };
thread_local bool t_deallocation_lock { false };

struct allocation_tracker {
    allocation_tracker() : is_alive{ true } {}
    ~allocation_tracker() { is_alive = false; }

    std::unordered_map<
        void*, bool,
        std::hash<void*>,
        std::equal_to<void*>,
        sys_allocator<std::pair<void* const, bool>>
    > inner;

    bool is_alive = false;
};

allocation_tracker g_tracked_allocations;

/**
 * @brief   Custom allocator primarily built for profiling.
 */
class aly {
public:
    auto allocate(std::size_t n) -> void* {
        if (g_sys_malloc == nullptr) {
            g_sys_malloc = load_function<malloc_sig>("malloc");
        }

        void* ptr = g_sys_malloc(n);
        profile_allocation(ptr, n);
        return ptr;
    }

    auto allocate_array(std::size_t n, std::size_t size) -> void* {
        if (g_sys_calloc == nullptr) {
            g_sys_calloc = load_function<calloc_sig>("calloc");
        }

        void* ptr = g_sys_calloc(n, size);

        // CAUTION: Due to the alignment requirements, the number of allocated bytes is not necessarily equal to num * size.
        // https://en.cppreference.com/w/c/memory/calloc.html
        profile_allocation(ptr, n * size);

        return ptr;
    }

    auto reallocate(void* ptr, std::size_t n) -> void* {
        if (g_sys_realloc == nullptr) {
            g_sys_realloc = load_function<realloc_sig>("realloc");
        }

        void* new_ptr = g_sys_realloc(ptr, n);
        if (ptr != new_ptr) {
            profile_deallocation(ptr);
            profile_allocation(new_ptr, n);
        }
        return new_ptr;
    }

    void deallocate(void* ptr) {
        if (g_sys_free == nullptr) {
            g_sys_free = load_function<free_sig>("free");
        }

        profile_deallocation(ptr);
        g_sys_free(ptr);
    }

private:
    std::mutex m_tracker_lock;

    void profile_allocation(void* ptr, std::size_t n) {
        if (t_allocation_lock || not g_tracked_allocations.is_alive) {
            return;
        }

        t_allocation_lock = true;
        #ifdef DSP_PROFILING_TRACE_LOGS
        fmt::println("Allocation: {} (size={}) [@{}]", ptr, n, gettid());
        #endif
        TracySecureAllocS(ptr, n, DSP_PROFILING_CALLSTACK_DEPTH);
        auto lock = std::lock_guard(m_tracker_lock);
        g_tracked_allocations.inner.insert({ptr, true});

        t_allocation_lock = false;
    }

    void profile_deallocation(void* ptr) {
        if (t_deallocation_lock || not g_tracked_allocations.is_alive) {
            return;
        }

        t_deallocation_lock = true;

        {
            auto lock = std::lock_guard(m_tracker_lock);
            const auto it = g_tracked_allocations.inner.find(ptr);
            if (it == std::end(g_tracked_allocations.inner)) {
                t_deallocation_lock = false;
                return;
            }

            g_tracked_allocations.inner.erase(it);
        }

        t_allocation_lock = true;
        #ifdef DSP_PROFILING_TRACE_LOGS
        fmt::println("Deallocation: {} [@{}]", ptr, gettid());
        #endif
        TracySecureFreeS(ptr, DSP_PROFILING_CALLSTACK_DEPTH);
        t_allocation_lock = false;

        t_deallocation_lock = false;
    }

};

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

static dsp::aly alloc;

extern "C" void* malloc(size_t n) {
    return alloc.allocate(n);
}

extern "C" void* calloc(size_t n, size_t size) {
    return alloc.allocate_array(n, size);
}

extern "C" void* realloc(void* ptr, size_t n) {
    return alloc.reallocate(ptr, n);
}

extern "C" void free(void* ptr) {
    alloc.deallocate(ptr);
}

#endif // DSP_MALLOC_PROFILING
