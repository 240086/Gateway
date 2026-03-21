#include "proxy/ProxyService.h"
#include "core/GatewayConnection.h"
#include <boost/asio.hpp>
#include "common/logger/Logger.h"
#include "network/protocol/InternalPacket.h"
#include "common/config/Config.h"
#include <iostream>

ProxyService &ProxyService::Instance()
{
    static ProxyService instance;
    return instance;
}

void ProxyService::Init(boost::asio::io_context &io)
{
    auto servers = Config::Instance().GetGameServers();

    for (const auto &node : servers)
    {
        ServerType type = StringToServerType(node.typeStr);
        if (type == ServerType::UNKNOWN)
        {
            LOG_ERROR("[Proxy] Ignoring unknown server type in config: {}", node.typeStr);
            continue;
        }

        LOG_INFO("[Proxy] Initializing Pool: [{}] -> {}:{} (Conns: {})",
                 node.typeStr, node.host, node.port, node.connections);

        auto &pool = pools_[type];

        if (!pool.IsInitialized())
        {
            pool.Init(io, node.host, node.port, node.connections);
        }
        else
        {
            LOG_WARN("[Proxy] Duplicate backend type: {}", node.typeStr);
        }
    }
}

void ProxyService::ForwardToBackend(
    std::shared_ptr<GatewayConnection> client,
    uint16_t msgId,
    const char *data,
    size_t len)
{
    // 1. 路由决策
    auto type = MessageRouter::Instance().Route(msgId);
    if (type == ServerType::UNKNOWN)
    {
        LOG_WARN("[Proxy] Unmapped MsgId: {}. Discarding.", msgId);
        return;
    }

    // 2. 获取连接池
    auto it = pools_.find(type);
    if (it == pools_.end())
    {
        LOG_ERROR("[Proxy] Pool for Type {} not initialized", (int)type);
        return;
    }

    // 3. 获取可用后端连接
    auto backend = it->second.Acquire();
    Metrics::Instance().Inc("proxy.forward.total");

    if (!backend || !backend->IsConnected())
    {
        Metrics::Instance().Inc("proxy.forward.fail");
        LOG_ERROR("[Proxy] Backend Type {} currently unavailable", (int)type);
        // 这里可以考虑给客户端回一个错误码包todo...
        return;
    }

    // 4. Session 记录 (改用 weak_ptr 防泄漏)
    uint32_t sid = client->GetSessionId();
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_[sid] = std::weak_ptr<GatewayConnection>(client);
    }

    // 5. 封包并异步发送
    InternalPacket pkt;
    pkt.SetSessionId(sid);
    pkt.SetMessageId(msgId);
    pkt.Append(data, len);

    // 产生二进制流，推入 Backend 异步写队列
    auto raw = std::make_shared<std::vector<char>>(pkt.Serialize());
    backend->Send(raw);

    // LOG_DEBUG("[Proxy] Forwarded Msg {} for Sid {}", msgId, sid);
}

void ProxyService::OnBackendReply(
    uint32_t sid,
    uint16_t msgId,
    const char *data,
    size_t len)
{
    Metrics::Instance().Inc("proxy.reply.total");
    std::shared_ptr<GatewayConnection> client;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end())
        {
            client = it->second.lock();
            if (!client)
            {
                sessions_.erase(it);
            }
        }
    }

    if (!client)
    {
        LOG_DEBUG("[Proxy] Session expired sid={}", sid);
        return;
    }

    client->SendRaw(msgId, data, len);
}

void ProxyService::RemoveSession(uint32_t sid)
{
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_.erase(sid);
}