/**
 * Part of Data Stream Processing tools.
 *
 * A Kafka client for performance measuring and functional testing.
 */

#include "stat.hh"

#include <dsp/cache.hh>
#include <dsp/daemon.hh>
#include <dsp/kafka.hh>
#include <dsp/sys.hh>
#include <dsp/tcp.hh>

#include <nova/log.hh>
#include <nova/main.hh>
#include <nova/random.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace po = boost::program_options;
using namespace std::literals;

struct metrics {
    std::size_t n_sent_messages;
    std::size_t n_drop_messages;
};

class dr_callback : public dsp::kf::delivery_handler {
public:
    dr_callback(std::shared_ptr<metrics> metrics)
        : m_metrics(metrics)
    {}

    void handle_error([[maybe_unused]] const rd_kafka_message_t* message) {
        ++m_metrics->n_drop_messages;
    }

    void handle_success([[maybe_unused]] const rd_kafka_message_t* message) {
        ++m_metrics->n_sent_messages;
    }

private:
    std::shared_ptr<metrics> m_metrics;

};

auto produce(const po::variables_map& args) {
    const auto broker = args["broker"].as<std::string>();
    const auto topic = args["topic"].as<std::string>();
    const auto count = args["count"].as<long>();
    const auto size = args["size"].as<std::size_t>();

    const auto data = nova::random().string<nova::alphanumeric_distribution>(size);
    nova::log::info("Generated payload with size {}: {}", size, data);

    auto metrics = std::make_shared<struct metrics>();

    auto cfg = dsp::kf::properties{};
    cfg.bootstrap_server(broker);
    cfg.delivery_callback(std::make_unique<dr_callback>(metrics));

    auto producer = dsp::kf::producer{ std::move(cfg) };

    const auto message = dsp::message{
        .key = { },
        .subject = topic,
        .properties = { { "ts", "1234" } },
        .payload = nova::data_view(data).to_vec()
    };

    auto stat = statistics{ };

    for (int i = 0; i < count; ++i) {
        producer.try_send(message);
        if (stat.observe(message.payload.size())) {     // TODO: full message size, potentially from delivery handler
            nova::topic_log::info(
                "kafka",
                "Messages sent {} (dropped: {}) -- {}",
                metrics->n_sent_messages,
                metrics->n_drop_messages,
                stat.to_string()
            );
        }
    }
}

auto consume([[maybe_unused]] const po::variables_map& args) {
    // const auto broker = args["broker"].as<std::string>();
    // const auto group_id = args["group-id"].as<std::string>();
    // const auto topic = args["topic"].as<std::string>();
    // const auto batch_size = args["batch-size"].as<std::size_t>();

    // auto cfg = dsp::kf_rdcpp::properties{};
    // cfg.bootstrap_server(broker);
    // cfg.group_id(group_id);
    // cfg.offset_earliest();

    // auto spinner = dsp::spinner{ };
    // spinner.set_prefix("Messages consumed");

    // auto stat = statistics{ };

    // auto consumer = dsp::kf_rdcpp::consumer{ std::move(cfg) };
    // consumer.subscribe(topic);

    // while (g_sigint == 0) {
        // for (const auto& message : consumer.consume(batch_size)) {
            // stat.observe(message->len());       // TODO: full message size
            // spinner.set_message(stat.to_string());
            // spinner.tick();
        // }
        // spinner.tick();
    // }

    // spinner.set_prefix("Finished");
    // spinner.finish();
}

auto parse_args_produce(const std::vector<std::string>& subargs)
        -> std::optional<boost::program_options::variables_map>
{
    auto arg_parser = po::options_description("Kafka producer client");

    arg_parser.add_options()
        ("broker,b", po::value<std::string>()->required(), "Address of the Kafka broker")
        ("topic,t", po::value<std::string>()->required(), "Topic name")
        ("count,c", po::value<long>()->required(), "Number of messages to send")
        ("size,s", po::value<std::size_t>()->required(), "The size of the messages to send (Max size: 65 533)")
        ("help,h", "Show this help message")
    ;

    po::variables_map args;
    po::store(po::command_line_parser(subargs).options(arg_parser).run(), args);

    if (args.contains("help")) {
        std::cerr << arg_parser << "\n";
        return std::nullopt;
    }

    args.notify();
    args.insert({ "command"s, po::variable_value("produce"s, false) });

    return args;
}

auto parse_args_consume(const std::vector<std::string>& subargs)
        -> std::optional<boost::program_options::variables_map>
{
    auto arg_parser = po::options_description("Kafka producer client");

    arg_parser.add_options()
        ("broker,b", po::value<std::string>()->required(), "Address of the Kafka broker")
        ("topic,t", po::value<std::string>()->required(), "Topic name")
        ("group-id,g", po::value<std::string>(), "Group ID")
        ("batch-size,s", po::value<std::size_t>(), "Consuming batch size")
        ("help,h", "Show this help message")
    ;

    po::variables_map args;
    po::store(po::command_line_parser(subargs).options(arg_parser).run(), args);

    if (args.contains("help")) {
        std::cerr << arg_parser << "\n";
        return std::nullopt;
    }

    args.notify();
    args.insert({ "command"s, po::variable_value("consume"s, false) });

    return args;
}

auto parse_args(int argc, char* argv[]) -> std::optional<boost::program_options::variables_map> {
    auto arg_parser = po::options_description("Kafka client (producer and consumer)");

    arg_parser.add_options()
        ("command", po::value<std::string>()->required(), "produce|consume|help")
        ("subargs", po::value<std::vector<std::string>>(), "Arguments for subcommand")
    ;

    auto positional_args = po::positional_options_description{ };
    positional_args
        .add("command", 1)
        .add("subargs", -1)
    ;

    po::variables_map args;

    auto parsed = po::command_line_parser(argc, argv)
        .options(arg_parser)
        .positional(positional_args)
        .allow_unregistered()
        .run();

    po::store(parsed, args);

    const auto command = args["command"].as<std::string>();
    const auto subargs = po::collect_unrecognized(parsed.options, po::include_positional);

    if (command == "help") {
        std::cerr << arg_parser << "\n";
        return std::nullopt;
    } else if (command == "produce") {
        return parse_args_produce(subargs);
    } else if (command == "consume") {
        return parse_args_consume(subargs);
    } else {
        throw nova::exception("Unsupported command {}, command");
    }
}

auto entrypoint([[maybe_unused]] const po::variables_map& args) -> int {
    nova::log::init("kafka");

    [[maybe_unused]] auto sig = dsp::signal_handler{ };
    const auto client = args["command"].as<std::string>();

    if (client == "produce") {
        produce(args);
    } else if (client == "consume") {
        consume(args);
    }

    return EXIT_SUCCESS;
}

NOVA_MAIN_ARG_PARSE(entrypoint, parse_args);
