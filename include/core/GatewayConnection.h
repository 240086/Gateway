#pragma once

#include <boost/asio.hpp>
#include <array>
#include <memory>
#include "network/buffer/RecvBuffer.h"
#include "network/protocol/PacketParser.h"
#include "proxy/BackendConnection.h"

class GatewayConnection : public std::enable_shared_from_this<GatewayConnection>
{
public:
    explicit GatewayConnection(boost::asio::ip::tcp::socket socket);

    void Start();

    void SendRaw(uint16_t msgId, const char *data, size_t len);

    uint32_t GetSessionId() const { return sessionId_; }

private:
    void DoRead();

private:
    boost::asio::ip::tcp::socket socket_;
    std::array<char, 1024> buffer_;
    uint32_t sessionId_;

    RecvBuffer recvBuffer_;
    PacketParser parser_;

    std::shared_ptr<BackendConnection> backend_;
};