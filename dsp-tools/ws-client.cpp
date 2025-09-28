/**
 * Part of Data Stream Processing tools.
 *
 * A WebSocket client for performance measuring and functional testing.
 *
 * Note: This is an initial, draft implementation.
 */

#include <libdsp/main.hpp>
#include <libdsp/tcp.hpp>
#include <libdsp/stat.hpp>

#include <libnova/random.hpp>
#include <libnova/parse.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <cstdlib>
#include <iostream>
#include <optional>

namespace po = boost::program_options;

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("WebSocket server");

    arg_parser.add_options()
        ("port,p", po::value<dsp::tcp::port_type>()->required(), "The port to listen on")
        ("count,c", po::value<std::string>()->required(), "Number of messages to send")
        ("size,s", po::value<std::size_t>()->required(), "The size of the messages to send (Max size: 65 533)")
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

class client : public std::enable_shared_from_this<client> {
public:
    client(boost::asio::any_io_executor exec)
        : m_resolver(exec)
        , m_websocket(exec)
    {
        m_websocket.binary(true);
    }

    /**
     * @brief   Connect to a WebSocket server.
     *
     * Spawns a reader coroutine that continuously reads.
     */
    auto connect(const std::string& host, dsp::tcp::port_type port, const std::string& target = "/")
            -> boost::asio::awaitable<void>
    {
        auto results = co_await m_resolver.async_resolve(host, fmt::format("{}", port), boost::asio::use_awaitable);
        co_await boost::asio::async_connect(m_websocket.next_layer(), results, boost::asio::use_awaitable);
        co_await m_websocket.async_handshake(host, target, boost::asio::use_awaitable);

        nova::topic_log::info("wsc", "Connected to {}:{}", host, port);

        boost::asio::co_spawn(m_websocket.get_executor(), reader(), boost::asio::detached);
    }

    /**
     * @brief   Send data.
     */
    auto send(nova::data_view data) -> boost::asio::awaitable<void> {
        co_await m_websocket.async_write(boost::asio::buffer(data.ptr(), data.size()), boost::asio::use_awaitable);
    }

private:
    boost::asio::ip::tcp::resolver m_resolver;
    boost::beast::websocket::stream<boost::asio::ip::tcp::socket> m_websocket;

    auto reader() -> boost::asio::awaitable<void> {
        try {
            while (true) {
                boost::beast::flat_buffer buffer;
                co_await m_websocket.async_read(buffer, boost::asio::use_awaitable);
                nova::topic_log::trace("wsc", "Received: {}", nova::data_view{ buffer.data().data(), buffer.data().size() });
            }
        }
        catch (const std::exception& e) {
            nova::topic_log::warn("wsc", "Error happened during reading: {}", e.what());
        }
    }
};

} // namespace ws

auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::init("wsc");

    const auto port = args["port"].as<dsp::tcp::port_type>();
    const auto size = args["size"].as<std::size_t>();
    const auto count = nova::to_number<long>(args["count"].as<std::string>()).value();

    const auto data = nova::random().string<nova::alphanumeric_distribution>(size);
    nova::topic_log::debug("wsc", "Generated payload with size {}: {}", size, data);

    boost::asio::io_context ioc;

    auto client = std::make_shared<ws::client>(ioc.get_executor());

    boost::asio::co_spawn(
        ioc,
        [client, port, data, count]() -> boost::asio::awaitable<void> {
            co_await client->connect("localhost", port);

            auto stat = dsp::statistics{ };
            long sent = 0;

            while (sent < count) {
                nova::topic_log::trace("wsc", "Sending {}", data);
                co_await client->send(data);
                if (stat.observe(data.size())) {
                    nova::topic_log::info("wsc", "{}", stat);
                }

                ++sent;
            }

            nova::topic_log::info("wsc", "Finished");
        },
        boost::asio::detached
    );

    std::thread io_thread([&ioc]{ ioc.run(); });
    io_thread.join();

    return EXIT_SUCCESS;
}

DSP_MAIN_ARG_PARSE(entrypoint, parse_args);
