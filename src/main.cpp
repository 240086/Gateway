#include "gateway/core/GatewayServer.h"
#include "gateway/proxy/ProxyService.h"
#include <boost/asio.hpp>
#include <iostream>

int main()
{
    try
    {
        boost::asio::io_context io;

        // --- 1. 初始化代理服务 (内部会创建 BackendPool) ---
        // 假设后端 GameServer 运行在 9000 端口
        // 我们通过 ProxyService 初始化 4 条连接到后端的 9000 端口
        ProxyService::Instance().Init(io, "127.0.0.1", 9000);

        // --- 2. 启动网关服务器 (对外监听端口) ---
        // 客户端连接网关的 10000 端口，避免与后端的 9000 冲突
        uint16_t gateway_port = 10000;
        GatewayServer server(io, gateway_port);
        server.Start();

        std::cout << "-----------------------------------------------" << std::endl;
        std::cout << "[Gateway] Status: RUNNING" << std::endl;
        std::cout << "[Gateway] Listening on port: " << gateway_port << std::endl;
        std::cout << "[Gateway] Proxying to Backend: 127.0.0.1:9000" << std::endl;
        std::cout << "-----------------------------------------------" << std::endl;

        // --- 3. 运行 IO 事件循环 ---
        io.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "[Gateway] Critical Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}