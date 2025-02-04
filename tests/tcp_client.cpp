#include <iostream>
#include <asio.hpp>

using asio::ip::tcp;

int main() {
    try {
        asio::io_context io_context;
        tcp::socket socket(io_context);
        socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 12345));

        std::cout << "Connected to server!" << std::endl;

        while (true) {
            std::string message;
            std::cout << "Enter message: ";
            std::getline(std::cin, message);

            asio::write(socket, asio::buffer(message));

            char reply[1024];
            std::size_t length = socket.read_some(asio::buffer(reply));
            std::cout << "Server response: " << std::string(reply, length) << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}
