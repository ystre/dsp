/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Kafka clients
 *
 * Wrappers around librdkafka's C API for maximum performance.
 *
 * Dev note: low-level code, pay extra attention when changing the code and use sanitizers!
 */

#pragma once

#include <dsp/cache.hh>

#include <nova/error.hh>
#include <nova/log.hh>

#include <librdkafka/rdkafka.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace dsp::kf {

template <typename Func>
struct caller {
    caller(Func fn)
        : m_fn(fn)
    {}

    template <typename T>
    void operator()(T* obj) const {
        m_fn(obj);
    }

private:
    std::function<Func> m_fn;
};

/**
 * @brief   An abstraction that hides C API and delegates to error and success handlers.
 *
 * TODO(design): Acutally hide the `rd_kafka_message_t` type from client code.
 */
class delivery_handler {
public:
    void operator()(const rd_kafka_message_t* message) {
        if (message->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            handle_error(message);
        } else {
            handle_success(message);
        }
    }

    virtual ~delivery_handler() = default;

protected:
    virtual void handle_error(const rd_kafka_message_t*) = 0;
    virtual void handle_success(const rd_kafka_message_t*) = 0;
};

class throttle_handler {
public:
    virtual void operator()(const std::string& broker_name, std::chrono::milliseconds throttle_time) = 0;
    virtual ~throttle_handler() = default;
};

class statistics_handler {
public:
    virtual void operator()(const std::string& json_str) = 0;
    virtual ~statistics_handler() = default;
};

namespace detail {

    enum RdKafkaLogLevel {
        emerg = 0,
        alert = 1,
        crit = 2,
        err = 3,
        warning = 4,
        notice = 5,
        info = 6,
        debug = 7,
    };

    constexpr std::size_t ErrorMsgLength = 512;

    struct callbacks_t {
        std::unique_ptr<delivery_handler> delivery = nullptr;
        std::unique_ptr<throttle_handler> throttle = nullptr;
        std::unique_ptr<statistics_handler> statistics = nullptr;
    };

    /**
     * @brief   Trampoline function to call delivery handler object.
     */
    inline void delivery_callback([[maybe_unused]] rd_kafka_t* client, const rd_kafka_message_t* message, void* opaque) {
        auto* context = static_cast<callbacks_t*>(opaque);
        context->delivery->operator()(message);
    }

    /**
     * @brief   Trampoline function to call throttle handler object.
     */
    inline void throttle_callback(
            [[maybe_unused]] rd_kafka_t* client,
            const char* broker_name,
            [[maybe_unused]] int32_t broker_id,
            int throttle_time_ms,
            void* opaque)
    {
        auto* context = static_cast<callbacks_t*>(opaque);
        context->throttle->operator()(broker_name, std::chrono::milliseconds{ throttle_time_ms });
    }

    inline void log_callback(
            [[maybe_unused]] const rd_kafka_t* client,
            int level,
            [[maybe_unused]] const char* fac,
            const char* msg)
    {
        using enum RdKafkaLogLevel;

        switch (level) {
            case emerg:     nova::topic_log::critical("kafka", "{}", msg);  break;
            case alert:     nova::topic_log::critical("kafka", "{}", msg);  break;
            case crit:      nova::topic_log::critical("kafka", "{}", msg);  break;
            case err:       nova::topic_log::error("kafka", "{}", msg);     break;
            case warning:   nova::topic_log::warn("kafka", "{}", msg);      break;
            case notice:    nova::topic_log::info("kafka", "{}", msg);      break;
            case info:      nova::topic_log::info("kafka", "{}", msg);      break;
            case debug:     nova::topic_log::debug("kafka", "{}", msg);     break;
        }
    }

    /**
     * @brief   Trampoline function to call statistics handler object.
     */
    inline int statistics_callback([[maybe_unused]] rd_kafka_t* client, char* json, size_t json_len, void* opaque) {
        auto* context = static_cast<callbacks_t*>(opaque);
        context->statistics->operator()(std::string{ json, json_len });
        return 0;
    }

} // namespace detail

/**
 * @brief   A factory-like class to create the configuration.
 *
 * Dual-API:
 * - "famous" properties are exposed via named functions,
 * - otherwise `set(key, value)` can be used.
 *
 * It holds both producer and consumer properties; not all of them applies to both.
 */
class properties {
    using delivery_callback_signature = void(rd_kafka_t*, const rd_kafka_message_t*, void*);
    using throttle_callback_signature = void(rd_kafka_t*, const char*, int32_t, int, void*);
    using statistics_callback_signature = int(rd_kafka_t*, char*, size_t, void*);

public:
    static constexpr auto BootstrapServers = "bootstrap.servers";
    static constexpr auto GroupId = "group.id";
    static constexpr auto OffsetReset = "auto.offset.reset";
    static constexpr auto StatisticsInterval = "statistics.interval.ms";

    /**
     * @brief   Set an arbitrary property.
     */
    void set(const std::string& key, const std::string& value) {
        m_cfg[key] = value;
    }

    void bootstrap_server(const std::string& value) {
        m_cfg[BootstrapServers] = value;
    }

    void statistics_interval(const std::string& value) {
        m_cfg[StatisticsInterval] = value;
    }

    void tls(const std::string& ca_loc) {
        m_cfg["security.protocol"] = "ssl";
        m_cfg["ssl.ca.location"] = ca_loc;
    }

    void mtls(const std::string& ca_loc, const std::string& cert_loc, const std::string& key_loc, const std::string& key_pw = "") {
        tls(ca_loc);
        m_cfg["ssl.certificate.location"] = cert_loc;
        m_cfg["ssl.key.location"] = key_loc;
        m_cfg["ssl.key.password"] = key_pw;
    }

    /**
     * @brief   Consumer Group ID.
     *
     * https://developer.confluent.io/faq/apache-kafka/kafka-clients/#kafka-clients-what-is-groupid-in-kafka
     */
    void group_id(const std::string& value) {
        m_cfg[GroupId] = value;
    }

    void offset_earliest() {
        m_cfg[OffsetReset] = "earliest";
    }

    void offset_latest() {
        m_cfg[OffsetReset] = "latest";
    }

    void delivery_callback(std::unique_ptr<delivery_handler> callback) {
        m_callbacks.delivery = std::move(callback);
    }

    void throttle_callback(std::unique_ptr<throttle_handler> callback) {
        m_callbacks.throttle = std::move(callback);
    }

    void statistics_callback(std::unique_ptr<statistics_handler> callback) {
        m_callbacks.statistics = std::move(callback);
    }

    /**
     * @brief   Create RdKafka configuration object.
     */
    auto create() {
        rd_kafka_conf_t* config = rd_kafka_conf_new();
        nova_assert(config != nullptr);

        set_basic_props(config);
        rd_kafka_conf_set_opaque(config, &m_callbacks);
        rd_kafka_conf_set_log_cb(config, detail::log_callback);

        if (m_callbacks.delivery != nullptr) {
            set(config, detail::delivery_callback);
        }

        if (m_callbacks.throttle != nullptr) {
            set(config, detail::throttle_callback);
        }

        if (m_callbacks.statistics != nullptr) {
            set(config, detail::statistics_callback);
        }

        return config;
    }

private:
    std::map<std::string, std::string> m_cfg;
    detail::callbacks_t m_callbacks;

    void set_basic_props(rd_kafka_conf_t* config) {
        char errstr[detail::ErrorMsgLength];
        for (const auto& [k, v] : m_cfg) {
            if (rd_kafka_conf_set(config, k.c_str(), v.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                throw nova::exception(errstr);
            }
        }
    }

    /**
     * @brief   Set a delivery callback.
     */
    void set(rd_kafka_conf_t* config, delivery_callback_signature value) {
        rd_kafka_conf_set_dr_msg_cb(config, value);
    }

    /**
     * @brief   Set a throttle callback.
     */
    void set(rd_kafka_conf_t* config, throttle_callback_signature value) {
        rd_kafka_conf_set_throttle_cb(config, value);
    }

    /**
     * @brief   Set a statistics callback.
     */
    void set(rd_kafka_conf_t* config, statistics_callback_signature value) {
        rd_kafka_conf_set_stats_cb(config, value);
    }
};

class producer {
    struct poller {
        static constexpr auto PollTimeout  = std::chrono::milliseconds{ 1000 };

        rd_kafka_t* producer;
        std::atomic_bool& keep_alive;

        void operator()() {
            while (keep_alive.load()) {
                rd_kafka_poll(producer, static_cast<int>(PollTimeout.count()));
            }
        }
    };

public:

    /**
     * @brief   Create a Kafka producer and start a background polling thread.
     */
    producer(properties props)
        : m_props(std::move(props))
    {
        auto config = m_props.create();

        char errstr[detail::ErrorMsgLength];
        m_producer = std::unique_ptr<rd_kafka_t, caller<decltype(rd_kafka_destroy)>>(
            rd_kafka_new(RD_KAFKA_PRODUCER, config, errstr, sizeof(errstr)),
            rd_kafka_destroy
        );

        if (m_producer == nullptr) {
            throw nova::exception("Failed to create producer: {}", errstr);
        }

        m_poll_thread = std::jthread(
            poller{
                m_producer.get(),
                m_keep_alive
            }
        );
    }

    producer(const producer&)               = delete;
    producer(producer&&)                    = delete;
    producer& operator=(const producer&)    = delete;
    producer& operator=(producer&&)         = delete;

    ~producer() {
        static constexpr auto FlushTimeout = std::chrono::milliseconds{ 5000 };

        if (m_producer != nullptr) {
            // TODO(design): Should the destructor flush? No opportunity to handle if flush times out.
            rd_kafka_flush(m_producer.get(), static_cast<int>(FlushTimeout.count()));
            if (m_keep_alive.load()) {
                stop();
            }
        }
    }

    /**
     * @brief   Flush.
     *
     * @returns false if it timed out.
     */
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout) -> bool {
        return rd_kafka_flush(m_producer.get(), static_cast<int>(timeout.count()))
            != RD_KAFKA_RESP_ERR__TIMED_OUT;
    }

    /**
     * @brief   Return the number of messages and events waiting in queues.
     *
     * - Messages to be sent or waiting for acknowledge.
     * - Delivery reports.
     * - Callbacks.
     */
    [[nodiscard]] auto queue_size() const -> int {
        return rd_kafka_outq_len(m_producer.get());
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
        static constexpr auto PollTimeout  = std::chrono::milliseconds{ 100 };

        bool doit = true;

        while (doit) {
            const auto status = send_impl(msg);

            switch (status) {
                case RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE:   throw nova::exception("Too large message");
                case RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION:   throw nova::exception("Unknown partition");
                case RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC:       throw nova::exception("Unknown topic");
                default: ; /* NO-OP */
            }

            if (status == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
                rd_kafka_poll(m_producer.get(), static_cast<int>(PollTimeout.count()));
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
    auto try_send(const dsp::message& msg) -> bool {
        const auto status = send_impl(msg);

        switch (status) {
            case RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE:  throw nova::exception("Too large message");
            case RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION:  throw nova::exception("Unknown partition");
            case RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC:      throw nova::exception("Unknown topic");
            case RD_KAFKA_RESP_ERR__QUEUE_FULL:         return false;
            default: ; /* NO-OP */
        }

        return true;
    }

    void stop() {
        nova::topic_log::info("kafka", "Stopping Kafka...");
        m_keep_alive.store(false);
    }

private:
    properties m_props;
    std::unique_ptr<rd_kafka_t, caller<decltype(rd_kafka_destroy)>> m_producer{ nullptr, rd_kafka_destroy };
    std::jthread m_poll_thread;
    std::atomic_bool m_keep_alive = true;

    struct topic_t {
        std::unique_ptr<rd_kafka_topic_partition_list_t, caller<decltype(rd_kafka_topic_partition_list_destroy)>> partitions{ nullptr, rd_kafka_topic_partition_list_destroy };
        std::unique_ptr<rd_kafka_topic_t, caller<decltype(rd_kafka_topic_destroy)>> handle{ nullptr, rd_kafka_topic_destroy };
    };

    using topics_t = std::map<std::string, topic_t>;
    topics_t m_topics;

    /**
     * @brief   Create a topic handle and cache it.
     *
     * TODO: Update metadata?
     */
    auto topic(const std::string& name) -> rd_kafka_topic_t* {
        if (not m_topics.contains(name)) {
            topic_t topic;

            // TODO: topic config
            // TODO: multiple partitions

            topic.partitions = std::unique_ptr<rd_kafka_topic_partition_list_t, caller<decltype(rd_kafka_topic_partition_list_destroy)>>(
                rd_kafka_topic_partition_list_new(1),
                rd_kafka_topic_partition_list_destroy
            );

            topic.handle = std::unique_ptr<rd_kafka_topic_t, caller<decltype(rd_kafka_topic_destroy)>>(
                rd_kafka_topic_new(m_producer.get(), name.c_str(), nullptr),
                rd_kafka_topic_destroy
            );

            nova_assert(topic.handle != nullptr);

            rd_kafka_topic_partition_list_add(topic.partitions.get(), name.c_str(), RD_KAFKA_PARTITION_UA);
            m_topics[name] = std::move(topic);
        }

        return m_topics[name].handle.get();
    }

    auto send_impl(const dsp::message& msg) -> rd_kafka_resp_err_t {
        rd_kafka_resp_err_t err;
        if (not msg.properties.empty()) {
            rd_kafka_headers_t* headers = rd_kafka_headers_new(msg.properties.size());

            for (const auto& [k, v] : msg.properties) {
                // TODO(error): RD_KAFKA_RESP_ERR__READ_ONLY if the headers are read-only, else RD_KAFKA_RESP_ERR_NO_ERROR
                // Does this error matter anyway?
                rd_kafka_header_add(headers, k.c_str(), static_cast<long>(k.size()), v.c_str(), static_cast<long>(v.size()));
            }

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"

            err = rd_kafka_producev(
                m_producer.get(),
                RD_KAFKA_V_RKT(topic(msg.subject)),
                RD_KAFKA_V_PARTITION(RD_KAFKA_PARTITION_UA),
                RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                RD_KAFKA_V_VALUE(reinterpret_cast<void*>(const_cast<std::byte*>(msg.payload.data())), msg.payload.size()),
                RD_KAFKA_V_KEY(reinterpret_cast<void*>(const_cast<std::byte*>(msg.key.data())), msg.key.size()),
                RD_KAFKA_V_HEADERS(headers),
                RD_KAFKA_V_END
            );

            #pragma GCC diagnostic pop

            if (err) {
                rd_kafka_headers_destroy(headers);
            }
        } else {
            rd_kafka_produce(
                topic(msg.subject),
                RD_KAFKA_PARTITION_UA,
                RD_KAFKA_MSG_F_COPY,
                reinterpret_cast<void*>(const_cast<std::byte*>(msg.payload.data())),
                msg.payload.size(),
                reinterpret_cast<const void*>(msg.key.data()),
                msg.key.size(),
                NULL
            );
            err = rd_kafka_last_error();
        }

        return err;
    }

};

} // namespace dsp::kf
