# redculture_server 应用层

该目录只放当前后端进程相关的应用代码，负责把 `include/rcs` 和 `src` 中封装好的基础能力组装成可运行的 HTTP 服务。

## 目录职责

- `main.cpp`：进程入口，读取环境变量和命令行参数，启动服务并处理退出信号。
- `include/redculture_server/application`：应用配置、服务上下文和应用装配对象声明。
- `include/redculture_server/api`：HTTP Controller、DTO 和路由挂载声明。
- `src/application`：创建基础服务、注册健康检查、挂载 Controller、启动 HTTP Server。
- `src/api`：具体 HTTP 接口实现，例如注册、登录、答题互动、TTS 和运维接口。

## 分层约定

- `include/rcs` 与 `src` 只放可复用的基础能力封装，例如鉴权、房间、存储、日志、网络、TTS、AI 编排等。
- `app/redculture_server` 放具体业务入口和 HTTP 接口，避免把应用层代码堆到工具层。
- C++ 方法名使用小驼峰，例如 `registerRoutes`、`isConnected`、`parseJsonBody`。
- 对外 API 的字段名可以继续使用 JSON 常见的下划线风格，例如 `player_id`、`scene_id`，方便 Unity 和 Apifox 测试。