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

class handler : public dsp::handler_frame<handler> {
public:
    handler(std::shared_ptr<dsp::cache> cache, const dsp::context& ctx)
        : m_cache(std::move(cache))
        , m_ctx(ctx)
        , m_appctx(std::any_cast<std::shared_ptr<context>>(m_ctx.app))
    {}

    auto do_process(nova::data_view data) -> std::size_t;
    void do_process(dat::heartbeat data);
    void do_process(dat::dyn_message data);
    void do_eof() { nova::topic_log::info("handler", "{}", perf_summary()); };

private:
    std::shared_ptr<dsp::cache> m_cache;
    dsp::context m_ctx;
    std::shared_ptr<context> m_appctx;

    void send(const dsp::message& msg);

};

class passthrough_handler : public dsp::handler_frame<passthrough_handler> {
public:
    passthrough_handler(std::shared_ptr<dsp::cache> cache, const dsp::context& ctx)
        : m_cache(std::move(cache))
        , m_ctx(ctx)
        , m_appctx(std::any_cast<std::shared_ptr<context>>(m_ctx.app))
    {}

    auto do_process(nova::data_view data) -> std::size_t;
    auto do_process(dat::message data) -> std::size_t;
    void do_eof() { nova::topic_log::info("handler", "{}", perf_summary()); };

private:
    std::shared_ptr<dsp::cache> m_cache;
    dsp::context m_ctx;
    std::shared_ptr<context> m_appctx;

};

class factory : public dsp::handler_factory {
public:
    factory(std::shared_ptr<dsp::cache> cache, handler_type type)
        : m_cache(std::move(cache))
        , m_type(type)
    {}

    auto create() -> std::unique_ptr<dsp::tcp::handler> override {
        switch (m_type) {
            case handler_type::passthrough:  return std::make_unique<passthrough_handler>(m_cache, m_ctx);
            case handler_type::telemetry:    return std::make_unique<handler>(m_cache, m_ctx);
        }
        nova::unreachable();
    }

    void context(dsp::context ctx) override {
        // TODO(design): Why is cache not in context?
        m_ctx = std::move(ctx);
    }

private:
    std::shared_ptr<dsp::cache> m_cache;
    handler_type m_type;

    dsp::context m_ctx;

};

} // namespace app
