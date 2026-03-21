#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>

#include "proxy/BackendPool.h"
#include "router/MessageRouter.h"

class GatewayConnection;

class ProxyService
{
public:
    static ProxyService &Instance();

    // 初始化多个后端
    void Init(boost::asio::io_context &io);

    void ForwardToBackend(std::shared_ptr<GatewayConnection> client,
                          uint16_t msgId,
                          const char *data,
                          size_t len);

    void OnBackendReply(uint32_t sid,
                        uint16_t msgId,
                        const char *data,
                        size_t len);

    void RemoveSession(uint32_t sid);

private:
    ProxyService() = default;

    // 🔥 多后端池
    std::unordered_map<ServerType, BackendPool> pools_;

    std::mutex mtx_;
    std::unordered_map<uint32_t, std::weak_ptr<GatewayConnection>> sessions_;
};