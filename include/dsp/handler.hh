/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Handlers
 */

#pragma once

#include "dsp/cache.hh"
#include "dsp/kafka.hh"
#include "dsp/tcp_handler.hh"

#include <nova/log.hh>
#include <nova/units.hh>
#include <nova/utils.hh>

#include <boost/asio/error.hpp>

#include <cstddef>
#include <string>

namespace dsp {

/**
 * @brief   TCP handler factory with DSP context.
 */
class handler_factory : public tcp::handler_factory {
public:
    virtual void context(context) { /* optional */ }
    virtual ~handler_factory() = default;
};

template <typename Derived>
class handler_frame : public tcp::handler {
    struct connection_metrics {
        std::size_t n_messages;
        std::size_t n_bytes;
    };

public:
    auto process(nova::data_view data) -> std::size_t override {
        if (data.empty()) {
            return 0;
        }

        const auto msg_size = static_cast<Derived*>(this)->do_process(data);
        if (msg_size == 0) {
            return 0;
        }

        ++m_connection_metrics.n_messages;
        m_connection_metrics.n_bytes += msg_size;

        return msg_size;
    }

    void on_connection_init(const tcp::connection_info& info) override {
        nova::topic_log::info("dsp", "Client connected: {}:{}", info.address, info.port);
    }

    void on_error(const boost::system::error_code& ec, const tcp::connection_info& info) override {
        if (ec == boost::asio::error::eof) {
            nova::topic_log::info("dsp", "Client disconnected: {}:{}", info.address, info.port);
            static_cast<Derived*>(this)->do_eof();
        } else {
            nova::topic_log::error("dsp", "Error happened in TCP connection: {} {}:{}", ec.to_string(), info.address, info.port);
            // TODO(feat): generic error handling (logging, alarms, metrics)
        }
    }

    void on_error(const nova::exception& ex, const tcp::connection_info& info) override {
        nova::topic_log::error("dsp", "Unhandled exception in TCP handler: {}", ex.what(), info.address, info.port);
        nova::topic_log::devel("dsp", "Backtrace: \n{}", ex.backtrace());
    }

protected:
    [[nodiscard]] auto n_bytes()    const { return m_connection_metrics.n_bytes; }
    [[nodiscard]] auto n_messages() const { return m_connection_metrics.n_messages; }

    [[nodiscard]] auto uptime() { return m_timer.elapsed(); }

    [[nodiscard]] auto perf_summary() -> std::string {
        const auto elapsed = nova::to_sec(uptime());
        const auto mbps = static_cast<double>(n_bytes()) / elapsed / nova::units::constants::MByte;
        const auto mps  = static_cast<double>(n_messages()) / elapsed / nova::units::constants::kilo;

        // FIXME: NaNs for very short lived connections.
        auto summary = fmt::format(
            "Summary: {:.3f} MBps and {:.0f}k MPS over {:.1f} seconds (total: {} bytes, {} messages)",
            mbps,
            mps,
            elapsed,
            n_bytes(),
            n_messages()
        );

        return summary;
    }

private:
    connection_metrics m_connection_metrics {};
    nova::stopwatch m_timer;

};

class kafka_message_handler {
public:
    void process(kf::message_view_owned& message) {
        // FIXME: Segfault without format specicifers.
        // TODO(feat): Expose a customization point.
        nova::topic_log::trace("dsp", "Message received {:lkvh}", message);
    }

private:
};

} // namespace dsp
