cmake_minimum_required(VERSION 3.16)
project(Gateway)

set(CMAKE_CXX_STANDARD 17)

# --- 1. 定义子模块路径 ---
set(ANIME_CORE_DIR ${CMAKE_SOURCE_DIR}/third_party/AnimeCore)

# --- 2. 查找源文件 (包含网关自身和子模块的源码) ---
# 扫描网关自身的源码
file(GLOB_RECURSE GATEWAY_SRC "src/*.cpp")

# 【核心修改】扫描子模块中的源码 (假设源码在 src 或根目录下)
# 如果子模块只有头文件，这一行可以删掉
file(GLOB_RECURSE CORE_SRC "${ANIME_CORE_DIR}/src/*.cpp" "${ANIME_CORE_DIR}/*.cpp")

# 合并所有源文件
set(ALL_SRCS ${GATEWAY_SRC} ${CORE_SRC})

# --- 3. 创建可执行目标 ---
add_executable(gateway ${ALL_SRCS})

# --- 4. 设置包含目录 ---
target_include_directories(gateway PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    # 【核心修改】直接包含子模块的路径，确保 #include 能找到文件
    ${ANIME_CORE_DIR}/include
    ${ANIME_CORE_DIR}
)

# --- 5. Windows 平台特殊配置 ---
if(WIN32)
    target_compile_definitions(gateway PRIVATE _WIN32_WINNT=0x0A00)
    
    # 链接系统网络库 (ws2_32 是 Boost.Asio 必须的)
    target_link_libraries(gateway PRIVATE ws2_32 mswsock)

    target_link_options(gateway PRIVATE -mconsole)
endif()

# --- 6. 查找并链接外部依赖 (如 Boost) ---
find_package(Boost REQUIRED COMPONENTS system)
target_link_libraries(gateway PRIVATE Boost::system)