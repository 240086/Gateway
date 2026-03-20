
```
Gateway
├─ CMakeLists.txt
├─ include
│  └─ gateway
│     ├─ config
│     │  └─ gateway.yaml
│     ├─ core
│     │  ├─ GatewayConnection.h
│     │  └─ GatewayServer.h
│     ├─ network
│     │  ├─ buffer
│     │  │  └─ RecvBuffer.h
│     │  └─ protocol
│     │     ├─ Packet.h
│     │     └─ PacketParser.h
│     ├─ proxy
│     │  ├─ BackendConnection.h
│     │  ├─ BackendPool.h
│     │  └─ ProxyService.h
│     ├─ router
│     └─ session
├─ src
│  ├─ config
│  ├─ core
│  │  ├─ GatewayConnection.cpp
│  │  └─ GatewayServer.cpp
│  ├─ main.cpp
│  ├─ network
│  │  ├─ buffer
│  │  │  └─ RecvBuffer.cpp
│  │  └─ protocol
│  │     ├─ Packet.cpp
│  │     └─ PacketParser.cpp
│  ├─ proxy
│  │  ├─ BackendConnection.cpp
│  │  ├─ BackendPool.cpp
│  │  └─ ProxyService.cpp
│  ├─ router
│  └─ session
└─ Structure.md

```