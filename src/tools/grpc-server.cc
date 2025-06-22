/**
 * Part of Data Stream Processing tools.
 *
 * A gRPC server.
 */

#include "service.grpc.pb.h"

#include <dsp/main.hh>
#include <dsp/tcp.hh>
#include <grpcpp/support/sync_stream.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <fmt/format.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include <cstdlib>
#include <iostream>
#include <optional>

namespace po = boost::program_options;

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("gRPC server");

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

class service : public service_grpc::Trans::Service {
    auto process(
        [[maybe_unused]] grpc::ServerContext* ctx,
        grpc::ServerReaderWriter<service_grpc::Message, service_grpc::Message>* stream
    ) -> grpc::Status override {
        auto request = service_grpc::Message{ };

        while (stream->Read(&request)) {
            auto reply = service_grpc::Message{ };
            reply.set_payload(fmt::format("Size: {}", request.payload().size()));
        }

        return grpc::Status::OK;
    }

};

class grpc_server {
public:
    grpc_server(const dsp::tcp::net_config& cfg) {
        m_builder.AddListeningPort(
            fmt::format("{}:{}", cfg.host, cfg.port),
            grpc::InsecureServerCredentials()
        );

        m_builder.RegisterService(&m_service);
    }

    void start() {
        auto server = m_builder.BuildAndStart();
        server->Wait();
    }

private:
    service m_service;
    grpc::ServerBuilder m_builder;

};


auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::init("grpc-server");

    const auto port = args["port"].as<dsp::tcp::port_type>();
    auto server = grpc_server{ dsp::tcp::net_config { "0.0.0.0", port } };
    // server.set(std::make_unique<factory>());
    server.start();

    return EXIT_SUCCESS;
}


DSP_MAIN_ARG_PARSE(entrypoint, parse_args);
