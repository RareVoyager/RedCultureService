# storage

数据存储层头文件：PostgreSQL 访问、数据模型持久化与查询接口。

## 当前职责

- `storage_service.hpp`：定义 PostgreSQL 存储配置、用户资料、答题记录、进度存档、事件日志和存储服务接口。
- 支持建表迁移、用户 upsert、答题记录写入/查询、进度保存/读取和事件日志写入/查询。

## 设计边界

当前模块以 PostgreSQL 为主存储，不直接依赖网络、鉴权、房间或 AI 模块。上层模块把已经整理好的业务数据传给 `StorageService`，由它负责落库。
