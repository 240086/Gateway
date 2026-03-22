#include "router/ShardManager.h"
#include "common/logger/Logger.h"

ShardManager &ShardManager::Instance()
{
    static ShardManager instance;
    return instance;
}

void ShardManager::SetShardCount(uint32_t count)
{
    std::unique_lock lock(rwMtx_); // 写锁，阻塞所有读写
    shardCount_ = (count > 0) ? count : 1;
    LOG_INFO("[ShardManager] Shard count updated to: {}", shardCount_);
}

uint32_t ShardManager::GetOrAssignShard(uint32_t sessionId)
{
    // 1. 尝试使用【共享读锁】查找已知会话
    {
        std::shared_lock readLock(rwMtx_);
        auto it = sessionShardMap_.find(sessionId);
        if (it != sessionShardMap_.end())
        {
            return it->second;
        }
    }

    // 2. 如果没找到，升级为【排他写锁】进行分配
    {
        std::unique_lock writeLock(rwMtx_);

        // 双重检查避免读写锁切换期间被其他线程插入
        auto it = sessionShardMap_.find(sessionId);
        if (it != sessionShardMap_.end())
        {
            return it->second;
        }

        // 轮询分配逻辑 (Round-Robin)
        uint32_t assignedShard = nextShardIdx_.fetch_add(1, std::memory_order_relaxed) % shardCount_;
        sessionShardMap_[sessionId] = assignedShard;

        LOG_DEBUG("[ShardManager] Assigned Session {} to Shard {}", sessionId, assignedShard);
        return assignedShard;
    }
}

void ShardManager::Remove(uint32_t sessionId)
{
    std::unique_lock lock(rwMtx_); // 写锁
    if (sessionShardMap_.erase(sessionId))
    {
        LOG_DEBUG("[ShardManager] Removed session: {}", sessionId);
    }
}