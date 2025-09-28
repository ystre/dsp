/**
 * Part of Data Stream Processing tools.
 *
 * A WebSocket server for performance measuring and functional testing.
 *
 * Note: This is an initial, draft implementation.
 */

#include <libdsp/main.hpp>
#include <libdsp/tcp.hpp>
#include <libnova/error.hpp>
#include <libnova/log.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>

namespace po = boost::program_options;

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("WebSocket server");

    arg_parser.add_options()
        ("port,p", po::value<dsp::tcp::port_type>()->required(), "The port to listen on")
        ("help,h", "Show this help message")
    ;

    po::variables_map args;
    po::store(po::parse_command_line(argc, argv, arg_parser), args);

    if (args.contains("help")) {
        std::cerr << arg_parser << "\n";
        return std::nullopt;
    }

    args.notify();

    return args;
}

namespace ws {

void throw_if_error(boost::beast::error_code ec) {
    if (ec) {
        throw nova::exception("{}", ec.message());
    }
}

/**
 * @brief   Handles a single WebSocket connection.
 */
class session : public std::enable_shared_from_this<session> {
public:
    explicit session(boost::asio::ip::tcp::socket socket)
        : m_websocket(std::move(socket))
    {}

    void run() {
        m_websocket.async_accept(
            boost::beast::bind_front_handler(&session::on_accept, shared_from_this())
        );
    }

private:
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> m_websocket;
    boost::beast::flat_buffer m_buffer;

    void on_accept(boost::beast::error_code ec) {
        try {
            throw_if_error(ec);
            do_read();
        }
        catch (std::exception& ex) {
            nova::topic_log::warn("wss", "Error happened in WebSocket connection: {}", ex.what());
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
            nova::topic_log::warn("wss", "Error during read: {}", ec.message());
            return;
        }

        // TODO(feat): Send message back.
        // m_websocket.text(m_websocket.got_text());

        m_websocket.async_write(
            m_buffer.data(),
            boost::beast::bind_front_handler(&session::on_write, shared_from_this())
        );
    }

    void on_write(boost::beast::error_code ec, [[maybe_unused]] std::size_t bytes) {
        if (ec) {
            nova::topic_log::warn("wss", "Error during write: {}", ec.message());
            return;
        }

        m_buffer.consume(m_buffer.size());
        do_read();
    }
};

/**
 * @brief   Accepts incoming TCP connections and creates sessions.
 */
class server : public std::enable_shared_from_this<server> {
public:
    server(boost::asio::io_context& ioc, boost::asio::ip::tcp::endpoint endpoint)
        : m_acceptor(ioc)
        , m_socket(ioc)
    {
        boost::beast::error_code ec;

        m_acceptor.open(endpoint.protocol(), ec);
        throw_if_error(ec);

        m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
        throw_if_error(ec);

        m_acceptor.bind(endpoint, ec);
        throw_if_error(ec);

        m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        throw_if_error(ec);
    }

    void run() {
        do_accept();
    }

private:
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::asio::ip::tcp::socket m_socket;

    void do_accept() {
        m_acceptor.async_accept(
            m_socket,
            [self = shared_from_this()](boost::beast::error_code ec) {
                try {
                    if (not ec) {
                        std::make_shared<session>(std::move(self->m_socket))->run();
                    }

                    self->do_accept();
                }
                catch (std::exception& ex) {
                    nova::topic_log::warn("wss", "Error happened in WebSocket server: {}", ex.what());
                }
            }
        );
    }
};

} // namespace ws

auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::init("wss");

    const auto port = args["port"].as<dsp::tcp::port_type>();
    boost::asio::io_context ioc;
    auto endpoint = boost::asio::ip::tcp::endpoint{boost::asio::ip::tcp::v4(), port};

    std::make_shared<ws::server>(ioc, endpoint)->run();

    ioc.run();

    return EXIT_SUCCESS;
}

DSP_MAIN_ARG_PARSE(entrypoint, parse_args);
