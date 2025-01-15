/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Kafka clients
 */

#pragma once

#include "dsp/cache.hh"
#include "dsp/metrics.hh"

#include <nova/log.hh>

#include <fmt/chrono.h>
#include <librdkafka/rdkafkacpp.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace dsp::kf {

using cfg_props = std::map<std::string, std::string>;
using delivery_callback_f = std::function<void(RdKafka::Message&)>;
using event_callback_f = std::function<void(RdKafka::Event&)>;

using kafka_record = std::unique_ptr<RdKafka::Message>;

namespace detail {

    /**
     * @brief   Delivery callback springboard.
     *
     * Contains a default implementation.
     *
     * TODO(metrics): Maybe from stat callback?
     */
    class delivery_callback : public RdKafka::DeliveryReportCb {
        struct cb_impl {
            void operator()(RdKafka::Message& message) {
                if (message.err() != RdKafka::ErrorCode::ERR_NO_ERROR) {
                    handle_error(message);
                } else {
                    handle_success(message);
                }
            }

            void handle_error(RdKafka::Message& message) {
                nova::topic_log::error("kafka", "Delivery error - {}", message.errstr());
            }

            void handle_success(RdKafka::Message& message) {
                nova::topic_log::trace(
                    "kafka",
                    "Delivery success to topic {}[{}]",
                    message.topic_name(),
                    message.offset()
                );
            }
        };

    public:
        delivery_callback()
            : m_callback(cb_impl{ })
        {}

        /**
         * @brief   Customization point.
         */
        void set_callback(delivery_callback_f func) {
            m_callback = std::move(func);
        }

        /**
         * @brief   Called by librdkafka on delivery.
         */
        void dr_cb(RdKafka::Message& message) override {
            std::invoke(m_callback, message);
        }

    private:
        delivery_callback_f m_callback;

    };

    class event_callback : public RdKafka::EventCb {
    public:
        struct cb_impl {
            void operator()(RdKafka::Event& event) {
                using enum RdKafka::Event::Type;

                switch (event.type()) {
                    case EVENT_ERROR:       handle_error(event);    break;
                    case EVENT_LOG:         handle_log(event);      break;
                    case EVENT_STATS:       handle_stats(event);    break;
                    case EVENT_THROTTLE:    handle_throttle(event); break;
                };
            }

            virtual void handle_log(const RdKafka::Event& event) {
                using enum RdKafka::Event::Severity;

                switch (event.severity()) {
                    case EVENT_SEVERITY_EMERG:    nova::topic_log::critical("kafka", "{}", event.str());  break;
                    case EVENT_SEVERITY_ALERT:    nova::topic_log::critical("kafka", "{}", event.str());  break;
                    case EVENT_SEVERITY_CRITICAL: nova::topic_log::critical("kafka", "{}", event.str());  break;
                    case EVENT_SEVERITY_ERROR:    nova::topic_log::error("kafka", "{}", event.str());     break;
                    case EVENT_SEVERITY_WARNING:  nova::topic_log::warn("kafka", "{}", event.str());      break;
                    case EVENT_SEVERITY_NOTICE:   nova::topic_log::info("kafka", "{}", event.str());      break;
                    case EVENT_SEVERITY_INFO:     nova::topic_log::info("kafka", "{}", event.str());      break;
                    case EVENT_SEVERITY_DEBUG:    nova::topic_log::debug("kafka", "{}", event.str());     break;
                }
            }

            virtual void handle_stats(const RdKafka::Event& event) {
                nova::topic_log::debug("kafka", "Received stats: {}", event.str().size());
            }

            virtual void handle_error(const RdKafka::Event& event) {
                nova::topic_log::warn("kafka", "{}", RdKafka::err2str(event.err()));
            }

            virtual void handle_throttle(const RdKafka::Event& event) {
                nova::topic_log::trace(
                    "kafka",
                    "Throttling: {} (broker: {})",
                    std::chrono::milliseconds{ event.throttle_time() },
                    event.broker_name()
                );
            }

            virtual ~cb_impl() = default;
        };

    public:
        event_callback()
            : m_callback(cb_impl{ })
        {}

        /**
         * @brief   Customization point.
         */
        void set_callback(event_callback_f func) {
            m_callback = std::move(func);
        }

        /**
         * @brief   Called by librdkafka on event delivery.
         */
        void event_cb(RdKafka::Event& event) override {
            std::invoke(m_callback, event);
        }

    private:
        event_callback_f m_callback;

    };

} // namespace detail

/**
 * @brief   A factory-like class to create the configuration.
 *
 * Dual-API
 */
class properties {
public:
    static constexpr auto BootstrapServers = "bootstrap.servers";
    static constexpr auto GroupId = "group.id";

    void bootstrap_server(const std::string& value) {
        m_cfg[BootstrapServers] = value;
    }

    /**
     * @brief   Group ID for ...
     *
     * Consumer only property.
     */
    void group_id(const std::string& value) {
        m_cfg[GroupId] = value;
    }

    void delivery_callback(std::unique_ptr<RdKafka::DeliveryReportCb> callback) {
        m_dr_cb = std::move(callback);
    }

    void event_callback(std::unique_ptr<RdKafka::EventCb> callback) {
        m_event_cb = std::move(callback);
    }

    auto create() -> std::unique_ptr<RdKafka::Conf> {
        auto config = std::unique_ptr<RdKafka::Conf>(
            RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)
        );

        if (config == nullptr) {
            throw std::runtime_error("Failed to create Kafka global configuration");
        }

        set_basic_props(config.get());

        if (m_dr_cb == nullptr) {
            m_dr_cb = std::make_unique<detail::delivery_callback>();
        }

        set(config.get(), m_dr_cb.get());

        if (m_event_cb == nullptr) {
            m_event_cb = std::make_unique<detail::event_callback>();
        }

        set(config.get(), m_event_cb.get());

        return config;
    }

    /**
     * @brief   Access the created delivery handler.
     */
    [[nodiscard]] auto delivery_callback() -> detail::delivery_callback* {
        auto* ptr = dynamic_cast<detail::delivery_callback*>(m_dr_cb.get());
        if (ptr == nullptr) {
            throw std::logic_error(
                "Delivery callback is not inherited from `delivery_callback`; "
                "Most likely it is a native librdkafka callback."
            );
        }
        return ptr;
    }

    /**
     * @brief   Access the created delivery handler.
     */
    [[nodiscard]] auto event_callback() -> detail::event_callback* {
        auto* ptr = dynamic_cast<detail::event_callback*>(m_event_cb.get());
        if (ptr == nullptr) {
            throw std::logic_error(
                "Event callback is not inherited from `event_callback`; "
                "Most likely it is a native librdkafka callback."
            );
        }
        return ptr;
    }

private:
    std::map<std::string, std::string> m_cfg;
    std::unique_ptr<RdKafka::DeliveryReportCb> m_dr_cb = nullptr;
    std::unique_ptr<RdKafka::EventCb> m_event_cb = nullptr;

    void set_basic_props(RdKafka::Conf* config) {
        std::string errstr;
        for (const auto& [k, v] : m_cfg) {
            if (config->set(k, v, errstr) != RdKafka::Conf::CONF_OK) {
                throw std::runtime_error(errstr);
            }
        }
    }

    /**
     * @brief   Set a delivery callback.
     */
    static void set(RdKafka::Conf* config, RdKafka::DeliveryReportCb* value) {
        std::string errstr;
        if (config->set("dr_cb", value, errstr) != RdKafka::Conf::CONF_OK) {
            throw std::runtime_error(errstr);
        }
    }

    /**
     * @brief   Set an event callback.
     */
    static void set(RdKafka::Conf* config, RdKafka::EventCb* value) {
        std::string errstr;
        if (config->set("event_cb", value, errstr) != RdKafka::Conf::CONF_OK) {
            throw std::runtime_error(errstr);
        }
    }

};

/**
 * @brief   A Kafka producer based on librdkafka's C++ wrapper.
 *
 * A background thread is started that continuously polls for delivery reports, etc...
 */
class producer {
    struct poller {
        RdKafka::Producer* producer;
        std::atomic_bool& keep_alive;

        // TODO(refact): Create Kafka's own metrics and expose them.
        std::shared_ptr<metrics_registry> stat;

        void operator()() {
            while (keep_alive.load()) {
                producer->poll(1000);
                if (stat != nullptr) {
                    stat->set("kafka_queue_size", producer->outq_len());
                }
            }
        }
    };

public:

    /**
     * @brief   Create a Kafka producer and start a background polling thread.
     */
    producer(properties props, std::shared_ptr<metrics_registry> metrics = nullptr)
        : m_props(std::move(props))
    {
        auto config = m_props.create();

        std::string errstr;
        m_producer = std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(config.get(), errstr));

        m_delivery_callback = m_props.delivery_callback();
        m_event_callback = m_props.event_callback();

        if (m_producer == nullptr) {
            throw std::runtime_error("Failed to create producer: " + errstr);
        }

        m_poll_thread = std::jthread(
            poller{
                m_producer.get(),
                m_keep_alive,
                std::move(metrics)
            }
        );
    }

    producer(const producer&)               = delete;
    producer(producer&&)                    = delete;
    producer& operator=(const producer&)    = delete;
    producer& operator=(producer&&)         = delete;

    ~producer() {
        if (m_producer != nullptr) {
            m_producer->flush(5000);
            if (m_keep_alive.load()) {
                stop();
            }
        }
    }

    void delivery_callback(delivery_callback_f callback) {
        m_delivery_callback->set_callback(std::move(callback));
    }

    void event_callback(event_callback_f callback) {
        m_event_callback->set_callback(std::move(callback));
    }

    /**
     * @brief   Enqueue a message.
     *
     * Spins in a loop if the queue is full. (Polling interval is 100ms by default)
     *
     * @throws  if an unexpected error happens:
     *            - Message is larger than "messages.max.bytes"
     *            - Unknown partition or topic
     */
    void send(const message& msg) {
        bool doit = true;

        while (doit) {
            const auto status = send_impl(msg);

            switch (status) {
                case RdKafka::ERR_MSG_SIZE_TOO_LARGE:   throw std::runtime_error("Too large message");
                case RdKafka::ERR__UNKNOWN_PARTITION:   throw std::runtime_error("Unknown partition");
                case RdKafka::ERR__UNKNOWN_TOPIC:       throw std::runtime_error("Unknown topic");
                default: ; /* NO-OP */
            }

            if (status == RdKafka::ERR__QUEUE_FULL) {
                m_producer->poll(100);
            } else {
                doit = false;
            }
        }
    }

    /**
     * @brief   Try to enqueue a message.
     *
     * If the internal producer queue is full, the function returns `false`.
     *
     * Use it in case of load shedding.
     *
     * @throws  if an unexpected error happens:
     *            - Message is larger than "messages.max.bytes"
     *            - Unknown partition or topic
     */
    auto try_send(const message& msg) -> bool {
        const auto status = send_impl(msg);

        switch (status) {
            case RdKafka::ERR_MSG_SIZE_TOO_LARGE:   throw std::runtime_error("Too large message");
            case RdKafka::ERR__UNKNOWN_PARTITION:   throw std::runtime_error("Unknown partition");
            case RdKafka::ERR__UNKNOWN_TOPIC:       throw std::runtime_error("Unknown topic");
            case RdKafka::ERR__QUEUE_FULL:          return false;
            default: ; /* NO-OP */
        }

        return true;
    }

    void flush() {
        m_producer->flush(5000);
    }

    void stop() {
        nova::topic_log::info("kafka", "Stopping Kafka...");
        m_keep_alive.store(false);
    }

    [[nodiscard]] auto queue() const -> int {
        return m_producer->outq_len();
    }

private:
    properties m_props;
    std::atomic_bool m_keep_alive { true };
    std::unique_ptr<RdKafka::Producer> m_producer { nullptr };

    std::jthread m_poll_thread;
    detail::delivery_callback* m_delivery_callback;
    detail::event_callback* m_event_callback;

    auto send_impl(const message& msg) -> RdKafka::ErrorCode {
        return m_producer->produce(
            msg.subject,
            RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,

            const_cast<std::byte*>(msg.payload.data()),
            msg.payload.size(),

            msg.key.data(),
            msg.key.size(),

            0,
            nullptr,
            nullptr
        );
    }

};

class consumer {
public:
    consumer(properties props)
        : m_props(std::move(props))
    {
        auto config = m_props.create();

        std::string errstr;
        m_consumer = std::unique_ptr<RdKafka::KafkaConsumer>(RdKafka::KafkaConsumer::create(config.get(), errstr));

        if (m_consumer == nullptr) {
            throw std::runtime_error("Failed to create consumer: " + errstr);
        }
    }

    consumer(const consumer&)               = delete;
    consumer(consumer&&)                    = delete;
    consumer& operator=(const consumer&)    = delete;
    consumer& operator=(consumer&&)         = delete;

    ~consumer() = default;

    void subscribe(const std::vector<std::string>& topics) {
        RdKafka::ErrorCode err = m_consumer->subscribe(topics);
        if (err) {
            throw nova::exception("Failed to subscribe to {} ({})", topics, RdKafka::err2str(err));
        }
    }

    void subscribe(const std::string& topic) {
        m_consumer->subscribe({ topic });
    }

    auto consume(std::size_t batch_size = 1, std::chrono::milliseconds timeout = std::chrono::milliseconds{ 500 }) -> std::vector<kafka_record> {
        auto msgs = std::vector<kafka_record>();
        msgs.reserve(batch_size);

        for (std::size_t i = 0; i < batch_size; ++i) {
            auto msg = kafka_record(m_consumer->consume(static_cast<int>(timeout.count())));

            nova_assert(msg != nullptr);
            switch (msg->err()) {
                case RdKafka::ERR_NO_ERROR:
                    msgs.push_back(std::move(msg));
                    break;
                case RdKafka::ERR__TIMED_OUT:
                    return msgs;
                default:
                    nova::topic_log::error("kafka", "Consumer error: {}", msg->errstr());
                    return msgs;
            }
        }

        return msgs;
    }

private:
    properties m_props;
    std::unique_ptr<RdKafka::KafkaConsumer> m_consumer { nullptr };

};

} // namespace dsp::kf
