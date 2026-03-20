#pragma once

#include <vector>
#include <cstdint>
#include <string>

struct PacketHeader
{
    uint32_t length;
    uint16_t messageId;
};

class Packet
{
public:
    Packet();

    void SetMessageId(uint16_t id);

    uint16_t GetMessageId() const;

    void Append(const char *data, size_t len);

    void Append(const std::string &s);

    const std::vector<char> &GetBuffer() const;

    std::vector<char> Serialize() const;

private:
    PacketHeader header_;

    std::vector<char> buffer_;
};