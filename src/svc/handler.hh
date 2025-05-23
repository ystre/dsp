/**
 * Part of Data Stream Processing framework.
 *
 * TCP connection handler for the actual business logic.
 */

#pragma once

#include <dsp/cache.hh>
#include <dsp/handler.hh>
#include <dsp/router.hh>
#include <dsp/tcp_handler.hh>

#include <nova/data.hh>
#include <nova/intrinsics.hh>

#include <any>
#include <cstddef>
#include <memory>

namespace app {

namespace dat {

    class message;
    class heartbeat;
    class dyn_message;

} // namespace dat

enum class handler_type {
    passthrough,
    telemetry,
};

struct context {
    dsp::router router;
    std::string topic;
    std::string script;
};

class handler : public dsp::tcp::handler_frame<handler> {
public:
    handler(const dsp::context& ctx)
        : m_ctx(ctx)
        , m_appctx(std::any_cast<std::shared_ptr<context>>(m_ctx.app))
    {}

    auto do_process(nova::data_view data) -> std::size_t;
    void do_process(dat::heartbeat data);
    void do_process(dat::dyn_message data);
    void do_eof() { nova::topic_log::info("handler", "{}", perf_summary()); };

private:
    dsp::context m_ctx;
    std::shared_ptr<context> m_appctx;

    void send(const dsp::message& msg);

};

class passthrough_handler : public dsp::tcp::handler_frame<passthrough_handler> {
public:
    passthrough_handler(const dsp::context& ctx)
        : m_ctx(ctx)
        , m_appctx(std::any_cast<std::shared_ptr<context>>(m_ctx.app))
    {}

    auto do_process(nova::data_view data) -> std::size_t;
    auto do_process(dat::message data) -> std::size_t;
    void do_eof() { nova::topic_log::info("handler", "{}", perf_summary()); };

private:
    dsp::context m_ctx;
    std::shared_ptr<context> m_appctx;

};

class factory : public dsp::tcp_handler_factory {
public:
    factory(handler_type type)
        : m_type(type)
    {}

    auto create() -> std::unique_ptr<dsp::tcp::handler> override {
        switch (m_type) {
            case handler_type::passthrough:  return std::make_unique<passthrough_handler>(m_ctx);
            case handler_type::telemetry:    return std::make_unique<handler>(m_ctx);
        }
        nova::unreachable();
    }

    void bind(dsp::context ctx) override {
        m_ctx = std::move(ctx);
    }

private:
    handler_type m_type;
    dsp::context m_ctx;

};

} // namespace app
