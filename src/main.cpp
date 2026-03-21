#include <boost/asio.hpp>
#include <iostream>
#include <memory>

#include "core/GatewayServer.h"
#include "proxy/ProxyService.h"
#include "router/MessageRouter.h" // 假设你已定义路由组件
#include "common/config/Config.h"
#include "common/logger/Logger.h"
#include "common/metrics/MetricsReporter.h"

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
    // 1. 基础 Logger 初始化（最先启动，确保后续所有步骤可查）
    Logger::Init();

    try
    {
        // 2. 加载配置（唯一真源）
        if (!Config::Instance().Load("gateway.yaml"))
        {
            LOG_FATAL("Critical: Cannot find gateway.yaml. Gateway terminated.");
            return 1;
        }

        auto &config = Config::Instance();
        boost::asio::io_context io;

        MetricsReporter reporter(io);
        reporter.Start();

        // 3. 初始化路由大脑 (从 YAML 读取 login_range, game_range)
        MessageRouter::Instance().Init();

        // 4. 初始化 Proxy 模块 (内部自动从 Config 读取后端列表并建立连接池)
        ProxyService::Instance().Init(io);

        // 5. 实例化并准备启动服务
        int gPort = config.GetGatewayPort();
        GatewayServer server(io, gPort);
        server.Start();

        // 6. 注册信号处理实现“优雅退出”
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io, &server](const boost::system::error_code &ec, int signal_number)
                           {
            if (!ec) {
                LOG_INFO("Signal {} received.", signal_number);
                GracefulExit(io, server);
            } });

        // 7. 打印规范的启动横幅 (Banner)
        LOG_INFO("================================================");
        LOG_INFO("AnimeGame Gateway Server [Active]");
        LOG_INFO("Listen Port    : {}", gPort);
        LOG_INFO("Worker Threads : {}", config.GetWorkerThreads());
        LOG_INFO("Config Status  : Loaded Successfully");
        LOG_INFO("Press Ctrl+C to shutdown safely.");
        LOG_INFO("================================================");

        // 8. 开启 IO 循环（阻塞直到 io.stop() 被调用）
        io.run();
    }
    catch (const std::exception &e)
    {
        LOG_FATAL("Gateway Service crashed: {}", e.what());
        return 1;
    }

    return 0;
}