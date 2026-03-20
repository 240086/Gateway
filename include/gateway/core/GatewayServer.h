#pragma once

#include <boost/asio.hpp>
#include <memory>

class GatewayServer {
public:
    GatewayServer(boost::asio::io_context& io, uint16_t port);

    void Start();

private:
    void DoAccept();

private:
    boost::asio::ip::tcp::acceptor acceptor_;
};