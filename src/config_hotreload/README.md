# config_hotreload

配置与热更新实现：配置监听、灰度切换、热更新控制。

## 文件说明

- `config_hotreload_service.cpp`：基于 `yaml-cpp` 读取 YAML 配置，解析成强类型 `AppConfig`，并通过文件修改时间判断是否需要热更新。

## 后续扩展

后续可以加入配置版本号、灰度配置、配置校验规则、模块级订阅、远端配置中心和敏感字段脱敏输出。
