/**
 * Part of Data Stream Processing tools.
 *
 * A TCP client for performance measuring and functional testing.
 */

#include <tools/stat.hh>

#include <libdsp/handler.hh>
#include <libdsp/main.hh>
#include <libdsp/sys.hh>
#include <libdsp/tcp-bare.hh>
#include <libdsp/tcp.hh>

#include <nova/data.hh>
#include <nova/log.hh>
#include <nova/random.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

namespace po = boost::program_options;

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("TCP server");

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

class handler : public dsp::handler_frame<handler> {
public:
    void do_eof() { nova::topic_log::info("handler", "{}", perf_summary()); };
    auto do_process(nova::data_view data) -> std::size_t {
        return data.size();
    }
};

class factory : public dsp::tcp::handler_factory {
public:
    auto create() -> std::unique_ptr<dsp::tcp::handler> override {
        return std::make_unique<handler>();
    }
};

auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::load_env_levels();
    nova::log::init("tcp-server");

    const auto port = args["port"].as<dsp::tcp::port_type>();
    auto server = dsp::tcp::server_bare{ dsp::tcp::net_config { "0.0.0.0", port } };
    server.set(std::make_unique<factory>());
    server.start();

    return EXIT_SUCCESS;
}

DSP_MAIN_ARG_PARSE(entrypoint, parse_args);
