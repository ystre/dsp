/**
 * Part of Data Stream Processing framework.
 *
 * DSP - TCP handler
 */

#pragma once

#include <nova/data.hh>
#include <nova/error.hh>

#include <boost/system/detail/error_code.hpp>

#include <any>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>

namespace dsp::tcp {

struct connection_info {
    std::string address;
    std::uint_least16_t port;
};

// tag::tcp-handler[]
class handler {
public:
    virtual auto process(nova::data_view) -> std::size_t = 0;
    virtual void on_connection_init(const connection_info&) = 0;
    virtual void on_error(const boost::system::error_code&, const connection_info&) = 0;
    virtual void on_error(const nova::exception&, const connection_info&) = 0;
    virtual ~handler() = default;
};
// end::tcp-handler[]

class handler_factory_interface {
public:
    virtual auto create() -> std::unique_ptr<handler> = 0;
    virtual ~handler_factory_interface() = default;
};

} // namespace dsp::tcp
