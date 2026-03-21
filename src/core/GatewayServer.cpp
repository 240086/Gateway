#include "core/GatewayServer.h"
#include "core/GatewayConnection.h"
#include "common/logger/Logger.h"
#include <iostream>

using boost::asio::ip::tcp;

GatewayServer::GatewayServer(boost::asio::io_context &io, uint16_t port)
    : acceptor_(io, tcp::endpoint(tcp::v4(), port)) {}

void GatewayServer::Start()
{
    LOG_INFO("[Gateway] Start accepting...");
    DoAccept();
}

void GatewayServer::DoAccept()
{
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
            if (!ec)
            {
                LOG_INFO("[Gateway] New connection");
                std::make_shared<GatewayConnection>(std::move(socket))->Start();
            }

            DoAccept();
        });
}

void GatewayServer::Stop()
{
    LOG_INFO("[Gateway] Stopping acceptor...");
    boost::system::error_code ec;

    // 关闭监听器，停止接收新连接
    acceptor_.close(ec);

    if (ec)
    {
        LOG_ERROR("[Gateway] Error closing acceptor: {}", ec.message());
    }
    else
    {
        LOG_INFO("[Gateway] Acceptor closed successfully.");
    }
}