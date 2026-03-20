#include "gateway/proxy/ProxyService.h"
#include "gateway/core/GatewayConnection.h"
#include <boost/asio.hpp>
#include <iostream>

ProxyService &ProxyService::Instance()
{
    static ProxyService instance;
    return instance;
}

void ProxyService::Init(boost::asio::io_context &io, const std::string &host, uint16_t port)
{
    // 建议配置从配置文件读取，这里暂时硬编码
    pool_.Init(io, host, port, 4);
}

void ProxyService::ForwardToBackend(std::shared_ptr<GatewayConnection> client, uint16_t msgId, const char *data, size_t len)
{
    auto backend = pool_.Acquire();
    if (!backend || !backend->IsConnected())
    {
        std::cerr << "[Proxy] No available backend connection\n";
        return;
    }

    uint32_t sid = client->GetSessionId();

    // 记录路由表（用于回包）
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_[sid] = client;
    }

    // 构造内部协议包 [Len(4)|Sid(4)|Id(2)|Body]
    auto packet = std::make_shared<std::vector<char>>();
    packet->resize(10 + len);

    uint32_t totalLen = htonl(static_cast<uint32_t>(6 + len)); // 内部包头长度
    uint32_t netSid = htonl(sid);
    uint16_t netId = htons(msgId);

    memcpy(packet->data(), &totalLen, 4);
    memcpy(packet->data() + 4, &netSid, 4);
    memcpy(packet->data() + 8, &netId, 2);
    memcpy(packet->data() + 10, data, len);

    backend->Send(packet);
}

// 2. 后端 -> 网关 -> 客户端 (由 BackendConnection 调用)
void ProxyService::OnBackendReply(uint32_t sid, uint16_t msgId, const char *data, size_t len)
{
    std::shared_ptr<GatewayConnection> client;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end())
            client = it->second.lock();
    }

    if (client)
    {
        client->SendRaw(msgId, data, len);
    }
}

void ProxyService::RemoveSession(uint32_t sid)
{
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_.erase(sid);
}