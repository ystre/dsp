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

#include <libdsp/cache.hpp>
#include <libdsp/profiler.hpp>

#include <libnova/error.hpp>
#include <libnova/expected.hpp>
#include <libnova/log.hpp>
#include <libnova/utils.hpp>

#include <fmt/format.h>
#include <librdkafka/rdkafka.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>

namespace dsp::kf {

namespace detail {

    constexpr auto PollTimeout = std::chrono::milliseconds { 1000 };

    struct kafka_del     { void operator()(rd_kafka_t* ptr)                      { rd_kafka_destroy(ptr); } };
    struct partition_del { void operator()(rd_kafka_topic_partition_list_t* ptr) { rd_kafka_topic_partition_list_destroy(ptr); } };
    struct topic_del     { void operator()(rd_kafka_topic_t* ptr)                { rd_kafka_topic_destroy(ptr); } };
    struct queue_del     { void operator()(rd_kafka_queue_t* ptr)                { rd_kafka_queue_destroy(ptr); } };

    struct topic_t {
        std::unique_ptr<rd_kafka_topic_partition_list_t, detail::partition_del> partitions { nullptr };
        std::unique_ptr<rd_kafka_topic_t, detail::topic_del> handle { nullptr };
    };

    using topics_t = std::unordered_map<std::string, topic_t>;

    /**
     * @brief   A view for consumed message. A thin wrapper around the message pointer.
     *
     * Cannot be copied.
     *
     * It is templated on the pointer type, using its constness.
     * - If the pointer is non-const, the view owns the underlying data.
     * - If the pointer is const, the view does not own the underlying data.
     *
     * Consume returns the non-const variant, while delivery callback uses const.
     *
     * CAUTION: All accessors return a view of the underlying data (unless trivial
     * types). No returned object should outlive the view that created them. Their
     * content must be copied into persistent objects.
     */
    template <typename RdKafkaMessage>
    class message_view {
    public:
        using header_map = std::unordered_map<const char*, nova::data_view>;

        message_view(RdKafkaMessage* message_ptr)
            : m_message_ptr(message_ptr)
        {}

        message_view(const message_view&)            = delete;
        message_view& operator=(const message_view&) = delete;

        message_view(message_view&& rhs) {
            m_message_ptr = rhs.m_message_ptr;
            rhs.m_message_ptr = nullptr;
        }

        message_view& operator=(message_view&& rhs) {
            m_message_ptr = rhs.m_message_ptr;
            rhs.m_message_ptr = nullptr;
            return *this;
        }

        ~message_view() {
            if constexpr (not std::is_const_v<RdKafkaMessage>) {
                if (m_message_ptr != nullptr) {
                    rd_kafka_message_destroy(m_message_ptr);
                }
            }
        }

        [[nodiscard]] auto ok() const -> bool {
            return m_message_ptr->err == RD_KAFKA_RESP_ERR_NO_ERROR;
        }

        [[nodiscard]] auto error_code() const -> int {
            return m_message_ptr->err;
        }

        [[nodiscard]] auto error_message() const -> std::string_view {
            return { rd_kafka_err2str(m_message_ptr->err) };
        }

        [[nodiscard]] auto eof() const -> bool {
            return m_message_ptr->err == RD_KAFKA_RESP_ERR__PARTITION_EOF;
        }

        [[nodiscard]] auto key() const -> nova::data_view {
            return { m_message_ptr->key, m_message_ptr->key_len };
        }

        [[nodiscard]] auto payload() const -> nova::data_view {
            return { m_message_ptr->payload, m_message_ptr->len };
        }

        [[nodiscard]] auto topic() const -> std::string_view {
            return rd_kafka_topic_name(m_message_ptr->rkt);
        }

        [[nodiscard]] auto partition() const -> std::int32_t {
            return m_message_ptr->partition;
        }

        [[nodiscard]] auto offset() const -> std::int64_t {
            return m_message_ptr->offset;
        }

        /**
         * Note: Headers are associated with the message by librdkafka
         */
        [[nodiscard]] auto headers() const -> header_map {
            header_map ret;
            rd_kafka_headers_t* headers;

            if (rd_kafka_message_headers(m_message_ptr, &headers) != RD_KAFKA_RESP_ERR_NO_ERROR) {
                return ret;
            };

            std::size_t index = 0;
            const char* key;
            const void* value;
            std::size_t size;

            while (rd_kafka_header_get_all(headers, index++, &key, &value, &size) == RD_KAFKA_RESP_ERR_NO_ERROR) {
                ret.insert({ key, nova::data_view(value, size) });
            }

            return ret;
        }

        /**
         * @brief   Accessing the underlying handle to the message.
         *
         * CAUTION: This exposes the low-level C-API for librdkafka.
         *
         * Working with headers can be costly. This is a way around this abstraction to
         * squeeze the most performance out of processing consumed messages.
         */
        [[nodiscard]] auto ptr() -> RdKafkaMessage* {
            return m_message_ptr;
        }

    private:
        RdKafkaMessage* m_message_ptr;

    };

} // namespace detail

/**
 * @brief   Owning message view used for consumed messages.
 */
using message_view_owned = detail::message_view<rd_kafka_message_t>;

/**
 * @brief   Non-owning message view used by delivery handlers.
 */
using message_view = detail::message_view<const rd_kafka_message_t>;

/**
 * @brief   An abstraction that hides C API and delegates to error and success handlers.
 */
class delivery_handler {
public:
    void operator()(const rd_kafka_message_t* msg) {
        if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            handle_error(message_view{ msg });
        } else {
            handle_success(message_view{ msg });
        }
    }

    virtual ~delivery_handler() = default;

protected:
    virtual void handle_error(message_view) = 0;
    virtual void handle_success(message_view) = 0;
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

class rebalance_handler {
public:
    // TODO(feat): Finish rebalance callback API.
    // virtual void operator()() = 0;
    virtual void operator()(
        rd_kafka_t* client,
        rd_kafka_resp_err_t* err,
        rd_kafka_topic_partition_list_t* partitions,
        void* opaque) = 0;

    virtual ~rebalance_handler() = default;
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
        std::unique_ptr<rebalance_handler> rebalance = nullptr;
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

    /**
     * @brief   Integrate Nova logging.
     */
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
     * @brief   Trampoline function to call rebalance handler object.
     *
     * Currently this is an example code copy-pasta'd from librdkafka's performance example.
     * Not in use.
     *
     * TODO(feat): Finish rebalance callback API.
     */
    inline void rebalance_callback(
            rd_kafka_t* rk,
            rd_kafka_resp_err_t err,
            rd_kafka_topic_partition_list_t* partitions,
            [[maybe_unused]] void* opaque)
    {
        rd_kafka_error_t *error     = NULL;
        rd_kafka_resp_err_t ret_err = RD_KAFKA_RESP_ERR_NO_ERROR;

        if (!strcmp(rd_kafka_rebalance_protocol(rk), "COOPERATIVE"))
                fprintf(stderr,
                        "%% This example has not been modified to "
                        "support -e (exit on EOF) when "
                        "partition.assignment.strategy "
                        "is set to an incremental/cooperative strategy: "
                        "-e will not behave as expected\n");

        switch (err) {
        case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
                fprintf(stderr,
                        "%% Group rebalanced (%s): "
                        "%d new partition(s) assigned\n",
                        rd_kafka_rebalance_protocol(rk), partitions->cnt);

                if (!strcmp(rd_kafka_rebalance_protocol(rk), "COOPERATIVE")) {
                        error = rd_kafka_incremental_assign(rk, partitions);
                } else {
                        ret_err = rd_kafka_assign(rk, partitions);
                }

                // partition_cnt += partitions->cnt;
                break;

        case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
                fprintf(stderr,
                        "%% Group rebalanced (%s): %d partition(s) revoked\n",
                        rd_kafka_rebalance_protocol(rk), partitions->cnt);

                if (!strcmp(rd_kafka_rebalance_protocol(rk), "COOPERATIVE")) {
                        error = rd_kafka_incremental_unassign(rk, partitions);
                        // partition_cnt -= partitions->cnt;
                } else {
                        ret_err       = rd_kafka_assign(rk, NULL);
                        // partition_cnt = 0;
                }

                break;

        default:
                break;
        }

        if (error) {
                fprintf(stderr, "%% incremental assign failure: %s\n",
                        rd_kafka_error_string(error));
                rd_kafka_error_destroy(error);
        } else if (ret_err) {
                fprintf(stderr, "%% assign failure: %s\n",
                        rd_kafka_err2str(ret_err));
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

    /**
     * @brief   Trampoline function to call rebalancer handler object.
     */
    inline int rebalance_callback(
            [[maybe_unused]] rd_kafka_t* client,
            rd_kafka_resp_err_t* err,
            rd_kafka_topic_partition_list_t* partitions,
            void* opaque)
    {
        auto* context = static_cast<callbacks_t*>(opaque);
        context->rebalance->operator()(client, err, partitions, opaque);
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
    static constexpr auto PartitionEof = "enable.partition.eof";

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

    void enable_partition_eof() {
        m_cfg[PartitionEof] = "true";
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
    auto create() -> rd_kafka_conf_t* {
        rd_kafka_conf_t* config = rd_kafka_conf_new();
        nova_assert(config != nullptr);

        set_basic_props(config);
        rd_kafka_conf_set_opaque(config, &m_callbacks);
        rd_kafka_conf_set_log_cb(config, detail::log_callback);

        // TODO(feat): Finish rebalance callback API.
        //             Must be added in consumer as it is a consumer-only configuration.
        //
        // rd_kafka_conf_set_rebalance_cb(config, detail::rebalance_callback);

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
    std::unordered_map<std::string, std::string> m_cfg;
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

/**
 * @brief   Kafka producer based on librdkafka's C-API.
 *
 * It has a background poller thread that continuously polls events. They can
 * be messages, delivery reports and other internal events.
 *
 * It is non-copyable and non-movable.
 *
 * Performance oriented implementation.
 *
 * Poll timeouts:
 * - 1 second by default, used by poller thread.
 * - 5 seconds for flushing from destructor - TODO(design): if this is a good idea.
 * - 100 milliseconds for retries in case of producer queue is full.
 */
class producer {
    struct poller {
        rd_kafka_t* producer;
        std::atomic_bool& keep_alive;

        void operator()() {
            DSP_PROFILING_ZONE("kafka-poll");
            while (keep_alive.load()) {
                rd_kafka_poll(producer, static_cast<int>(detail::PollTimeout.count()));
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
        m_producer = std::unique_ptr<rd_kafka_t, detail::kafka_del>(
            rd_kafka_new(RD_KAFKA_PRODUCER, config, errstr, sizeof(errstr))
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
            [[maybe_unused]] const auto success = flush(FlushTimeout);
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
    [[nodiscard]] auto queue_size() const -> std::size_t {
        auto ret = rd_kafka_outq_len(m_producer.get());
        nova_assert(ret >= 0);
        // TODO(nova): Safe cast.
        return static_cast<std::size_t>(ret);
    }

    /**
     * @brief   Enqueue a message.
     *
     * Spins in a loop if the queue is full. (Polling interval is 100ms; hard-coded)
     *
     * @throws  if an unexpected error happens:
     *            - Message is larger than "messages.max.bytes"
     *            - Unknown partition or topic
     *            - `RD_KAFKA_RESP_ERR__READ_ONLY` if the headers are read-only
     */
    void send(const dsp::message& msg) {
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
     *            - `RD_KAFKA_RESP_ERR__READ_ONLY` if the headers are read-only
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
        nova::topic_log::debug("kafka", "Stopping librdkafka producer...");
        m_keep_alive.store(false);
    }

private:
    properties m_props;
    std::unique_ptr<rd_kafka_t, detail::kafka_del> m_producer{ nullptr };
    std::jthread m_poll_thread;
    std::atomic_bool m_keep_alive = true;

    detail::topics_t m_topics;

    /**
     * @brief   Create a topic handle and cache it.
     *
     * TODO: Update metadata?
     */
    auto topic(const std::string& name) -> rd_kafka_topic_t* {
        if (not m_topics.contains(name)) {
            detail::topic_t topic;

            // TODO: topic config
            // TODO: multiple partitions

            topic.partitions = std::unique_ptr<rd_kafka_topic_partition_list_t, detail::partition_del>(rd_kafka_topic_partition_list_new(1));
            topic.handle = std::unique_ptr<rd_kafka_topic_t, detail::topic_del>(rd_kafka_topic_new(m_producer.get(), name.c_str(), nullptr));

            nova_assert(topic.handle != nullptr);

            rd_kafka_topic_partition_list_add(topic.partitions.get(), name.c_str(), RD_KAFKA_PARTITION_UA);
            m_topics[name] = std::move(topic);
        }

        return m_topics[name].handle.get();
    }

    auto send_impl(const dsp::message& msg) -> rd_kafka_resp_err_t {
        DSP_PROFILING_ZONE("kafka-produce");
        rd_kafka_resp_err_t err;

        if (not msg.properties.empty()) {
            rd_kafka_headers_t* headers = rd_kafka_headers_new(msg.properties.size());

            for (const auto& [k, v] : msg.properties) {
                const rd_kafka_resp_err_t header_err = rd_kafka_header_add(
                    headers,
                    k.c_str(),
                    static_cast<long>(k.size()),
                    v.c_str(),
                    static_cast<long>(v.size())
                );

                if (header_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                    throw nova::exception("{}", rd_kafka_err2str(header_err));
                }
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

/**
 * @brief   Kafka consumer based on librdkafka's C-API.
 *
 * It is non-copyable and non-movable.
 *
 * Performance oriented implementation.
 */
class consumer {
public:

    /**
     * @brief   Create a Kafka consumer.
     */
    consumer(properties props)
        : m_props(std::move(props))
    {
        auto config = m_props.create();
        // TODO(feat): Configure rebalance callback.

        char errstr[detail::ErrorMsgLength];
        m_consumer = std::unique_ptr<rd_kafka_t, detail::kafka_del>(
            rd_kafka_new(RD_KAFKA_CONSUMER, config, errstr, sizeof(errstr))
        );

        if (m_consumer == nullptr) {
            throw nova::exception("Failed to create consumer: {}", errstr);
        }

        rd_kafka_poll_set_consumer(m_consumer.get());

        m_queue = std::unique_ptr<rd_kafka_queue_t, detail::queue_del>(
            rd_kafka_queue_get_consumer(m_consumer.get())
        );
    }

    consumer(const consumer&)               = delete;
    consumer(consumer&&)                    = delete;
    consumer& operator=(const consumer&)    = delete;
    consumer& operator=(consumer&&)         = delete;

    ~consumer() {
        nova::topic_log::debug("kafka", "Stopping librdkafka consumer...");
        unsubscribe();

        rd_kafka_consumer_close(m_consumer.get());
        nova::topic_log::debug("kafka", "librdkafka consumer has been stopped");
    }

    /**
     * @brief   Return the current number of elements in queue.
     */
    [[nodiscard]] auto queue_size() const -> std::size_t {
        return rd_kafka_queue_length(m_queue.get());
    }

    void subscribe(const std::vector<std::string>& topics) {
        rd_kafka_topic_partition_list_t *subscription = rd_kafka_topic_partition_list_new(static_cast<int>(topics.size()));
        for (const auto& topic : topics) {
            rd_kafka_topic_partition_list_add(subscription, topic.c_str(), RD_KAFKA_PARTITION_UA);      // Partition is ignored by subscribe().
        }

        rd_kafka_resp_err_t err = rd_kafka_subscribe(m_consumer.get(), subscription);
        if (err) {
            rd_kafka_topic_partition_list_destroy(subscription);
            throw nova::exception(rd_kafka_err2str(err));
        }

        rd_kafka_topic_partition_list_destroy(subscription);
    }

    void subscribe(const std::string& topic) {
        subscribe(std::vector<std::string>{ topic });
    }

    /**
     * @brief   Unsubscribe with an infinite timeout.
     *
     * Timeout is hard-coded in librdkafka.
     *
     * TODO(err): Design properly exposing errors from librdkafka.
     */
    void unsubscribe() {
        rd_kafka_resp_err_t err = rd_kafka_unsubscribe(m_consumer.get());
        if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            nova::topic_log::warn("kafka", "Error during unsubscribe: {}", rd_kafka_err2str(err));
        }
    }

    /**
     * @brief   Consume messages in batches.
     *
     * Suggested minimum batch size is 10 for high throughput.
     *
     * Consider using smaller batches or smaller timeout than the default
     * 1 second if the expected throughput is low and low latency is required.
     *
     * Errors are logged, but not returned to the caller.
     *
     * Note: Consumed messages have their headers parsed upon calling
     * `message_view_owned::headers()`, therefore it should not matter
     * from a performance perspective if the messages have headers or not,
     * unless they are explicitly accessed.
     *
     * @returns a batch of owned message views.
     */
    auto consume(std::size_t batch_size, std::chrono::milliseconds timeout = detail::PollTimeout)
            -> std::vector<message_view_owned>
    {
        DSP_PROFILING_ZONE("kafka-consume");
        std::vector<rd_kafka_message_t*> messages(batch_size);

        auto n = rd_kafka_consume_batch_queue(m_queue.get(), static_cast<int>(timeout.count()), messages.data(), batch_size);
        if (n == -1) {
            nova::topic_log::warn("kafka", "Error during consuming: {}", rd_kafka_err2str(rd_kafka_last_error()));
        } else {
            messages.resize(static_cast<std::size_t>(n));
        }

        std::vector<message_view_owned> ret;
        ret.reserve(batch_size);
        std::ranges::transform(messages, std::back_inserter(ret), [](rd_kafka_message_t* msg) { return message_view_owned{ msg }; });
        return ret;
    }

private:
    properties m_props;
    std::unique_ptr<rd_kafka_t, detail::kafka_del> m_consumer { nullptr };
    std::unique_ptr<rd_kafka_queue_t, detail::queue_del> m_queue { nullptr };

    detail::topics_t m_topics;

};

} // namespace dsp::kf

template <>
class fmt::formatter<dsp::kf::message_view_owned> {
public:

    /**
     * @brief   Parses format specifiers and stores them in the formatter.
     *
     * Reference: https://fmt.dev/latest/api/#formatting-user-defined-types
     *
     * [ctx.begin(), ctx.end()) is a, possibly empty, character range that
     * contains a part of the format string starting from the format
     * specifications to be parsed, e.g. in
     *
     * fmt::format("{:f} continued", ...);
     *
     * the range will contain "f} continued". The formatter should parse
     * specifiers until '}' or the end of the range.
     */
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();

        for (; it != ctx.end() && *it != '}'; ++it) {}

        m_format_spec = std::string_view{ ctx.begin(), it };
        return it;
    }

    /**
     * @brief   Format a Kafka message.
     *
     * - l = location (topic name, partition, offset)
     * - k = key
     * - v = value/payload
     * - h = headers
     *
     * Example output: "dev-test [0] at offset 18 key=someKey value=someValue headers={"h1": value}
     */
    template <typename FmtContext>
    constexpr auto format(const dsp::kf::message_view_owned& msg, FmtContext& ctx) const {
        if (m_format_spec.empty() || m_format_spec.find('l') != std::string_view::npos) {
            fmt::format_to(
                ctx.out(),
                "{} [{}] at offset {} ",
                msg.topic(),
                msg.partition(),
                msg.offset()
            );
        }

        if (m_format_spec.find('k') != std::string_view::npos) {
            fmt::format_to(ctx.out(), "key={} ", msg.key());
        }

        if (m_format_spec.find('v') != std::string_view::npos) {
            fmt::format_to(ctx.out(), "value={} ", msg.payload());
        }

        if (m_format_spec.find('h') != std::string_view::npos) {
            fmt::format_to(ctx.out(), "headers={}", msg.headers());
        }

        return ctx.out();
    }

private:
    std::string_view m_format_spec;
};
