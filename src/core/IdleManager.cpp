#include "core/IdleManager.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"

IdleManager &IdleManager::Instance()
{
    static IdleManager instance;
    return instance;
}

void IdleManager::Init(boost::asio::io_context &io, uint64_t timeoutMs)
{
    timeout_duration_ = std::chrono::milliseconds(timeoutMs);
    // 使用 make_unique 安全初始化 timer
    timer_ = std::make_unique<boost::asio::steady_timer>(io);

    LOG_INFO("[IdleManager] Initialized with {}ms timeout", timeoutMs);
    Tick();
}

void IdleManager::Add(uint32_t sid, std::shared_ptr<Connection> conn)
{
    size_t idx = GetShard(sid);
    std::lock_guard<std::mutex> lock(shards_[idx].mtx);
    shards_[idx].conns[sid] = conn;
}

void IdleManager::Remove(uint32_t sid)
{
    size_t idx = GetShard(sid);
    std::lock_guard<std::mutex> lock(shards_[idx].mtx);
    shards_[idx].conns.erase(sid);
}

void IdleManager::Tick()
{
    // 每 5 秒进行一次“大扫除”
    timer_->expires_after(std::chrono::seconds(5));
    timer_->async_wait([this](const boost::system::error_code &ec)
                       {
                           if (ec)
                               return;

                           // 🔥 修复点 1：获取当前单调时钟的微秒数值 (uint64_t)
                           uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                                 std::chrono::steady_clock::now().time_since_epoch())
                                                 .count();

                           // 🔥 修复点 2：将超时阈值也转换为微秒数值 (uint64_t)
                           uint64_t timeout_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                                     timeout_duration_)
                                                     .count();

                           // 逐个分片进行检查，减小单次锁定的时间
                           for (size_t i = 0; i < SHARD_COUNT; ++i)
                           {
                               std::vector<uint32_t> expired_sids;
                               {
                                   std::lock_guard<std::mutex> lock(shards_[i].mtx);
                                   auto it = shards_[i].conns.begin();
                                   while (it != shards_[i].conns.end())
                                   {
                                       // 尝试提升为 shared_ptr
                                       auto conn = it->second.lock();
                                       if (!conn)
                                       {
                                           it = shards_[i].conns.erase(it); // 清理已销毁的连接句柄
                                           continue;
                                       }

                                       // 🔥 修复点 3：现在两边都是 uint64_t，可以直接相减并比较
                                       if (now_us - conn->GetLastActiveTimeUs() > timeout_us)
                                       {
                                           expired_sids.push_back(it->first);
                                           it = shards_[i].conns.erase(it); // 从管理器移除
                                       }
                                       else
                                       {
                                           ++it;
                                       }
                                   }
                               }

                               // 在锁外执行真正的清理逻辑，防止 ProxyService 回调导致死锁
                               for (uint32_t sid : expired_sids)
                               {
                                   LOG_WARN("[Idle] Session {} timed out, force closing.", sid);
                                   ProxyService::Instance().RemoveSession(sid);
                               }
                           }

                           Tick(); // 递归开启下一轮定时器
                       });
}