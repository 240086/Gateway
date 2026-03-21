#pragma once

#include "gateway/network/buffer/RecvBuffer.h"
#include <functional>
#include <cstdint>

class PacketParser
{
public:
    using PacketCallback = std::function<void(uint16_t, const char *, size_t)>;

    void Parse(RecvBuffer &buffer, PacketCallback callback);

private:
    static constexpr size_t HEADER_SIZE = 6;
    static constexpr uint32_t MAX_PACKET_SIZE = 65536;
};