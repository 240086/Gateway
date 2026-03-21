
```
Gateway
├─ CMakeLists.txt
├─ gateway.yaml
├─ include
│  ├─ core
│  │  ├─ GatewayConnection.h
│  │  └─ GatewayServer.h
│  ├─ proxy
│  │  ├─ BackendConnection.h
│  │  ├─ BackendPool.h
│  │  └─ ProxyService.h
│  ├─ router
│  │  ├─ MessageRouter.h
│  │  └─ Router.h
│  └─ session
├─ logs
│  └─ server_log_2026-03-21.txt
├─ src
│  ├─ core
│  │  ├─ GatewayConnection.cpp
│  │  └─ GatewayServer.cpp
│  ├─ main.cpp
│  ├─ proxy
│  │  ├─ BackendConnection.cpp
│  │  ├─ BackendPool.cpp
│  │  └─ ProxyService.cpp
│  ├─ router
│  │  └─ MessageRouter.cpp
│  └─ session
├─ Structure.md
└─ third_party
   └─ AnimeCore
      ├─ CMakeLists.txt
      ├─ include
      │  ├─ common
      │  │  ├─ config
      │  │  │  └─ Config.h
      │  │  ├─ ErrorCode.h
      │  │  ├─ logger
      │  │  │  └─ Logger.h
      │  │  ├─ metrics
      │  │  │  ├─ Metrics.h
      │  │  │  └─ MetricsReporter.h
      │  │  └─ thread
      │  │     ├─ GlobalThreadPool.h
      │  │     └─ ThreadPool.h
      │  └─ network
      │     ├─ asio
      │     │  └─ AsioContextPool.h
      │     ├─ buffer
      │     │  └─ RecvBuffer.h
      │     ├─ Connection.h
      │     ├─ protocol
      │     │  ├─ IMessage.h
      │     │  ├─ InternalPacket.h
      │     │  ├─ InternalPacketParser.h
      │     │  ├─ MessageId.h
      │     │  ├─ Packet.h
      │     │  ├─ PacketParser.h
      │     │  └─ ProtoMessage.h
      │     └─ TcpServer.h
      ├─ src
      │  ├─ common
      │  │  ├─ config
      │  │  │  └─ Config.cpp
      │  │  ├─ logger
      │  │  │  └─ Logger.cpp
      │  │  ├─ metrics
      │  │  │  ├─ Metrics.cpp
      │  │  │  └─ MetricsReporter.cpp
      │  │  └─ thread
      │  │     ├─ GlobalThreadPool.cpp
      │  │     └─ ThreadPool.cpp
      │  └─ network
      │     ├─ asio
      │     │  └─ AsioContextPool.cpp
      │     ├─ buffer
      │     │  └─ RecvBuffer.cpp
      │     ├─ Connection.cpp
      │     ├─ protocol
      │     │  ├─ InternalPacket.cpp
      │     │  ├─ InternalPacketParser.cpp
      │     │  ├─ Packet.cpp
      │     │  └─ PacketParser.cpp
      │     └─ TcpServer.cpp
      └─ Structure.md

```