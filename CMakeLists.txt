cmake_minimum_required(VERSION 3.16)
project(AnimeGateway)

set(CMAKE_CXX_STANDARD 17)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
find_package(yaml-cpp REQUIRED)

# --- 1. 寻找或包含子模块 ---
# 假设 AnimeCore 文件夹在 Gateway 同级或三方目录下
add_subdirectory(${CMAKE_SOURCE_DIR}/../AnimeCore ${CMAKE_BINARY_DIR}/AnimeCore)

# --- 2. 网关自身源码 ---
file(GLOB_RECURSE GATEWAY_SRC
    "src/core/*.cpp"
    "src/proxy/*.cpp"
    "src/config/*.cpp"
    "src/router/*.cpp"
    "src/session/*.cpp"
    "src/limit/*.cpp"
    "src/main.cpp"
)

add_executable(gateway ${GATEWAY_SRC})

# --- 3. 包含路径 ---
target_include_directories(gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- 4. 关键：链接 AnimeCore ---
# 这一步会自动带入 AnimeCore 的 include 目录和它依赖的 Boost/spdlog
target_link_libraries(gateway PRIVATE AnimeCore)

# 自动拷贝配置文件到 build 目录
configure_file(
    "${CMAKE_SOURCE_DIR}/gateway.yaml"
    "${CMAKE_BINARY_DIR}/gateway.yaml"
    COPYONLY
)

# --- 5. Windows 平台兼容 ---
if(WIN32)
    target_link_libraries(gateway PRIVATE ws2_32 mswsock)
    target_compile_definitions(gateway PRIVATE _WIN32_WINNT=0x0A00)
endif()