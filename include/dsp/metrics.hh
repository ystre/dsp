/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Metrics
 *
 * TODO(design):
 * - Register metrics beforehand for validation.
 * - Wrap Prometheus library.
 */

#pragma once

#include <dsp/profiler.hh>
#include <nova/type_traits.hh>

#include <prometheus/counter.h>
#include <prometheus/family.h>
#include <prometheus/registry.h>

#include <functional>
#include <unordered_map>
#include <memory>
#include <string>

namespace dsp {

class metrics_registry {
    using counter_t = prometheus::Family<prometheus::Counter>;
    using gauge_t = prometheus::Family<prometheus::Gauge>;

public:
    auto increment(const std::string& name, nova::arithmetic auto value, const prometheus::Labels& labels = {}) {
        DSP_PROFILING_ZONE("metrics");
        // TODO(safety): thread-safety
        auto& family = add_counter(name);

        // TODO(perf): Add labels only once (do not call it if no labels).
        auto& x = family.Add(labels);
        x.Increment(static_cast<double>(value));
    }

    auto set(const std::string& name, nova::arithmetic auto value, const prometheus::Labels& labels = {}) {
        DSP_PROFILING_ZONE("metrics");
        // TODO(safety): thread-safety
        auto& family = add_gauge(name);

        // TODO(perf): Add labels only once (do not call it if no labels).
        auto& x = family.Add(labels);
        x.Set(static_cast<double>(value));
    }

    /**
     * @brief   For binding with Prometheus Exposer.
     */
    [[nodiscard]] auto prometheus_handle() -> std::shared_ptr<prometheus::Registry> {
        return m_registry;
    }

private:
    std::shared_ptr<prometheus::Registry> m_registry = std::make_shared<prometheus::Registry>();

    std::unordered_map<std::string, std::reference_wrapper<counter_t>> m_counters;
    std::unordered_map<std::string, std::reference_wrapper<gauge_t>> m_gauges;

    auto add_counter(const std::string& name) -> counter_t& {
        const auto it = m_counters.find(name);
        if (it != std::end(m_counters)) {
            return it->second;
        }

        auto& counter = prometheus::BuildCounter()
            .Name(name)
            // .Labels(metric.labels())
            // .Help("")
            .Register(*m_registry);

        m_counters.emplace(name, std::ref(counter));
        return counter;
    }

    auto add_gauge(const std::string& name) -> gauge_t& {
        const auto it = m_gauges.find(name);
        if (it != std::end(m_gauges)) {
            return it->second;
        }

        auto& gauge = prometheus::BuildGauge()
            .Name(name)
            // .Labels(metric.labels())
            // .Help("")
            .Register(*m_registry);

        m_gauges.emplace(name, std::ref(gauge));
        return gauge;
    }
};

} // namespace dsp
