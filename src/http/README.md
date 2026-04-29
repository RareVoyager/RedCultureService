# HTTP 接入层实现

该目录实现基于 Boost.Beast 的 HTTP Server 和 Router。

主要职责：

- 接收 TCP HTTP 请求。
- 转换为项目内部 `HttpRequest`。
- 调用 `HttpRouter` 分发业务 handler。
- 将 `HttpResponse` 转换为 HTTP 响应返回给 Unity 或运维工具。

高频游戏同步不建议走这里，仍应使用 TCP/WebSocket 等实时通道。
