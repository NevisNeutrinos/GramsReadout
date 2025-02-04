#include "tcp_server.h"

TcpServer::TcpServer(asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
    std::cout << "Server started on port " << port << "..." << std::endl;
    StartAccept();
}

void TcpServer::StartAccept() {
    auto socket = std::make_shared<tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*socket, [this, socket](std::error_code ec) {
      if (!ec) {
        std::cout << "New client connected!" << std::endl;
        std::thread(&TcpServer::HandleClient, this, socket).detach();
      }
      StartAccept();  // Accept the next client
    });
}

void TcpServer::HandleClient(const std::shared_ptr<tcp::socket>& socket) {
    try {
        char data[1024];
        while (true) {
            std::size_t length = socket->read_some(asio::buffer(data));
            std::cout << "Received: " << std::string(data, length) << std::endl;
            asio::write(*socket, asio::buffer(data, length));  // Echo back
        }
    } catch (const std::exception& e) {
        std::cerr << "Client disconnected: " << e.what() << std::endl;
    }
}

// int main() {
//     try {
//         asio::io_context io_context;
//         TcpServer server(io_context, 12345);
//         io_context.run();  // Run the IO context loop
//     } catch (const std::exception& e) {
//         std::cerr << "Error: " << e.what() << std::endl;
//     }
//     return 0;
// }
