#pragma once
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <boost/asio.hpp>

// 1. 扩充 RequestKey：加入 seqId 防止并发请求覆盖
struct RequestKey
{
    uint32_t sid;
    uint16_t msgId;
    uint32_t seqId; // 核心新增：请求序列号

    bool operator==(const RequestKey &other) const
    {
        return sid == other.sid && msgId == other.msgId && seqId == other.seqId;
    }
};

struct RequestKeyHash
{
    size_t operator()(const RequestKey &k) const
    {
        size_t h = 0;
        // 使用类似 boost::hash_combine 的算法，不要手动位移 48 位
        auto combine = [&](size_t val)
        {
            h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        combine(k.sid);
        combine(k.msgId);
        combine(k.seqId);
        return h;
    }
};

class RequestManager
{
public:
    static RequestManager &Instance();

    void Init(boost::asio::io_context &io, int timeoutMs);
    void Add(uint32_t sid, uint16_t msgId, uint32_t seqId);
    bool OnReply(uint32_t sid, uint16_t msgId, uint32_t seqId);
    void RemoveSession(uint32_t sid);

private:
    RequestManager() = default;
    void RemoveSingleRequest(const RequestKey &key);

private:
    boost::asio::io_context *io_ = nullptr;
    int timeoutMs_ = 1000;

    struct Context
    {
        std::shared_ptr<boost::asio::steady_timer> timer;
        uint64_t startTimeUs;
    };

    static constexpr size_t SHARD_COUNT = 64;

    // 🔥 请求分片
    struct RequestShard
    {
        std::mutex mtx;
        std::unordered_map<RequestKey, Context, RequestKeyHash> map;
    };

    // 🔥 Session请求索引分片
    struct SessionShard
    {
        std::mutex mtx;
        std::unordered_map<uint32_t, std::vector<RequestKey>> map;
    };

    std::array<RequestShard, SHARD_COUNT> requestShards_;
    std::array<SessionShard, SHARD_COUNT> sessionShards_;

    inline size_t GetReqShardIdx(const RequestKey &key) const
    {
        return RequestKeyHash{}(key) % SHARD_COUNT;
    }

    inline size_t GetSessShardIdx(uint32_t sid) const
    {
        return sid % SHARD_COUNT;
    }
};