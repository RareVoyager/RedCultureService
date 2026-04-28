# observability

可观测性层头文件：日志规范、指标采集、追踪打点接口。

## 当前职责

- `telemetry_service.hpp`：定义日志级别、结构化日志事件、指标类型、指标快照、追踪 span 和统一可观测性服务。
- 支持结构化日志、counter/gauge/histogram 指标、简单 span 计时和 Prometheus 文本导出。

## 设计边界

本模块负责采集和格式化日志/指标，不直接启动 HTTP 服务。后续运维模块可以调用 `export_prometheus()` 暴露 `/metrics` 接口。
