#pragma once

#include <boost/asio.hpp>
#include <array>
#include <memory>
#include <atomic>
#include <chrono>
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

    inline uint32_t GetSessionId() const { return sessionId_; }

    inline uint32_t NextSeqId()
    {
        return seqId_.fetch_add(1, std::memory_order_relaxed);
    }

    inline uint32_t GetRemoteIP() const
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

    void UpdateActivity()
    {
        lastActiveTime_.store(NowUs(), std::memory_order_relaxed);
    }

    uint64_t GetLastActiveTime() const
    {
        return lastActiveTime_.load(std::memory_order_relaxed);
    }

    void Close();
    void HandleDisconnect();

private:
    void DoRead();

    inline static uint64_t NowUs()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

private:
    boost::asio::ip::tcp::socket socket_;
    std::array<char, 1024> buffer_;
    uint32_t sessionId_;

    RecvBuffer recvBuffer_;
    PacketParser parser_;

    std::shared_ptr<BackendConnection> backend_;
    std::atomic<uint32_t> seqId_{0};

    std::atomic<uint64_t> lastActiveTime_{0};
};