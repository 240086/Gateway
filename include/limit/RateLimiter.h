#pragma once
#include <unordered_map>
#include <array>
#include <mutex>
#include <chrono>

class RateLimiter
{
public:
    static RateLimiter &Instance();

    // 支持不同维度的配置
    void Init(int ipRate, int ipBurst, int sidRate, int sidBurst);

    // 统一入口：先查 IP，再查 Session
    bool Allow(uint32_t sid, uint32_t ip);

    // 显式清理（主要用于 Session）
    void RemoveSid(uint32_t sid);

private:
    RateLimiter() = default;

    struct Bucket
    {
        double tokens = 0.0;
        std::chrono::steady_clock::time_point lastRefill;
    };

    static constexpr size_t SHARD_COUNT = 64;
    struct Shard
    {
        std::mutex mtx;
        std::unordered_map<uint64_t, Bucket> map; // 使用 uint64 兼容不同 key
    };

    std::array<Shard, SHARD_COUNT> shards_;

    // 配置项
    int ipRate_, ipBurst_, sidRate_, sidBurst_;

    // 核心通用限流逻辑
    bool AllowInternal(uint64_t key, int rate, int burst);

    inline size_t GetShard(uint64_t key) const { return key % SHARD_COUNT; }
};