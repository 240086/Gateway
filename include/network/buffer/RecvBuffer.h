#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

class RecvBuffer
{
public:

    void Append(const char* data, size_t len);

    size_t Size() const;

    const char* Data() const;

    void Consume(size_t len);

private:

    std::vector<char> buffer_;
};