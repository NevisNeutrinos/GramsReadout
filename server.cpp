#include "src/networking/tcp_server.h"
#include <iostream>
// #include "asio.hpp"
// using asio::ip::tcp;

int main() {
    try {
        asio::io_context io_context;
        TcpServer server(io_context, 12345);
        io_context.run();  // Run the IO context loop
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}