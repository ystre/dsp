/**
 * Part of Data Stream Processing framework.
 *
 * DSP - Cache
 */

#pragma once

#include <nova/data.hh>
#include <nova/error.hh>

#include <any>
#include <memory>
#include <string>
#include <unordered_map>

namespace dsp {

class metrics_registry;
class cache;

struct context {
    std::shared_ptr<metrics_registry> stats;
    std::shared_ptr<class cache> cache;
    std::any app;
};

// tag::message[]
struct message {
    nova::bytes key;
    std::string subject;
    std::unordered_map<std::string, std::string> properties;
    nova::bytes payload;
};
// end::message[]

class northbound_interface {
public:
    virtual bool send(const message&) = 0;
    virtual void stop() = 0;
    virtual void update(metrics_registry&) { /* optional */ }
    virtual ~northbound_interface() = default;
};

/**
 * @brief   A virtual cache, a proxy, that broadcasts messages to all attached
 *          northbound interfaces.
 */
class cache {
    using interfaces_a = std::unordered_map<std::string, std::unique_ptr<northbound_interface>>;

public:

    void attach_northbound(const std::string& name, std::unique_ptr<northbound_interface> interface) {
        m_interfaces.insert({ name, std::move(interface) });
    }

    /**
     * @brief   Send a message.
     *
     * @returns with false if any interface failed to process the message.
     */
    auto send(const message& msg) -> bool {
        auto success = true;

        for (const auto& [_, x] : m_interfaces) {
            if (not x->send(msg)) {
                success = false;
            }
        }

        return success;
    }

    /**
     * @brief   Gracefully stop all interfaces.
     *
     * Generally, this should not be required as destructors should make
     * sure the proper clean-up.
     */
    void stop() {
        for (const auto& [_, x] : m_interfaces) {
            x->stop();
        }
    }

    /**
     * @brief   Access an attached northbound interface.
     *
     * @throws  in case the interface is unknown or the type is incorrect.
     */
    template <typename Interface>
    [[nodiscard]] auto get_northbound(const std::string& name) -> Interface* {
        auto it = m_interfaces.find(name);
        if (it == std::end(m_interfaces)) {
            throw nova::exception("Unknown interface with name: {}", name);
        }

        auto* ptr = dynamic_cast<Interface*>(it->second.get());
        if (ptr == nullptr) {
            throw nova::exception("Cast failed: interface type mismatch");
        }

        return ptr;
    }

    [[nodiscard]] auto interfaces() const -> const interfaces_a& {
        return m_interfaces;
    }

private:
    interfaces_a m_interfaces;

};

} // namespace dsp
