#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>
#include <boost/asio.hpp>

#include "proxy/BackendPool.h"
#include "router/MessageRouter.h"

// 前置声明，避免头文件循环依赖
class GatewayConnection;

class ProxyService
{
public:
    static ProxyService &Instance();

    // 初始化多个后端连接池
    void Init(boost::asio::io_context &io);

    // 接收来自客户端的数据，转发给对应的后端
    void ForwardToBackend(std::shared_ptr<GatewayConnection> client,
                          uint16_t msgId,
                          const char *data,
                          size_t len);

    // 接收来自后端的响应，回发给对应的客户端
    void OnBackendReply(uint32_t sid,
                        uint16_t msgId,
                        uint32_t seqId, // 🔥 统一命名为 seqId
                        const char *data,
                        size_t len);

    // 玩家断开连接时的全链路清理
    void RemoveSession(uint32_t sid);

private:
    ProxyService() = default;

private:
    // 🔥 多后端连接池 (LOGIN, GAME 等)
    std::unordered_map<ServerType, BackendPool> pools_;

    // 会话映射表保护锁
    std::mutex mtx_;

    // 🔥 必须使用 weak_ptr！防止 GatewayConnection 的循环引用导致内存泄漏
    std::unordered_map<uint32_t, std::weak_ptr<GatewayConnection>> sessions_;
};