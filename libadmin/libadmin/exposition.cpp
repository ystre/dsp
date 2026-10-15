#include "exposition.hpp"

#include <format>

namespace admin {

void exposition::family(std::string_view name, std::string_view type,
                        std::string_view help) {
    std::format_to(std::back_inserter(m_out), "# HELP {} {}\n# TYPE {} {}\n",
                   name, help, name, type);
}

void exposition::prefix(std::string_view name, std::string_view labels) {
    m_out.append(name);
    if (not labels.empty()) {
        m_out.push_back('{');
        m_out.append(labels);
        m_out.push_back('}');
    }
    m_out.push_back(' ');
}

void exposition::sample(std::string_view name, std::string_view labels,
                        std::uint64_t value) {
    prefix(name, labels);
    std::format_to(std::back_inserter(m_out), "{}\n", value);
}

void exposition::sample(std::string_view name, std::string_view labels,
                        double value) {
    prefix(name, labels);
    std::format_to(std::back_inserter(m_out), "{}\n", value);
}

} // namespace admin
