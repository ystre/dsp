/**
 * Part of Data Stream Processing framework.
 *
 * An example service that is used for testing.
 */

#include "handler.hh"

#include <dsp/dsp.hh>
#include <dsp/handler.hh>
#include <dsp/http.hh>
#include <dsp/kafka.hh>
#include <dsp/router.hh>
#include <dsp/stat.hh>

#include <nova/error.hh>
#include <nova/expected.hh>
#include <nova/io.hh>
#include <nova/log.hh>
#include <nova/main.hh>
#include <nova/yaml.hh>

#include <boost/algorithm/string/replace.hpp>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/syslog_sink.h>

#include <sys/syslog.h>

#include <any>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>

using AppContext = std::shared_ptr<app::context>;

auto log_error(const nova::error& error) {
    nova::topic_log::error("dsp", "{}", error.message);
    return nova::expected<std::string, nova::error>{ nova::unexpect, error };
}

auto fatal(const nova::error& error) -> nova::expected<std::string, nova::error> {
    throw std::runtime_error(error.message);
}

auto read_config(const std::string& path) {
    nova::topic_log::debug("dsp", "Reading config from `{}`", path);
    return nova::expected<nova::yaml, nova::error>{
        nova::yaml(std::filesystem::path(path))
    };
}

/**
 * @brief   An example custom Kafka delivery handler.
 */
class delivery_handler : public dsp::kf::delivery_handler {
public:
    delivery_handler(std::shared_ptr<dsp::metrics_registry> m)
        : m_metrics(std::move(m))
    {}

    void handle_error(dsp::kf::message_view message) override {
        nova::topic_log::error("app", "Delivery error to [{}] ({})", message.topic(), message.error_message());
        m_metrics->increment("drop_messages_total", 1,                      { { "drop_type", "kafka_delivery" } });
        m_metrics->increment("drop_bytes_total", message.payload().size(),  { { "drop_type", "kafka_delivery" } });
    }

    void handle_success(dsp::kf::message_view message) override {
        // auto topic_name = message.topic_name();
        // boost::replace_all(topic_name, "-", "_");

        // nova::topic_log::trace("app", "Kafka delivery success to {}", message.topic_name());
        m_metrics->increment("sent_messages_total", 1,                      { { "topic", "na" } });
        m_metrics->increment("sent_bytes_total", message.payload().size(),  { { "topic", "na" } });
    }

private:
    std::shared_ptr<dsp::metrics_registry> m_metrics;

};

/**
 * @brief   Exposing Kafka throttling as a gauge.
 */
struct throttle_handler : public dsp::kf::throttle_handler {
    throttle_handler(std::shared_ptr<dsp::metrics_registry> m)
        : m_metrics(std::move(m))
    {}

    void operator()(const std::string& broker_name, std::chrono::milliseconds throttle_time) {
        m_metrics->set("kafka_throttling_time_ms", throttle_time.count(), { { "broker", broker_name } });
    }

    std::shared_ptr<dsp::metrics_registry> m_metrics;
};

/**
 * @brief   Exposing Kafka throttling as a gauge.
 */
struct statistics_handler : public dsp::kf::statistics_handler {
    statistics_handler(std::shared_ptr<dsp::metrics_registry> m)
        : m_metrics(std::move(m))
    {}

    void operator()(const std::string& json_str) {
        nova::topic_log::debug("kafka", "{}", json_str);
    }

    std::shared_ptr<dsp::metrics_registry> m_metrics;
};

/**
 * @brief   An example how to create new northbound interfaces.
 */
struct custom_northbound : public dsp::northbound_interface {
    bool send(const dsp::message& msg) override {
        const auto str = nova::data_view{ msg.payload }.as_string();
        nova::topic_log::trace("app", "Message: {}", str);
        return true;
    }

    void stop() override { /* NO-OP */ }
};

/**
 * @brief   Custom message handler for performance measuring.
 */
class kafka_message_handler : public dsp::kf::handler {
public:

    /**
     * @brief   Reimplementing `dsp::kf::handler_frame` because of custom EOF handling.
     *
     * Starts a timer at the first non-error message.
     * Logs statistics at EOF.
     *
     * TODO(feat): Measure each partition separately.
     */
    void process(dsp::kf::message_view_owned& message) override {
        if (not message.ok()) {
            if (message.eof()) {
                nova::topic_log::debug("app", "End of partition {}[{}] at offset {}", message.topic(), message.partition(), message.offset());

                if (m_stats.has_value()) {
                    nova::topic_log::info("app", "{}", m_stats->summary());
                    nova::topic_log::debug("app", "Stopping application... (SIGINT)");
                    std::raise(SIGINT);
                    m_stats = std::nullopt;
                }

                return;
            }

            nova::topic_log::warn("app", "Error message: {} ({})", message.error_message(), message.error_code());
            return;
        }

        if (not m_stats.has_value()) {
            m_stats = dsp::statistics();
        }

        do_process(message);
    }

    void bind(dsp::context ctx) override {
        m_appctx = std::any_cast<AppContext>(ctx.app);
        m_ctx = std::move(ctx);
    }

private:
    dsp::context m_ctx;
    AppContext m_appctx;

    std::optional<dsp::statistics> m_stats {};

    void do_process(dsp::kf::message_view_owned& message) {
        static const auto LabelLoadShed = std::map<std::string, std::string>{ { "drop_type", "load_shed" } };

        nova::topic_log::trace("app", "Message received {:lkvh}", message);

        const auto msg = dsp::message{
            .key = message.key().to_vec(),
            .subject = m_appctx->topic,
            .properties = {},
            .payload = message.payload().to_vec()
        };

        m_ctx.stats->increment("process_messages_total", 1);
        m_ctx.stats->increment("process_bytes_total", msg.payload.size());
        m_stats->observe(msg.payload.size());

        if (not m_ctx.cache->send(msg)) {
            m_ctx.stats->increment("drop_messages_total", 1, LabelLoadShed);
            m_ctx.stats->increment("drop_bytes_total", msg.payload.size(), LabelLoadShed);
        }
    }

};

class oam_handler {
public:
    oam_handler(std::shared_ptr<app::context> ctx, const std::string& script)
        : m_ctx(std::move(ctx))
        , m_script_path(script)
    {}

    void operator()(const http::request<http::string_body>& req, http::response<http::string_body>& res) {
        if (req.method() == http::verb::post && req.target() == "/reload") {
            if (const auto code = nova::read_file(m_script_path); not code.has_value()) {
                nova::topic_log::warn("oam", "{}", code.error().message);
            } else {
                m_ctx->script = *code;
                nova::topic_log::info("oam", "Script is reloaded");
            }
        } else {
            res.result(http::status::not_found);
            res.body() = "Endpoint not found";
        }

        res.prepare_payload();
    }

private:
    std::shared_ptr<app::context> m_ctx;
    std::string m_script_path;

};

[[nodiscard]] auto read_handler_cfg(const nova::yaml& cfg) {
    const auto handler = cfg.lookup<std::string>("app.handler");
    if (handler == "telemetry") {
        return app::handler_type::telemetry;
    } else if (handler == "passthrough") {
        return app::handler_type::passthrough;
    } else {
        throw nova::exception(fmt::format("Invalid handler type: {}", handler));
    }
}

void log_init() {
    using namespace nova::units::literals;

    auto stderr_sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("/tmp/dsp.log", nova::units::bytes{ 100_MB }.count(), 1);
    auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>("dsp", LOG_PID, LOG_USER, true);

    stderr_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%f %z] [%n @%t] %^[%l]%$ %v");
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%f %z] [%n @%t] %^[%l]%$ %v");

    nova::topic_log::create_multi(
        { "app", "dsp", "dsp-cfg", "handler", "dsp-tcp", "kafka", "oam" },
        { stderr_sink, file_sink, syslog_sink }
    );

    nova::log::init();
}

/**
 * @brief   Read configuration file and initialize DSP runtime with custom logic.
 */
auto entrypoint([[maybe_unused]] auto args) -> int {
    log_init();
    nova::topic_log::info("app", "Starting service");

    const auto cfg = nova::getenv("DSP_CONFIG")
        .or_else(fatal)
        .and_then(read_config)
    ;

    auto service = dsp::service(*cfg);
    auto nb_builder = service.cfg_northbound();

    try {
        nb_builder.kafka_props().delivery_callback(std::make_unique<delivery_handler>(service.get_metrics()));
        nb_builder.kafka_props().throttle_callback(std::make_unique<throttle_handler>(service.get_metrics()));
        nb_builder.kafka_props().statistics_callback(std::make_unique<statistics_handler>(service.get_metrics()));
        nb_builder.build();
    } catch (const std::exception& ex) {
        nova::topic_log::warn("app", "Cannot attach Kafka callbacks, northbound interface is either not enabled or not a Kafka producer");
    }

    auto app_ctx = std::make_shared<app::context>();
    app_ctx->router = dsp::router{ };
    app_ctx->topic = cfg->lookup<std::string>("app.topic");

    auto sb_builder = service.cfg_southbound();

    if (const auto sb = cfg->lookup<std::string>("dsp.interfaces.southbound.type"); sb == "tcp") {
        sb_builder.tcp_handler<app::factory>(read_handler_cfg(*cfg));
    } else if (sb == "kafka") {
        std::unique_ptr<dsp::kf::handler> handler = std::make_unique<kafka_message_handler>();
        sb_builder.kafka_handler(std::move(handler));
        sb_builder.kafka_props().offset_earliest();
    } else {
        nova::topic_log::critical("app", "Invalid southbound configuration: {}", sb);
    }

    sb_builder.bind(std::make_any<AppContext>(app_ctx));
    sb_builder.build();

    service.northbound("custom-nb", std::make_unique<custom_northbound>());

    // TODO(feat): Proper HTTP shutdown without hanging the process.
    // auto oam = dsp::http_server{
        // "0.0.0.0",
        // 9500,
        // oam_handler{ app_cfg, cfg->lookup<std::string>("app.script") },
    // };

    // logging::info("app", "Starting OAM server on port 9500");

    // auto oam_thread = std::jthread([&oam]() { oam.run(); } );

    service.start();

    nova::topic_log::info("app", "Service stopped");
    return EXIT_SUCCESS;
}

NOVA_MAIN(entrypoint);
