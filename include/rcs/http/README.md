# HTTP 接入层公开接口

该目录定义轻量 HTTP 接入层的公开类型。

主要职责：

- `HttpRequest`/`HttpResponse`：屏蔽 Boost.Beast 细节，给业务层提供稳定请求响应模型。
- `HttpRouter`：按 HTTP method 和 path 分发到具体 handler。
- `HttpServer`：承载真实 HTTP 监听配置，供 application 层启动服务。

这里不放具体业务逻辑，业务接口应注册在 application 层。
