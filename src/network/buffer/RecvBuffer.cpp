#include "network/buffer/RecvBuffer.h"

void RecvBuffer::Append(const char* data, size_t len)
{
    buffer_.insert(buffer_.end(), data, data + len);
}

size_t RecvBuffer::Size() const
{
    return buffer_.size();
}

const char* RecvBuffer::Data() const
{
    return buffer_.data();
}

void RecvBuffer::Consume(size_t len)
{
    buffer_.erase(buffer_.begin(), buffer_.begin() + len);
}