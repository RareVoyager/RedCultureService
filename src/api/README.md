# API 接口层实现

该目录实现 `include/rcs/api` 暴露的业务接口层。

主要职责：
- 将 HTTP 请求转换为业务服务调用。
- 将业务服务返回值转换为统一 JSON 响应：`code`、`msg`、`data`。
- 统一处理 Token、玩家身份、请求参数解析、错误响应。
- 保持 `app` 层足够薄，让可测试、可复用逻辑都落在 `src`。

目录说明：
- `controllers/`：登录、房间、互动、TTS、运维接口的控制器实现。
- `http_utils.cpp`：接口层通用 HTTP/JSON 工具实现。
- `server_routes.cpp`：统一挂载所有业务接口。
