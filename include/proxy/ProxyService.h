#pragma once
#include <memory>
#include <unordered_map>
#include <mutex>
#include "proxy/BackendPool.h"

class GatewayConnection;

class ProxyService
{
public:
    static ProxyService &Instance();

    // 初始化连接池
    void Init(boost::asio::io_context &io, const std::string &host, uint16_t port);

    void ForwardToBackend(std::shared_ptr<GatewayConnection> client, uint16_t msgId, const char *data, size_t len);

    void OnBackendReply(uint32_t sid, uint16_t msgId, const char *data, size_t len);

    void RemoveSession(uint32_t sid);

private:
    ProxyService() = default;
    BackendPool pool_;

    std::mutex mtx_;
    std::unordered_map<uint32_t, std::weak_ptr<GatewayConnection>> sessions_;
};