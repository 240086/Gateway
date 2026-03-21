#include "proxy/BackendPool.h"

void BackendPool::Init(boost::asio::io_context &io, const std::string &host, uint16_t port, size_t size)
{
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (!conns_.empty())
        return;

    for (size_t i = 0; i < size; ++i)
    {
        auto conn = std::make_shared<BackendConnection>(io);
        conn->Connect(host, port); // 内部需实现断线自动重连逻辑
        conns_.push_back(conn);
    }
}

std::shared_ptr<BackendConnection> BackendPool::Acquire()
{
    if (conns_.empty())
        return nullptr;

    size_t start_idx = index_.fetch_add(1, std::memory_order_relaxed) % conns_.size();

    // 简单的健康检查：如果当前连接不可用，尝试找下一个
    for (size_t offset = 0; offset < conns_.size(); ++offset)
    {
        size_t current = (start_idx + offset) % conns_.size();
        if (conns_[current]->IsConnected())
        { // 需要在 BackendConnection 实现该方法
            return conns_[current];
        }
    }

    // 如果全断开了，默认返回当前的，由上层处理发送失败
    return conns_[start_idx];
}