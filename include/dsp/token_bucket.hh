/**
 * Part of Data Stream Processing framework.
 *
 * Token bucket algorithm for rate limiting.
 */

#pragma once

#include <nova/utils.hh>

#include <chrono>
#include <thread>

namespace dsp {

class token_bucket {
public:
    token_bucket(long limit, double rate)
        : m_tokens(limit)
        , m_limit(limit)
        , m_rate(rate)
        , m_last_replenished(nova::now())
    {}

    auto take(long tokens = 1) -> long {
        m_tokens -= tokens;

        if (m_tokens < 0) {
            wait(-m_tokens);
            replenish();
        }

        return tokens;
    }

    void replenish() {
        const auto now = nova::now();
        m_tokens += accumulate(now - m_last_replenished);
        m_last_replenished = now;
    }

    auto accumulate(std::chrono::nanoseconds elapsed) -> long {
        return std::min(
            static_cast<long>(m_rate * nova::to_sec(elapsed)),
            m_limit
        );
    }

    void wait(long tokens) {
        const auto x = std::chrono::milliseconds{ static_cast<long>(static_cast<double>(tokens) / m_rate * 1000) };
        if (x < std::chrono::milliseconds{ 500 }) {
            const auto until = nova::now() + x;
            while (nova::now() < until) {}
        } else {
            std::this_thread::sleep_for(x);
        }
    }

private:
    long m_tokens;
    long m_limit;
    double m_rate;

    std::chrono::nanoseconds m_last_replenished;

};

} // namespace dsp
