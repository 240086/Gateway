#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <csignal>

// AnimeCore / 基础设施
#include "common/config/Config.h"
#include "common/logger/Logger.h"
#include "common/metrics/MetricsReporter.h"
#include "network/TcpServer.h"
#include "network/Connection.h"
#include "network/asio/AsioContextPool.h"
#include "network/protocol/ClientPacketParser.h"

// 网关核心
#include "proxy/ProxyService.h"
#include "router/MessageRouter.h"
#include "limit/RateLimiter.h"
#include "session/RequestManager.h"
#include "core/IdleManager.h"

using tcp = boost::asio::ip::tcp;

/**
 * @brief 优雅退出逻辑
 */
void GracefulExit(boost::asio::io_context &io,
                  std::shared_ptr<TcpServer> server)
{
    LOG_INFO("----------------------------------------------");
    LOG_INFO("Shutting down Gateway Server...");

    if (server)
        server->Stop();

    io.stop();

    LOG_INFO("Gateway Server stopped gracefully.");
    LOG_INFO("----------------------------------------------");
}

int main()
{
    Logger::Init();

    try
    {
        auto &config = Config::Instance();
        if (!config.Load("gateway.yaml"))
        {
            LOG_FATAL("Critical: Cannot find gateway.yaml.");
            return 1;
        }

        boost::asio::io_context mainIo;

        // 🔥 线程池（关键）
        int workerThreads = config.GetValue<int>("server.worker_threads", 4);
        AsioContextPool contextPool(workerThreads);

        // --------------------------------------------------------
        // 📊 Metrics
        // --------------------------------------------------------
        MetricsReporter reporter(mainIo);
        reporter.Start();

        // --------------------------------------------------------
        // 🔥 基础组件初始化
        // --------------------------------------------------------

        RateLimiter::Instance().Init(
            config.GetValue<int>("limit.ip_qps", 10000),
            config.GetValue<int>("limit.ip_burst", 10000),
            config.GetValue<int>("limit.sid_qps", 1000),
            config.GetValue<int>("limit.sid_burst", 1000));

        RequestManager::Instance().Init(
            mainIo,
            config.GetValue<int>("timeout.backend_request_ms", 1500));

        IdleManager::Instance().Init(
            mainIo,
            config.GetValue<int>("timeout.client_idle_ms", 60000));

        MessageRouter::Instance().Init();
        ProxyService::Instance().Init(mainIo);

        // --------------------------------------------------------
        // 🔥 Gateway（核心替代）
        // --------------------------------------------------------

        int gPort = config.GetValue<int>("server.listen_port", 10000);

        auto server = std::make_shared<TcpServer>(
            mainIo,
            contextPool,
            gPort,

            // 🔹 Connection Factory
            [](boost::asio::io_context &io)
            {
                return std::make_shared<Connection>(io, Callbacks{}, Options{});
            },

            // 🔥 Gateway逻辑注入（核心）
            [](const std::shared_ptr<Connection> &conn)
            {
                static std::atomic<uint64_t> sidGen{1000};

                uint64_t sid = sidGen++;

                conn->SetSessionId(sid);
                conn->SetConnectionId(sid);

                IdleManager::Instance().Add(sid, conn);

                LOG_INFO("[Gateway] New connection sid={}", sid);

                conn->SetParser(std::make_unique<ClientPacketParser>());

                Callbacks cb;

                // -----------------------------
                // 收包 → 转发到后端
                // -----------------------------
                cb.onPacket = [](const std::shared_ptr<Connection> &conn, std::shared_ptr<IMessage> msg)
                {
                    uint16_t msgId = msg->GetMsgId();
                    const char *data = msg->GetData();
                    size_t len = msg->GetDataLen();

                    ProxyService::Instance().ForwardToBackend(conn, msgId, data, len);
                };

                // -----------------------------
                // 断开连接
                // -----------------------------
                cb.onClosed =
                    [](const std::shared_ptr<Connection> &conn,
                       uint64_t cid,
                       uint64_t sid)
                {
                    ProxyService::Instance().RemoveSession(sid);
                    IdleManager::Instance().Remove(sid);

                    LOG_INFO("[Gateway] Disconnected sid={}", sid);
                };

                // -----------------------------
                // Session清理（超时 / 异步）
                // -----------------------------
                cb.onSessionCleanup =
                    [](uint64_t sid)
                {
                    ProxyService::Instance().RemoveSession(sid);
                };

                conn->SetCallbacks(std::move(cb));
            });

        server->StartAccept();

        // --------------------------------------------------------
        // 🔥 信号处理
        // --------------------------------------------------------
        boost::asio::signal_set signals(mainIo, SIGINT, SIGTERM);

        signals.async_wait(
            [&mainIo, server](const boost::system::error_code &ec, int sig)
            {
                if (!ec)
                {
                    LOG_INFO("Signal {} received.", sig);
                    GracefulExit(mainIo, server);
                }
            });

        // --------------------------------------------------------
        // 🚀 启动信息
        // --------------------------------------------------------
        LOG_INFO("================================================");
        LOG_INFO("AnimeGame Gateway Server [ACTIVE]");
        LOG_INFO("Node Name      : {}", config.GetValue<std::string>("server.name", "Gateway"));
        LOG_INFO("Node ID        : {}", config.GetValue<int>("server.id", 1));
        LOG_INFO("Listen Port    : {}", gPort);
        LOG_INFO("Worker Threads : {}", workerThreads);
        LOG_INFO("================================================");

        // 🔥 启动线程池
        contextPool.Run();

        // 主线程跑 accept + timer
        mainIo.run();
    }
    catch (const std::exception &e)
    {
        LOG_FATAL("Gateway crashed: {}", e.what());
        return 1;
    }

    return 0;
}