#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>
#include <array>
#include <vector>
#include <chrono>
#include <boost/asio.hpp>
#include "network/Connection.h"

class IdleManager
{
public:
    static IdleManager &Instance();

    // 初始化：传入 io_context 和 超时毫秒数
    void Init(boost::asio::io_context &io, uint64_t timeoutMs);

    // 注册连接
    void Add(uint32_t sid, std::shared_ptr<Connection> conn);

    // 手动移除（如玩家正常下线）
    void Remove(uint32_t sid);

private:
    IdleManager() = default;
    void Tick();

    // 分片锁设计：将 10k+ 连接分散到 32 个桶中，降低遍历时的锁竞争
    static constexpr size_t SHARD_COUNT = 32;
    struct IdleShard
    {
        std::mutex mtx;
        std::unordered_map<uint32_t, std::weak_ptr<Connection>> conns;
    };

    std::array<IdleShard, SHARD_COUNT> shards_;

    // 使用 unique_ptr 管理 timer，规避 boost::asio::timer 不可赋值的问题
    std::unique_ptr<boost::asio::steady_timer> timer_;
    std::chrono::milliseconds timeout_duration_{60000}; // 默认 60s

    inline size_t GetShard(uint32_t sid) const { return sid % SHARD_COUNT; }
};