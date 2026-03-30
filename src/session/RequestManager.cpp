#include "session/RequestManager.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include <algorithm>
#include "common/metrics/Metrics.h"

RequestManager &RequestManager::Instance()
{
    static RequestManager instance;
    return instance;
}

void RequestManager::Init(boost::asio::io_context &io, int timeoutMs)
{
    io_ = &io;
    timeoutMs_ = timeoutMs;
}

void RequestManager::Add(uint32_t sid, uint16_t msgId, uint32_t seqId)
{
    RequestKey key{sid, msgId, seqId};

    auto timer = std::make_shared<boost::asio::steady_timer>(*io_);
    timer->expires_after(std::chrono::milliseconds(timeoutMs_));

    size_t rIdx = GetReqShardIdx(key);
    size_t sIdx = GetSessShardIdx(sid);

    uint64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

    {
        std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);
        requestShards_[rIdx].map[key] = {timer, nowUs}; // ✅ 修改
    }

    // 2. 写入 Session 索引 (细粒度锁)
    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        sessionShards_[sIdx].map[sid].push_back(key);
    }

    timer->async_wait([this, key](const boost::system::error_code &ec)
                      {
        if (!ec)
        {
            Metrics::Instance().Inc(MetricId::RequestTimeout);
            LOG_WARN("[Timeout] sid={}, msgId={}, seqId={}", key.sid, key.msgId, key.seqId);
            // ProxyService::Instance().OnBackendReply(key.sid, key.msgId, key.seqId, nullptr, 0);
            this->RemoveSingleRequest(key);
        } });
}

bool RequestManager::OnReply(uint32_t sid, uint16_t msgId, uint32_t seqId)
{
    RequestKey key{sid, msgId, seqId};
    size_t rIdx = GetReqShardIdx(key);

    bool found = false;
    uint64_t startUs = 0;
    {
        std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);
        auto it = requestShards_[rIdx].map.find(key);
        if (it != requestShards_[rIdx].map.end())
        {
            it->second.timer->cancel();
            startUs = it->second.startTimeUs;
            requestShards_[rIdx].map.erase(it);
            found = true;
        }
    }

    // 某些协议使用“请求/响应 msgId 不同”的约定（如 100 -> 110），
    // 此时按完整 key 无法命中。降级到 sid + seqId 匹配，确保合法响应不被误丢弃。
    if (!found)
        found = TryRemoveBySidSeq(sid, seqId, startUs);

    if (!found)
        found = TryRemoveBySidMsg(sid, msgId, startUs);

    if (!found)
        found = TryRemoveSinglePendingBySid(sid, startUs);

    if (!found)
        return false;

    uint64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

    Metrics::Instance().ObserveLatency(nowUs - startUs);
    // 清理索引
    size_t sIdx = GetSessShardIdx(sid);
    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto &reqs = sessionShards_[sIdx].map[sid];
        auto reqIt = std::find(reqs.begin(), reqs.end(), key);
        if (reqIt != reqs.end())
            reqs.erase(reqIt);
        if (reqs.empty())
            sessionShards_[sIdx].map.erase(sid);
    }

    return true;
}

void RequestManager::RemoveSession(uint32_t sid)
{
    size_t sIdx = GetSessShardIdx(sid);
    std::vector<RequestKey> pendingKeys;

    // 🔥 关键防死锁操作：将 keys 拷贝出来，快速释放 Session 锁
    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto it = sessionShards_[sIdx].map.find(sid);
        if (it == sessionShards_[sIdx].map.end())
            return;

        pendingKeys = std::move(it->second);
        sessionShards_[sIdx].map.erase(it);
    }

    // 🔥 在没有锁住 Session 字典的情况下，去逐个清理 Request 字典
    for (const auto &key : pendingKeys)
    {
        size_t rIdx = GetReqShardIdx(key);
        std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);

        auto ctxIt = requestShards_[rIdx].map.find(key);
        if (ctxIt != requestShards_[rIdx].map.end())
        {
            ctxIt->second.timer->cancel();
            requestShards_[rIdx].map.erase(ctxIt);
        }
    }
}

void RequestManager::RemoveSingleRequest(const RequestKey &key)
{
    // 与 OnReply 清理逻辑类似，分别独立加锁即可，不再赘述，防止同时持有两把锁
    size_t rIdx = GetReqShardIdx(key);
    {
        std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);
        requestShards_[rIdx].map.erase(key);
    }

    size_t sIdx = GetSessShardIdx(key.sid);
    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto it = sessionShards_[sIdx].map.find(key.sid);
        if (it != sessionShards_[sIdx].map.end())
        {
            auto &reqs = it->second;
            auto reqIt = std::find(reqs.begin(), reqs.end(), key);
            if (reqIt != reqs.end())
                reqs.erase(reqIt);
            if (reqs.empty())
                sessionShards_[sIdx].map.erase(it);
        }
    }
}

bool RequestManager::TryRemoveBySidSeq(uint32_t sid, uint32_t seqId, uint64_t &startUs)
{
    size_t sIdx = GetSessShardIdx(sid);
    std::vector<RequestKey> keys;

    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto sit = sessionShards_[sIdx].map.find(sid);
        if (sit == sessionShards_[sIdx].map.end())
            return false;
        keys = sit->second;
    }

    for (const auto &k : keys)
    {
        if (k.seqId != seqId)
            continue;

        size_t rIdx = GetReqShardIdx(k);
        {
            std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);
            auto it = requestShards_[rIdx].map.find(k);
            if (it == requestShards_[rIdx].map.end())
                continue;

            it->second.timer->cancel();
            startUs = it->second.startTimeUs;
            requestShards_[rIdx].map.erase(it);
        }

        {
            std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
            auto sit = sessionShards_[sIdx].map.find(sid);
            if (sit != sessionShards_[sIdx].map.end())
            {
                auto &reqs = sit->second;
                auto reqIt = std::find(reqs.begin(), reqs.end(), k);
                if (reqIt != reqs.end())
                    reqs.erase(reqIt);
                if (reqs.empty())
                    sessionShards_[sIdx].map.erase(sit);
            }
        }
        return true;
    }

    return false;
}

bool RequestManager::TryRemoveBySidMsg(uint32_t sid, uint16_t msgId, uint64_t &startUs)
{
    size_t sIdx = GetSessShardIdx(sid);
    std::vector<RequestKey> keys;

    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto sit = sessionShards_[sIdx].map.find(sid);
        if (sit == sessionShards_[sIdx].map.end())
            return false;
        keys = sit->second;
    }

    for (const auto &k : keys)
    {
        if (k.msgId != msgId)
            continue;

        size_t rIdx = GetReqShardIdx(k);
        {
            std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);
            auto it = requestShards_[rIdx].map.find(k);
            if (it == requestShards_[rIdx].map.end())
                continue;

            it->second.timer->cancel();
            startUs = it->second.startTimeUs;
            requestShards_[rIdx].map.erase(it);
        }

        {
            std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
            auto sit = sessionShards_[sIdx].map.find(sid);
            if (sit != sessionShards_[sIdx].map.end())
            {
                auto &reqs = sit->second;
                auto reqIt = std::find(reqs.begin(), reqs.end(), k);
                if (reqIt != reqs.end())
                    reqs.erase(reqIt);
                if (reqs.empty())
                    sessionShards_[sIdx].map.erase(sit);
            }
        }
        return true;
    }

    return false;
}

bool RequestManager::TryRemoveSinglePendingBySid(uint32_t sid, uint64_t &startUs)
{
    size_t sIdx = GetSessShardIdx(sid);
    RequestKey onlyKey{};
    bool hasSingle = false;

    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto sit = sessionShards_[sIdx].map.find(sid);
        if (sit == sessionShards_[sIdx].map.end())
            return false;
        if (sit->second.size() != 1)
            return false;

        onlyKey = sit->second.front();
        hasSingle = true;
    }

    if (!hasSingle)
        return false;

    size_t rIdx = GetReqShardIdx(onlyKey);
    {
        std::lock_guard<std::mutex> lock(requestShards_[rIdx].mtx);
        auto it = requestShards_[rIdx].map.find(onlyKey);
        if (it == requestShards_[rIdx].map.end())
            return false;

        it->second.timer->cancel();
        startUs = it->second.startTimeUs;
        requestShards_[rIdx].map.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(sessionShards_[sIdx].mtx);
        auto sit = sessionShards_[sIdx].map.find(sid);
        if (sit != sessionShards_[sIdx].map.end())
        {
            auto &reqs = sit->second;
            auto reqIt = std::find(reqs.begin(), reqs.end(), onlyKey);
            if (reqIt != reqs.end())
                reqs.erase(reqIt);
            if (reqs.empty())
                sessionShards_[sIdx].map.erase(sit);
        }
    }
    return true;
}