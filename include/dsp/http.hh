// TODO(refact): Copy-pasta from our friend GPT.

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>

#include <string>
#include <functional>

namespace http = boost::beast::http;
namespace net = boost::asio;
using boost_tcp = boost::asio::ip::tcp;

namespace dsp {

class http_server {
public:
    using RequestHandler = std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)>;

    http_server(const std::string& address, unsigned short port, RequestHandler handler)
        : ioc_{}, acceptor_{net::make_strand(ioc_)}, request_handler_{std::move(handler)} {
        // Resolve address and bind socket
        boost_tcp::endpoint endpoint{net::ip::make_address(address), port};
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(net::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen(net::socket_base::max_listen_connections);

        start_accepting();
    }

    void run() {
        ioc_.run();
    }

private:
    void start_accepting() {
        acceptor_.async_accept(
            net::make_strand(ioc_),
            [this](boost::system::error_code ec, boost_tcp::socket socket) {
                if (!ec) {
                    std::make_shared<Session>(std::move(socket), request_handler_)->start();
                }
                start_accepting();
            });
    }

    class Session : public std::enable_shared_from_this<Session> {
    public:
        Session(boost_tcp::socket socket, RequestHandler handler)
            : socket_(std::move(socket)), request_handler_{std::move(handler)} {}

        void start() {
            read_request();
        }

    private:
        void read_request() {
            auto self = shared_from_this();
            http::async_read(
                socket_, buffer_, request_,
                [self](boost::system::error_code ec, std::size_t bytes_transferred) {
                    boost::ignore_unused(bytes_transferred);
                    if (!ec) {
                        self->handle_request();
                    }
                });
        }

        void handle_request() {
            response_.result(http::status::ok);
            request_handler_(request_, response_);
            respond();
        }

        void respond() {
            auto self = shared_from_this();
            http::async_write(
                socket_, response_,
                [self](boost::system::error_code ec, std::size_t bytes_transferred) {
                    boost::ignore_unused(bytes_transferred);
                    self->socket_.shutdown(boost_tcp::socket::shutdown_send, ec);
                });
        }

        boost_tcp::socket socket_;
        boost::beast::flat_buffer buffer_;
        http::request<http::string_body> request_;
        http::response<http::string_body> response_;
        RequestHandler request_handler_;
    };

    net::io_context ioc_;
    boost_tcp::acceptor acceptor_;
    RequestHandler request_handler_;
};

} // namespace dsp
