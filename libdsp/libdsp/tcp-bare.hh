/**
 * Part of Data Stream Processing framework.
 *
 * DSP - TCP server
 */

#include <libdsp/tcp.hh>
#include <libdsp/tcp_handler.hh>

#include <nova/log.hh>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

namespace dsp::tcp {

class server_bare {
public:
    server_bare(net_config cfg)
        : m_config(cfg)
    {}

    /**
     * @brief   Start the TCP server.
     *
     * It's a blocking call.
     */
    void start() {
        static constexpr std::size_t BufferSize = 1024;

        int server_fd, client_fd;
        struct sockaddr_in server_addr, client_addr;
        std::array<char, BufferSize> buffer;
        socklen_t addr_len = sizeof(client_addr);

        // Create a socket (IP protocol)
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }

        // Initialize the server address structure
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all network interfaces
        server_addr.sin_port = htons(m_config.port);  // Convert port to network byte order

        // Bind the socket to the specified address and port
        if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
            perror("Bind failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // Start listening for incoming connections
        if (listen(server_fd, 5) == -1) {
            perror("Listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // Accept an incoming connection
        if ((client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len)) == -1) {
            perror("Accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        auto handler = m_factory->create();
        handler->on_connection_init(dsp::tcp::connection_info{ .address = "NA", .port = 0 });
        printf("Client connected.\n");

        // Communicate with the client
        while (1) {
            memset(buffer.data(), 0, BufferSize);  // Clear the buffer
            ssize_t bytes_received = recv(client_fd, buffer.data(), BufferSize - 1, 0);
            if (bytes_received <= 0) {
                printf("Client disconnected.\n");
                break;
            }

            auto data = nova::data_view{ buffer.data(), static_cast<std::size_t>(bytes_received) };
            handler->process(data);

            // Echo the data back to the client
            // if (send(client_fd, buffer, static_cast<std::size_t>(bytes_received), 0) == -1) {
                // perror("Send failed");
                // break;
            // }
        }

        // Clean up and close the sockets
        close(client_fd);
        close(server_fd);
    }

    void stop();

    void set(std::shared_ptr<handler_factory> factory) {
        m_factory = std::move(factory);
    }

    [[nodiscard]] auto port() const -> port_type { return m_config.port; }
    // [[nodiscard]] auto metrics() -> const server_metrics& { return *m_metrics; }

private:
    port_type m_port;

    std::shared_ptr<handler_factory> m_factory { nullptr };
    net_config m_config;

    // std::shared_ptr<server_metrics> m_metrics = std::make_shared<server_metrics>();

};

} // namespace dsp::tcp
