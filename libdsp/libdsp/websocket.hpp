/**
 * Part of Data Stream Processing framework.
 *
 * DSP - WebSocket handler
 */

#pragma once

#include <libdsp/tcp.hpp>
#include <libdsp/tcp_handler.hpp>

#include <libnova/data.hpp>
#include <libnova/error.hpp>
#include <libnova/log.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <memory>

namespace dsp::ws {

inline void throw_if_error(boost::beast::error_code ec) {
    if (ec) {
        throw nova::exception("{}", ec.message());
    }
}

/**
 * @brief   Handles a single WebSocket connection.
 *
 * TODO: rename to connection.
 */
class session : public std::enable_shared_from_this<session> {
public:
    explicit session(
            boost::asio::ip::tcp::socket socket,
            std::unique_ptr<tcp::handler> handler
        )
        : m_websocket(std::move(socket))
        , m_handler(std::move(handler))
        , m_connection_info({
            socket.remote_endpoint().address().to_string(),
            socket.remote_endpoint().port()
        })
    {
        m_handler->on_connection_init(m_connection_info);
    }

    void start() {
        m_websocket.async_accept(
            boost::beast::bind_front_handler(&session::on_accept, shared_from_this())
        );
    }

private:
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> m_websocket;
    boost::beast::flat_buffer m_buffer;

    std::unique_ptr<tcp::handler> m_handler;
    tcp::connection_info m_connection_info;

    void on_accept(boost::beast::error_code ec) {
        try {
            throw_if_error(ec);
            do_read();
        }
        catch (std::exception& ex) {
            nova::topic_log::warn("dsp-ws", "Error happened in WebSocket connection: {}", ex.what());
        }
    }

    /**
     * @brief   Read a message into the buffer.
     */
    void do_read() {
        m_websocket.async_read(
            m_buffer,
            boost::beast::bind_front_handler(&session::on_read, shared_from_this())
       );
    }

    void on_read(boost::beast::error_code ec, [[maybe_unused]] std::size_t bytes) {
        if (ec == boost::beast::websocket::error::closed) {
            return;
        }

        if (ec) {
            nova::topic_log::warn("dsp-ws", "Error during read: {}", ec.message());
            return;
        }

        // try {
            while (
                auto processed = m_handler->process(
                    nova::data_view{
                        m_buffer.data().data(),
                        m_buffer.size()
                    }
                )
            ) {
                m_buffer.consume(processed);
                // m_metrics->buffer -= processed;
            }
        // } catch (const nova::exception& ex) {
            // m_handler->on_error(ex, m_connection_info);
            // close();
            // break;
        // } catch (const std::exception& ex) {
            // m_handler->on_error(nova::exception(ex.what()), m_connection_info);
            // close();
            // break;
        // } catch (...) {
            // m_handler->on_error(nova::exception("Unknown error"), m_connection_info);
            // close();
            // break;
        // }

        // TODO(feat): Send message back.
        // m_websocket.text(m_websocket.got_text());

        m_websocket.async_write(
            m_buffer.data(),
            boost::beast::bind_front_handler(&session::on_write, shared_from_this())
        );
    }

    void on_write(boost::beast::error_code ec, [[maybe_unused]] std::size_t bytes) {
        if (ec) {
            nova::topic_log::warn("dsp-ws", "Error during write: {}", ec.message());
            return;
        }

        // m_buffer.consume(m_buffer.size());
        do_read();
    }
};

/**
 * @brief   Accepts incoming TCP connections and creates sessions.
 */
class server : public std::enable_shared_from_this<server> {
public:
    server(const tcp::net_config& cfg);

    void start() {
        if (m_factory == nullptr) {
            throw nova::exception("No handler factory is set in TCP server");
        }

        do_accept();

        m_io_context.run();
    }

    void stop() {
        nova::topic_log::info("dsp-tcp", "Stopping WebSocket server...");
        m_io_context.stop();
    }

    void set(std::shared_ptr<tcp::handler_factory> factory) {
        m_factory = std::move(factory);
    }

    [[nodiscard]] auto port() const -> tcp::port_type { return m_config.port; }
    // [[nodiscard]] auto metrics() -> const server_metrics& { return *m_metrics; }

private:
    boost::asio::ip::tcp::endpoint m_endpoint;
    boost::asio::io_context m_io_context;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::ip::tcp::socket m_socket;
    std::shared_ptr<tcp::handler_factory> m_factory { nullptr };
    tcp::net_config m_config;

    void do_accept() {
        m_acceptor.async_accept(
            m_socket,
            [self = shared_from_this()](boost::beast::error_code ec) {
                try {
                    if (not ec) {
                        std::make_shared<session>(
                            std::move(self->m_socket),
                            self->m_factory->create()
                            // TODO: metrics
                        )->start();
                    }

                    self->do_accept();
                }
                catch (std::exception& ex) {
                    nova::topic_log::warn("dsp-ws", "Error happened in WebSocket server: {}", ex.what());
                }
            }
        );
    }
};


} // namespace dsp::ws
