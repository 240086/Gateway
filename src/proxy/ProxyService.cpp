#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include "network/protocol/InternalPacket.h"
#include "common/config/Config.h"
#include "router/ShardManager.h"
#include "session/RequestManager.h"
#include "common/metrics/Metrics.h"
#include "limit/RateLimiter.h"

ProxyService &ProxyService::Instance()
{
    static ProxyService instance;
    return instance;
}

#include "proxy/ProxyService.h"
#include "common/config/Config.h"
#include "common/logger/Logger.h"

void ProxyService::Init(boost::asio::io_context &io)
{
    // 1. 获取 Config 单例
    const auto &config = Config::Instance();

    // 2. 获取后端列表
    // 使用 GetValue<std::vector<YAML::Node>> 获取整个列表
    // 如果路径不存在或不是列表，GetValue 会触发 catch 并返回空的 vector
    YAML::Node servers = config.GetNode("backend.game_servers");

    if (!servers || !servers.IsSequence())
    {
        LOG_ERROR("[Proxy] backend.game_servers is missing or not a list!");
        return;
    }

    // 3. 遍历列表中的每一个服务器配置项
    for (size_t i = 0; i < servers.size(); ++i)
    {
        const YAML::Node &item = servers[i];

        try
        {
            // 直接使用 item["key"].as<T>(default) 获取具体字段
            // 注意：这里不需要 template 关键字，因为 item 是具体的 YAML::Node 对象
            std::string typeStr = item["type"] ? item["type"].as<std::string>() : "UNKNOWN";
            std::string host = item["host"] ? item["host"].as<std::string>() : "127.0.0.1";
            int port = item["port"] ? item["port"].as<int>() : 9000;
            int connections = item["connections"] ? item["connections"].as<int>() : 1;

            // 转换类型
            ServerType type = StringToServerType(typeStr);
            if (type == ServerType::UNKNOWN)
            {
                LOG_ERROR("[Proxy] Ignoring unknown server type at index {}: {}", i, typeStr);
                continue;
            }

            LOG_INFO("[Proxy] Initializing Pool: [{}] -> {}:{} (Conns: {})",
                     typeStr, host, port, connections);

            // 4. 初始化连接池
            auto &pool = pools_[type];

            if (!pool.IsInitialized())
            {
                pool.Init(io, host, port, connections);
            }
            else
            {
                LOG_WARN("[Proxy] Duplicate backend type [{}], skipping extra config.", typeStr);
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("[Proxy] Failed to parse backend node at index {}: {}", i, e.what());
        }
    }

    if (pools_.empty())
    {
        LOG_ERROR("[Proxy] No backend pools initialized! Gateway has nowhere to forward packets.");
    }
}

void ProxyService::ForwardToBackend(
    std::shared_ptr<Connection> client,
    uint16_t msgId,
    const char *data,
    size_t len)
{
    uint32_t sid = client->GetSessionId();

    uint32_t seqId = client->NextSeqId();

    auto type = MessageRouter::Instance().Route(msgId);
    if (type == ServerType::UNKNOWN)
    {
        LOG_WARN("[Proxy] Unknown msgId {}", msgId);
        return;
    }

    auto it = pools_.find(type);
    if (it == pools_.end())
    {
        LOG_ERROR("[Proxy] Pool not found");
        return;
    }

    uint32_t shardId = ShardManager::Instance().GetOrAssignShard(sid);
    auto backend = it->second.AcquireByShard(shardId);
    if (!backend)
    {
        LOG_ERROR("[Proxy] No backend available");
        return;
    }

    RequestManager::Instance().Add(sid, msgId, seqId);

    {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_[sid] = client;
    }

    InternalPacket pkt;
    pkt.SetSessionId(sid);
    pkt.SetMessageId(msgId);
    pkt.SetSequenceId(seqId);
    pkt.Append(data, len);

    backend->Send(std::make_shared<std::vector<char>>(pkt.Serialize()));
}

void ProxyService::OnBackendReply(
    uint32_t sid,
    uint16_t msgId,
    uint32_t seqId,
    const char *data,
    size_t len)
{
    if (!RequestManager::Instance().OnReply(sid, msgId, seqId))
        return;

    std::shared_ptr<Connection> client;

    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end())
        {
            client = it->second.lock();
            if (!client)
                sessions_.erase(it);
        }
    }

    if (!client)
        return;

    InternalPacket pkt;
    pkt.SetSessionId(sid);
    pkt.SetMessageId(msgId);
    pkt.SetSequenceId(seqId);
    pkt.Append(data, len);

    client->SendRaw(std::make_shared<std::vector<char>>(pkt.Serialize()));
}

void ProxyService::RemoveSession(uint32_t sid)
{
    // 1. 局部范围锁：清理映射表
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_.erase(sid);
    }

    // 2. 🔥 O(N_player) 极速清理该玩家所有未完成的请求，防止内存及定时器泄漏
    RequestManager::Instance().RemoveSession(sid);

    // 3. 🔥 解除物理分片绑定，允许该 SessionID 下次重新分配
    ShardManager::Instance().Remove(sid);

    RateLimiter::Instance().RemoveSid(sid);

    LOG_DEBUG("[Proxy] Cleaned up full lifecycle for sid={}", sid);
}