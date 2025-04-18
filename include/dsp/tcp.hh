/**
 * Part of Data Stream Processing framework.
 *
 * DSP - TCP server
 */

#pragma once

#include "dsp/tcp_handler.hh"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#pragma GCC diagnostic pop

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

namespace dsp::tcp {

using port_type = std::uint_least16_t;

struct net_config {
    std::string host;
    port_type port;
};

struct server_metrics {
    std::atomic_uint64_t n_connections;
    std::atomic_uint64_t buffer;
};

class server {
public:
    server(const net_config& cfg);

    /**
     * @brief   Start the TCP server.
     *
     * It's a blocking call.
     */
    void start();

    void stop();

    void set(std::shared_ptr<handler_factory_interface> factory) {
        m_factory = std::move(factory);
    }

    [[nodiscard]] auto port() const -> port_type { return m_config.port; }
    [[nodiscard]] auto metrics() -> const server_metrics& { return *m_metrics; }

private:
    boost::asio::io_context m_io_context;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::shared_ptr<handler_factory_interface> m_factory { nullptr };
    net_config m_config;

    std::shared_ptr<server_metrics> m_metrics = std::make_shared<server_metrics>();

    auto accept() -> boost::asio::awaitable<void>;
};

class client {
public:
    client();

    void connect(const net_config& cfg);
    void connect(const std::string& address);
    auto send(nova::data_view) -> nova::bytes;

private:
    boost::asio::io_context m_io_context;
    boost::asio::ip::tcp::socket m_socket;

};

} // namespace dsp::tcp
