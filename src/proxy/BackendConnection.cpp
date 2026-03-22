#include "proxy/BackendConnection.h"
#include "proxy/ProxyService.h"
#include "common/logger/Logger.h"
#include "common/metrics/Metrics.h"
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
                                                                  ScheduleReconnect();
                                                              }
                                                          }));
}

void BackendConnection::Send(std::shared_ptr<std::vector<char>> packet)
{
    auto self = shared_from_this();

    boost::asio::post(strand_,
                      [this, self, packet]()
                      {
                          if (state_ != State::CONNECTED)
                              return;
                          Metrics::Instance().Inc("backend.send_bytes", packet->size());
                          Metrics::Instance().Inc("backend.send_packets");
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
                                           recvBuffer_.Append(buffer->data(), len);
                                           Metrics::Instance().Inc("backend.recv_bytes", len);
                                           internalParser_.Parse(
                                               recvBuffer_,
                                               [this](uint32_t sid, uint16_t msgId, uint32_t seqId, const char *data, size_t len)
                                               {
                                                   ProxyService::Instance().OnBackendReply(sid, msgId, seqId, data, len);
                                               });

                                           DoRead();
                                       }
                                       else
                                       {
                                           LOG_WARN("[Backend] Connection lost: {}", ec.message());
                                           HandleClose();
                                           // TODO: 后续我们会加自动重连
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
                                           HandleClose();
                                       }
                                   }));
}

void BackendConnection::HandleClose()
{
    auto self = shared_from_this();
    Metrics::Instance().Inc("backend.disconnect");
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
