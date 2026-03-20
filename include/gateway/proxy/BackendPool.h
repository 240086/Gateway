#pragma once
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include "gateway/proxy/BackendConnection.h"

class BackendPool
{
public:
    // 禁用拷贝
    BackendPool(const BackendPool &) = delete;
    BackendPool &operator=(const BackendPool &) = delete;
    BackendPool() = default;

    void Init(boost::asio::io_context &io, const std::string &host, uint16_t port, size_t size);

    // 获取一个可用的连接
    std::shared_ptr<BackendConnection> Acquire();

private:
    std::vector<std::shared_ptr<BackendConnection>> conns_;
    std::atomic<size_t> index_{0};
    std::mutex init_mutex_;
};