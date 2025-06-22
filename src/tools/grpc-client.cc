/**
 * Part of Data Stream Processing tools.
 *
 * A gRPC client.
 */

#include "service.grpc.pb.h"

#include <dsp/stat.hh>
#include <dsp/main.hh>
#include <dsp/tcp.hh>

#include <nova/data.hh>
#include <nova/log.hh>
#include <nova/parse.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <fmt/format.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>

namespace po = boost::program_options;

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("gRPC client");

    arg_parser.add_options()
        ("address,t", po::value<std::string>()->required(), "Address of the target")
        ("count,c", po::value<std::string>()->required(), "Number of messages to send")
        ("data,d", po::value<std::string>()->required(), "The message payload")
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
        const service_grpc::Message* request,
        service_grpc::Message* reply
    ) -> grpc::Status override {
        reply->set_payload(fmt::format("Size: {}", request->payload().size()));
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

class grpc_client {
public:
    grpc_client(std::shared_ptr<grpc::Channel> channel)
        : m_stub(service_grpc::Trans::NewStub(channel))
    {}

    void send(nova::data_view data) {
        auto message = service_grpc::Message{ };
        message.set_payload(data.as_string());

        auto reply = service_grpc::Message{ };
        auto ctx = grpc::ClientContext{ };

        grpc::Status status = m_stub->process(&ctx, message, &reply);

        if (not status.ok()) {
            nova::log::error("gRPC error: {} [{}]", status.error_message(), static_cast<int>(status.error_code()));
        } else {
            nova::log::trace("Reply: {}", reply.payload());
        }
    }

private:
    std::unique_ptr<service_grpc::Trans::Stub> m_stub;

};


auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::init("grpc-server");

    const auto address = args["address"].as<std::string>();
    const auto data = args["data"].as<std::string>();
    const auto count = nova::to_number<long>(args["count"].as<std::string>()).value();

    auto client = grpc_client(
        grpc::CreateChannel(
            address,
            grpc::InsecureChannelCredentials()
        )
    );

    auto stat = dsp::statistics{ };
    for (long i = 0; i < count; ++i) {
        client.send(data);
        if (stat.observe(data.size())) {
            nova::log::info("{}", stat);
        }
    }

    return EXIT_SUCCESS;
}


DSP_MAIN_ARG_PARSE(entrypoint, parse_args);
