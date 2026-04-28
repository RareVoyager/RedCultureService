# ops

运维与部署层头文件：健康检查、优雅停服、版本信息和基础管理接口。

## 当前职责

- `ops_service.hpp`：定义服务状态、版本信息、组件健康检查、管理请求/响应和运维服务入口。
- 支持 `/health`、`/ready`、`/version`、`/metrics`、`/shutdown` 这类基础管理路由。

## 设计边界

本模块不直接绑定 HTTP 框架。后续可以用 Boost.Beast、gRPC 或其他网关把真实请求转换成 `AdminRequest`，再交给 `OpsService` 处理。
