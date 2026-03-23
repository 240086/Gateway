#include "router/MessageRouter.h"
#include "common/config/Config.h"

MessageRouter &MessageRouter::Instance()
{
    static MessageRouter instance;
    return instance;
}

void MessageRouter::Init()
{
    // 获取 Config 单例
    auto &config = Config::Instance();

    // 1. 处理 login_range [min, max]
    auto loginNode = config.GetNode("routing.login_range");
    if (loginNode.IsSequence() && loginNode.size() >= 2)
    {
        loginRange_.first = loginNode[0].as<int>();
        loginRange_.second = loginNode[1].as<int>();
    }

    // 2. 处理 game_range [min, max]
    auto gameNode = config.GetNode("routing.game_range");
    if (gameNode.IsSequence() && gameNode.size() >= 2)
    {
        gameRange_.first = gameNode[0].as<int>();
        gameRange_.second = gameNode[1].as<int>();
    }

    // 3. 打印对齐后的路由信息
    LOG_INFO("[Router] Init success: login[{}-{}], game[{}-{}]",
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