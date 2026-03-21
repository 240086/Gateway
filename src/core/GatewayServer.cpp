#include "core/GatewayServer.h"
#include "core/GatewayConnection.h"
#include <iostream>

using boost::asio::ip::tcp;

GatewayServer::GatewayServer(boost::asio::io_context &io, uint16_t port)
    : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {}

void GatewayServer::Start()
{
    std::cout << "[Gateway] Start accepting..." << std::endl;
    DoAccept();
}

void GatewayServer::DoAccept()
{
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
            if (!ec)
            {
                std::cout << "[Gateway] New connection" << std::endl;

                std::make_shared<GatewayConnection>(std::move(socket))->Start();
            }

            DoAccept();
        });
}