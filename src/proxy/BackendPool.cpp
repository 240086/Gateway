#include "proxy/BackendPool.h"
#include "common/logger/Logger.h"

void BackendPool::Init(boost::asio::io_context &io,
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
        auto conn = std::make_shared<BackendConnection>(io);
        conn->Connect(host, port);
        conns_.push_back(conn);
    }

    initialized_ = true;

    LOG_INFO("[BackendPool] Initialized with {} connections", size);
}

std::shared_ptr<BackendConnection> BackendPool::Acquire()
{
    if (conns_.empty())
        return nullptr;

    size_t size = conns_.size();
    if (size == 0)
        return nullptr;

    // round-robin 起点
    size_t start = index_.fetch_add(1, std::memory_order_relaxed) % size;

    // 🔥 快速路径：优先尝试当前
    auto conn = conns_[start];
    if (conn->IsConnected())
        return conn;

    // 🔥 fallback：最多扫描 2 个（避免 O(N)）
    for (size_t i = 1; i < std::min<size_t>(size, 3); ++i)
    {
        size_t idx = (start + i) % size;
        auto c = conns_[idx];

        if (c->IsConnected())
            return c;
    }

    // ❗全部不可用（降级策略）
    // 不刷日志（避免日志风暴）
    return nullptr;
}