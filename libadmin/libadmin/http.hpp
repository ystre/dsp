/**
 * Minimal admin HTTP endpoint (GET only, one request per connection).
 *
 * Serves `/metrics`, `/healthz` and `/readyz` for the hello-world binaries.
 * Not a general HTTP server: requests are read once (up to 4 KiB) and answered
 * with `Connection: close`. Good enough for Prometheus and kubelet probes.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>

namespace admin {

struct response {
    int status               = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
};

using handler = std::function<response(std::string_view path)>;

class http_server {
public:
    /// Binds and listens on `0.0.0.0:port` (non-blocking). Throws on failure.
    explicit http_server(std::uint16_t port);
    ~http_server();

    http_server(const http_server&)                    = delete;
    auto operator=(const http_server&) -> http_server& = delete;

    /// Listening socket, for callers that multiplex it in their own loop.
    [[nodiscard]] auto fd() const noexcept -> int { return m_fd; }

    /// Accepts and answers every pending connection (call when `fd()` is
    /// readable).
    void serve_pending(const handler& h);

    /// Serves until `stop` is requested (for running on a dedicated thread).
    void run(const handler& h, std::stop_token stop);

private:
    int m_fd = -1;
};

/// The `/healthz` and `/readyz` answers shared by every binary.
[[nodiscard]] auto probe(bool ok) -> response;

} // namespace admin
