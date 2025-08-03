/**
 * Part of Data Stream Processing framework.
 *
 * DSP - TCP server
 */

#include "dsp/tcp.hh"
#include <dsp/profiler.hh>

#include <nova/data.hh>         // TODO(refact): only an alias definition is needed from the header
#include <nova/error.hh>
#include <nova/log.hh>
#include <nova/units.hh>

#include <boost/algorithm/string.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#pragma GCC diagnostic pop

#include <cstdint>

#include <coroutine>
#include <memory>
#include <string>
#include <vector>

namespace asio = boost::asio;
using asio::ip::tcp;

namespace dsp::tcp {

class connection : public std::enable_shared_from_this<connection> {
    static constexpr nova::units::bytes BufferSize { nova::units::MBytes{ 1 } };

public:
    connection(
            ::tcp::socket socket,
            std::unique_ptr<handler> handler,
            std::shared_ptr<server_metrics> metrics
    )
        : m_socket(std::move(socket))
        , m_handler(std::move(handler))
        , m_metrics(std::move(metrics))
        , m_connection_info({
            m_socket.remote_endpoint().address().to_string(),
            m_socket.remote_endpoint().port()
        })
    {
        m_metrics->n_connections += 1;
        m_handler->on_connection_init(m_connection_info);
    }

    void start() {
        asio::co_spawn(
            m_socket.get_executor(),
            [self = shared_from_this()] { return self->handle_connection(); },
            asio::detached
        );
    }

    void close() {
        nova::topic_log::info("dsp-tcp", "Disconnecting: {}:{}", m_connection_info.address, m_connection_info.port);
        m_socket.shutdown(boost::asio::socket_base::shutdown_both);
        m_socket.close();
    }

    auto send(nova::data_view data) -> asio::awaitable<void> {
        co_await asio::async_write(m_socket, asio::buffer(data.ptr(), data.size()), asio::use_awaitable);
    }

    ~connection() {
        m_metrics->n_connections -= 1;
    }

private:
    ::tcp::socket m_socket;
    std::unique_ptr<handler> m_handler;
    std::shared_ptr<server_metrics> m_metrics;

    connection_info m_connection_info;

    /**
     * @brief   Read from socket and call the connection handler.
     *
     * The connection has an internal buffer from which the connection handler
     * is served as long as it can process some of the data.
     *
     * All thrown exceptions are reported back to handler and the connection
     * is closed after that.
     */
    auto handle_connection() -> asio::awaitable<void> {
        DSP_PROFILING_ZONE("tcp");
        auto buf = asio::streambuf{};

        while (true) {
            // FIXME(perf): Buffer size can overshoot the 1MB making a 2MB vector.
            auto buffer = buf.prepare(BufferSize.count());

            boost::system::error_code ec;
            std::size_t n = co_await m_socket.async_read_some(
                buffer,
                asio::redirect_error(asio::use_awaitable, ec)
            );

            if (ec) {
                m_handler->on_error(ec, m_connection_info);
                break;
            }

            if (n == 0) {
                continue;
            }

            buf.commit(n);
            m_metrics->buffer += n;

            try {
                while (
                    auto processed = m_handler->process(
                        nova::data_view{
                            buf.data().data(),
                            buf.size()
                        }
                    )
                ) {
                    buf.consume(processed);
                    m_metrics->buffer -= processed;
                }
            } catch (const nova::exception& ex) {
                m_handler->on_error(ex, m_connection_info);
                close();
                break;
            } catch (const std::exception& ex) {
                m_handler->on_error(nova::exception(ex.what()), m_connection_info);
                close();
                break;
            } catch (...) {
                m_handler->on_error(nova::exception("Unknown error"), m_connection_info);
                close();
                break;
            }
        }
    }
};

server::server(const net_config& cfg)
    : m_acceptor(
        m_io_context,
        ::tcp::endpoint{ ::tcp::v6(), cfg.port }
    )
    , m_config(cfg)
{}

void server::start() {
    if (m_factory == nullptr) {
        throw nova::exception("No handler factory is set in TCP server");
    }

    asio::co_spawn(
        m_acceptor.get_executor(),
        [this]() { return accept(); },
        asio::detached
    );

    m_io_context.run();
}

void server::stop() {
    nova::topic_log::info("dsp-tcp", "Stopping TCP server...");
    m_io_context.stop();
}

/**
 * @brief   Accept an incoming connection and create a connection handler.
 */
auto server::accept() -> asio::awaitable<void> {
    try {
        while (true) {
            ::tcp::socket socket = co_await m_acceptor.async_accept(asio::use_awaitable);

            std::make_shared<connection>(
                std::move(socket),
                m_factory->create(),
                m_metrics
            )->start();
        }
    } catch (const std::exception& e) {
        nova::topic_log::error("dsp-tcp", "{}", e.what());
    }
}

client::client()
    : m_socket(m_io_context)
{}

void client::connect(const net_config& cfg) {
    auto resolver = ::tcp::resolver{ m_io_context };
    ::tcp::resolver::results_type endpoints = resolver.resolve(cfg.host, std::to_string(cfg.port));

    asio::connect(m_socket, endpoints);
}

void client::connect(const std::string& address) {
    std::vector<std::string> parts;
    boost::split(parts, address, boost::is_any_of(":"), boost::algorithm::token_compress_on);

    auto resolver = ::tcp::resolver{ m_io_context };
    ::tcp::resolver::results_type endpoints = resolver.resolve(parts[0], parts[1]);

    asio::connect(m_socket, endpoints);
}

auto client::send(nova::data_view data) -> nova::bytes {
    asio::write(m_socket, asio::buffer(data.ptr(), data.size()));

    auto buffer = nova::bytes{};
    asio::read(m_socket, asio::buffer(buffer));

    return buffer;
}

// auto send(nova::data_view data) -> asio::awaitable<void> {
    // co_await asio::async_write(m_connection.m_socket, asio::buffer(data.as_string(0, data.size())), asio::use_awaitable);
// }

} // namespace dsp::tcp
