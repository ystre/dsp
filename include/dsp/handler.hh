/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Handlers
 */

#pragma once

#include "dsp/cache.hh"
#include "dsp/kafka.hh"
#include "dsp/profiler.hh"
#include "dsp/stat.hh"
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
class tcp_handler_factory : public tcp::handler_factory {
public:
    virtual void bind(context) { /* optional */ }
    virtual ~tcp_handler_factory() = default;
};

namespace tcp {

template <typename Derived>
class handler_frame : public handler {
public:
    auto process(nova::data_view data) -> std::size_t override {
        DSP_PROFILING_ZONE("tcp-handler");
        if (data.empty()) {
            return 0;
        }

        const auto msg_size = static_cast<Derived*>(this)->do_process(data);
        if (msg_size == 0) {
            return 0;
        }

        m_stats.observe(msg_size);

        return msg_size;
    }

    void on_connection_init(const connection_info& info) override {
        nova::topic_log::info("dsp", "Client connected: {}:{}", info.address, info.port);
    }

    void on_error(const boost::system::error_code& ec, const connection_info& info) override {
        if (ec == boost::asio::error::eof) {
            nova::topic_log::info("dsp", "Client disconnected: {}:{}", info.address, info.port);
            static_cast<Derived*>(this)->do_eof();
        } else {
            nova::topic_log::error("dsp", "Error happened in TCP connection: {} {}:{}", ec.to_string(), info.address, info.port);
            // TODO(feat): generic error handling (logging, alarms, metrics)
        }
    }

    void on_error(const nova::exception& ex, const connection_info& info) override {
        nova::topic_log::error("dsp", "Unhandled exception in TCP handler: {}", ex.what(), info.address, info.port);
        nova::topic_log::devel("dsp", "Backtrace: \n{}", ex.backtrace());
    }

protected:
    [[nodiscard]] auto n_bytes()    const { return m_stats.n_bytes(); }
    [[nodiscard]] auto n_messages() const { return m_stats.n_messages(); }
    [[nodiscard]] auto uptime()     const { return m_stats.uptime(); }

    [[nodiscard]] auto perf_summary() -> std::string {
        return m_stats.summary();
    }

private:
    statistics m_stats;

};

} // namespace tcp

namespace kf {

class handler  {
public:
    virtual void process(kf::message_view_owned& message) = 0;

    virtual void bind(context) { /* optional */ }
    virtual ~handler() = default;
};

template <typename Derived>
class handler_frame : public kf::handler  {
public:
    void process(kf::message_view_owned& message) override {
        DSP_PROFILING_ZONE("kafka-handler");
        if (not message.ok()) {
            if (message.eof()) {
                nova::topic_log::debug(
                    "dsp",
                    "End of partition: {}[{}] at offset {}",
                    message.topic(),
                    message.partition(),
                    message.offset()
                );

                return;
            }

            nova::topic_log::warn("dsp", "Kafka error message: {}", message.error_message());
            return;
        }

        static_cast<Derived*>(this)->do_process(message);
    }

private:

};

} // namespace kf

} // namespace dsp
