#include "proxy/ProxyService.h"
#include "core/GatewayConnection.h"
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
    // 1. 获取核心状态
    uint32_t sid = client->GetSessionId();
    uint32_t ip = client->GetRemoteIP();
    // 🔥 限流检查（第一道防线）
    if (!RateLimiter::Instance().Allow(sid, ip))
    {
        LOG_WARN("[RateLimit] Blocked request: sid={}, ip={}", sid, ip);

        // 可选：返回错误包
        client->SendRaw(msgId, nullptr, 0);
        return;
    }

    uint32_t seqId = client->NextSeqId(); // 🔥 获取递增序列号，解决高并发覆盖问题

    // 2. 路由决策 (确定去哪个类型的服务器)
    auto type = MessageRouter::Instance().Route(msgId);
    if (type == ServerType::UNKNOWN)
    {
        LOG_WARN("[Proxy] Unmapped MsgId: {}. Discarding.", msgId);
        return;
    }

    auto it = pools_.find(type);
    if (it == pools_.end())
    {
        LOG_ERROR("[Proxy] Pool for Type {} not initialized", (int)type);
        return;
    }

    // 3. 物理分片决策 (确保有状态路由)
    uint32_t shardId = ShardManager::Instance().GetOrAssignShard(sid);
    auto backend = it->second.AcquireByShard(shardId);

    if (!backend)
    {
        Metrics::Instance().Inc("proxy.forward.fail");
        LOG_ERROR("[Proxy] No backend available for shard {}", shardId);
        // todo: 给客户端回发系统繁忙的错误包
        return;
    }

    // 4. 🔥 注册请求生命周期 (超时监控开始)
    RequestManager::Instance().Add(sid, msgId, seqId);

    // 5. 登记 Session 映射
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_[sid] = std::weak_ptr<GatewayConnection>(client);
    }

    // 6. 协议封装与转发
    InternalPacket pkt;
    pkt.SetSessionId(sid);
    pkt.SetMessageId(msgId);
    pkt.SetSequenceId(seqId); // 🔥 必须注入 seqId
    pkt.Append(data, len);

    Metrics::Instance().Inc("proxy.forward.total");
    backend->Send(std::make_shared<std::vector<char>>(pkt.Serialize()));
}

void ProxyService::OnBackendReply(
    uint32_t sid,
    uint16_t msgId,
    uint32_t seqId,
    const char *data,
    size_t len)
{
    // 1. 🔥 迟到包拦截 & 取消超时定时器
    bool isRequestValid = RequestManager::Instance().OnReply(sid, msgId, seqId);
    if (!isRequestValid)
    {
        LOG_WARN("[Proxy] Late packet dropped sid={}, msgId={}, seqId={}", sid, msgId, seqId);
        return; // 直接丢弃，防止客户端状态机混乱
    }

    Metrics::Instance().Inc("proxy.reply.total");
    std::shared_ptr<GatewayConnection> client;

    // 2. 查找并锁定对应的客户端连接
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end())
        {
            client = it->second.lock(); // 尝试提升弱引用
            if (!client)
            {
                // 如果对象已被销毁，顺手清理悬空指针
                sessions_.erase(it);
            }
        }
    }

    // 3. 如果玩家已掉线，丢弃包
    if (!client)
    {
        LOG_DEBUG("[Proxy] Session expired. Reply dropped. sid={}", sid);
        return;
    }

    // 4. 完美闭环：将数据透传回客户端
    client->SendRaw(msgId, data, len);
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