#pragma once

#include <boost/asio.hpp>
#include <array>
#include <memory>

class GatewayConnection : public std::enable_shared_from_this<GatewayConnection> {
public:
    explicit GatewayConnection(boost::asio::ip::tcp::socket socket);

    void Start();

private:
    void DoRead();

private:
    boost::asio::ip::tcp::socket socket_;
    std::array<char, 1024> buffer_;
};