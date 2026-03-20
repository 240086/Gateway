cmake_minimum_required(VERSION 3.16)
project(Gateway)

set(CMAKE_CXX_STANDARD 17)

# 1. 查找源文件
file(GLOB_RECURSE SRC "src/*.cpp")

# 2. 【关键】先创建可执行目标，目标名统一使用小写 gateway
add_executable(gateway ${SRC})

# 3. 设置包含目录 (针对刚刚创建的目标 gateway)
target_include_directories(gateway PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

# 4. Windows 平台特殊配置
if(WIN32)
    # 宏定义
    target_compile_definitions(gateway PRIVATE _WIN32_WINNT=0x0A00)
    
    # 链接网络库
    target_link_libraries(gateway PRIVATE ws2_32 mswsock)

    # 显式指定控制台模式 (MinGW 常用 -mconsole)
    target_link_options(gateway PRIVATE -mconsole)
endif()