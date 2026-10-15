/**
 * Hello-world server (Phase 1).
 *
 * Exercises the CI pipeline, the image, the Helm charts and the dashboards:
 * one thread, poll(), the benchmark wire protocol (data / echo / req) and the
 * Phase 1 subset of the metrics contract (contract/metrics.toml).
 *
 * This is NOT the design. The real server (thread-per-core reactors over
 * io_uring / epoll, doc/architecture.adoc) is Phase 2.
 */

#include <libadmin/exposition.hpp>
#include <libadmin/http.hpp>
#include <libproto/frame.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr auto metric_prefix = std::string_view{ PROJ_METRIC_PREFIX };

std::atomic_bool g_stop{ false };

void on_signal(int /*signo*/) {
    g_stop.store(true);
}

struct options {
    std::uint16_t port       = 7200;
    std::uint16_t admin_port = 9555;
};

struct counters {
    std::uint64_t accepted        = 0;
    std::uint64_t closed_peer     = 0;
    std::uint64_t closed_error    = 0;
    std::uint64_t closed_protocol = 0;
    std::uint64_t rx_bytes        = 0;
    std::uint64_t tx_bytes        = 0;
    std::uint64_t rx_data         = 0;
    std::uint64_t rx_echo_req     = 0;
    std::uint64_t rx_req          = 0;
    std::uint64_t tx_echo_rsp     = 0;
    std::uint64_t tx_rsp          = 0;
    std::uint64_t err_bad_magic   = 0;
    std::uint64_t err_bad_version = 0;
    std::uint64_t err_too_large   = 0;
    std::uint64_t err_bad_type    = 0;
    std::uint64_t loop_iterations = 0;
    std::uint64_t open            = 0;
};

struct connection {
    int fd = -1;
    std::vector<std::byte> in;  ///< Received, not yet processed bytes.
    std::vector<std::byte> out; ///< Replies built while processing one read.
};

auto usage() -> std::string {
    return std::format(
        "Usage: {}-server [--port N] [--admin-port N] [--version]\n"
        "Hello-world server (Phase 1): data / echo / req over the benchmark "
        "protocol.\n",
        PROJ_NAME);
}

auto parse(int argc, char** argv) -> options {
    options opts;
    const auto args =
        std::span{ argv, static_cast<std::size_t>(argc) }.subspan(1);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto arg = std::string_view{ args[i] };
        auto next      = [&]() -> std::uint16_t {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(
                    std::format("missing value for {}", arg));
            }
            return static_cast<std::uint16_t>(std::stoul(args[++i]));
        };
        if (arg == "--port") {
            opts.port = next();
        } else if (arg == "--admin-port") {
            opts.admin_port = next();
        } else if (arg == "--version") {
            std::cout << std::format("{}-server {} ({})\n", PROJ_NAME,
                                     PROJ_VERSION, PROJ_GIT_HASH);
            std::exit(0);
        } else if (arg == "--help" or arg == "-h") {
            std::cout << usage();
            std::exit(0);
        } else {
            throw std::invalid_argument(
                std::format("unknown argument: {}", arg));
        }
    }
    return opts;
}

auto listen_tcp(std::uint16_t port) -> int {
    const int fd =
        ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::system_category(), "socket");
    }
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0
        or ::listen(fd, SOMAXCONN) < 0) {
        throw std::system_error(errno, std::system_category(),
                                std::format("bind/listen :{}", port));
    }
    return fd;
}

/// Blocking write of the whole buffer (hello-world simplification: no send
/// queue).
auto write_all(int fd, std::span<const std::byte> data) -> bool {
    while (not data.empty()) {
        pollfd pfd{ .fd = fd, .events = POLLOUT, .revents = 0 };
        const auto n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n < 0 and (errno == EAGAIN or errno == EINTR)) {
            ::poll(&pfd, 1, 100);
            continue;
        }
        if (n <= 0) {
            return false;
        }
        data = data.subspan(static_cast<std::size_t>(n));
    }
    return true;
}

void append_reply(connection& conn, proto::header h,
                  std::span<const std::byte> payload) {
    const auto offset = conn.out.size();
    conn.out.resize(offset + proto::header_size + h.len);
    proto::encode(
        h, std::span{ conn.out }.subspan(offset).first<proto::header_size>());
    if (not payload.empty()) {
        std::memcpy(conn.out.data() + offset + proto::header_size,
                    payload.data(), payload.size());
    }
}

/**
 * @brief   Processes every complete frame in `conn.in`.
 *
 * @returns false on a protocol error (the connection must be closed).
 */
auto process(connection& conn, counters& c) -> bool {
    std::size_t pos = 0;
    while (true) {
        const auto avail = std::span{ conn.in }.subspan(pos);
        const auto res   = proto::decode(avail);
        if (not res.value) {
            switch (res.error) {
                case proto::decode_error::short_buffer: break;
                case proto::decode_error::bad_magic:
                    ++c.err_bad_magic;
                    return false;
                case proto::decode_error::bad_version:
                    ++c.err_bad_version;
                    return false;
                case proto::decode_error::too_large:
                    ++c.err_too_large;
                    return false;
            }
            break;
        }
        const auto& h = *res.value;
        if (avail.size() < proto::header_size + h.len) {
            break;
        }
        const auto payload = avail.subspan(proto::header_size, h.len);

        switch (h.type) {
            case proto::msg_type::data: ++c.rx_data; break;
            case proto::msg_type::echo_req: {
                ++c.rx_echo_req;
                auto rsp = h;
                rsp.type = proto::msg_type::echo_rsp;
                append_reply(conn, rsp, payload);
                ++c.tx_echo_rsp;
                break;
            }
            case proto::msg_type::req: {
                ++c.rx_req;
                if (h.aux > proto::max_payload) {
                    ++c.err_too_large;
                    return false;
                }
                auto rsp = h;
                rsp.type = proto::msg_type::rsp;
                rsp.len  = h.aux;
                rsp.aux  = 0;
                append_reply(conn, rsp,
                             {}); // Zero-filled payload of `aux` bytes.
                ++c.tx_rsp;
                break;
            }
            default: ++c.err_bad_type; return false;
        }
        pos += proto::header_size + h.len;
    }
    conn.in.erase(conn.in.begin(),
                  conn.in.begin() + static_cast<std::ptrdiff_t>(pos));
    return true;
}

auto metrics(const counters& c) -> std::string {
    const auto p   = metric_prefix;
    const auto lbl = std::string_view{ R"(reactor="0",transport="tcp")" };
    auto n         = [p](std::string_view suffix) {
        return std::format("{}_{}", p, suffix);
    };

    admin::exposition out;
    out.family(n("build_info"), "gauge", "Always 1.");
    out.sample(n("build_info"),
               std::format(R"(version="{}",revision="{}",backend="hello")",
                           PROJ_VERSION, PROJ_GIT_HASH),
               std::uint64_t{ 1 });
    out.family(n("reactors"), "gauge", "Number of reactors.");
    out.sample(n("reactors"), "", std::uint64_t{ 1 });

    out.family(n("connections_accepted_total"), "counter",
               "Accepted connections.");
    out.sample(n("connections_accepted_total"), lbl, c.accepted);
    out.family(n("connections_closed_total"), "counter", "Closed connections.");
    out.sample(n("connections_closed_total"),
               std::format(R"({},reason="peer")", lbl), c.closed_peer);
    out.sample(n("connections_closed_total"),
               std::format(R"({},reason="error")", lbl), c.closed_error);
    out.sample(n("connections_closed_total"),
               std::format(R"({},reason="protocol")", lbl), c.closed_protocol);
    out.family(n("connections"), "gauge", "Open connections.");
    out.sample(n("connections"), lbl, c.open);

    out.family(n("rx_bytes_total"), "counter", "Bytes received.");
    out.sample(n("rx_bytes_total"), lbl, c.rx_bytes);
    out.family(n("tx_bytes_total"), "counter", "Bytes sent.");
    out.sample(n("tx_bytes_total"), lbl, c.tx_bytes);

    out.family(n("rx_messages_total"), "counter", "Frames received.");
    out.sample(n("rx_messages_total"), std::format(R"({},type="data")", lbl),
               c.rx_data);
    out.sample(n("rx_messages_total"),
               std::format(R"({},type="echo_req")", lbl), c.rx_echo_req);
    out.sample(n("rx_messages_total"), std::format(R"({},type="req")", lbl),
               c.rx_req);
    out.family(n("tx_messages_total"), "counter", "Frames sent.");
    out.sample(n("tx_messages_total"),
               std::format(R"({},type="echo_rsp")", lbl), c.tx_echo_rsp);
    out.sample(n("tx_messages_total"), std::format(R"({},type="rsp")", lbl),
               c.tx_rsp);

    out.family(n("protocol_errors_total"), "counter", "Malformed input.");
    out.sample(n("protocol_errors_total"),
               std::format(R"({},reason="bad_magic")", lbl), c.err_bad_magic);
    out.sample(n("protocol_errors_total"),
               std::format(R"({},reason="bad_version")", lbl),
               c.err_bad_version);
    out.sample(n("protocol_errors_total"),
               std::format(R"({},reason="too_large")", lbl), c.err_too_large);
    out.sample(n("protocol_errors_total"),
               std::format(R"({},reason="bad_type")", lbl), c.err_bad_type);

    out.family(n("loop_iterations_total"), "counter", "Event-loop iterations.");
    out.sample(n("loop_iterations_total"), R"(reactor="0")", c.loop_iterations);
    return std::move(out).str();
}

void run(const options& opts) {
    const int listener = listen_tcp(opts.port);
    admin::http_server admin_http{ opts.admin_port };
    counters c;

    std::vector<connection> conns;
    std::vector<pollfd> pfds;
    std::array<std::byte, 64 * 1024> buf{};

    const auto admin_handler = [&c](std::string_view path) -> admin::response {
        if (path == "/metrics") {
            return { .content_type = "text/plain; version=0.0.4; charset=utf-8",
                     .body         = metrics(c) };
        }
        if (path == "/healthz" or path == "/readyz") {
            return admin::probe(not g_stop.load());
        }
        return { .status = 404, .body = "not found\n" };
    };

    std::cerr << std::format("{}-server {} listening on :{} (admin :{})\n",
                             PROJ_NAME, PROJ_VERSION, opts.port,
                             opts.admin_port);

    while (not g_stop.load()) {
        pfds.clear();
        pfds.push_back({ .fd = listener, .events = POLLIN, .revents = 0 });
        pfds.push_back(
            { .fd = admin_http.fd(), .events = POLLIN, .revents = 0 });
        for (const auto& conn : conns) {
            pfds.push_back({ .fd = conn.fd, .events = POLLIN, .revents = 0 });
        }

        if (::poll(pfds.data(), pfds.size(), 200) < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::system_category(), "poll");
        }
        ++c.loop_iterations;

        if (pfds[1].revents & POLLIN) {
            admin_http.serve_pending(admin_handler);
        }

        // Connections first (their indices in `pfds` are offset by 2), then
        // accept.
        for (std::size_t i = conns.size(); i-- > 0;) {
            const auto revents = pfds[i + 2].revents;
            if (revents == 0) {
                continue;
            }
            auto& conn   = conns[i];
            const auto n = ::recv(conn.fd, buf.data(), buf.size(), 0);
            bool close   = false;
            if (n > 0) {
                c.rx_bytes += static_cast<std::uint64_t>(n);
                conn.in.insert(conn.in.end(), buf.begin(), buf.begin() + n);
                if (not process(conn, c)) {
                    ++c.closed_protocol;
                    close = true;
                } else if (not conn.out.empty()) {
                    if (write_all(conn.fd, conn.out)) {
                        c.tx_bytes += conn.out.size();
                    } else {
                        ++c.closed_error;
                        close = true;
                    }
                    conn.out.clear();
                }
            } else if (n == 0) {
                ++c.closed_peer;
                close = true;
            } else if (errno != EAGAIN and errno != EINTR) {
                ++c.closed_error;
                close = true;
            }
            if (close) {
                ::close(conn.fd);
                conns.erase(conns.begin() + static_cast<std::ptrdiff_t>(i));
                --c.open;
            }
        }

        if (pfds[0].revents & POLLIN) {
            while (true) {
                const int fd = ::accept4(listener, nullptr, nullptr,
                                         SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (fd < 0) {
                    break;
                }
                const int one = 1;
                ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
                conns.push_back({ .fd = fd, .in = {}, .out = {} });
                ++c.accepted;
                ++c.open;
            }
        }
    }

    std::cerr << std::format(
        "shutting down: accepted={} rx_bytes={} rx_messages={}\n", c.accepted,
        c.rx_bytes, c.rx_data + c.rx_echo_req + c.rx_req);
    for (const auto& conn : conns) {
        ::close(conn.fd);
    }
    ::close(listener);
}

} // namespace

auto main(int argc, char** argv) -> int {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    try {
        run(parse(argc, argv));
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n' << usage();
        return 1;
    }
    return 0;
}
