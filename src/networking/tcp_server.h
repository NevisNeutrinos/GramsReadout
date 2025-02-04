#ifndef TCP_SERVER_H_
#define TCP_SERVER_H_

#include <iostream>
#include <asio.hpp>
#include <thread>
#include <memory>

using asio::ip::tcp;

class TcpServer {
public:
    TcpServer(asio::io_context& io_context, short port);

private:
    tcp::acceptor acceptor_;

    void StartAccept();
    void HandleClient(const std::shared_ptr<tcp::socket>& socket);
};

#endif  // TCP_SERVER_H_
