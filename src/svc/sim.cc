/**
 * Part of Data Stream Processing framework.
 *
 * A simulator to test DSP handlers.
 */

#include <dsp/daemon.hh>
#include <dsp/tcp.hh>

#include <nova/data.hh>
#include <nova/log.hh>
#include <nova/main.hh>
#include <nova/utils.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace po = boost::program_options;

struct heartbeat {
    std::uint64_t client_id;
    std::uint64_t sequence;
    std::uint64_t timestamp;
};

auto serialize(const heartbeat& data) -> nova::bytes {
    static constexpr auto LengthPrefix = static_cast<std::uint16_t>(sizeof(heartbeat) + 4);
    static constexpr auto MessageType = std::uint16_t{ 0 };

    auto ser = nova::serializer_context{ LengthPrefix };
    ser(LengthPrefix);
    ser(MessageType);
    ser(data.client_id);
    ser(data.sequence);
    ser(data.timestamp);

    return ser.data();
};

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("Telemetry simulator");

    arg_parser.add_options()
        ("help,h", "Print help message")
        ("address,a", po::value<std::string>()->default_value("localhost:7200"), "Server address")
        ("client-id", po::value<int>()->default_value(72), "Client ID used in heartbeat messages")
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
    nova::log::init();
    nova::topic_log::create(std::vector<std::string>{ "sim", "dsp-tcp" });

    [[maybe_unused]] auto sig = dsp::signal_handler{ };

    auto hb = heartbeat {
        .client_id = 72,
        .sequence = 0,
        .timestamp = static_cast<std::uint64_t>(nova::now().count())
    };

    auto client = dsp::tcp::client{};
    client.connect(args["address"].as<std::string>());

    while (g_sigint == 0) {
        const auto data = serialize(hb);
        const auto resp = client.send(nova::data_view{ data });
        hb.sequence += 1;
        hb.timestamp = static_cast<std::uint64_t>(nova::now().count());

        std::this_thread::sleep_for(1s);
    }

    return EXIT_SUCCESS;
}

NOVA_MAIN_ARG_PARSE(entrypoint, parse_args);
