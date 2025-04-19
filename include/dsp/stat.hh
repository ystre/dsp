#pragma once

#include "sys.hh"

#include <nova/utils.hh>
#include <nova/units.hh>

#include <fmt/format.h>

#include <chrono>
#include <cstdlib>

namespace dsp {

/**
 * @brief   Throughput and system statistics.
 *
 * - Messages per second
 * - Bytes per second
 * - CPU usage
 * - Memory usage (Resident Set Size)
 */
class statistics {
    friend class fmt::formatter<statistics>;

    static constexpr auto RefreshInterval = std::chrono::seconds{ 1 };

public:

    /**
     * @brief   Increment and refresh numbers.
     *
     * System information is updated in one second intervals.
     *
     * @returns true if the interval has passed. Use it to decide periodical logging for example.
     */
    auto observe(std::size_t size, long n = 1) -> bool {
        m_total_messages += n;
        m_total_bytes += static_cast<long>(size);

        if (const auto elapsed = m_update_timer.elapsed(); elapsed > RefreshInterval) {
            m_sys.refresh();
            m_sys_stats = m_sys.stats();
            m_update_timer.reset();

            double elapsed_f = nova::to_sec(elapsed);
            auto messages = m_total_messages - m_messages_prev;
            auto bytes = m_total_bytes - m_bytes_prev;

            m_mps = static_cast<double>(messages) / elapsed_f;
            m_Bps = static_cast<double>(bytes) / elapsed_f;

            m_messages_prev = m_total_messages;
            m_bytes_prev = m_total_bytes;

            return true;
        }

        return false;
    }

    [[nodiscard]] auto n_messages() const -> long { return m_total_messages; }
    [[nodiscard]] auto n_bytes()    const -> long { return m_total_bytes; }

    void reset_uptime() { m_uptime.reset(); }

private:
    dsp::system_info m_sys;
    nova::stopwatch m_update_timer;
    nova::stopwatch m_uptime;

    long m_total_messages = 0;
    long m_total_bytes = 0;
    long m_messages_prev = 0;
    long m_bytes_prev = 0;

    dsp::process_stats m_sys_stats;
    double m_mps;
    double m_Bps;
};

} // namespace dsp


template <>
class fmt::formatter<dsp::statistics> {
public:

    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();

        for (; it != ctx.end() && *it != '}'; ++it) {}

        m_format_spec = std::string_view{ ctx.begin(), it };
        return it;
    }

    /**
     * @brief   Format statistics.
     *
     * - **empty** = all
     * - m (minimal) = MBps and MPS
     *
     * Example output: "0.000 MBps  0.00k MPS over 0.01 seconds (total: 200000 bytes, 1000 messages)  CPU:   0.0%  RSS: 0.0 MB"
     */
    template <typename FmtContext>
    constexpr auto format(const dsp::statistics& stat, FmtContext& ctx) const {
        if (m_format_spec.empty()) {
            return format_all(stat, ctx);
        }

        if (m_format_spec.find('m') != std::string_view::npos) {
            return format_minimal(stat, ctx);
        }

        // TODO(feat): More fine-grained customization.

        return ctx.out();
    }

private:
    std::string_view m_format_spec;

    template <typename FmtContext>
    constexpr auto format_minimal(const dsp::statistics& stat, FmtContext& ctx) const {
        fmt::format_to(
            ctx.out(),
            "{:.3f} MBps  {:.2f}k MPS",
            stat.m_Bps / nova::units::constants::MByte,
            stat.m_mps / nova::units::constants::kilo
        );

        return ctx.out();
    }

    template <typename FmtContext>
    constexpr auto format_all(const dsp::statistics& stat, FmtContext& ctx) const {
        fmt::format_to(
            ctx.out(),
            "{:.3f} MBps  "
            "{:.2f}k MPS  "
            "over {:.2f} seconds "
            "(total: {} bytes, {} messages)"
            "  "
            "CPU: {:>5.1f}%  "
            "RSS: {:.1f} MB",
            stat.m_Bps / nova::units::constants::MByte,
            stat.m_mps / nova::units::constants::kilo,
            nova::to_sec(stat.m_uptime.elapsed()),
            stat.n_bytes(),
            stat.n_messages(),
            stat.m_sys_stats.cpu,
            stat.m_sys_stats.rss
        );

        return ctx.out();
    }

};
