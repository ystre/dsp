/**
 * Part of Data Stream Processing tools.
 *
 * A TCP client for performance measuring and functional testing.
 */

#include <dsp/sys.hh>
#include <dsp/tcp.hh>

#include <nova/data.hh>
#include <nova/log.hh>
#include <nova/main.hh>
#include <nova/random.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>

namespace po = boost::program_options;

struct config {
    std::size_t count;
    std::size_t batch_size;
    std::size_t rate_limit;
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
 * @brief   Throughput and system statistics.
 *
 * - Messages per second
 * - Bit per second
 * - CPU usage
 * - Memory usage (Resident Set Size)
 *
 * TODO(feat): Summary statistics.
 * TODO(refact): Move to a common place.
 */
class statistics {
    static constexpr auto RefreshInterval = std::chrono::seconds{ 1 };

public:
    void observe(std::size_t size, std::size_t n = 1) {
        m_total_messages += n;
        m_total_bytes += size;

        if (const auto elapsed = m_timer.elapsed(); elapsed > RefreshInterval) {
            m_sys.refresh();
            m_sys_stats = m_sys.stats();
            m_timer.reset();

            double elapsed_f = nova::to_sec(elapsed);
            auto messages = m_total_messages - m_messages_prev;
            auto bytes = m_total_bytes - m_bytes_prev;

            m_mps = static_cast<double>(messages) / elapsed_f;
            m_bps = static_cast<double>(bytes) / elapsed_f * 8;

            m_messages_prev = m_total_messages;
            m_bytes_prev = m_total_bytes;
        }
    }

    auto to_string() -> std::string {
        return fmt::format(
            "{:.2f} MBps  "
            "{:.2f}k MPS "
            "  "
            "CPU: {:>5.1f}%  "
            "RSS: {:.1} MB",
            m_bps / nova::units::constants::MByte / 8,
            m_mps / nova::units::constants::kilo,
            m_sys_stats.cpu,
            m_sys_stats.rss
        );
    }

private:
    dsp::system_info m_sys;
    nova::stopwatch m_timer;

    std::size_t m_total_messages = 0;
    std::size_t m_total_bytes = 0;
    std::size_t m_messages_prev = 0;
    std::size_t m_bytes_prev = 0;

    dsp::process_stats m_sys_stats;
    double m_mps;
    double m_bps;
};

/**
 * @brief   Send a number of messages in batches.
 *
 * The remainder is not sent out, i.e., if the last batch is smaller than the rest.
 */
auto send(const std::string& address, const nova::bytes& message, const config& cfg) {
    auto client = dsp::tcp::client{ };
    client.connect(address);

    auto spinner = dsp::spinner{ };
    spinner.max_iterations(static_cast<std::size_t>(cfg.count));
    spinner.set_prefix("Messages sent");

    auto stat = statistics{ };
    const auto n = cfg.count / cfg.batch_size;

    try {
        for (std::size_t i = 0; i < n; ++i) {
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
        ("rate-limit", po::value<std::size_t>()->default_value(0), "Rate limiting (MPS)")
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
    const auto rate_limit = args["rate-limit"].as<std::size_t>();

    const auto message = batch(generate_data(size), batch_size);
    send(address, message, config{ count, batch_size, rate_limit });

    return EXIT_SUCCESS;
}

NOVA_MAIN_ARG_PARSE(entrypoint, parse_args);
