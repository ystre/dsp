/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Daemon
 *
 * A daemon that runs on the main thread with OS signal handling. The daemon
 * optionally has a watch dog for background activities.
 */

#pragma once

#include <nova/error.hh>
#include <nova/log.hh>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <thread>

inline std::atomic_int g_sigint  { 0 };
inline std::atomic_int g_sigterm { 0 };
inline std::atomic_int g_sigusr1 { 0 };
inline std::atomic_int g_sigusr2 { 0 };

namespace dsp {

/**
 * @brief   Handle SIGINT, SIGTERM and SIGUSRs.
 *
 * SIGINT once is graceful, but the second calls into `std::abort`.
 */
class signal_handler {
public:
    signal_handler() {
        std::signal(SIGINT, handle_signal_int);
        std::signal(SIGTERM, handle_signal_term);
        std::signal(SIGUSR1, handle_signal_usr1);
        std::signal(SIGUSR2, handle_signal_usr2);
    }

private:

    static void handle_signal_int(int signal) {
        if (signal == SIGINT) {
            auto n = g_sigint.fetch_add(1);
            if (n > 1) {
                std::abort();
            }
        }
    }

    static void handle_signal_term(int signal) {
        if (signal == SIGTERM) {
            g_sigterm.fetch_add(1);
        }
    }

    static void handle_signal_usr1(int signal) {
        if (signal == SIGUSR1) {
            g_sigusr1.fetch_add(1);
        }
    }

    static void handle_signal_usr2(int signal) {
        if (signal == SIGUSR2) {
            g_sigusr2.fetch_add(1);
        }
    }
};

/**
 * @brief   A daemon that keeps alive the process until it is terminted via signal.
 *
 * SIGINT or SIGTERM signals to stop.
 *
 * A function can be attached to do periodical background activities.
 */
class daemon {
public:
    using watch_dog = std::function<bool()>;

    /**
     * @brief   Attach a function that is called periodically.
     */
    void attach(watch_dog func) {
        m_function = std::move(func);
    }

    void start(std::chrono::seconds interval) {
        nova::topic_log::info("dsp", "Starting daemon");

        while (m_alive) {
            if (has_signal_stop()) {
                stop();
            }

            if (m_function) {
                try {
                    if (not std::invoke(m_function)) {
                        stop();
                    }
                } catch (const nova::exception& ex) {
                    nova::topic_log::critical("dsp", "An exception happened in the watch dog attached to the daemon: {}", ex.what());
                    stop();
                }
            }

            std::this_thread::sleep_for(interval);
        }

        nova::topic_log::info("dsp", "Daemon has been stopped");
    }

private:
    bool m_alive { true };
    signal_handler m_signal_handler;

    watch_dog m_function;

    void stop() {
        nova::topic_log::info("dsp", "Shutting down...");
        m_alive = false;
    }

    [[nodiscard]] auto has_signal_stop() -> bool {
        if (g_sigint.load() > 0) {
            nova::topic_log::debug("dsp", "SIGINT received");
            return true;
        }

        if (g_sigterm.load() > 0) {
            nova::topic_log::debug("dsp", "SIGTERM received");
            return true;
        }

        return false;
    }
};

} // namespace dsp
