/**
 * Prometheus text exposition format (0.0.4) builder.
 *
 * Phase 1: plain string building at scrape time. Phase 2 keeps the interface
 * and feeds it from single-writer per-reactor shards (o1 pattern).
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace admin {

class exposition {
public:
    /// Writes the `# HELP` and `# TYPE` lines of a metric family.
    void family(std::string_view name, std::string_view type,
                std::string_view help);

    /// Writes one sample; `labels` is the inside of `{...}` or empty.
    void sample(std::string_view name, std::string_view labels,
                std::uint64_t value);
    void sample(std::string_view name, std::string_view labels, double value);

    [[nodiscard]] auto str() && -> std::string { return std::move(m_out); }

private:
    std::string m_out;

    void prefix(std::string_view name, std::string_view labels);
};

} // namespace admin
