#pragma once

#include <nova/log.hh>

#include <grpcpp/grpcpp.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include <memory>

namespace dsp::grpc {

class client {
public:
    client(std::shared_ptr<grpc::Channel> channel)
        : m_stub(service_grpc::Trans::NewStub(channel))
    {
    }

    void send(nova::data_view data) {
        auto message = service_grpc::Message{ };
        message.set_payload(data.as_string());
        m_stream->Write(message);
    }

    void end_stream() {
        m_stream->WritesDone();
        auto status = m_stream->Finish();

        if (not status.ok()) {
            nova::topic_log::error("dsp", "gRPC error: {} [{}]", status.error_message(), static_cast<int>(status.error_code()));
        } else {
            nova::topic_log::debug("dsp", "Stream success");
        }
    }

private:
    std::unique_ptr<service_grpc::Trans::Stub> m_stub;
    grpc::ClientContext m_ctx;
    std::shared_ptr<grpc::ClientReaderWriter<service_grpc::Message, service_grpc::Message>> m_stream = m_stub->process(&m_ctx);

};


} // namespace dsp
