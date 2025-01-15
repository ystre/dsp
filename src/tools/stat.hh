#pragma once

#include <dsp/sys.hh>

#include <nova/utils.hh>
#include <nova/units.hh>

#include <fmt/format.h>

#include <chrono>
#include <cstdlib>

/**
 * @brief   Throughput and system statistics.
 *
 * - Messages per second
 * - Bit per second
 * - CPU usage
 * - Memory usage (Resident Set Size)
 *
 * TODO(feat): Summary statistics.
 * TODO(refact): Move to a common place.
 */
class statistics {
    static constexpr auto RefreshInterval = std::chrono::seconds{ 1 };

public:
    void observe(std::size_t size, std::size_t n = 1) {
        m_total_messages += n;
        m_total_bytes += size;

        if (const auto elapsed = m_timer.elapsed(); elapsed > RefreshInterval) {
            m_sys.refresh();
            m_sys_stats = m_sys.stats();
            m_timer.reset();

            double elapsed_f = nova::to_sec(elapsed);
            auto messages = m_total_messages - m_messages_prev;
            auto bytes = m_total_bytes - m_bytes_prev;

            m_mps = static_cast<double>(messages) / elapsed_f;
            m_bps = static_cast<double>(bytes) / elapsed_f * 8;

            m_messages_prev = m_total_messages;
            m_bytes_prev = m_total_bytes;
        }
    }

    auto to_string() -> std::string {
        return fmt::format(
            "{:.2f} MBps  "
            "{:.2f}k MPS "
            "  "
            "CPU: {:>5.1f}%  "
            "RSS: {:.1} MB",
            m_bps / nova::units::constants::MByte / 8,
            m_mps / nova::units::constants::kilo,
            m_sys_stats.cpu,
            m_sys_stats.rss
        );
    }

private:
    dsp::system_info m_sys;
    nova::stopwatch m_timer;

    std::size_t m_total_messages = 0;
    std::size_t m_total_bytes = 0;
    std::size_t m_messages_prev = 0;
    std::size_t m_bytes_prev = 0;

    dsp::process_stats m_sys_stats;
    double m_mps;
    double m_bps;
};
