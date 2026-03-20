#include "gateway/proxy/BackendConnection.h"
#include "gateway/core/GatewayConnection.h"
#include "gateway/proxy/ProxyService.h"
#include <iostream>

using boost::asio::ip::tcp;

BackendConnection::BackendConnection(boost::asio::io_context &io)
    : socket_(io), connected_(false)
{
}

void BackendConnection::Connect(const std::string &host, uint16_t port)
{
    tcp::resolver resolver(socket_.get_executor());
    auto endpoints = resolver.resolve(host, std::to_string(port));

    auto self = shared_from_this();

    boost::asio::async_connect(socket_, endpoints,
                               [this, self](boost::system::error_code ec, tcp::endpoint)
                               {
                                   if (!ec)
                                   {
                                       connected_ = true;
                                       std::cout << "[Gateway] Connected to GameServer\n";
                                       DoRead();
                                   }
                               });
}

void BackendConnection::Send(std::shared_ptr<std::vector<char>> packet)
{
    if (!connected_)
        return;
    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(*packet),
                             [self, packet](boost::system::error_code ec, std::size_t)
                             {
                                 if (ec)
                                     std::cerr << "[Backend] Send failed\n";
                             });
}

void BackendConnection::DoRead()
{
    auto self = shared_from_this();

    std::array<char, 1024> buffer;

    socket_.async_read_some(
        boost::asio::buffer(buffer),
        [this, self, buffer](boost::system::error_code ec, std::size_t len) mutable
        {
            if (!ec)
            {
                recvBuffer_.Append(buffer.data(), len);

                parser_.Parse(recvBuffer_,
                              [this](uint16_t msgId, const char *data, size_t len)
                              {
                                  auto client = client_.lock();
                                  if (client)
                                  {
                                      client->SendRaw(msgId, data, len);
                                  }
                              });

                DoRead();
            }
        });
}

// 假设 parser 已经解析出内部包 [sid, msgId, data, len]
void BackendConnection::OnReadPacket(uint32_t sid, uint16_t msgId, const char *data, size_t len)
{
    // 关键：将回包路由回 ProxyService
    ProxyService::Instance().OnBackendReply(sid, msgId, data, len);
}