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

    // 1) login_range：优先 routing.*，再尝试 router.*，最后使用默认值
    int loginMin = config.GetValue<int>("routing.login_range.0",
                                        config.GetValue<int>("router.login_range.0", 100));
    int loginMax = config.GetValue<int>("routing.login_range.1",
                                        config.GetValue<int>("router.login_range.1", 999));

    if (loginMin > loginMax)
    {
        LOG_WARN("[Router] login_range invalid ({}>{}), using defaults [100, 999]", loginMin, loginMax);
        loginMin = 100;
        loginMax = 999;
    }
    loginRange_ = {loginMin, loginMax};

    // 2) game_range：优先 routing.*，再尝试 router.*，最后使用默认值
    int gameMin = config.GetValue<int>("routing.game_range.0",
                                       config.GetValue<int>("router.game_range.0", 1000));
    int gameMax = config.GetValue<int>("routing.game_range.1",
                                       config.GetValue<int>("router.game_range.1", 4999));

    if (gameMin > gameMax)
    {
        LOG_WARN("[Router] game_range invalid ({}>{}), using defaults [1000, 4999]", gameMin, gameMax);
        gameMin = 1000;
        gameMax = 4999;
    }
    gameRange_ = {gameMin, gameMax};

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