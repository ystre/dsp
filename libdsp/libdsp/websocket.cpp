#include <libdsp/websocket.hpp>

namespace dsp::ws {

server::server(const tcp::net_config& cfg)
    : m_endpoint{ boost::asio::ip::tcp::v6(), cfg.port }
    , m_acceptor(m_io_context, m_endpoint)
    , m_socket(m_io_context)
    , m_config(cfg)
{
    boost::beast::error_code ec;

    // m_acceptor.open(m_endpoint.protocol(), ec);
    // throw_if_error(ec);

    m_acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
    throw_if_error(ec);

    m_acceptor.bind(m_endpoint, ec);
    throw_if_error(ec);

    m_acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
    throw_if_error(ec);
}


} // namespace dsp::ws
