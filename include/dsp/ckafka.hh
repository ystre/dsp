#pragma once

#include <dsp/cache.hh>

#include <nova/error.hh>
#include <nova/log.hh>

#include <librdkafka/rdkafka.h>

#include <atomic>
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

    void delivery_callback(dr_cb_f callback) {
        m_dr_cb = std::move(callback);
    }

    auto create() {
        auto config = rd_kafka_conf_new();

        if (config == nullptr) {
            throw std::runtime_error("Failed to create Kafka global configuration");
        }

        set_basic_props(config);

        return config;
    }

    /**
     * @brief   Access the created delivery handler.
     */
    // [[nodiscard]] auto delivery_callback() -> detail::delivery_callback* {
        // auto* ptr = dynamic_cast<detail::delivery_callback*>(m_dr_cb.get());
        // if (ptr == nullptr) {
            // throw std::logic_error(
                // "Delivery callback is not inherited from `delivery_callback`; "
                // "Most likely it is a native librdkafka callback."
            // );
        // }
        // return ptr;
    // }

private:
    std::map<std::string, std::string> m_cfg;
    dr_cb_f m_dr_cb = nullptr;

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
    static void set(rd_kafka_conf_t* config, dr_cb_f value) {
        rd_kafka_conf_set_dr_msg_cb(config, value.target<dr_cb_f_sig>());
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
            // TODO: rd_kafka_resp_err_t
            rd_kafka_flush(m_producer.get(), 5000);
            if (m_keep_alive.load()) {
                stop();
            }
        }
    }

    auto send(const dsp::message& msg) -> int {
        // if (not msg.properties.empty()) {
            // rd_kafka_headers_t *hdrs_copy;
            rd_kafka_resp_err_t err;

            // hdrs_copy = rd_kafka_headers_copy(hdrs);

            // TODO: V_RKT instead of V_TOPIC

            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wold-style-cast"

            err = rd_kafka_producev(
                m_producer.get(),
                RD_KAFKA_V_TOPIC(msg.subject.c_str()),
                // RD_KAFKA_V_PARTITION(partition),
                RD_KAFKA_V_MSGFLAGS(0 | RD_KAFKA_MSG_F_COPY),
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

    void stop() {
        nova::topic_log::info("kafka", "Stopping Kafka...");
        m_keep_alive.store(false);
    }

private:
    properties m_props;
    std::unique_ptr<rd_kafka_t, destroyer<decltype(rd_kafka_destroy)>> m_producer{ nullptr, rd_kafka_destroy };
    std::jthread m_poll_thread;
    std::atomic_bool m_keep_alive = true;
};

} // namespace dsp::kf
