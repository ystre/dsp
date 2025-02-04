/**
 * Data Stream Processing Framework.
 */

#pragma once

#include "dsp/cache.hh"
#include "dsp/daemon.hh"
#include "dsp/handler.hh"
#include "dsp/interfaces.hh"
#include "dsp/kafka.hh"
#include "dsp/metrics.hh"
#include "dsp/tcp.hh"

#include <nova/log.hh>
#include <nova/yaml.hh>

#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include <any>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

constexpr auto DspVersionMajor = 0;
constexpr auto DspVersionMinor = 1;
constexpr auto DspVersionPatch = 0;

namespace dsp {

class service;

class northbound_builder {
    friend service;

public:
    void build();
    auto kafka_props() -> std::shared_ptr<kf::properties>;

private:
    bool m_enabled;
    std::string m_name;
    service* m_service_handle;
    std::any m_cfg;

};


/**
 * @brief   The Service which provides the runtime framework.
 */
class service {
    friend northbound_builder;

public:
    service(const nova::yaml& config)
        : m_config(config)
    {
        init_southbound();
        init_metrics();
    }

    void start() {
        if (m_tcp_server != nullptr) {
            m_worker_threads.emplace_back([this]() {
                nova::topic_log::info("dsp", "Starting TCP server on port {}", m_tcp_server->port());
                m_tcp_server->start();
            });
        }

        start_daemon();
    }

    /**
     * @brief   Stop execution.
     *
     * All worker threads are detached. Each component must provide a stop
     * function that blocks until the necessary resources are cleaned-up in
     * a graceful manner.
     *
     * For some reason, if the main thread falls off of main function faster,
     * than the worker threads stop, it can make the process hang.
     */
    void stop() {
        m_tcp_server->stop();
        m_cache->stop();

        for (auto& th : m_worker_threads) {
            th.detach();
        }
    }

    [[nodiscard]] auto get_metrics() -> std::shared_ptr<metrics_registry> {
        return m_metrics;
    }

    /**
     * @brief   Create a handler factory and attach it to the service.
     */
    template <typename Factory, typename ...Args>
    void handler(Args&& ...args) {
        m_handler_factory = std::make_shared<Factory>(m_cache, std::forward<Args>(args)...);
        m_tcp_server->set(m_handler_factory);
    }

    /**
     * @brief   Attach a northbound interface.
     */
    void northbound(const std::string& name, std::unique_ptr<northbound_interface> interface) {
        m_cache->attach_northbound(name, std::move(interface));
    }

    /**
     * @brief   Access a northbound interface.
     */
    template <typename Interface>
    [[nodiscard]] auto northbound(const std::string& name) -> Interface* {
        return m_cache->get_northbound<Interface>(name);
    }

    /**
     * @brief   Bind application context.
     */
    void bind_context(std::any ctx) {
        m_handler_factory->context(
            context{
                .stats = m_metrics,
                .app = std::move(ctx)
            }
        );
    }

    auto cfg_northbound() -> northbound_builder {
        auto builder = northbound_builder{ };
        builder.m_service_handle = this;

        if (const auto nbi_type = lookup<std::string>("interfaces.northbound.type"); nbi_type == "kafka") {
            if (not lookup<bool>("interfaces.northbound.enabled")) {
                builder.m_enabled = false;
                return builder;
            }

            builder.m_enabled = true;

            builder.m_name = lookup<std::string>("interfaces.northbound.name");

            auto kafka_cfg = std::make_shared<kf::properties>();
            kafka_cfg->bootstrap_server(lookup<std::string>("interfaces.northbound.address"));

            // FIXME: yaml.lookup with non-existent key
            try {
                kafka_cfg->statistics_interval(lookup<std::string>("interfaces.northbound.statistics-interval-ms"));
            } catch (...) {}

            // TODO(cfg): generic librdkafka config

            builder.m_cfg = std::make_any<std::shared_ptr<kf::properties>>(kafka_cfg);
            return builder;
        } else {
            throw std::runtime_error(fmt::format("Unsupported configuration: {}", nbi_type));
        }
    }


private:
    daemon m_daemon_thread;
    nova::yaml m_config;
    std::vector<std::jthread> m_worker_threads;

    std::shared_ptr<cache> m_cache = std::make_shared<cache>();
    std::shared_ptr<handler_factory> m_handler_factory = nullptr;
    std::unique_ptr<tcp::server> m_tcp_server = nullptr;
    std::unique_ptr<pm_exposer> m_exposer;
    std::shared_ptr<metrics_registry> m_metrics;

    void init_southbound() {
        if (lookup<std::string>("interfaces.southbound.type") == "tcp") {
            const auto port = lookup<tcp::port_type>("interfaces.southbound.port");
            m_tcp_server = std::make_unique<tcp::server>(tcp::net_config { "0.0.0.0", port });
        } else {
            throw std::runtime_error("Unsupported configuration");
        }
    }

    /**
     * @brief   Create metrics registry and Prometheus Exposer.
     */
    void init_metrics() {
        m_metrics = std::make_shared<metrics_registry>();

        if (not lookup<bool>("interfaces.metrics.enabled")) {
            return;
        }

        const auto port = lookup<tcp::port_type>("interfaces.metrics.port");
        m_exposer = std::make_unique<pm_exposer>(std::to_string(port), m_metrics);
    }

    /**
     * @brief   Start a daemon thread which keeps alive the service.
     *
     * It is a blocking call.
     *
     * When the daemon stops, all other threads must be stopped.
     *
     * Daemon can be stopped via sending SIGINT or SIGTERM to the process.
     */
    void start_daemon() {
        m_daemon_thread.attach([this]() -> bool {
            // TODO(feat): If TCP server is enabled.
            const auto& m = m_tcp_server->metrics();
            m_metrics->set("connection_count", m.n_connections.load());
            m_metrics->set("tcp_buffer_size", m.buffer.load());
            m_metrics->set("tcp_buffer_capacity", m.buffer_capacity.load());

            // TODO: If Kafka is enabled. No hardcoding interface name.
            try {
                m_metrics->set("kafka_queue_size", northbound<dsp::kafka_producer>("main-nb")->queue_size());
            } catch (...) {}

            return true;
        });

        // FIXME: yaml.lookup with non-existent key
        m_daemon_thread.start(std::chrono::seconds{ lookup<int>("daemon-interval") });
        stop();
    }

    template <typename T>
    [[nodiscard]] auto lookup(const std::string& path) -> T {
        auto result = m_config.lookup<T>(fmt::format("dsp.{}", path));
        nova::topic_log::info("dsp-cfg", "{}={}", path, result);
        return result;
    }

};

void northbound_builder::build() {
    m_service_handle->m_cache->attach_northbound(
        m_name,
        std::make_unique<kafka_producer>(
            std::move(std::any_cast<std::shared_ptr<dsp::kf::properties>>(m_cfg).operator*())
        )
    );
}
auto northbound_builder::kafka_props() -> std::shared_ptr<kf::properties> {
    return std::any_cast<std::shared_ptr<dsp::kf::properties>>(m_cfg);
}

} // namespace dsp
