#include "proxy/BackendConnection.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include "common/metrics/Metrics.h"
#include "network/protocol/InternalPacket.h"
#include "common/thread/GlobalThreadPool.h"
#include <iostream>

using boost::asio::ip::tcp;

BackendConnection::BackendConnection(boost::asio::io_context &io)
    : socket_(io),
      reconnectTimer_(io),
      strand_(boost::asio::make_strand(io)),
      resolver_(io)
{
}

void BackendConnection::Connect(const std::string &host, uint16_t port)
{
    host_ = host;
    port_ = port;
    DoResolve();
}

void BackendConnection::DoResolve()
{
    if (state_ == State::CONNECTING || state_ == State::CONNECTED)
        return;

    state_ = State::CONNECTING;

    auto self = shared_from_this();

    resolver_.async_resolve(host_, std::to_string(port_),
                            boost::asio::bind_executor(strand_,
                                                       [this, self](boost::system::error_code ec, tcp::resolver::results_type endpoints)
                                                       {
                                                           if (!ec)
                                                           {
                                                               endpoints_ = std::move(endpoints); // ✅ 保存
                                                               DoConnect();                       // ✅ 不再传参
                                                           }
                                                           else
                                                           {
                                                               LOG_ERROR("[Backend] Resolve failed: {}", ec.message());
                                                               OnFailure();
                                                               ScheduleReconnect();
                                                           }
                                                       }));
}

// 2. 修改后的 DoConnect，不再包含同步代码
void BackendConnection::DoConnect()
{
    auto self = shared_from_this();

    boost::asio::async_connect(socket_, endpoints_,
                               boost::asio::bind_executor(strand_,
                                                          [this, self](boost::system::error_code ec, const tcp::endpoint &endpoint)
                                                          {
                                                              if (!ec)
                                                              {
                                                                  state_ = State::CONNECTED;
                                                                  LOG_INFO("[Backend] Connected to {}:{}", host_, port_);
                                                                  failCount_ = 0;
                                                                  DoRead();
                                                              }
                                                              else
                                                              {
                                                                  LOG_ERROR("[Backend] Connect failed: {}", ec.message());
                                                                  OnFailure();
                                                                  ScheduleReconnect();
                                                              }
                                                          }));
}

void BackendConnection::Send(std::shared_ptr<std::vector<char>> packet)
{
    if (!IsAvailable())
    {
        LOG_WARN("[Backend] Circuit breaker is OPEN. Dropping packet.");
        return;
    }

    auto self = shared_from_this();

    boost::asio::post(strand_,
                      [this, self, packet]()
                      {
                          if (state_ != State::CONNECTED)
                              return;
                          if (writeQueue_.size() >= maxQueueSize_)
                          {
                              LOG_WARN("[Backend] Write queue full, dropping packet.");
                              return;
                          }

                          bool writing = !writeQueue_.empty();
                          writeQueue_.push_back(packet);

                          if (!writing)
                          {
                              DoWrite();
                          }
                      });
}

void BackendConnection::DoRead()
{
    auto self = shared_from_this();
    // 💡 优化 1：使用类的成员 buffer_ (8192) 替代频繁的 shared_ptr 分配
    // 假设你在头文件定义了 char buffer_[8192];

    socket_.async_read_some(
        boost::asio::buffer(buffer_),
        boost::asio::bind_executor(strand_,
                                   [this, self](boost::system::error_code ec, std::size_t len)
                                   {
                                       if (!ec)
                                       {
                                           // 1. 数据入库
                                           recvBuffer_.Append(buffer_, len);

                                           // 💡 优化 2：预分配大小，减少 vector 扩容频率
                                           static thread_local std::vector<std::shared_ptr<anime::IMessage>> msgList;
                                           msgList.clear();

                                           // 2. 解析
                                           internalParser_.Parse(recvBuffer_, msgList);

                                           // 3. 处理
                                           for (auto &msg : msgList)
                                           {
                                               OnSuccess();
                                               auto internalPkg = std::static_pointer_cast<InternalPacket>(msg);
                                               LOG_DEBUG("[Network-Debug] RECV FROM GS: sid={}, msgId={}, seqId={}",
                                                         internalPkg->GetSessionId(), internalPkg->GetMsgId(), internalPkg->GetSequenceId());

                                               if (msg->GetType() != anime::MessageType::INTERNAL)
                                               {
                                                   LOG_ERROR("[Backend] Unexpected message type: {}", (int)msg->GetType());
                                                   continue;
                                               }

                                            //    auto internalPkg = std::static_pointer_cast<InternalPacket>(msg);

                                               // 🔥 必须拷贝数据（避免buffer复用问题）
                                               auto data = std::make_shared<std::vector<char>>(
                                                   internalPkg->GetData(),
                                                   internalPkg->GetData() + internalPkg->GetDataLen());

                                               auto sessionId = internalPkg->GetSessionId();
                                               auto msgId = internalPkg->GetMsgId();
                                               auto seqId = internalPkg->GetSequenceId();

                                               // 🔥 投递到业务线程池
                                               GlobalThreadPool::Instance().GetPool().Enqueue(
                                                   [sessionId, msgId, seqId, data]()
                                                   {
                                                       ProxyService::Instance().OnBackendReply(
                                                           sessionId,
                                                           msgId,
                                                           seqId,
                                                           data->data(),
                                                           data->size());
                                                   });
                                           }

                                           DoRead();
                                       }
                                       else
                                       {
                                           if (ec != boost::asio::error::operation_aborted)
                                           {
                                               LOG_WARN("[Backend] Connection lost: {}", ec.message());
                                               HandleClose();
                                           }
                                       }
                                   }));
}

void BackendConnection::DoWrite()
{
    auto self = shared_from_this();

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(*writeQueue_.front()),
        boost::asio::bind_executor(strand_,
                                   [this, self](boost::system::error_code ec, std::size_t)
                                   {
                                       if (!ec)
                                       {
                                           writeQueue_.pop_front();

                                           if (!writeQueue_.empty())
                                           {
                                               DoWrite();
                                           }
                                       }
                                       else
                                       {
                                           LOG_ERROR("[Backend] Write failed: {}", ec.message());
                                           OnFailure();
                                           HandleClose();
                                       }
                                   }));
}

void BackendConnection::HandleClose()
{
    auto self = shared_from_this();
    boost::asio::post(strand_,
                      [this, self]()
                      {
                          if (state_ == State::DISCONNECTED)
                              return;

                          state_ = State::DISCONNECTED;

                          boost::system::error_code ignored;
                          socket_.close(ignored);

                          writeQueue_.clear();

                          ScheduleReconnect();
                      });
}

void BackendConnection::ScheduleReconnect()
{
    state_ = State::RECONNECTING;

    auto self = shared_from_this();

    reconnectTimer_.expires_after(std::chrono::seconds(3));

    reconnectTimer_.async_wait(
        [this, self](boost::system::error_code ec)
        {
            if (!ec)
            {
                LOG_INFO("[Backend] Reconnecting...");
                DoConnect();
            }
        });
}

bool BackendConnection::IsAvailable() const
{
    auto state = cbState_.load(std::memory_order_relaxed);

    if (state == CBState::CLOSED)
        return state_ == State::CONNECTED;

    if (state == CBState::OPEN)
    {
        auto now = std::chrono::steady_clock::now();
        if (now - lastFailTime_ > std::chrono::seconds(OPEN_TIMEOUT_SEC))
        {
            // 进入 HALF_OPEN
            const_cast<BackendConnection *>(this)->cbState_ = CBState::HALF_OPEN;
            return state_ == State::CONNECTED;
        }
        return false;
    }

    // HALF_OPEN
    return state_ == State::CONNECTED;
}

void BackendConnection::OnSuccess()
{
    Metrics::Instance().Inc(MetricId::CircuitBreakerClose);
    if (cbState_.load() != CBState::CLOSED)
    {
        LOG_INFO("[CircuitBreaker] CLOSED (Recovered)");
        cbState_.store(CBState::CLOSED, std::memory_order_relaxed);
    }
    failCount_.store(0, std::memory_order_relaxed);
}

void BackendConnection::OnFailure()
{
    uint32_t fails = failCount_.fetch_add(1) + 1;
    lastFailTime_ = std::chrono::steady_clock::now();
    Metrics::Instance().Inc(MetricId::BackendFail);

    if (fails >= FAIL_THRESHOLD)
    {
        if (cbState_.exchange(CBState::OPEN) != CBState::OPEN)
        {
            LOG_WARN("[CircuitBreaker] OPEN triggered (Threshold reached)");
        }
    }
}