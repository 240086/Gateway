#include "core/GatewayConnection.h"
#include "network/protocol/PacketParser.h"
#include "proxy/BackendConnection.h"
#include "proxy/ProxyService.h"
#include <iostream>

using boost::asio::ip::tcp;

GatewayConnection::GatewayConnection(tcp::socket socket)
    : socket_(std::move(socket)) {}

void GatewayConnection::Start()
{
    std::cout << "[Gateway] New connection\n";

    // 生成唯一 SessionId (简单示例：原子计数器)
    static std::atomic<uint32_t> s_id_gen{1000};
    sessionId_ = s_id_gen++;
    DoRead();
}

void GatewayConnection::DoRead()
{
    auto self = shared_from_this();
    socket_.async_read_some(boost::asio::buffer(buffer_),
                            [this, self](boost::system::error_code ec, std::size_t length)
                            {
                                if (!ec)
                                {
                                    recvBuffer_.Append(buffer_.data(), length);
                                    parser_.Parse(recvBuffer_, [this, self](uint16_t msgId, const char *data, size_t len)
                                                  {
                    // 交给 ProxyService 转发
                    ProxyService::Instance().ForwardToBackend(self, msgId, data, len); });
                                    DoRead();
                                }
                                else
                                {
                                    ProxyService::Instance().RemoveSession(sessionId_);
                                }
                            });
}

void GatewayConnection::SendRaw(uint16_t msgId, const char *data, size_t len)
{
    auto out = std::make_shared<std::vector<char>>(6 + len);
    uint32_t netLen = htonl(len);
    uint16_t netId = htons(msgId);

    memcpy(out->data(), &netLen, 4);
    memcpy(out->data() + 4, &netId, 2);
    memcpy(out->data() + 6, data, len);

    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(*out),
                             [self, out](boost::system::error_code, std::size_t)
                             {
                                 // out 捕获在 lambda 中，保证了异步发送时的内存安全
                             });
}