#include "core/GatewayConnection.h"
#include "network/protocol/PacketParser.h"
#include "proxy/BackendConnection.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include "network/protocol/Packet.h"
#include <iostream>

using boost::asio::ip::tcp;

GatewayConnection::GatewayConnection(tcp::socket socket)
    : socket_(std::move(socket)) {}

void GatewayConnection::Start()
{
    // 生成唯一 SessionId (简单示例：原子计数器)
    static std::atomic<uint32_t> s_id_gen{1000};
    sessionId_ = s_id_gen++;
    LOG_INFO("[Gateway] New connection, sid={}", sessionId_);
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
                                    Metrics::Instance().Inc("gateway.ingress_bytes", length);
                                    recvBuffer_.Append(buffer_.data(), length);
                                    parser_.Parse(recvBuffer_, [this, self](uint16_t msgId, const char *data, size_t len)
                                                  {
                                                    Metrics::Instance().Inc("gateway.recv_packets");
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
    Packet pkt;
    pkt.SetMessageId(msgId);
    pkt.Append(data, len);

    auto raw = std::make_shared<std::vector<char>>(pkt.Serialize());

    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(*raw),
                             [self, raw](boost::system::error_code, std::size_t)
                             {
                                 // out 捕获在 lambda 中，保证了异步发送时的内存安全
                             });
}