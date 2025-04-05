/**
 * Part of Data Stream Processing framework.
 *
 * TCP connection handler for the actual business logic.
 */

#include "handler.hh"

#include "dsp/metrics.hh"

#include <nova/data.hh>
#include <nova/log.hh>

#include <fmt/core.h>

#include <cstdint>
#include <cstddef>
#include <string>

namespace app {

namespace dat {

    /**
     * @brief   An opaque message prefixed with a 2-byte length field.
     */
    class message {
    public:
        static constexpr auto LengthPrefixSize = std::size_t{ 2 };

        message(nova::data_view data) : m_data(data) {}

        [[nodiscard]] auto length()  const { return m_data.as_number<std::uint16_t>(0); }
        [[nodiscard]] auto payload() const { return m_data.subview(LengthPrefixSize, length()); }

    private:
        nova::data_view m_data;

    };

    class telemetry : public message {
    public:
        static constexpr auto MinimumLength = LengthPrefixSize + 2;

        enum type : std::uint16_t {
            heartbeat,
            dyn_message
        };

        telemetry(nova::data_view data)
            : message(data)
            , m_data(this->payload())
        {}

        [[nodiscard]] auto type()              const { return m_data.as_number<std::uint16_t>(0); }
        [[nodiscard]] auto payload_telemetry() const { return m_data.subview(2); }

    private:
        nova::data_view m_data;

    };

    class heartbeat : public telemetry {
    public:
        heartbeat(nova::data_view data)
            : telemetry(data)
            , m_data(this->payload_telemetry())
        {}

        [[nodiscard]] auto client_id()  const { return m_data.as_number<std::uint64_t>(0); }
        [[nodiscard]] auto sequence()   const { return m_data.as_number<std::uint64_t>(8); }
        [[nodiscard]] auto timestamp()  const { return m_data.as_number<std::uint64_t>(16); }

    private:
        nova::data_view m_data;

    };

    class dyn_message : public telemetry {
    public:
        dyn_message(nova::data_view data)
            : telemetry(data)
            , m_data(this->payload_telemetry())
        {}

        [[nodiscard]] auto view()   const { return m_data.subview(0); }
        [[nodiscard]] auto length() const { return m_data.size(); }

    private:
        nova::data_view m_data;

    };

} // namespace dat

auto handler::do_process(nova::data_view data) -> std::size_t {
    if (data.size() < dat::telemetry::MinimumLength) { return 0; }

    const auto msg = dat::telemetry{ data };
    if (data.size() < msg.length()) { return 0; }

    // TODO(feat): Register metrics beforehand to ensure proper naming.
    m_ctx.stats->increment("receive_messages_total", 1);
    m_ctx.stats->increment("receive_bytes_total", msg.length());

    switch (msg.type()) {
        case dat::telemetry::type::heartbeat:
            do_process(dat::heartbeat{ data });
            break;
        case dat::telemetry::type::dyn_message:
            do_process(dat::dyn_message{ data });
            break;
        default:
            throw nova::exception("Unsupported message type");
    }

    return msg.length();
}

[[nodiscard]] auto deserialize(dat::heartbeat data) {
    return fmt::format(
        "Client ID: {} "
        "Sequence : {} "
        "Unix epoch: {}",
        data.client_id(),
        data.sequence(),
        data.timestamp()
    );
}

/**
 * @brief   Send a message based on routing configuration.
 *
 * Messages can be mirrored to multiple places.
 *
 * The following metrics are in use:
 * - processed messages and bytes (labels: subject)
 * - dropped messages and bytes (labels: drop_type[load_shed,not_needed])
 */
void handler::send(const dsp::message& msg) {
    static const auto LabelLoadShed  = std::map<std::string, std::string>{ { "drop_type", "load_shed" } };
    static const auto LabelNotNeeded = std::map<std::string, std::string>{ { "drop_type", "not_needed" } };
    static const auto LabelSubject = [](const std::string& subject) -> std::map<std::string, std::string> {
        return { { "subject", subject} };
    };

    const auto messages = m_appctx->router.route(msg);
    for (const auto& m : messages) {
        if (m_cache->send(m)) {
            m_ctx.stats->increment("process_messages_total", 1, LabelSubject(m.subject));
            m_ctx.stats->increment("process_bytes_total", m.payload.size(), LabelSubject(m.subject));
        } else {
            m_ctx.stats->increment("drop_messages_total", 1, LabelLoadShed);
            m_ctx.stats->increment("drop_bytes_total", m.payload.size(), LabelLoadShed);
        }
    }

    if (messages.empty()) {
        m_ctx.stats->increment("drop_messages_total", 1, LabelNotNeeded);
        m_ctx.stats->increment("drop_bytes_total", msg.payload.size(), LabelNotNeeded);
    }
}

void handler::do_process(dat::heartbeat data) {
    const auto msg = dsp::message{
        .key = { nova::data_view{ std::to_string(data.client_id()) }.to_vec() },
        .subject = { },
        .properties = {
            { "type", "heartbeat" }
        },
        .payload = nova::data_view{ deserialize(data) }.to_vec()
    };

    send(msg);
}

void handler::do_process(dat::dyn_message data) {
    const auto msg = dsp::message {
        .key = { },
        .subject = { },
        .properties = { },
        .payload = data.view().to_vec()
    };

    send(msg);
}

auto passthrough_handler::do_process(nova::data_view data) -> std::size_t {
    if (data.size() < dat::message::LengthPrefixSize) { return 0; }

    const auto msg = dat::message(data);
    if (data.size() < msg.length()) { return 0; }

    // TODO(feat): Register metrics beforehand to ensure proper naming.
    m_ctx.stats->increment("receive_messages_total", 1);
    m_ctx.stats->increment("receive_bytes_total", msg.length());

    return do_process(msg);

    return msg.length();
}

auto passthrough_handler::do_process(dat::message data) -> std::size_t {
    static const auto LabelLoadShed = std::map<std::string, std::string>{ { "drop_type", "load_shed" } };

    // TODO(refact): Make the proper abstraction to handle binary data with Lua
    // auto& rawlua = m_appctx->lua.handle();
    // auto table = rawlua.create_table();
    // for (std::size_t i = 0; i < msg_size; ++i) {
        // table[i+1] = data.span()[i];
    // }

    // m_appctx->lua.store("data", table);
    // m_appctx->lua.eval(m_appctx->script);
    // auto key = m_appctx->lua.var<std::string>("key");
    // auto payload = m_appctx->lua.var<std::string>("payload");

    const auto msg = dsp::message {
        .key = {},
        .subject = m_appctx->topic,
        .properties = {},
        .payload = data.payload().to_vec()
    };

    if (not m_cache->send(msg)) {
        m_ctx.stats->increment("drop_messages_total", 1, LabelLoadShed);
        m_ctx.stats->increment("drop_bytes_total", data.length(), LabelLoadShed);
    }

    return data.length();
}

} // namespace app
