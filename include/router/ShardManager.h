#pragma once
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>
#include <atomic>

class ShardManager
{
public:
    static ShardManager &Instance();

    // 初始化/更新分片总数（从 Config 加载时调用）
    void SetShardCount(uint32_t count);

    // 获取已分配的分片 ID，若未分配则根据轮询分配一个新的
    // 返回值范围：[0, shardCount_ - 1]
    uint32_t GetOrAssignShard(uint32_t sessionId);

    // 玩家断开连接时必须调用，防止内存泄漏
    void Remove(uint32_t sessionId);

private:
    ShardManager() = default;
    ~ShardManager() = default;

    // 禁止拷贝
    ShardManager(const ShardManager &) = delete;
    ShardManager &operator=(const ShardManager &) = delete;

private:
    // 使用读写锁：高并发下 Get 操作不互斥
    mutable std::shared_mutex rwMtx_;

    // 会话到分片的映射表
    std::unordered_map<uint32_t, uint32_t> sessionShardMap_;

    // 轮询计数器（使用原子变量保证分配时的线程安全）
    std::atomic<uint32_t> nextShardIdx_{0};

    // 当前逻辑分片总数（对应 BackendPool 的连接数）
    uint32_t shardCount_ = 1;
};