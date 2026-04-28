# observability

可观测性实现：日志落地、指标上报、追踪链路写入。

## 文件说明

- `telemetry_service.cpp`：基于 `spdlog` 输出结构化日志，在内存中维护指标，并导出 Prometheus 文本格式。

## 后续扩展

后续可以接入 prometheus-cpp HTTP exposer、OpenTelemetry trace exporter、日志文件滚动、采样策略和告警规则。
