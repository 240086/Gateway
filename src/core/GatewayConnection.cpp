#include "core/GatewayConnection.h"
#include "network/protocol/PacketParser.h"
#include "proxy/BackendConnection.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include "network/protocol/Packet.h"
#include "limit/RateLimiter.h"
#include "core/IdleManager.h"
#include <iostream>

using boost::asio::ip::tcp;

GatewayConnection::GatewayConnection(tcp::socket socket)
    : socket_(std::move(socket)) {}

void GatewayConnection::Start()
{
    // 生成唯一 SessionId (简单示例：原子计数器)
    static std::atomic<uint32_t> s_id_gen{1000};
    sessionId_ = s_id_gen++;
    UpdateActivity();
    IdleManager::Instance().Add(sessionId_, shared_from_this());

    Metrics::Instance().Inc(MetricId::ConnectionActive);
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
                                    UpdateActivity();
                                    recvBuffer_.Append(buffer_.data(), length);
                                    parser_.Parse(recvBuffer_, [this, self](uint16_t msgId, const char *data, size_t len)
                                                  {
                                                    uint32_t ip = socket_.remote_endpoint().address().to_v4().to_uint();
                                                    if (!RateLimiter::Instance().Allow(sessionId_, ip))
                                                    {
                                                        LOG_WARN("[RateLimit] sid={} blocked", sessionId_);
                                                        return;
                                                    } 
                                                    // 交给 ProxyService 转发
                                                    ProxyService::Instance().ForwardToBackend(self, msgId, data, len); });
                                    DoRead();
                                }
                                else
                                {
                                    Metrics::Instance().Add(MetricId::ConnectionActive, -1);
                                    ProxyService::Instance().RemoveSession(sessionId_);
                                    HandleDisconnect();
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
                             [this, self, raw](const boost::system::error_code &ec, std::size_t)
                             {
                                 if (!ec)
                                     UpdateActivity();
                                 // out 捕获在 lambda 中，保证了异步发送时的内存安全
                             });
}

void GatewayConnection::Close()
{
    boost::system::error_code ec;
    socket_.close(ec); // 强制关闭 Socket，会触发 DoRead 里的 ec 逻辑
}

void GatewayConnection::HandleDisconnect()
{
    // 统一资源清理逻辑
    Metrics::Instance().Add(MetricId::ConnectionActive, -1);

    // 1. 从 ProxyService 移除（清理挂起的请求）
    ProxyService::Instance().RemoveSession(sessionId_);

    // 2. 从 IdleManager 移除（停止心跳检查）
    IdleManager::Instance().Remove(sessionId_);
}