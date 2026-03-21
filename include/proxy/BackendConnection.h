#pragma once
#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include "network/buffer/RecvBuffer.h"
#include "network/protocol/PacketParser.h"

class GatewayConnection;

class BackendConnection : public std::enable_shared_from_this<BackendConnection>
{
public:
    BackendConnection(boost::asio::io_context &io);

    void Connect(const std::string &host, uint16_t port);

    bool IsConnected() const { return connected_; }

    void Send(std::shared_ptr<std::vector<char>> packet);

    void OnReadPacket(uint32_t sid, uint16_t msgId, const char *data, size_t len);

private:
    void DoRead();

private:
    boost::asio::ip::tcp::socket socket_;

    RecvBuffer recvBuffer_;
    PacketParser parser_;

    // 🔥 关键：当前绑定的客户端（先用简单版）
    std::weak_ptr<GatewayConnection> client_;
    std::atomic<bool> connected_;
};