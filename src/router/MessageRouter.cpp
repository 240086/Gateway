#include "router/MessageRouter.h"
#include "common/config/Config.h"
#include "common/logger/Logger.h" // 确保包含日志头文件

MessageRouter &MessageRouter::Instance()
{
    static MessageRouter instance;
    return instance;
}

void MessageRouter::Init()
{
    const auto &config = Config::Instance();

    // 1. 获取 login_range
    // 注意：GetValue 内部会调用 FindNode("routing.login_range")
    YAML::Node loginNode = config.GetNode("routing.login_range");

    if (loginNode && loginNode.IsSequence() && loginNode.size() >= 2)
    {
        loginRange_ = {loginNode[0].as<int>(), loginNode[1].as<int>()};
    }
    else
    {
        LOG_ERROR("[Router] Invalid login_range config, using defaults [100, 999]");
        loginRange_ = {100, 999};
    }

    // 2. 获取 game_range
    auto gameNode = config.GetNode("routing.game_range");
    if (gameNode && gameNode.IsSequence() && gameNode.size() >= 2)
    {
        gameRange_ = {gameNode[0].as<int>(), gameNode[1].as<int>()};
    }
    else
    {
        LOG_ERROR("[Router] Invalid game_range config, using defaults [1000, 4999]");
        gameRange_ = {1000, 4999};
    }

    LOG_INFO("[Router] Init success: login[{}-{}], game[{}-{}]",
             loginRange_.first, loginRange_.second,
             gameRange_.first, gameRange_.second);
}

ServerType MessageRouter::Route(uint16_t msgId) const
{
    // 优先级判断：由于你的 MSG_S2C_LOGIN_RESP 是 1100，
    // 如果 loginRange 包含了 1100，它会先返回 LOGIN。

    if (msgId >= (uint16_t)loginRange_.first && msgId <= (uint16_t)loginRange_.second)
        return ServerType::LOGIN;

    if (msgId >= (uint16_t)gameRange_.first && msgId <= (uint16_t)gameRange_.second)
        return ServerType::GAME;

    LOG_WARN("[Router] Unknown msgId: {}, out of all ranges", msgId);
    return ServerType::UNKNOWN;
}