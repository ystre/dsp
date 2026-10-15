#include "http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <format>
#include <stdexcept>
#include <system_error>

namespace admin {

namespace {

auto reason(int status) -> std::string_view {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

/// Extracts the path of a `GET <path> HTTP/1.x` request line.
auto request_path(std::string_view req) -> std::string_view {
    if (not req.starts_with("GET ")) {
        return {};
    }
    req.remove_prefix(4);
    const auto end = req.find_first_of(" ?");
    return end == std::string_view::npos ? std::string_view{}
                                         : req.substr(0, end);
}

void write_all(int fd, std::string_view data) {
    while (not data.empty()) {
        const auto n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (n < 0 and errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return;
        }
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}

void answer(int fd, const handler& h) {
    // The client socket is blocking with a short timeout: probes and scrapes
    // send the whole request at once.
    constexpr auto timeout = timeval{ .tv_sec = 1, .tv_usec = 0 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    std::array<char, 4096> buf{};
    const auto n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
        return;
    }

    const auto path = request_path(
        std::string_view{ buf.data(), static_cast<std::size_t>(n) });
    const auto rsp = path.empty()
                         ? response{ .status = 400, .body = "bad request\n" }
                         : h(path);

    write_all(
        fd, std::format("HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: "
                        "{}\r\nConnection: close\r\n\r\n",
                        rsp.status, reason(rsp.status), rsp.content_type,
                        rsp.body.size()));
    write_all(fd, rsp.body);
}

} // namespace

http_server::http_server(std::uint16_t port)
    : m_fd(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) {
    if (m_fd < 0) {
        throw std::system_error(errno, std::system_category(), "admin: socket");
    }
    const int one = 1;
    ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(m_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0
        or ::listen(m_fd, 64) < 0) {
        const auto err = errno;
        ::close(m_fd);
        throw std::system_error(err, std::system_category(),
                                std::format("admin: bind/listen :{}", port));
    }
}

http_server::~http_server() {
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

void http_server::serve_pending(const handler& h) {
    while (true) {
        const int client = ::accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            return; // EAGAIN (nothing pending) or a transient error.
        }
        answer(client, h);
        ::close(client);
    }
}

void http_server::run(const handler& h, std::stop_token stop) {
    while (not stop.stop_requested()) {
        pollfd pfd{ .fd = m_fd, .events = POLLIN, .revents = 0 };
        if (::poll(&pfd, 1, 200) > 0) {
            serve_pending(h);
        }
    }
}

auto probe(bool ok) -> response {
    return ok ? response{ .status = 200, .body = "ok\n" }
              : response{ .status = 503, .body = "not ready\n" };
}

} // namespace admin
