/**
 * Part of Data Stream Processing tools.
 *
 * A TCP client for performance measuring and functional testing.
 */

#include "stat.hh"

#include <dsp/tcp.hh>
#include <dsp/token_bucket.hh>
#include <dsp/sys.hh>

#include <nova/data.hh>
#include <nova/log.hh>
#include <nova/main.hh>
#include <nova/random.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace po = boost::program_options;

struct config {
    std::size_t count;
    std::size_t batch_size;
    long rate_limit;
    bool non_blocking;
};

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
    nova::log::debug("Generated payload with size {}: {}", size, data);

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

/**
 * @brief   Make a copy of `data` `batch_size` times.
 */
[[nodiscard]] auto batch(nova::bytes data, std::size_t batch_size) -> nova::bytes {
    auto ret = nova::bytes{ };

    for (std::size_t i = 0; i < batch_size; ++i) {
        std::ranges::copy(data, std::back_inserter(ret));
    }

    return ret;
}

/**
 * @brief   Send a number of messages in batches.
 *
 * The remainder is not sent out, i.e., if the last batch is smaller than the rest.
 */
auto send(const std::string& address, const nova::bytes& message, const config& cfg) {
    auto client = dsp::tcp::client{ };
    client.connect(address);
    client.non_blocking(cfg.non_blocking);

    auto spinner = dsp::spinner{ };
    spinner.max_iterations(static_cast<std::size_t>(cfg.count));
    spinner.set_prefix("Messages sent");

    auto stat = statistics{ };
    const auto n = cfg.count / cfg.batch_size;

    auto tokens = dsp::token_bucket{ cfg.rate_limit, static_cast<double>(cfg.rate_limit) };

    try {
        for (std::size_t i = 0; i < n; ++i) {
            // TODO(feat): Reconnect.

            tokens.take(static_cast<long>(cfg.batch_size));
            const auto resp = client.send(nova::data_view{ message });
            stat.observe(message.size(), cfg.batch_size);
            spinner.set_message(stat.to_string());
            spinner.tick();
        }
    } catch (...) {
        spinner.set_prefix("Aborted");
        spinner.finish();
        throw;
    }

    spinner.set_prefix("Finished");
    spinner.finish();
}

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("TCP client");

    arg_parser.add_options()
        ("address,t", po::value<std::string>()->required(), "Address of the target")
        ("count,c", po::value<std::size_t>()->required(), "Number of messages to send")
        ("size,s", po::value<std::uint16_t>()->required(), "The size of the messages to send (Max size: 65 533")
        ("batch,b", po::value<std::size_t>()->default_value(1), "Size of the batches")
        ("rate-limit", po::value<long>()->default_value(0), "Rate limiting (MPS)")
        ("non-blocking", po::value<bool>()->default_value(false), "Whether TCP send is blocking or not")
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

auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::load_env_levels();
    nova::log::init("tcp-client");

    const auto size = args["size"].as<std::uint16_t>();
    const auto address = args["address"].as<std::string>();
    const auto batch_size = args["batch"].as<std::size_t>();
    const auto count = args["count"].as<std::size_t>();
    const auto rate_limit = args["rate-limit"].as<long>();
    const auto non_blocking = args["non-blocking"].as<bool>();

    const auto message = batch(generate_data(size), batch_size);
    send(address, message, config{ count, batch_size, rate_limit, non_blocking });

    return EXIT_SUCCESS;
}

NOVA_MAIN_ARG_PARSE(entrypoint, parse_args);
