/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Interfaces
 *
 * Interfaces wrapping various clients (sources, sinks). Integration point
 * with DSP runtime.
 */

#pragma once

#include <libdsp/cache.hpp>
#include <libdsp/handler.hpp>
#include <libdsp/kafka.hpp>
#include <libdsp/metrics.hpp>
#include <libdsp/tcp.hpp>

#include <libnova/log.hpp>
#include <libnova/not_null.hpp>

#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include <chrono>
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
    virtual void bind(context ctx) = 0;

    /**
     * @brief   Return a listener function.
     *
     * The function is expected to have an event-loop.
     *
     * It is started on a separate thread by DSP framework when the service is
     * started.
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

    void update(metrics_registry& metrics) override {
        metrics.set("kafka_queue_size", m_kafka_client.queue_size());
    }

private:
    kf::producer m_kafka_client;

};

struct kafka_cfg {
    kf::properties props;
    std::vector<std::string> topics;
    std::size_t batch_size;
    std::chrono::milliseconds poll_timeout;

};

class kafka_listener : public southbound_interface {
public:
    kafka_listener(context ctx, kafka_cfg cfg, std::unique_ptr<kf::handler> handler)
        : m_kafka_client(std::move(cfg.props))
        , m_handler(std::move(handler))
        , m_batch_size(cfg.batch_size)
        , m_topics(std::move(cfg.topics))
    {
        bind(std::move(ctx));
    }

    /**
     * @brief   Create listener function.
     */
    auto listener() -> std::function<void()> override {
        return [this]() {
            nova::topic_log::info("dsp", "Starting Kafka listener (consuming topics: {})", m_topics);
            m_kafka_client.subscribe(m_topics);

            while (m_alive) {
                for (auto& message : m_kafka_client.consume(m_batch_size, m_poll_timeout)) {
                    m_handler->process(message);
                }
            }

            nova::topic_log::info("dsp", "Kafka listener stopped");
        };
    }

    void stop() override {
        nova::topic_log::debug("dsp", "Stopping Kafka listener...");
        m_alive.store(false);
    }

    /**
     * @brief   Update Kafka client metrics.
     *
     * Note: Curently there are no custom Kafka client metrics.
     * Internal client (librdkafka) metrics are updated via statistics
     * callback.
     */
    void update(metrics_registry&) override { /* NO-OP */ }

private:
    kf::consumer m_kafka_client;
    nova::not_null<std::unique_ptr<kf::handler>> m_handler;

    std::atomic_bool m_alive { true };
    std::size_t m_batch_size { 1 };
    std::chrono::milliseconds m_poll_timeout { 100 };
    std::vector<std::string> m_topics;

    void bind(context ctx) override {
        m_handler->bind(std::move(ctx));
    }

};

class tcp_listener : public southbound_interface {
public:
    tcp_listener(context ctx, const tcp::net_config& cfg, std::shared_ptr<tcp_handler_factory> factory)
        : m_tcp_server(cfg)
        , m_handler_factory(std::move(factory))
    {
        bind(std::move(ctx));
        m_tcp_server.set(m_handler_factory);
    }

    auto listener() -> std::function<void()> override {
        return [this]() {
            nova::topic_log::info("dsp", "Starting TCP server on port {}", m_tcp_server.port());
            m_tcp_server.start();
        };
    }

    void stop() override {
        m_tcp_server.stop();
    }

    void update(metrics_registry& metrics) override {
        const auto& m = m_tcp_server.metrics();
        metrics.set("connection_count", m.n_connections.load());
        metrics.set("tcp_buffer_size", m.buffer.load());
    }

private:
    tcp::server m_tcp_server;
    std::shared_ptr<tcp_handler_factory> m_handler_factory;

    void bind(context ctx) override {
        m_handler_factory->bind(std::move(ctx));
    }

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
