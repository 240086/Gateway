#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

enum class ServerType
{
    LOGIN = 1,
    GAME = 2,
    CHAT = 3,
    UNKNOWN = 999
};

// 专门处理字符串到枚举的安全转换
inline ServerType StringToServerType(const std::string &type)
{
    static const std::unordered_map<std::string, ServerType> typeMap = {
        {"LOGIN", ServerType::LOGIN},
        {"GAME", ServerType::GAME},
        {"CHAT", ServerType::CHAT}};

    auto it = typeMap.find(type);
    if (it != typeMap.end())
    {
        return it->second;
    }

    return ServerType::UNKNOWN;
}