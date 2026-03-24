#include "proxy/BackendPool.h"
#include "common/logger/Logger.h"

void BackendPool::Init(AsioContextPool &pool,
                       const std::string &host,
                       uint16_t port,
                       size_t size)
{
    std::lock_guard<std::mutex> lock(init_mutex_);

    if (initialized_)
        return;

    if (size == 0)
    {
        LOG_FATAL("[BackendPool] init size = 0");
        return;
    }

    conns_.reserve(size);

    for (size_t i = 0; i < size; ++i)
    {
        // 🔥 每次循环从池里取下一个 io_context
        // 这样连接 1 在线程 A，连接 2 在线程 B，充分利用多核
        auto &io = pool.GetIOContext();

        auto conn = std::make_shared<BackendConnection>(io);
        conn->Connect(host, port);
        conns_.push_back(conn);
    }

    initialized_ = true;

    LOG_INFO("[BackendPool] Initialized with {} connections", size);
}

// 优化后的逻辑片段
std::shared_ptr<BackendConnection> BackendPool::AcquireByShard(uint32_t shardId)
{
    if (conns_.empty())
        return nullptr;

    size_t size = conns_.size();
    size_t primary = shardId % size;

    // 1. 优先尝试玩家绑定的物理分片
    if (conns_[primary]->IsAvailable())
    {
        aliveCount_.fetch_add(1, std::memory_order_relaxed);
        return conns_[primary];
    }

    // 2. 只有当主节点挂了，才尝试备选
    // 审计建议：备选节点可以加一个简单的负载感知或随机偏移，防止流量过于集中在 primary+1
    for (size_t i = 1; i <= 2; ++i)
    { // 最多尝试 2 个邻居，防止死循环
        size_t secondary = (primary + i) % size;
        if (conns_[secondary]->IsAvailable())
        {
            // 🚩 警告：此处发生了路由漂移，建议打一个 warn 日志，方便监控后端压力
            LOG_WARN("[Proxy] Shard {} primary link down, falling back to {}", shardId, secondary);
            return conns_[secondary];
        }
    }

    return nullptr;
}