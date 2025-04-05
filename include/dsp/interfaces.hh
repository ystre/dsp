/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Interfaces
 *
 * Interfaces wrapping various clients (sources, sinks). Integration point
 * with DSP runtime.
 */

#pragma once

#include "dsp/cache.hh"
#include "dsp/handler.hh"
#include "dsp/kafka.hh"
#include "dsp/metrics.hh"
#include "dsp/tcp.hh"

#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include <string>
#include <utility>
#include <memory>

namespace dsp {

class southbound_interface {
public:

    /**
     * @brief    Bind DSP context with the southbound interface.
     *
     * It contains global objects for the framework, like metrics.
     *
     * It also contains an application context opaque to DSP framework.
     */
    virtual void bind_context(context ctx) = 0;

    /**
     * @brief   Return a listener function.
     *
     * The function is expected to be a blocking call to be run on a separate
     * thread.
     */
    virtual auto listener() -> std::function<void()> = 0;

    /**
     * @brief   Stop listener.
     *
     * It is called by DSP Service `stop()`.
     */
    virtual void stop() = 0;

    /**
     * @brief   Expose the metrics of the listener.
     *
     * It is called by DSP Service periodically from the daemon thread.
     */
    virtual void update(metrics_registry&) { /* optional */ }

    virtual ~southbound_interface() = default;

};

/**
 * @brief   A thin wrapper around the Kafka client.
 */
class kafka_producer : public northbound_interface {
public:
    kafka_producer(kf::properties props)
        : m_kafka_client(std::move(props))
    {}

    void stop() override {
        m_kafka_client.stop();
    }

    auto send(const message& msg) -> bool override {
        return m_kafka_client.try_send(msg);
    }

    auto queue_size() const -> int {
        return m_kafka_client.queue_size();
    }

    void update(metrics_registry& metrics) override {
        metrics.set("kafka_queue_size", queue_size());
    }

private:
    kf::producer m_kafka_client;

};

class kafka_listener : public southbound_interface {
public:
    kafka_listener(kf::properties props)
        : m_kafka_client(std::move(props))
    {}

    auto listener() -> std::function<void()> override {
        return [this]() {
            nova::topic_log::info("dsp", "Starting Kafka consumer (consuming topics: {})", m_topics);
            m_kafka_client.subscribe(m_topics);
            while (m_alive) {
                for (auto& message : m_kafka_client.consume(m_batch_size)) {
                    m_handler.process(message);
                }
            }
        };
    }

    void stop() override {
        m_alive.store(false);
    }

    void bind_context(context) override {
        // TODO(feat): Use context (cache).
    }

private:
    kf::consumer m_kafka_client;
    kafka_message_handler m_handler;

    std::atomic_bool m_alive { true };
    std::size_t m_batch_size { 10 };                        // TODO(cfg): Make it configurable.
    std::vector<std::string> m_topics { "dev-test" };       // TODO(cfg): Make it configurable.

};

class tcp_listener : public southbound_interface {
public:
    tcp_listener(const tcp::net_config& cfg)
        : m_tcp_server(cfg)
    {}

    auto listener() -> std::function<void()> override {
        return [this]() {
            nova::topic_log::info("dsp", "Starting TCP server on port {}", m_tcp_server.port());
            m_tcp_server.start();
        };
    }

    void stop() override {
        m_tcp_server.stop();
    }

    /**
     * @brief   Set TCP handler factory.
     */
    void set(std::shared_ptr<handler_factory> factory) {
        m_handler_factory = std::move(factory);
        m_tcp_server.set(m_handler_factory);
    }

    void bind_context(context ctx) override {
        m_handler_factory->context(std::move(ctx));
    }

    void update(metrics_registry& metrics) override {
        const auto& m = m_tcp_server.metrics();
        metrics.set("connection_count", m.n_connections.load());
        metrics.set("tcp_buffer_size", m.buffer.load());
        metrics.set("tcp_buffer_capacity", m.buffer_capacity.load());
    }

private:
    tcp::server m_tcp_server;
    std::shared_ptr<handler_factory> m_handler_factory;

};

/**
 * @brief   A wrapper around Prometheus Exposer.
 *
 * Uses one socket for IPv4 and IPv6.
 */
class pm_exposer {
public:
    pm_exposer(const std::string& address, std::shared_ptr<metrics_registry> registry)
        : m_server(std::make_unique<prometheus::Exposer>(fmt::format("+{}", address)))
    {
        m_server->RegisterCollectable(std::move(registry)->prometheus_handle());
    }

private:
    std::unique_ptr<prometheus::Exposer> m_server;

};

} // namespace dsp
