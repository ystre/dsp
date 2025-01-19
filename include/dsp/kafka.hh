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
    virtual void handle_error(const rd_kafka_message_t*) {}
    virtual void handle_success(const rd_kafka_message_t*) {}
};

namespace detail {

    constexpr std::size_t ErrorMsgLength = 512;

    /**
     * @brief   Trampoline function to call delivery handler object.
     */
    inline void delivery_callback([[maybe_unused]] rd_kafka_t* client, const rd_kafka_message_t* message, void* opaque) {
        auto* context = static_cast<delivery_handler*>(opaque);

        // TODO(design): Generic opaque; context should contain a delivery
        //               callback amongst other things.
        context->operator()(message);
    }
}

/**
 * @brief   A factory-like class to create the configuration.
 *
 * Dual-API
 */
class properties {
    using delivery_callback_signature = void(rd_kafka_t*, const rd_kafka_message_t*, void*);

public:
    static constexpr auto BootstrapServers = "bootstrap.servers";

    void bootstrap_server(const std::string& value) {
        m_cfg[BootstrapServers] = value;
    }

    void delivery_callback(std::unique_ptr<delivery_handler> callback) {
        m_delivery_callback = std::move(callback);
    }

    void set(const std::string& key, const std::string& value) {
        m_cfg[key] = value;
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

    auto create() {
        auto config = rd_kafka_conf_new();

        if (config == nullptr) {
            throw nova::exception("Failed to create Kafka global configuration");
        }

        set_basic_props(config);

        if (m_delivery_callback != nullptr) {
            set(config, detail::delivery_callback);
        }

        return config;
    }

private:

    std::map<std::string, std::string> m_cfg;
    std::unique_ptr<delivery_handler> m_delivery_callback = nullptr;

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
        // TODO(design): Generic opaque/context, all callback receives it.
        rd_kafka_conf_set_opaque(config, m_delivery_callback.get());
        rd_kafka_conf_set_dr_msg_cb(config, value);
    }
};

class producer {
    struct poller {
        rd_kafka_t* producer;
        std::atomic_bool& keep_alive;

        void operator()() {
            while (keep_alive.load()) {
                rd_kafka_poll(producer, 1000);
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

        // m_delivery_callback = m_props.delivery_callback();

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
        if (m_producer != nullptr) {
            // TODO(design): Should the destructor flush? No opportunity to handle if flush times out.
            rd_kafka_flush(m_producer.get(), 5000);
            if (m_keep_alive.load()) {
                stop();
            }
        }
    }

    /**
     * @brief
     *
     * @returns false if it timed out.
     */
    [[nodiscard]] auto flush(std::chrono::milliseconds timeout) -> bool {
        return rd_kafka_flush(m_producer.get(), static_cast<int>(timeout.count()))
            != RD_KAFKA_RESP_ERR__TIMED_OUT;
    }

    /**
     * @brief   Create a topic handle and cache it.
     */
    auto topic(const std::string& topic) {
        // TODO: multiple partitions
        m_topic_partition_list = std::unique_ptr<rd_kafka_topic_partition_list_t, caller<decltype(rd_kafka_topic_partition_list_destroy)>>(
            rd_kafka_topic_partition_list_new(1),
            rd_kafka_topic_partition_list_destroy
        );

        // TODO: multiple topics
        // TODO: topic config
        m_topic = std::unique_ptr<rd_kafka_topic_t, caller<decltype(rd_kafka_topic_destroy)>>(
            rd_kafka_topic_new(m_producer.get(), topic.c_str(), nullptr),
            rd_kafka_topic_destroy
        );
        // TODO: Do we still need this assert?
        nova_assert(m_topic != nullptr);

        rd_kafka_topic_partition_list_add(m_topic_partition_list.get(), topic.c_str(), RD_KAFKA_PARTITION_UA);
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

    std::unique_ptr<rd_kafka_topic_partition_list_t, caller<decltype(rd_kafka_topic_partition_list_destroy)>> m_topic_partition_list{ nullptr, rd_kafka_topic_partition_list_destroy };
    std::unique_ptr<rd_kafka_topic_t, caller<decltype(rd_kafka_topic_destroy)>> m_topic{ nullptr, rd_kafka_topic_destroy };

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
                RD_KAFKA_V_RKT(m_topic.get()),
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
                m_topic.get(),
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
