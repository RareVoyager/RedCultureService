-- RedCultureService PostgreSQL 精简版数据库结构
-- 设计目标：贴合当前 Unity 实际需求，只支撑注册登录、答题互动、AI 语音导航、AI 语音讲解、TTS 音频索引、玩家进度和业务日志。
-- 注意：本脚本只创建必要表，不再创建场景配置表、互动触发点配置表、房间表等偏运营后台的数据结构。

CREATE TABLE IF NOT EXISTS rcs_schema_migrations (
    version TEXT PRIMARY KEY,
    description TEXT NOT NULL,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS rcs_users (
    player_id TEXT PRIMARY KEY,
    account TEXT NOT NULL,
    password_hash TEXT NOT NULL DEFAULT '',
    display_name TEXT NOT NULL DEFAULT '',
    avatar_url TEXT NOT NULL DEFAULT '',
    role TEXT NOT NULL DEFAULT 'player',
    status TEXT NOT NULL DEFAULT 'active',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

ALTER TABLE rcs_users
    ADD COLUMN IF NOT EXISTS password_hash TEXT NOT NULL DEFAULT '',
    ADD COLUMN IF NOT EXISTS avatar_url TEXT NOT NULL DEFAULT '',
    ADD COLUMN IF NOT EXISTS role TEXT NOT NULL DEFAULT 'player',
    ADD COLUMN IF NOT EXISTS status TEXT NOT NULL DEFAULT 'active';

CREATE TABLE IF NOT EXISTS rcs_player_sessions (
    id BIGSERIAL PRIMARY KEY,
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    token_id TEXT NOT NULL DEFAULT '',
    connection_id BIGINT NOT NULL DEFAULT 0,
    client_ip TEXT NOT NULL DEFAULT '',
    user_agent TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ,
    revoked_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS rcs_cultural_interactions (
    id BIGSERIAL PRIMARY KEY,
    service_interaction_id BIGINT NOT NULL DEFAULT 0,
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    room_id BIGINT NOT NULL DEFAULT 0,
    scene_id TEXT NOT NULL DEFAULT '',
    trigger_id TEXT NOT NULL DEFAULT '',
    interaction_type TEXT NOT NULL DEFAULT 'qa',
    ai_flow_id BIGINT NOT NULL DEFAULT 0,
    topic TEXT NOT NULL DEFAULT '',
    question TEXT NOT NULL DEFAULT '',
    answer TEXT NOT NULL DEFAULT '',
    explanation TEXT NOT NULL DEFAULT '',
    navigation_text TEXT NOT NULL DEFAULT '',
    audio_id TEXT,
    status TEXT NOT NULL DEFAULT 'started',
    score DOUBLE PRECISION NOT NULL DEFAULT 0 CHECK (score >= 0),
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    started_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    answered_at TIMESTAMPTZ,
    completed_at TIMESTAMPTZ
);

ALTER TABLE rcs_cultural_interactions
    ADD COLUMN IF NOT EXISTS service_interaction_id BIGINT NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS room_id BIGINT NOT NULL DEFAULT 0,
    ADD COLUMN IF NOT EXISTS interaction_type TEXT NOT NULL DEFAULT 'qa',
    ADD COLUMN IF NOT EXISTS navigation_text TEXT NOT NULL DEFAULT '';

CREATE TABLE IF NOT EXISTS rcs_answer_records (
    id BIGSERIAL PRIMARY KEY,
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    interaction_id BIGINT REFERENCES rcs_cultural_interactions(id) ON DELETE SET NULL,
    question_id TEXT NOT NULL DEFAULT '',
    question TEXT NOT NULL DEFAULT '',
    answer TEXT NOT NULL DEFAULT '',
    correct BOOLEAN NOT NULL DEFAULT false,
    score DOUBLE PRECISION NOT NULL DEFAULT 0 CHECK (score >= 0),
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

ALTER TABLE rcs_answer_records
    ADD COLUMN IF NOT EXISTS interaction_id BIGINT REFERENCES rcs_cultural_interactions(id) ON DELETE SET NULL;

CREATE TABLE IF NOT EXISTS rcs_progress_records (
    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
    scene_id TEXT NOT NULL DEFAULT '',
    progress JSONB NOT NULL DEFAULT '{}'::jsonb,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (player_id, scene_id)
);

CREATE TABLE IF NOT EXISTS rcs_tts_audio_resources (
    audio_id TEXT PRIMARY KEY,
    player_id TEXT REFERENCES rcs_users(player_id) ON DELETE SET NULL,
    interaction_id BIGINT REFERENCES rcs_cultural_interactions(id) ON DELETE SET NULL,
    mime_type TEXT NOT NULL,
    format TEXT NOT NULL DEFAULT 'mp3',
    byte_size BIGINT NOT NULL DEFAULT 0 CHECK (byte_size >= 0),
    duration_ms BIGINT NOT NULL DEFAULT 0 CHECK (duration_ms >= 0),
    storage_type TEXT NOT NULL DEFAULT 'memory',
    storage_uri TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    expires_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS rcs_event_logs (
    id BIGSERIAL PRIMARY KEY,
    level TEXT NOT NULL DEFAULT 'info',
    category TEXT NOT NULL DEFAULT '',
    message TEXT NOT NULL DEFAULT '',
    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_rcs_users_account_unique
    ON rcs_users(account);

CREATE INDEX IF NOT EXISTS idx_rcs_users_status
    ON rcs_users(status);

CREATE INDEX IF NOT EXISTS idx_rcs_sessions_player_created
    ON rcs_player_sessions(player_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_sessions_token
    ON rcs_player_sessions(token_id)
    WHERE token_id <> '';

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_player_started
    ON rcs_cultural_interactions(player_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_service_id
    ON rcs_cultural_interactions(service_interaction_id);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_scene_trigger
    ON rcs_cultural_interactions(scene_id, trigger_id, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_type_started
    ON rcs_cultural_interactions(interaction_type, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_flow
    ON rcs_cultural_interactions(ai_flow_id);

CREATE INDEX IF NOT EXISTS idx_rcs_interactions_status
    ON rcs_cultural_interactions(status, started_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_answer_records_player_created
    ON rcs_answer_records(player_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_answer_records_interaction
    ON rcs_answer_records(interaction_id);

CREATE INDEX IF NOT EXISTS idx_rcs_progress_records_scene
    ON rcs_progress_records(scene_id);

CREATE INDEX IF NOT EXISTS idx_rcs_tts_audio_expires
    ON rcs_tts_audio_resources(expires_at);

CREATE INDEX IF NOT EXISTS idx_rcs_tts_audio_player_created
    ON rcs_tts_audio_resources(player_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_event_logs_created
    ON rcs_event_logs(created_at DESC);

CREATE INDEX IF NOT EXISTS idx_rcs_event_logs_category_created
    ON rcs_event_logs(category, created_at DESC);

INSERT INTO rcs_schema_migrations (version, description)
VALUES ('001_unity_minimal_schema', '创建 Unity 实际需求版核心表结构')
ON CONFLICT (version) DO NOTHING;
