#pragma once

#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <vector>
#include <deque>
#include <atomic>

#include "network/buffer/RecvBuffer.h"
#include "network/protocol/InternalPacketParser.h"

class BackendConnection : public std::enable_shared_from_this<BackendConnection>
{
public:
    enum class State
    {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING
    };
    enum class CBState
    {
        CLOSED,
        OPEN,
        HALF_OPEN
    };

public:
    explicit BackendConnection(boost::asio::io_context &io);

    void Connect(const std::string &host, uint16_t port);

    bool IsConnected() const { return state_ == State::CONNECTED; }
    bool IsAvailable() const;
    void Send(std::shared_ptr<std::vector<char>> packet);

private:
    void DoResolve();
    void DoConnect();
    void DoRead();
    void DoWrite();

    void HandleClose();
    void ScheduleReconnect();

    void OnSuccess();
    void OnFailure();

private:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::resolver::results_type endpoints_;
    boost::asio::steady_timer reconnectTimer_;

    // endpoint cache（用于重连）
    std::string host_;
    uint16_t port_;

    // 状态机
    std::atomic<State> state_{State::DISCONNECTED};

    // 收包
    RecvBuffer recvBuffer_;
    InternalPacketParser internalParser_;
    char buffer_[8192];

    // 写队列（关键！）
    size_t maxQueueSize_ = 10000;
    std::deque<std::shared_ptr<std::vector<char>>> writeQueue_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;

    std::atomic<CBState> cbState_{CBState::CLOSED};

    std::atomic<uint32_t> failCount_{0};

    std::chrono::steady_clock::time_point lastFailTime_;

    static constexpr uint32_t FAIL_THRESHOLD = 5;
    static constexpr int OPEN_TIMEOUT_SEC = 5;
};