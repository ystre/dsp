/**
 * Part of Data Stream Processing framework.
 *
 * DSP - System utilities
 */

#pragma once

#include <nova/io.hh>
#include <nova/units.hh>
#include <nova/utils.hh>

#include <boost/algorithm/string.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace dsp {

// TODO(refact): Move out to a common place.
class spinner {
    static constexpr std::chrono::milliseconds UpdateInterval { 200 };

public:
    void tick() {
        const auto elapsed = m_timer.elapsed();

        if (elapsed > UpdateInterval) {
            display();

            m_timer.reset();
            ++m_updates;
        }

        ++m_iterations;
    }

    void finish() {
        display(m_finish_char);
        fmt::print("\n");
    }

    void set_message(const std::string& msg) {
        m_message = msg;
    }

    void set_prefix(const std::string& msg) {
        m_prefix = msg;
    }

    void max_iterations(std::size_t n) {
        m_max_iterations = n;
    }

private:
    std::size_t m_iterations = 0;
    std::size_t m_max_iterations = 0;

    std::size_t m_updates = 0;
    std::size_t m_max_message_length = 0;

    // https://unicode.org/charts/nameslist/c_2800.html
    std::vector<std::string_view> m_bars {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" };
    std::string_view m_finish_char = "⠿";

    std::string m_prefix;
    std::string m_message;

    nova::stopwatch m_timer;

    auto next_bar_chars() -> std::string_view {
        return m_bars[m_updates % m_bars.size()];
    }

    void update_message_length(std::string_view message) {
        if (m_max_message_length < message.size()) {
            m_max_message_length = message.size();
        }
    }

    void display(std::string_view bar_char = {}) {
        // TODO(refact): separate running spinner with the number of iterations clearly
        if (m_max_iterations > 0) {
            const auto message = fmt::format(
                "[{}: {:.2f}M] {}  {}",
                m_prefix,
                static_cast<double>(m_iterations) / 1'000'000.0,
                bar_char.empty() ? next_bar_chars() : bar_char,
                m_message
            );

            update_message_length(message);
            fmt::print(stderr, "{: <{}}\r", message, m_max_message_length);
        }
        else {
            const auto message = fmt::format(
                "{} {}  {}",
                m_prefix,
                bar_char.empty() ? next_bar_chars() : bar_char,
                m_message
            );

            update_message_length(message);
            fmt::print(stderr, "{: <{}}\r", message, m_max_message_length);
        }
    }
};

struct process_stats {
    double cpu;
    double user_time;
    double sys_time;
    double rss;
};

enum stat_index {
    user_time = 12,
    sys_time = 13,
    rss = 22,
};

[[nodiscard]] inline auto parse_stat_file(std::string data) -> process_stats {
    static constexpr auto Fields = 51;
    auto str_parts = std::vector<std::string>(Fields);

    auto pos = data.find_last_of(')');

    data = data.substr(pos);
    boost::split(str_parts, data, boost::is_any_of(" "), boost::algorithm::token_compress_on);

    return process_stats{
        .cpu = 0.0,
        .user_time = static_cast<double>(std::stoull(str_parts[stat_index::user_time])),
        .sys_time  = static_cast<double>(std::stoull(str_parts[stat_index::sys_time])),
        .rss       = static_cast<double>(std::stoull(str_parts[stat_index::rss])),
    };
}

class system_info {
public:
    system_info()
        : m_pid(getpid())
    {}

    void refresh() {
        const auto stat = nova::read_file(fmt::format("/proc/{}/stat", m_pid)).value();
        m_stats = parse_stat_file(stat);

        m_stats.user_time /= static_cast<double>(m_kernel_clock_cycle);
        m_stats.sys_time  /= static_cast<double>(m_kernel_clock_cycle);
        m_stats.rss       *= static_cast<double>(m_page_size) / nova::units::constants::MByte;

        const auto cpu_time_prev = m_stats_prev.user_time + m_stats_prev.sys_time;
        const auto cpu_time      = m_stats.user_time + m_stats.sys_time;
        m_stats.cpu = (cpu_time - cpu_time_prev) * 100.0;

        m_stats_prev = m_stats;
    }

    auto stats() const -> process_stats {
        return m_stats;
    }

private:
    int m_pid;
    long m_kernel_clock_cycle = sysconf(_SC_CLK_TCK);
    long m_page_size = sysconf(_SC_PAGESIZE);

    process_stats m_stats;
    process_stats m_stats_prev {};
};

} // namespace dsp
