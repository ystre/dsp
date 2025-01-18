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
#include "dsp/kafka.hh"
#include "dsp/metrics.hh"

#include <prometheus/exposer.h>

#include <prometheus/registry.h>
#include <utility>
#include <memory>

namespace dsp {

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

    // void delivery_callback(kf::delivery_callback_f callback) {
        // m_kafka_client.delivery_callback(std::move(callback));
    // }

    // void event_callback(kf::event_callback_f callback) {
        // m_kafka_client.event_callback(std::move(callback));
    // }

private:
    kf::producer m_kafka_client;

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
