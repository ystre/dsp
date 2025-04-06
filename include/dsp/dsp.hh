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
#include <thread>
#include <type_traits>
#include <vector>

constexpr auto DspVersionMajor = 0;
constexpr auto DspVersionMinor = 1;
constexpr auto DspVersionPatch = 0;

namespace dsp {

class service;

// TODO(naming): ?
class northbound_builder {
    friend service;

public:

    /**
     * @brief   Build the interface, i.e., finish configuring.
     */
    void build();
    auto kafka_props() -> std::shared_ptr<kf::properties>;

private:
    std::string m_name;
    service* m_service_handle;
    std::any m_cfg;

    template <typename T>
    [[nodiscard]]
    static auto cast(std::any& any) -> T {
        try {
            return std::any_cast<T>(any);
        } catch (const std::bad_any_cast& ex) {
            throw nova::exception("{}", ex.what());
        }
    }

};

class southbound_builder {
    friend service;

public:

    /**
     * @brief   Build the interface, i.e., finish configuring.
     */
    void build();

    /**
     * @brief   Bind application context to southbound interface.
     *
     * It is wrapped in DSP context and forwarded to the interface.
     */
    void bind_context(std::any ctx);

    auto kafka_props() -> std::shared_ptr<kf::properties>;

    /**
     * @brief   Create a handler factory and attach it to the service.
     */
    template <typename Factory, typename ...Args>
        requires requires { std::is_base_of_v<handler_factory, Factory>; }
    void tcp_handler(Args&& ...args);

    void kafka_handler(std::unique_ptr<kafka_handler_interface> handler);

private:
    service* m_service_handle;
    std::any m_cfg;
    std::any m_ctx;

    std::unique_ptr<kafka_handler_interface> m_kafka_handler { nullptr };

    template <typename T>
    [[nodiscard]]
    static auto cast(std::any& any) -> T {
        try {
            return std::any_cast<T>(any);
        } catch (const std::bad_any_cast& ex) {
            throw nova::exception("{}", ex.what());
        }
    }

};

/**
 * @brief   The Service which provides the runtime framework.
 */
class service {
    friend northbound_builder;
    friend southbound_builder;

public:
    service(const nova::yaml& config)
        : m_config(config)
    {
        init_metrics();
    }

    void start() {
        if (m_southbound != nullptr) {
            m_worker_threads.emplace_back(m_southbound->listener());
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
        if (m_southbound != nullptr) {
            m_southbound->stop();
        }

        m_cache->stop();

        for (auto& th : m_worker_threads) {
            th.detach();
        }
    }

    [[nodiscard]] auto get_metrics() -> std::shared_ptr<metrics_registry> {
        return m_metrics;
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
        requires requires { std::is_base_of_v<northbound_interface, Interface>; }
    [[nodiscard]] auto northbound(const std::string& name) -> Interface* {
        return m_cache->get_northbound<Interface>(name);
    }

    /**
     * @brief   Configure a southbound interface.
     *
     * Supported interface types:
     * - TCP listener (immediately created)
     * - Kafka listener (deferred creation via builder)
     */
    auto cfg_southbound() -> southbound_builder {
        auto builder = southbound_builder{ };
        builder.m_service_handle = this;

        if (const auto sbi_type = lookup<std::string>("interfaces.southbound.type"); sbi_type == "tcp") {
            const auto port = lookup<tcp::port_type>("interfaces.southbound.port");
            m_southbound = std::make_unique<tcp_listener>(tcp::net_config{ "0.0.0.0", port });
        } else if (sbi_type == "kafka") {
            auto kafka_cfg = std::make_shared<kf::properties>();
            kafka_cfg->bootstrap_server(lookup<std::string>("interfaces.southbound.address"));
            kafka_cfg->group_id(lookup<std::string>("interfaces.southbound.groupid"));

            // TODO(cfg): generic librdkafka config
            // kafka_cfg->set("debug", "all");

            // FIXME: yaml.lookup with non-existent key
            try {
                kafka_cfg->statistics_interval(lookup<std::string>("interfaces.southbound.statistics-interval-ms"));
            } catch (...) {}

            builder.m_cfg = std::make_any<std::shared_ptr<kf::properties>>(kafka_cfg);
        } else if (sbi_type == "custom") {
            /* NO-OP */
        } else {
            throw nova::exception("Unsupported southbound configuration: {}", sbi_type);
        }

        return builder;
    }

    auto cfg_northbound() -> northbound_builder {
        auto builder = northbound_builder{ };
        builder.m_service_handle = this;

        if (lookup<std::string>("interfaces.northbound.type") == "custom") {
            return builder;
        }

        if (const auto nbi_type = lookup<std::string>("interfaces.northbound.type"); nbi_type == "kafka") {
            if (not lookup<bool>("interfaces.northbound.enabled")) {
                return builder;
            }

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
            throw nova::exception("Unsupported northbound configuration: {}", nbi_type);
        }
    }


private:
    daemon m_daemon_thread;
    nova::yaml m_config;
    std::vector<std::jthread> m_worker_threads;

    std::shared_ptr<cache> m_cache = std::make_shared<cache>();
    std::unique_ptr<southbound_interface> m_southbound = nullptr;
    std::unique_ptr<pm_exposer> m_exposer = nullptr;
    std::shared_ptr<metrics_registry> m_metrics = nullptr;

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
     * It exposes metrics for all interfaces that support metrics.
     *
     * It is a blocking call.
     *
     * When the daemon stops, all other threads must be stopped.
     *
     * Daemon can be stopped via sending SIGINT or SIGTERM to the process.
     */
    void start_daemon() {
        m_daemon_thread.attach([this]() -> bool {
            m_southbound->update(*m_metrics);
            for (const auto& interface : m_cache->interfaces()) {
                interface.second->update(*m_metrics);
            }

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
            std::move(cast<std::shared_ptr<dsp::kf::properties>>(m_cfg).operator*())
        )
    );
}

auto northbound_builder::kafka_props() -> std::shared_ptr<kf::properties> {
    return cast<std::shared_ptr<dsp::kf::properties>>(m_cfg);
}

void southbound_builder::build() {
    auto& sb = m_service_handle->m_southbound;

    // TODO(design): Common class interface for different southbound interfaces.
    sb = std::make_unique<kafka_listener>(
        std::move(cast<std::shared_ptr<dsp::kf::properties>>(m_cfg).operator*()),
        std::move(m_kafka_handler)
    );

    // TODO(refact): Bind context in constructor.
    sb->bind_context(
        context{
            .stats = m_service_handle->m_metrics,
            .cache = m_service_handle->m_cache,
            .app = std::move(m_ctx)
        }
    );
}

void southbound_builder::bind_context(std::any ctx) {
    m_ctx = std::move(ctx);
}

template <typename Factory, typename ...Args>
    requires requires { std::is_base_of_v<handler_factory, Factory>; }
void southbound_builder::tcp_handler(Args&& ...args) {
    auto* listener = dynamic_cast<tcp_listener*>(m_service_handle->m_southbound.get());
    if (listener == nullptr) {
        throw nova::exception("Cannot set a TCP handler on a non-TCP type southbound interface");
    }
    listener->set(std::make_shared<Factory>(m_service_handle->m_cache, std::forward<Args>(args)...));
}

void southbound_builder::kafka_handler(std::unique_ptr<kafka_handler_interface> handler) {
    m_kafka_handler = std::move(handler);
}

auto southbound_builder::kafka_props() -> std::shared_ptr<kf::properties> {
    return cast<std::shared_ptr<dsp::kf::properties>>(m_cfg);
}

} // namespace dsp
