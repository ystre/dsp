#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace dsp {

template <typename T>
class queue {
public:
    void push(T value) {
        {
            std::lock_guard lock(m_mtx);
            m_queue.push(std::move(value));
        }
        m_cv.notify_one();
    }

    T pop() {
        std::unique_lock lock(m_mtx);
        m_cv.wait(lock, [&]{ return not m_queue.empty(); });
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mtx;
    std::condition_variable m_cv;
};

class task {
public:
    task(std::function<void()> func)
        : m_func(std::move(func))
    {}

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    void operator()() {
        return std::invoke(m_func);
    }

private:
    std::function<void()> m_func;

};

template <typename InputT, typename OutputT>
struct pipe {
    queue<InputT> in;
    queue<OutputT> out;

    // TODO: pop and push ?
};

template <typename InputT, typename OutputT>
class worker {
public:
    worker(std::shared_ptr<pipe<InputT, OutputT>> pipe)
        : m_thread(create())
        , m_pipe(std::move(pipe))
    {}

private:
    std::jthread m_thread;
    std::atomic_bool m_alive { true };
    std::shared_ptr<pipe<InputT, OutputT>> m_pipe;

    auto create() -> std::jthread {
        return [this]() {
            while (m_alive) {
                auto message = m_pipe->in.pop();
                m_pipe->out.push(m_process(message));
            }
        };
    }
};

// TODO(refact): Avoid type contamination.
template <typename InputT, typename OutputT>
class thread_pool {
public:
    thread_pool(int jobs, task<InputT, OutpuT>)
        : n_jobs(jobs)
    {
        for (int i = 0; i < jobs; ++i) {
            m_workers.emplace_back(m_pipe);
        }
    }

    // TODO(feat): Runtime reconfiguration
    // void jobs(int n) {
        // n_jobs = n;
    // }

private:
    int n_jobs;
    std::shared_ptr<pipe<InputT, OutputT>> m_pipe = std::make_shared<pipe<InputT, OutputT>>();
    std::vector<worker<InputT, OutputT>> m_workers;

};

}
