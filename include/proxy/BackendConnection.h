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

public:
    explicit BackendConnection(boost::asio::io_context &io);

    void Connect(const std::string &host, uint16_t port);

    bool IsConnected() const { return state_ == State::CONNECTED; }

    void Send(std::shared_ptr<std::vector<char>> packet);

private:
    void DoConnect();
    void DoRead();
    void DoWrite();

    void HandleClose();
    void ScheduleReconnect();

private:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer reconnectTimer_;

    // endpoint cache（用于重连）
    std::string host_;
    uint16_t port_;

    // 状态机
    std::atomic<State> state_{State::DISCONNECTED};

    // 收包
    RecvBuffer recvBuffer_;
    InternalPacketParser internalParser_;

    // 写队列（关键！）
    std::deque<std::shared_ptr<std::vector<char>>> writeQueue_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
};