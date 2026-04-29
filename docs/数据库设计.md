# PostgreSQL 数据库设计计划

本文档记录 RedCultureService 的 PostgreSQL 数据库设计计划。目标是先支持 Unity 游戏业务闭环，再逐步扩展为可运营、可审计、可分析的后端数据模型。

## 设计目标

数据库主要保存长期价值数据：

- 玩家资料与账号映射。
- Unity 场景和红色文化互动点配置。
- 房间与玩家参与记录。
- 一次完整红色文化互动流程。
- 玩家答题、AI 讲解、TTS 音频资源索引。
- 玩家进度。
- 服务事件日志和运营审计。

不建议放入 PostgreSQL 的短生命周期数据：

- TCP 连接状态。
- 心跳状态。
- 高频玩家位置同步。
- 临时匹配队列。
- 短期 AI/TTS 任务队列。

这些更适合内存或 Redis。

## 模块映射

| C++ 模块 | PostgreSQL 数据 |
| --- | --- |
| `auth` | `rcs_users`, 可选 `rcs_player_sessions` |
| `room` | `rcs_rooms`, `rcs_room_members` |
| `gameplay` | `rcs_cultural_interactions`, `rcs_answer_records`, `rcs_progress_records` |
| `ai_orchestrator` | AI 结果落入 `rcs_cultural_interactions` |
| `voice_tts` | `rcs_tts_audio_resources` |
| `storage` | 所有 PostgreSQL CRUD 和 migrate |
| `observability` / `ops` | `rcs_event_logs`, 可选审计表 |

## 命名规范

- 表名统一使用 `rcs_` 前缀。
- 主键优先使用业务稳定 ID，例如 `player_id`, `scene_id`, `trigger_id`。
- 流程型记录使用 `BIGSERIAL` 主键。
- 时间字段统一使用 `TIMESTAMPTZ`。
- 可扩展业务字段统一使用 `JSONB metadata`。
- 状态字段先使用 `TEXT`，后续稳定后可以升级为 PostgreSQL enum 或 check constraint。

## ER 关系

```mermaid
erDiagram
    rcs_users ||--o{ rcs_player_sessions : owns
    rcs_users ||--o{ rcs_room_members : joins
    rcs_users ||--o{ rcs_cultural_interactions : starts
    rcs_users ||--o{ rcs_answer_records : answers
    rcs_users ||--o{ rcs_progress_records : progresses

    rcs_scenes ||--o{ rcs_interaction_triggers : contains
    rcs_scenes ||--o{ rcs_progress_records : tracks

    rcs_rooms ||--o{ rcs_room_members : has
    rcs_rooms ||--o{ rcs_cultural_interactions : hosts

    rcs_interaction_triggers ||--o{ rcs_cultural_interactions : triggers

    rcs_cultural_interactions ||--o{ rcs_answer_records : records
    rcs_cultural_interactions ||--o| rcs_tts_audio_resources : audio
```

## 表分层

### 第一阶段：必须实现

- `rcs_users`
- `rcs_scenes`
- `rcs_interaction_triggers`
- `rcs_rooms`
- `rcs_room_members`
- `rcs_cultural_interactions`
- `rcs_answer_records`
- `rcs_progress_records`
- `rcs_tts_audio_resources`
- `rcs_event_logs`

### 第二阶段：增强

- `rcs_player_sessions`
- `rcs_ai_call_records`
- `rcs_admin_audit_logs`
- `rcs_scene_assets`
- `rcs_question_bank`

## 第一阶段建表 SQL

### 玩家表

```sql
CREATE TABLE IF NOT EXISTS rcs_users (
    player_id TEXT PRIMARY KEY,
    account TEXT NOT NULL,
    display_name TEXT NOT NULL,
    avatar_url TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

说明：

- `player_id` 是游戏内稳定玩家 ID。
- `account` 可映射平台账号、游客账号或内部测试账号。
- `metadata` 可放设备、渠道、头像扩展、平台信息等。

### 场景表

```sql
CREATE TABLE IF NOT EXISTS rcs_scenes (
    scene_id TEXT PRIMARY KEY,
    scene_name TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    enabled BOOLEAN NOT NULL DEFAULT true,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

说明：

- `scene_id` 对应 Unity 场景或关卡 ID。
- `metadata` 可保存场景版本、推荐人数、资源包版本等。

### 互动点配置表

```sql
CREATE TABLE IF NOT EXISTS rcs_interaction_triggers (
    trigger_id TEXT PRIMARY KEY,
    scene_id TEXT NOT NULL REFERENCES rcs_scenes(scene_id) ON DELETE CASCADE,
    topic TEXT NOT NULL,
    trigger_type TEXT NOT NULL DEFAULT 'area',
    prompt_template TEXT NOT NULL DEFAULT '',
    tts_voice_id TEXT NOT NULL DEFAULT 'default',
    cooldown_seconds INTEGER NOT NULL DEFAULT 3,
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    enabled BOOLEAN NOT NULL DEFAULT true,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

说明：

- `trigger_type` 建议值：`area`, `npc`, `artifact`, `manual`, `progress`。
- `prompt_template` 后续可覆盖默认 AI 问题模板。
- `metadata` 可保存 Unity 坐标、半径、文物编号、展示文案等。

### 房间表

```sql
CREATE TABLE IF NOT EXISTS rcs_rooms (
    id BIGSERIAL PRIMARY KEY,
    mode TEXT NOT NULL DEFAULT 'default',
    state TEXT NOT NULL DEFAULT 'waiting',
    max_players INTEGER NOT NULL DEFAULT 4,
    auto_start_when_full BOOLEAN NOT NULL DEFAULT true,
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    closed_at TIMESTAMPTZ
);
```

说明：

- 当前 `room` 模块主要使用内存，数据库表用于审计、回放、运营统计。
- `state` 建议值：`waiting`, `playing`, `closed`。

### 房间成员表

```sql
CREATE TABLE IF NOT EXISTS rcs_room_members (
    room_id BIGINT NOT NULL REFERENCES rcs_rooms(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    ready BOOLEAN NOT NULL DEFAULT false,
    joined_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    left_at TIMESTAMPTZ,
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    PRIMARY KEY (room_id, player_id)
);
```

说明：

- 当前实时房间状态仍在内存中维护。
- PostgreSQL 记录用于查询玩家参与过哪些房间。

### 红色文化互动流程表

```sql
CREATE TABLE IF NOT EXISTS rcs_cultural_interactions (
    id BIGSERIAL PRIMARY KEY,
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    room_id BIGINT REFERENCES rcs_rooms(id) ON DELETE SET NULL,
    scene_id TEXT NOT NULL,
    trigger_id TEXT NOT NULL,
    ai_flow_id BIGINT NOT NULL,
    topic TEXT NOT NULL,
    question TEXT NOT NULL DEFAULT '',
    answer TEXT NOT NULL DEFAULT '',
    explanation TEXT NOT NULL DEFAULT '',
    audio_id TEXT,
    status TEXT NOT NULL DEFAULT 'started',
    score DOUBLE PRECISION NOT NULL DEFAULT 0,
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    started_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    answered_at TIMESTAMPTZ
);
```

说明：

- 这是 gameplay 模块最核心的业务表。
- 一行代表一次完整互动流程。
- `status` 建议值：`started`, `question_generated`, `answered`, `explained`, `failed`。
- `trigger_id` 暂不强制外键，避免 Unity 配置先行、数据库配置滞后导致联调受阻。后续稳定后可加外键。

### 答题记录表

```sql
CREATE TABLE IF NOT EXISTS rcs_answer_records (
    id BIGSERIAL PRIMARY KEY,
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    interaction_id BIGINT REFERENCES rcs_cultural_interactions(id) ON DELETE SET NULL,
    question_id TEXT NOT NULL,
    question TEXT NOT NULL,
    answer TEXT NOT NULL,
    correct BOOLEAN NOT NULL DEFAULT false,
    score DOUBLE PRECISION NOT NULL DEFAULT 0,
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

说明：

- 保留独立答题表，方便后续做学习统计、排行榜、复盘。
- `interaction_id` 关联完整互动流程。
- `metadata` 可保存 AI 评分理由、标准答案、题目难度等。

### 进度表

```sql
CREATE TABLE IF NOT EXISTS rcs_progress_records (
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    scene_id TEXT NOT NULL,
    progress JSONB NOT NULL DEFAULT '{}'::jsonb,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (player_id, scene_id)
);
```

说明：

- `progress` 建议保存当前场景已完成互动点、最后触发点、分数、任务阶段。
- 不强制外键到 `rcs_scenes`，方便 Unity 新场景灰度联调。

### TTS 音频资源表

```sql
CREATE TABLE IF NOT EXISTS rcs_tts_audio_resources (
    audio_id TEXT PRIMARY KEY,
    player_id TEXT REFERENCES rcs_users(player_id) ON DELETE SET NULL,
    interaction_id BIGINT REFERENCES rcs_cultural_interactions(id) ON DELETE SET NULL,
    mime_type TEXT NOT NULL,
    format TEXT NOT NULL,
    byte_size BIGINT NOT NULL DEFAULT 0,
    duration_ms BIGINT NOT NULL DEFAULT 0,
    storage_type TEXT NOT NULL DEFAULT 'memory',
    storage_uri TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ
);
```

说明：

- 当前 TTS 音频字节在内存缓存中，数据库只记录索引。
- 未来可以把 `storage_type` 改为 `file`, `s3`, `oss`, `redis`。
- `storage_uri` 保存真实音频地址或对象存储 key。

### 事件日志表

```sql
CREATE TABLE IF NOT EXISTS rcs_event_logs (
    id BIGSERIAL PRIMARY KEY,
    level TEXT NOT NULL,
    category TEXT NOT NULL,
    message TEXT NOT NULL,
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

说明：

- 适合保存重要业务事件，不替代 spdlog 文件日志。
- `category` 示例：`gameplay.interaction`, `auth.login`, `room.lifecycle`, `storage.error`。

## 索引计划

```sql
CREATE INDEX IF NOT EXISTS idx_rcs_users_account
    ON rcs_users(account);

CREATE INDEX IF NOT EXISTS idx_rcs_scenes_enabled
    ON rcs_scenes(enabled);

CREATE INDEX IF NOT EXISTS idx_rcs_triggers_scene_enabled
    ON rcs_interaction_triggers(scene_id, enabled);

CREATE INDEX IF NOT EXISTS idx_rcs_rooms_state_created
    ON rcs_rooms(state, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_room_members_player
    ON rcs_room_members(player_id, joined_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_player_started
    ON rcs_cultural_interactions(player_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_room_started
    ON rcs_cultural_interactions(room_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_scene_trigger
    ON rcs_cultural_interactions(scene_id, trigger_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_answer_records_player_created
    ON rcs_answer_records(player_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_progress_records_scene
    ON rcs_progress_records(scene_id);

CREATE INDEX IF NOT EXISTS idx_rcs_tts_audio_expires
    ON rcs_tts_audio_resources(expires_at);

CREATE INDEX IF NOT EXISTS idx_rcs_event_logs_created
    ON rcs_event_logs(created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_event_logs_category_created
    ON rcs_event_logs(category, created_at DESC);
```

## JSONB 字段建议

### `rcs_interaction_triggers.metadata`

```json
{
  "unity_position": {"x": 1.0, "y": 0.0, "z": 3.5},
  "radius": 2.0,
  "artifact_id": "artifact_001",
  "difficulty": "normal"
}
```

### `rcs_progress_records.progress`

```json
{
  "completed_triggers": ["trigger_long_march"],
  "last_trigger_id": "trigger_long_march",
  "last_score": 0.8,
  "chapter": "chapter_01"
}
```

### `rcs_cultural_interactions.metadata`

```json
{
  "ai_provider": "openai",
  "model": "example-model",
  "latency_ms": 1200,
  "client_version": "unity-0.1.0"
}
```

## 迁移策略

建议分三步：

1. 新增 `database/schema.sql`，作为人工可读的完整建表脚本。
2. 扩展 `StorageService::migrate()`，让服务启动时能自动创建第一阶段表结构。
3. 后续引入迁移版本表，例如：

```sql
CREATE TABLE IF NOT EXISTS rcs_schema_migrations (
    version TEXT PRIMARY KEY,
    description TEXT NOT NULL,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
```

迁移版本命名建议：

```text
001_initial_core_tables.sql
002_add_cultural_interactions.sql
003_add_tts_audio_resources.sql
```

## 与当前代码的差距

当前 `StorageService` 已有：

- `rcs_users`
- `rcs_answer_records`
- `rcs_progress_records`
- `rcs_event_logs`

下一步需要新增：

- `rcs_scenes`
- `rcs_interaction_triggers`
- `rcs_rooms`
- `rcs_room_members`
- `rcs_cultural_interactions`
- `rcs_tts_audio_resources`

同时需要扩展 C++ 数据结构和接口：

- `SceneConfig`
- `InteractionTrigger`
- `RoomRecord`
- `RoomMemberRecord`
- `CulturalInteractionRecord`
- `TtsAudioResourceRecord`

## 推荐实施顺序

1. 创建 `database/schema.sql`。
2. 扩展 `StorageService::migrate()` 到完整第一阶段表。
3. 新增 scene/trigger 的 CRUD，用于 Unity 场景配置。
4. 新增 cultural interaction 的 start/update/complete 接口。
5. 修改 `CulturalInteractionService`，从写 `EventLog + AnswerRecord` 升级为写 `CulturalInteractionRecord + AnswerRecord + ProgressRecord + TtsAudioResourceRecord`。
6. 补 `storage_example`，演示初始化场景、创建互动点、写入一次完整互动记录。
7. 再考虑迁移版本管理和后台管理接口。

## 本地开发建议

创建数据库：

```bash
createdb redculture
```

启动服务前设置：

```bash
export RCS_POSTGRES_URI=postgresql://postgres:postgres@127.0.0.1:5432/redculture
```

检查表：

```bash
psql "$RCS_POSTGRES_URI" -c "\dt"
```

查看最近互动：

```bash
psql "$RCS_POSTGRES_URI" -c "
SELECT id, player_id, scene_id, trigger_id, status, score, started_at, answered_at
FROM rcs_cultural_interactions
ORDER BY started_at DESC
LIMIT 20;
"
```
