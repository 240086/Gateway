#include "proxy/BackendConnection.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include "common/metrics/Metrics.h"
#include "network/protocol/InternalPacket.h"
#include <iostream>

using boost::asio::ip::tcp;

BackendConnection::BackendConnection(boost::asio::io_context &io)
    : socket_(io),
      reconnectTimer_(io),
      strand_(boost::asio::make_strand(io))
{
}

void BackendConnection::Connect(const std::string &host, uint16_t port)
{
    host_ = host;
    port_ = port;

    DoConnect();
}

void BackendConnection::DoConnect()
{
    if (state_ == State::CONNECTING || state_ == State::CONNECTED)
        return;

    state_ = State::CONNECTING;

    auto self = shared_from_this();

    boost::asio::ip::tcp::resolver resolver(socket_.get_executor());
    auto endpoints = resolver.resolve(host_, std::to_string(port_));
    boost::asio::async_connect(socket_, endpoints,
                               boost::asio::bind_executor(strand_,
                                                          [this, self](boost::system::error_code ec, auto)
                                                          {
                                                              if (!ec)
                                                              {
                                                                  state_ = State::CONNECTED;
                                                                  LOG_INFO("[Backend] Connected to {}:{}", host_, port_);

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
    auto buffer = std::make_shared<std::array<char, 4096>>();

    socket_.async_read_some(
        boost::asio::buffer(*buffer),
        boost::asio::bind_executor(strand_,
                                   [this, self, buffer](boost::system::error_code ec, std::size_t len)
                                   {
                                       if (!ec)
                                       {
                                           // 1. 将原始数据存入 Buffer
                                           recvBuffer_.Append(buffer->data(), len);

                                           // 2. 准备一个临时容器接收解析出来的消息对象
                                           std::vector<std::shared_ptr<anime::IMessage>> messages;

                                           // 3. ✅ 调用新接口
                                           internalParser_.Parse(recvBuffer_, messages);

                                           // 4. 遍历解析出的消息并处理
                                           for (auto &msg : messages)
                                           {
                                               // 熔断器计数
                                               OnSuccess();

                                               // 因为是内网包，我们知道它一定是 InternalPacket
                                               auto internalPkg = std::static_pointer_cast<InternalPacket>(msg);

                                               // 触发 ProxyService 的回包逻辑
                                               ProxyService::Instance().OnBackendReply(
                                                   internalPkg->GetSessionId(),
                                                   internalPkg->GetMsgId(),
                                                   internalPkg->GetSequenceId(),
                                                   internalPkg->GetData(),
                                                   internalPkg->GetDataLen());
                                           }

                                           // 继续下一轮读
                                           DoRead();
                                       }
                                       else
                                       {
                                           LOG_WARN("[Backend] Connection lost: {}", ec.message());
                                           HandleClose();
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
    Metrics::Instance().Inc(MetricId::CircuitBreakerOpen);
    failCount_.store(0, std::memory_order_relaxed);
    cbState_ = CBState::CLOSED;
}

void BackendConnection::OnFailure()
{
    uint32_t fails = failCount_.fetch_add(1) + 1;
    lastFailTime_ = std::chrono::steady_clock::now();
    Metrics::Instance().Inc(MetricId::BackendFail);

    if (fails >= FAIL_THRESHOLD)
    {
        cbState_ = CBState::OPEN;
        LOG_WARN("[CircuitBreaker] OPEN triggered");
    }
}