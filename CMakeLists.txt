cmake_minimum_required(VERSION 3.16)
project(AnimeGateway)

# --- 模式保底逻辑 ---
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    # 设置可选范围（供 GUI 使用）
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# 打印当前模式，让你在执行 cmake .. 时一眼看到结果
message(STATUS "------------------------------------------------")
message(STATUS "  Build Configuration: ${CMAKE_BUILD_TYPE}")
message(STATUS "------------------------------------------------")

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
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway.yaml"
    "${CMAKE_CURRENT_BINARY_DIR}/gateway.yaml"
    COPYONLY
)

# --- 5. Windows 平台兼容 ---
if(WIN32)
    target_link_libraries(gateway PRIVATE ws2_32 mswsock)
    target_compile_definitions(gateway PRIVATE _WIN32_WINNT=0x0A00)
endif()