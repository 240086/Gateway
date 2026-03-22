#pragma once
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include "proxy/BackendConnection.h"

class BackendPool
{
public:
    BackendPool() = default;
    BackendPool(const BackendPool &) = delete;
    BackendPool &operator=(const BackendPool &) = delete;

    void Init(boost::asio::io_context &io,
              const std::string &host,
              uint16_t port,
              size_t size);

    std::shared_ptr<BackendConnection> AcquireByShard(uint32_t shardId);

        bool IsInitialized() const
    {
        return initialized_;
    }

private:
    std::vector<std::shared_ptr<BackendConnection>> conns_;

    // round-robin
    std::atomic<size_t> index_{0};

    std::mutex init_mutex_;

    bool initialized_ = false;

    // 🔥 健康统计（简化版）
    std::atomic<size_t> aliveCount_{0};
};