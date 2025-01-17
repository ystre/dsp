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
#include <stdexcept>
#include <string>
#include <thread>

#include <cstdint>

namespace dsp::kf {

using dr_cb_f_sig = void(rd_kafka_t*, const rd_kafka_message_t*, void*);
using dr_cb_f = std::function<void(rd_kafka_t*, const rd_kafka_message_t*, void*)>;

template <typename Func>
struct destroyer {
    destroyer(Func fn)
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
 * @brief   A factory-like class to create the configuration.
 *
 * Dual-API
 */
class properties {
public:
    static constexpr auto BootstrapServers = "bootstrap.servers";

    void bootstrap_server(const std::string& value) {
        m_cfg[BootstrapServers] = value;
    }

    void delivery_callback(dr_cb_f_sig callback) {
        m_dr_cb = std::move(callback);
    }

    auto create() {
        auto config = rd_kafka_conf_new();

        if (config == nullptr) {
            throw std::runtime_error("Failed to create Kafka global configuration");
        }

        set_basic_props(config);

        if (m_dr_cb != nullptr) {
            set(config, m_dr_cb);
        }

        return config;
    }

private:
    std::map<std::string, std::string> m_cfg;
    void(*m_dr_cb)(rd_kafka_t*, const rd_kafka_message_t*, void*);
    // dr_cb_f m_dr_cb = nullptr;

    void set_basic_props(rd_kafka_conf_t* config) {
        char errstr[512];
        for (const auto& [k, v] : m_cfg) {
            if (rd_kafka_conf_set(config, k.c_str(), v.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
                throw std::runtime_error(errstr);
            }
        }
    }

    /**
     * @brief   Set a delivery callback.
     */
    static void set(rd_kafka_conf_t* config, dr_cb_f_sig value) {
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

        char errstr[512];
        m_producer = std::unique_ptr<rd_kafka_t, destroyer<decltype(rd_kafka_destroy)>>(
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
        rd_kafka_topic_partition_list_t *topics = rd_kafka_topic_partition_list_new(1);

        // TODO: multiple topics
        // TODO: topic config
        m_topic = rd_kafka_topic_new(m_producer.get(), topic.c_str(), nullptr);
        nova_assert(m_topic != nullptr);

        rd_kafka_topic_partition_list_add(topics, topic.c_str(), RD_KAFKA_PARTITION_UA);
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
            case RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE:  throw std::runtime_error("Too large message");
            case RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION:  throw std::runtime_error("Unknown partition");
            case RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC:      throw std::runtime_error("Unknown topic");
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
    std::unique_ptr<rd_kafka_t, destroyer<decltype(rd_kafka_destroy)>> m_producer{ nullptr, rd_kafka_destroy };
    std::jthread m_poll_thread;
    std::atomic_bool m_keep_alive = true;

    rd_kafka_topic_t* m_topic = nullptr;

    auto send_impl(const dsp::message& msg) -> rd_kafka_resp_err_t {
        // if (not msg.properties.empty()) {
            // rd_kafka_headers_t *hdrs_copy;
            rd_kafka_resp_err_t err;

            // hdrs_copy = rd_kafka_headers_copy(hdrs);

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"

            err = rd_kafka_producev(
                m_producer.get(),
                RD_KAFKA_V_RKT(m_topic),
                // RD_KAFKA_V_PARTITION(partition),
                RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                RD_KAFKA_V_VALUE(reinterpret_cast<void*>(const_cast<std::byte*>(msg.payload.data())), msg.payload.size()),
                RD_KAFKA_V_KEY(reinterpret_cast<void*>(const_cast<std::byte*>(msg.key.data())), msg.key.size()),
                // RD_KAFKA_V_HEADERS(hdrs_copy),
                RD_KAFKA_V_END
            );

            #pragma GCC diagnostic pop

            // if (err)
                    // rd_kafka_headers_destroy(hdrs_copy);

            return err;
        // } else {
            // if (rd_kafka_produce(rkt, partition, msgflags, payload, size,
                                 // key, key_size, NULL) == -1)
                // return rd_kafka_last_error();
        // }
    }

};

} // namespace dsp::kf
