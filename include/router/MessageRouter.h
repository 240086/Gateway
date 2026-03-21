#pragma once
#include <cstdint>
#include "common/logger/Logger.h"
#include "router/Router.h"

class MessageRouter
{
public:
    static MessageRouter &Instance();

    void Init();

    ServerType Route(uint16_t msgId) const;

private:
    MessageRouter() = default;

    std::pair<int, int> loginRange_;
    std::pair<int, int> gameRange_;
};