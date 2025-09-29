/**
 * Part of Data Stream Processing tools.
 *
 * A WebSocket client for performance measuring and functional testing.
 *
 * Note: This is an initial, draft implementation.
 */

// TODO: Fix this "Werror" properly.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#include <libdsp/main.hpp>
#pragma GCC diagnostic pop

#include <libdsp/tcp.hpp>
#include <libdsp/stat.hpp>

#include <libnova/data.hpp>
#include <libnova/random.hpp>
#include <libnova/parse.hpp>
#include <libnova/units.hpp>

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
        ("batch,b", po::value<std::string>()->required(), "Number of messages to batch together")
        ("size,s", po::value<std::uint16_t>()->required(), "The size of the messages to send (Max size: 65 533")
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

// TODO(refact): Copy pasta'd from tcp-client.

/**
 * @brief   An example message holding randomly generated data.
 */
struct message_t {
    std::uint16_t prefix;
    std::uint16_t type;
    std::string payload;
};

/**
 * @brief   Serializer for `message_t`.
 */
auto serialize(const message_t& msg) -> nova::bytes {
    auto ser = nova::serializer_context(msg.prefix);
    ser(msg.prefix);
    ser(msg.type);
    ser(msg.payload);

    return ser.data();
}

/**
 * @brief   Generate random data with or without length prefix.
 */
[[nodiscard]] auto generate_data(std::uint16_t size) -> nova::bytes {
    const auto data = nova::random().string<nova::alphanumeric_distribution>(size);

    static constexpr std::uint16_t PrefixSize = 4;
    static constexpr std::uint16_t DynamicMessageType = 1;
    const auto length_prefix = size + PrefixSize;
    nova::log::debug("Length prefix: {}", length_prefix);

    return serialize(
        message_t{
            .prefix = static_cast<std::uint16_t>(length_prefix),
            .type = DynamicMessageType,
            .payload = data
        }
    );
}


namespace ws {

class client : public std::enable_shared_from_this<client> {
    static constexpr nova::units::bytes BufferSize { nova::units::MBytes{ 1 } };

public:
    client(boost::asio::any_io_executor exec)
        : m_resolver(exec)
        , m_websocket(exec)
    {
        m_websocket.binary(true);
        // m_websocket.auto_fragment(false);
        // m_websocket.write_buffer_bytes(BufferSize.count());
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
        // m_websocket.next_layer().set_option(boost::asio::ip::tcp::no_delay(true));
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
            boost::beast::flat_buffer buffer;
            while (true) {
                buffer.consume(buffer.size());
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
    const auto size = args["size"].as<std::uint16_t>();
    const auto count = nova::to_number<long>(args["count"].as<std::string>()).value();
    const auto batch_size = nova::to_number<int>(args["batch"].as<std::string>()).value();

    static constexpr auto BatchBufferSize = nova::units::kBytes{ 64 };

    const auto data = generate_data(size);
    nova::topic_log::debug("wsc", "Generated payload with size {}: {}", size, data);

    boost::asio::io_context ioc;

    auto client = std::make_shared<ws::client>(ioc.get_executor());

    boost::asio::co_spawn(
        ioc,
        [client, port, data, count, batch_size]() -> boost::asio::awaitable<void> {
            co_await client->connect("localhost", port);

            auto stat = dsp::statistics{ };
            long sent = 0;

            auto buffer = nova::stream_buffer<>{ nova::units::bytes{ BatchBufferSize }.count() };

            while (sent < count) {
                std::size_t sent_bytes = 0;
                for (int i = 0; i < batch_size; ++i) {
                    sent_bytes += buffer.write(data);
                    ++sent;
                }

                nova::topic_log::trace("wsc", "Sending {}", data);

                co_await client->send(buffer.view());
                buffer.consume();

                if (stat.observe(sent_bytes, batch_size)) {
                    nova::topic_log::info("wsc", "{}", stat);
                }
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
