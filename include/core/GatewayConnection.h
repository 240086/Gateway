#pragma once

#include <boost/asio.hpp>
#include <array>
#include <memory>
#include "network/buffer/RecvBuffer.h"
#include "network/protocol/PacketParser.h"
#include "proxy/BackendConnection.h"
#include "common/metrics/Metrics.h"

class GatewayConnection : public std::enable_shared_from_this<GatewayConnection>
{
public:
    explicit GatewayConnection(boost::asio::ip::tcp::socket socket);

    void Start();

    void SendRaw(uint16_t msgId, const char *data, size_t len);

    uint32_t GetSessionId() const { return sessionId_; }

    uint32_t NextSeqId()
    {
        return seqId_.fetch_add(1, std::memory_order_relaxed);
    }

    uint32_t GetRemoteIP() const
    {
        try
        {
            // 将 IPv4 地址转换为网络字节序的 uint32
            return socket_.remote_endpoint().address().to_v4().to_uint();
        }
        catch (...)
        {
            return 0;
        }
    }

private:
    void DoRead();

private:
    boost::asio::ip::tcp::socket socket_;
    std::array<char, 1024> buffer_;
    uint32_t sessionId_;

    RecvBuffer recvBuffer_;
    PacketParser parser_;

    std::shared_ptr<BackendConnection> backend_;
    std::atomic<uint32_t> seqId_{0};
};