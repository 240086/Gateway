#include "gateway/core/GatewayConnection.h"
#include <iostream>

using boost::asio::ip::tcp;

GatewayConnection::GatewayConnection(tcp::socket socket)
    : socket_(std::move(socket)) {}

void GatewayConnection::Start() {
    DoRead();
}

void GatewayConnection::DoRead() {
    auto self = shared_from_this();

    socket_.async_read_some(
        boost::asio::buffer(buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                std::cout << "[Gateway] Received: " << length << " bytes" << std::endl;

                // 👉 当前阶段：只打印，不解析
                DoRead();
            } else {
                std::cout << "[Gateway] Connection closed" << std::endl;
            }
        });
}