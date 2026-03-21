#include "network/protocol/PacketParser.h"
#include <cstring>
#include <boost/asio/detail/socket_ops.hpp>
#include <iostream>

namespace socket_ops = boost::asio::detail::socket_ops;

void PacketParser::Parse(RecvBuffer &buffer, PacketCallback callback)
{
    while (true)
    {
        // 1️⃣ 包头是否完整
        if (buffer.Size() < HEADER_SIZE)
            return;

        const char *data = buffer.Data();

        uint32_t length;
        uint16_t msgId;

        std::memcpy(&length, data, 4);
        std::memcpy(&msgId, data + 4, 2);

        // ✅ 必须转换字节序
        length = socket_ops::network_to_host_long(length);
        msgId = socket_ops::network_to_host_short(msgId);

        // 2️⃣ 安全校验
        if (length > MAX_PACKET_SIZE)
        {
            std::cout << "[Gateway] Invalid packet length: " << length << std::endl;
            return;
        }

        // 3️⃣ 是否完整
        if (buffer.Size() < HEADER_SIZE + length)
            return;

        const char* payload = data + HEADER_SIZE;

        // 4️⃣ 回调
        if (callback)
        {
            callback(msgId, payload, length);
        }

        // 5️⃣ 消费
        buffer.Consume(HEADER_SIZE + length);
    }
}