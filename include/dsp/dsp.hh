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

class northbound_builder {
    friend service;

public:

    /**
     * @brief   Instantiate the interface.
     */
    void build();
    auto kafka_props() -> kf::properties&;

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

    enum class type {
        empty,
        tcp,
        kafka
    };

public:

    /**
     * @brief   Instantiate the interface.
     *
     * This function is responsible for binding DSP context with the interface.
     */
    void build();

    /**
     * @brief   Bind application context to southbound interface.
     *
     * It is wrapped in DSP context and forwarded to the interface.
     */
    void bind(std::any appctx);

    auto kafka_props() -> kf::properties&;

    /**
     * @brief   Create a handler factory and attach it to the service.
     */
    template <typename Factory, typename ...Args>
        requires requires { std::is_base_of_v<tcp::handler_factory, Factory>; }
    void tcp_handler(Args&& ...args);

    void kafka_handler(std::unique_ptr<kf::handler_interface> handler);

private:
    service* m_service_handle;
    std::any m_cfg;
    std::any m_appctx;

    type m_type { type::empty };

    std::unique_ptr<kf::handler_interface> m_kafka_handler { nullptr };
    std::shared_ptr<tcp::handler_factory> m_tcp_factory { nullptr };

    template <typename T>
    [[nodiscard]]
    static auto cast(std::any& any) -> T {
        try {
            return std::any_cast<T>(any);
        } catch (const std::bad_any_cast& ex) {
            throw nova::exception("{}", ex.what());
        }
    }

    void build_kafka();
    void build_tcp();

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
     * - TCP listener
     * - Kafka listener
     */
    auto cfg_southbound() -> southbound_builder {
        auto builder = southbound_builder{ };
        builder.m_service_handle = this;

        if (const auto sbi_type = lookup<std::string>("interfaces.southbound.type"); sbi_type == "tcp") {
            const auto port = lookup<tcp::port_type>("interfaces.southbound.port");
            builder.m_cfg = std::make_any<tcp::net_config>("0.0.0.0", port);
        } else if (sbi_type == "kafka") {
            auto cfg = std::make_shared<kafka_cfg>();
            cfg->props.bootstrap_server(lookup<std::string>("interfaces.southbound.address"));
            cfg->props.group_id(lookup<std::string>("interfaces.southbound.groupid"));
            cfg->props.enable_partition_eof();

            // TODO(cfg): generic librdkafka config
            // cfg->props.set("debug", "all");

            // FIXME: yaml.lookup with non-existent key
            try {
                cfg->props.statistics_interval(lookup<std::string>("interfaces.southbound.statistics-interval-ms"));
            } catch (...) {}

            cfg->topics = lookup<std::vector<std::string>>("interfaces.southbound.topics");
            cfg->batch_size = lookup<std::size_t>("interfaces.southbound.batchSize");

            // TODO(refact): Parse chrono from YAML.
            cfg->poll_timeout = std::chrono::milliseconds{ lookup<long>("interfaces.southbound.pollTimeoutMs") };

            builder.m_cfg = std::make_any<std::shared_ptr<kafka_cfg>>(cfg);
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

inline void northbound_builder::build() {
    m_service_handle->m_cache->attach_northbound(
        m_name,
        std::make_unique<kafka_producer>(
            std::move(cast<std::shared_ptr<dsp::kf::properties>>(m_cfg).operator*())
        )
    );
}

inline auto northbound_builder::kafka_props() -> kf::properties& {
    return cast<std::shared_ptr<dsp::kf::properties>>(m_cfg).operator*();
}

inline void southbound_builder::build() {
    if (not m_appctx.has_value()) {
        nova::topic_log::warn("dsp", "Application context is empty");
    }

    switch (m_type) {
        case type::empty:   throw nova::exception("Southbound handler is not set");
        case type::tcp:     build_tcp();    break;
        case type::kafka:   build_kafka();  break;
    }
}

inline void southbound_builder::build_kafka() {
    auto& sb = m_service_handle->m_southbound;

    sb = std::make_unique<kafka_listener>(
        context{
            .stats = m_service_handle->m_metrics,
            .cache = m_service_handle->m_cache,
            .app = std::move(m_appctx)
        },
        std::move(cast<std::shared_ptr<kafka_cfg>>(m_cfg).operator*()),
        std::move(m_kafka_handler)
    );
}

inline void southbound_builder::build_tcp() {
    auto& sb = m_service_handle->m_southbound;

    auto listener = std::make_unique<tcp_listener>(
        context{
            .stats = m_service_handle->m_metrics,
            .cache = m_service_handle->m_cache,
            .app = std::move(m_appctx)
        },
        cast<tcp::net_config>(m_cfg),
        m_tcp_factory
    );

    sb = std::move(listener);
}

inline void southbound_builder::bind(std::any appctx) {
    m_appctx = std::move(appctx);
}

template <typename Factory, typename ...Args>
    requires requires { std::is_base_of_v<tcp::handler_factory, Factory>; }
void southbound_builder::tcp_handler(Args&& ...args) {
    m_tcp_factory = std::make_shared<Factory>(std::forward<Args>(args)...);
    m_type = type::tcp;
}

inline void southbound_builder::kafka_handler(std::unique_ptr<kf::handler_interface> handler) {
    m_kafka_handler = std::move(handler);
    m_type = type::kafka;
}

inline auto southbound_builder::kafka_props() -> kf::properties& {
    return cast<std::shared_ptr<kafka_cfg>>(m_cfg)->props;
}

} // namespace dsp
