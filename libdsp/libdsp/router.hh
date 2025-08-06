/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Router
 */

#pragma once

#include <libdsp/cache.hh>
#include <libdsp/profiler.hh>

#include <string>
#include <utility>
#include <vector>

namespace dsp {

class router {
    static const std::pair<std::string, std::string> Everything;

    enum class match_type {
        exact,
    };

    enum class action_type {
        allow,
        deny,
    };

    struct rule_t {
        std::string name;
        int priority;
        std::pair<std::string, std::string> condition;
        action_type action;
        match_type matcher;
        std::string destination;
        std::string subject;
    };

public:
    router() {
        const auto rule = rule_t{
            .name = "",
            .priority = 1,
            .condition = { "type", "heartbeat" },
            .action = action_type::allow,
            .matcher = match_type::exact,
            .destination = "main-nb",
            .subject = "heartbeats"
        };

        const auto rule2 = rule_t{
            .name = "",
            .priority = 2,
            .condition = { "type", "heartbeat" },
            // .condition = { "*", "*" },
            // .action = action_type::allow,
            .action = action_type::deny,
            .matcher = match_type::exact,
            .destination = "main-nb",
            .subject = "dev-test"
        };

        // TODO(feat): Sanity check priorities (they must be unique).
        // TODO(feat): Sort by priority.
        m_rules.push_back(rule);
        m_rules.push_back(rule2);
    }

    [[nodiscard]] auto route(const dsp::message& msg) -> std::vector<dsp::message> {
        DSP_PROFILING_ZONE("route");
        auto ret = std::vector<dsp::message>{ };

        for (const auto& rule : m_rules) {
            bool is_allowed = false;
            if (rule.condition == Everything) {
                is_allowed = match("*", rule);
            } else {
                auto it = msg.properties.find(rule.condition.first);
                if (it != std::end(msg.properties)) {
                    is_allowed = match(it->second, rule);
                } else {
                    is_allowed = default_match(rule);
                }
            }

            if (is_allowed) {
                auto out = msg;
                out.subject = rule.subject;
                ret.push_back(out);
            }
        }

        return ret;
    }

private:
    std::vector<rule_t> m_rules;

    [[nodiscard]] auto match(const auto& value, const rule_t& rule) -> bool {
        if (rule.action == action_type::allow) {
            return value == rule.condition.second;
        }
        return value != rule.condition.second;
    }

    /**
     * @brief   Default boolean value when a message does not match a rule.
     *
     * A message that does not match an allow rule is not needed.
     * A message that does not match a deny rule is needed.
     */
    [[nodiscard]] auto default_match(const rule_t& rule) -> bool {
        return rule.action == action_type::deny;
    }
};

inline const std::pair<std::string, std::string> router::Everything = std::make_pair(std::string{ "*" }, std::string{ "*" });

} // namespace dsp
