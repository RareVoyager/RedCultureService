# Application 应用集成层公开接口

该目录定义服务应用的组装入口。

主要职责：

- 保存 `ApplicationConfig`。
- 持有 `ServiceContext`，统一管理鉴权、房间、AI、TTS、存储、业务、运维模块。
- 声明 HTTP API 注册函数。

业务模块不应该依赖 HTTP 细节，HTTP handler 只负责协议转换。
