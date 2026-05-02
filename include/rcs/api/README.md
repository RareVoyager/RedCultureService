# API 接口层公开头文件

该目录用于封装面向 Unity、Apifox、运维工具调用的 HTTP API 入口。

主要职责：
- `server_routes.hpp`：统一注册正式业务接口，`app` 入口只需要调用一次。
- `http_utils.hpp`：封装 JSON 请求解析、统一响应格式、Bearer Token 解析、玩家身份解析等通用能力。
- `controllers/`：按业务域拆分接口控制器，避免把所有接口写在一个大文件里。
- `dto/`：存放接口层请求/响应 DTO，和底层领域模型保持适度隔离。

约定：
- 这里可以依赖 `application`、`auth`、`room`、`gameplay`、`voice_tts`、`ops` 等服务层。
- 这里不直接写进程启动逻辑，启动逻辑留在 `app/redculture_server/main.cpp`。
- 新增 Unity HTTP 接口时，优先在 `controllers/` 中新增或扩展控制器。
