# net

网络接入层实现：连接、编解码、心跳、限流等核心逻辑。

## 文件说明

- `message.cpp`：实现网络帧的编码和解码，当前帧头为 8 字节：payload 长度、消息类型、flags。
- `rate_limiter.cpp`：实现固定时间窗口限流。
- `tcp_connection.cpp`：实现基于 Boost.Asio 的异步 TCP 连接。
- `network_gateway.cpp`：实现服务端监听、连接注册、连接移除、单播和广播。

## 后续扩展

后续可以在这里继续加入协议版本校验、客户端断线重连策略、心跳超时检测、IP 级限流和 TLS 接入。
