# Application 应用集成层实现

该目录负责把基础模块串成可运行服务。

主要职责：

- 创建本地 mock AI/TTS 客户端，便于 Unity 先联调链路。
- 根据配置可选连接 PostgreSQL。
- 注册 `/api/v1/...` HTTP 接口。
- 启动和停止 HTTP Server。

后续接入真实大模型或真实 TTS 时，优先替换这里注入的客户端实现。
