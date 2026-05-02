# 数据库脚本

该目录保存 RedCultureService 的 PostgreSQL 初始化和迁移脚本。

## 当前脚本

- `schema.sql`：第一阶段核心表结构，可重复执行。

## 本地初始化

```bash
createdb redculture
psql postgresql://postgres:postgres@127.0.0.1:5432/redculture -f database/schema.sql
```

## 服务自动迁移

启动服务前设置：

```bash
export RCS_POSTGRES_URI=postgresql://postgres:postgres@127.0.0.1:5432/redculture
```

服务启动时会通过 `StorageService::migrate()` 自动创建第一阶段表结构。

## 检查表结构

```bash
psql "$RCS_POSTGRES_URI" -c "\dt rcs_*"
```

## 后续迁移建议

当前 `schema.sql` 适合第一阶段开发。进入生产前建议拆成版本化迁移文件，例如：

- `001_initial_core_schema.sql`
- `002_add_scene_admin_fields.sql`
- `003_add_ai_call_records.sql`

迁移记录表已经预留为 `rcs_schema_migrations`。
