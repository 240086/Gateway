cmake_minimum_required(VERSION 3.16)
project(Gateway)

set(CMAKE_CXX_STANDARD 17)

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

include_directories(
    include
)

file(GLOB_RECURSE SRC
    src/*.cpp
)

add_executable(gateway ${SRC})

if(WIN32)
    # 链接必要的网络库
    target_link_libraries(gateway PRIVATE ws2_32 mswsock)

    # 确保使用 console 子系统
    # 实际上，默认情况下就是 console，你可以尝试先注释掉 target_link_options 看看
    target_link_options(gateway PRIVATE -mconsole)
endif()