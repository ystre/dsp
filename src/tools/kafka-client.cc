/**
 * Part of Data Stream Processing tools.
 *
 * A Kafka client for performance measuring and functional testing.
 */

#include <dsp/cache.hh>
#include <dsp/ckafka.hh>
#include <dsp/tcp.hh>

#include <nova/log.hh>
#include <nova/main.hh>
#include <nova/random.hh>

#include <boost/program_options.hpp>

#include <cstddef>
#include <iostream>
#include <optional>

namespace po = boost::program_options;

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("TCP client");

    arg_parser.add_options()
        ("broker,b", po::value<std::string>()->required(), "Address of the Kafka broker")
        ("topic,t", po::value<std::string>()->required(), "Topic name")
        ("count,c", po::value<long>()->required(), "Number of messages to send")
        ("size,s", po::value<std::size_t>()->required(), "The size of the messages to send (Max size: 65 533")
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
    nova::log::init("kafka");

    const auto broker = args["broker"].as<std::string>();
    const auto topic = args["topic"].as<std::string>();
    const auto count = args["count"].as<long>();
    const auto size = args["size"].as<std::size_t>();

    const auto data = nova::random().string<nova::alphanumeric_distribution>(size);
    nova::log::info("Generated payload with size {}: {}", size, data);

    auto cfg = kf::properties{};
    cfg.bootstrap_server(broker);

    auto producer = kf::producer{ std::move(cfg) };

    const auto msg = dsp::message{
        .key = { },
        .subject = topic,
        .properties = { },
        .payload = nova::data_view(data).to_vec()
    };

    for (int i = 0; i < count; ++i) {
        producer.send_impl(msg);
    }

    // producer.flush();

    nova::log::info("Finished");

    return EXIT_SUCCESS;
}

NOVA_MAIN_ARG_PARSE(entrypoint, parse_args);
