#include "router/MessageRouter.h"
#include "common/config/Config.h"

MessageRouter &MessageRouter::Instance()
{
    static MessageRouter instance;
    return instance;
}

void MessageRouter::Init()
{
    auto &config = Config::Instance();

    loginRange_ = config.GetLoginRange();
    gameRange_ = config.GetGameRange();

    LOG_INFO("[Router] Init: login[{}-{}], game[{}-{}]",
             loginRange_.first, loginRange_.second,
             gameRange_.first, gameRange_.second);
}

ServerType MessageRouter::Route(uint16_t msgId) const
{
    if (msgId >= loginRange_.first && msgId <= loginRange_.second)
        return ServerType::LOGIN;

    if (msgId >= gameRange_.first && msgId <= gameRange_.second)
        return ServerType::GAME;

    LOG_WARN("[Router] Unknown msgId: {}", msgId);
    return ServerType::UNKNOWN;
}