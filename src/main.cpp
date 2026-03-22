#include <boost/asio.hpp>
#include <iostream>
#include <memory>

#include "core/GatewayServer.h"
#include "proxy/ProxyService.h"
#include "router/MessageRouter.h"
#include "common/config/Config.h"
#include "common/logger/Logger.h"
#include "common/metrics/MetricsReporter.h"
#include "limit/RateLimiter.h"
#include "session/RequestManager.h"

/**
 * @brief 优雅退出逻辑：停止 IO 循环并清理资源
 */
void GracefulExit(boost::asio::io_context &io, GatewayServer &server)
{
    LOG_INFO("----------------------------------------------");
    LOG_INFO("Shutting down Gateway Server...");

    // 1. 停止接收新连接
    server.Stop();

    // 2. 停止 IO 循环（这会让 io.run() 返回）
    io.stop();

    LOG_INFO("Gateway Server stopped gracefully.");
    LOG_INFO("----------------------------------------------");
}

int main()
{
    Logger::Init();

    try
    {
        if (!Config::Instance().Load("gateway.yaml"))
        {
            LOG_FATAL("Critical: Cannot find gateway.yaml. Gateway terminated.");
            return 1;
        }

        auto &config = Config::Instance();
        boost::asio::io_context io;

        MetricsReporter reporter(io);
        reporter.Start();

        // --------------------------------------------------------
        // 🔥 基础设施初始化 (Infrastructure Init)
        // --------------------------------------------------------

        RateLimiter::Instance().Init(
            config.GetIpQps(),
            config.GetIpBurst(),
            config.GetSidQps(),
            config.GetSidBurst());

        RequestManager::Instance().Init(io, config.GetBackendRequestTimeout());

        // --------------------------------------------------------
        // 🎮 业务组件初始化 (Business Logic Init)
        // --------------------------------------------------------

        MessageRouter::Instance().Init();
        ProxyService::Instance().Init(io);

        int gPort = config.GetGatewayPort();
        GatewayServer server(io, gPort);
        server.Start();

        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io, &server](const boost::system::error_code &ec, int signal_number)
                           {
            if (!ec) {
                LOG_INFO("Signal {} received.", signal_number);
                GracefulExit(io, server);
            } });

        LOG_INFO("================================================");
        LOG_INFO("AnimeGame Gateway Server [Active]");
        LOG_INFO("Listen Port    : {}", gPort);
        LOG_INFO("Worker Threads : {}", config.GetWorkerThreads());
        LOG_INFO("Config Status  : Loaded Successfully");
        LOG_INFO("================================================");

        io.run();
    }
    catch (const std::exception &e)
    {
        LOG_FATAL("Gateway Service crashed: {}", e.what());
        return 1;
    }

    return 0;
}