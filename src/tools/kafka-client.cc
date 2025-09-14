/**
 * Part of Data Stream Processing tools.
 *
 * A Kafka client for performance measuring and functional testing.
 */

#include <dsp/cache.hh>
#include <dsp/daemon.hh>
#include <dsp/kafka.hh>
#include <dsp/main.hh>
#include <dsp/profiler.hh>
#include <dsp/stat.hh>
#include <dsp/sys.hh>
#include <dsp/tcp.hh>

#include <nova/log.hh>
#include <nova/parse.hh>
#include <nova/random.hh>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/program_options.hpp>
#pragma GCC diagnostic pop

#include <atomic>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace po = boost::program_options;
using namespace std::literals;

struct metrics {
    std::atomic_int64_t n_sent_messages;
    std::atomic_int64_t n_drop_messages;
};

class dr_callback : public dsp::kf::delivery_handler {
public:
    dr_callback(std::shared_ptr<metrics> metrics)
        : m_metrics(metrics)
    {}

    void handle_error([[maybe_unused]] dsp::kf::message_view message) {
        m_metrics->n_drop_messages.fetch_add(1);
    }

    void handle_success([[maybe_unused]] dsp::kf::message_view message) {
        m_metrics->n_sent_messages.fetch_add(1);
    }

private:
    std::shared_ptr<metrics> m_metrics;

};

[[nodiscard]] inline auto split_key_values(const std::vector<std::string>& xs) {
    auto ret = std::vector<std::pair<std::string, std::string>>{ };

    for (const auto& x : xs) {
        const auto kv = nova::split(x, "=");
        if (kv.size() != 2) {
            throw nova::exception("The result of key-value splitting must be exactly 2 elements");
        }
        ret.emplace_back(kv[0], kv[1]);
    }

    return ret;
}

auto produce(const po::variables_map& args) {
    const auto broker = args["broker"].as<std::string>();
    const auto topic = args["topic"].as<std::string>();
    const auto count = nova::to_number<long>(args["count"].as<std::string>()).value();
    const auto size = args["size"].as<std::size_t>();

    const auto data = nova::random().string<nova::alphanumeric_distribution>(size);
    nova::topic_log::debug("kfc", "Generated payload with size {}: {}", size, data);

    auto metrics = std::make_shared<struct metrics>();

    auto cfg = dsp::kf::properties{};
    cfg.bootstrap_server(broker);
    cfg.delivery_callback(std::make_unique<dr_callback>(metrics));

    if (args.contains("kafka-config")) {
        for (const auto& [key, value] : split_key_values(args["kafka-config"].as<std::vector<std::string>>())) {
            cfg.set(key, value);
        }
    }

    auto producer = dsp::kf::producer{ std::move(cfg) };

    const auto message = dsp::message{
        .key = { },
        .subject = topic,
        .properties = { { "ts", "1234" } },
        .payload = nova::data_view(data).to_vec()
    };

    auto stat = dsp::statistics{ };

    long enqueued = 0;
    while (enqueued < count) {
        if (producer.try_send(message)) {
            ++enqueued;
            if (stat.observe(message.payload.size())) {
                nova::topic_log::info("kfc", "{} - Dropped: {} - Queue: {}", stat, metrics->n_drop_messages.load(), producer.queue_size());
            }
        }
        else {
            metrics->n_drop_messages.fetch_add(1);
        }
    }

    if (not producer.flush(5s)) {
        nova::topic_log::warn("kfc", "Flush timed out");
    }

    nova::topic_log::info("kfc", "{} - Dropped: {}", stat, metrics->n_drop_messages.load());
    nova::topic_log::info("kfc", "{}", stat.summary());
}

auto consume([[maybe_unused]] const po::variables_map& args) {
    const auto broker = args["broker"].as<std::string>();
    const auto group_id = args["group-id"].as<std::string>();
    const auto topic = args["topic"].as<std::string>();
    const auto batch_size = args["batch-size"].as<std::size_t>();
    const auto exit_eof = args["exit-eof"].as<bool>();

    const auto max_messages = [&]() {
        const auto c = args["count"].as<std::string>();
        if (c == "max") {
            return std::numeric_limits<long>::max();
        }
        return nova::to_number<long>(c).value();
    }();

    auto cfg = dsp::kf::properties{};
    cfg.bootstrap_server(broker);
    cfg.group_id(group_id);
    cfg.offset_earliest();
    cfg.enable_partition_eof();

    auto stat = dsp::statistics{ };

    if (args.contains("kafka-config")) {
        for (const auto& [key, value] : split_key_values(args["kafka-config"].as<std::vector<std::string>>())) {
            cfg.set(key, value);
        }
    }

    auto consumer = dsp::kf::consumer{ std::move(cfg) };
    consumer.subscribe(topic);

    nova::topic_log::info("kfc", "Subscribed to: {}", topic);

    bool eof = false;

    while (g_sigint == 0 && stat.n_messages() < max_messages) {
        for (const auto& message : consumer.consume(batch_size)) {
            if (eof) {
                stat.reset_uptime();
                eof = false;
            }

            if (message.eof()) {
                nova::topic_log::debug(
                    "kfc",
                    "End of partition [{}] has been reached at offset {}",
                    message.partition(),
                    message.offset()
                );

                eof = true;

                // TODO(feat): Handle EOF correctly in case of multiple topics.
                if (exit_eof) {
                    nova::topic_log::info("kfc", "{}", stat);
                    return;
                }
                continue;
            }

            nova::topic_log::trace("kfc", "Message consumed: {:lkvh}", message);
            if (stat.observe(message.payload().size())) {
                nova::topic_log::info("kfc", "{} - Queue: {}", stat, consumer.queue_size());
            }
        }
    }

    nova::topic_log::info("kfc", "{}", stat);
    nova::topic_log::info("kfc", "{}", stat.summary());
}

auto parse_args_produce(const std::vector<std::string>& subargs)
        -> std::optional<boost::program_options::variables_map>
{
    auto arg_parser = po::options_description("Kafka producer client");

    arg_parser.add_options()
        ("broker,b", po::value<std::string>()->required(), "Address of the Kafka broker")
        ("topic,t", po::value<std::string>()->required(), "Topic name")
        ("count,c", po::value<std::string>()->required(), "Number of messages to send")
        ("size,s", po::value<std::size_t>()->required(), "The size of the messages to send (Max size: 65 533)")
        ("kafka-config,X", po::value<std::vector<std::string>>()->multitoken(), "Kafka configuration")
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
        ("group-id,g", po::value<std::string>()->required(), "Group ID")
        ("count,c", po::value<std::string>()->default_value("max"),
            "Number of messages to consume (note: at least batch size number of messages will be consumed)")
        ("exit-eof,e", po::value<bool>()->default_value(false), "Exit if EOF is reached")
        ("batch-size,B", po::value<std::size_t>()->default_value(1), "Consuming batch sizes")
        ("kafka-config,X", po::value<std::vector<std::string>>()->multitoken(), "Kafka configuration")
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
    nova::log::init("kfc");

    dsp::start_profiler();

    [[maybe_unused]] auto sig = dsp::signal_handler{ };
    const auto client = args["command"].as<std::string>();

    if (client == "produce") {
        produce(args);
    } else if (client == "consume") {
        consume(args);
    }

    dsp::stop_profiler();

    return EXIT_SUCCESS;
}

DSP_MAIN_ARG_PARSE(entrypoint, parse_args);
