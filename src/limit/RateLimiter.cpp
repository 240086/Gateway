#include "limit/RateLimiter.h"
#include <algorithm>

RateLimiter &RateLimiter::Instance()
{
    static RateLimiter inst;
    return inst;
}

void RateLimiter::Init(int ipRate, int ipBurst, int sidRate, int sidBurst)
{
    ipRate_ = ipRate;
    ipBurst_ = ipBurst;
    sidRate_ = sidRate;
    sidBurst_ = sidBurst;
}

bool RateLimiter::Allow(uint32_t sid, uint32_t ip)
{
    // 1. IP 限流：高位补 0 (默认)
    uint64_t ipKey = static_cast<uint64_t>(ip);
    if (!AllowInternal(ipKey, ipRate_, ipBurst_))
        return false;

    // 2. Session 限流：高位补 1 (染色)
    // 这样 64 位下，ipKey 永远不会等于 sidKey
    uint64_t sidKey = (1ULL << 32) | static_cast<uint64_t>(sid);
    return AllowInternal(sidKey, sidRate_, sidBurst_);
}

bool RateLimiter::AllowInternal(uint64_t key, int rate, int burst)
{
    auto now = std::chrono::steady_clock::now();
    size_t idx = GetShard(key);

    std::lock_guard<std::mutex> lock(shards_[idx].mtx);
    auto &bucket = shards_[idx].map[key];

    // 初始化判定：使用 time_point() 判定更稳健
    if (bucket.lastRefill == std::chrono::steady_clock::time_point())
    {
        bucket.tokens = static_cast<double>(burst);
        bucket.lastRefill = now;
    }

    // Refill 逻辑
    auto elapsed = std::chrono::duration<double>(now - bucket.lastRefill).count();
    if (elapsed > 0)
    {
        bucket.tokens = std::min(static_cast<double>(burst), bucket.tokens + elapsed * rate);
        bucket.lastRefill = now;
    }

    // 消费
    if (bucket.tokens >= 1.0)
    {
        bucket.tokens -= 1.0;
        return true;
    }
    return false;
}

void RateLimiter::RemoveSid(uint32_t sid)
{
    uint64_t sidKey = (1ULL << 32) | static_cast<uint64_t>(sid);
    size_t idx = GetShard(sidKey);
    std::lock_guard<std::mutex> lock(shards_[idx].mtx);
    shards_[idx].map.erase(sidKey);
}