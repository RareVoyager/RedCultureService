#include "rcs/storage/storage_service.hpp"

#include <algorithm>
#include <mutex>
#include <pqxx/pqxx>
#include <stdexcept>
#include <utility>

namespace rcs::storage {

namespace {

StorageResult ok() {
    return StorageResult{true, {}};
}

StorageResult fail(const std::exception& e) {
    return StorageResult{false, e.what()};
}

nlohmann::json parseJsonOrEmpty(const std::string& text) {
    if (text.empty()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(text);
}

void ensureConnected(const std::unique_ptr<pqxx::connection>& connection) {
    if (!connection || !connection->is_open()) {
        throw std::runtime_error("PostgreSQL connection is not open");
    }
}

void runInitialSchema(pqxx::work& tx) {
    tx.exec(R"SQL(
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
        VALUES ('001_unity_minimal_schema', 'create unity minimal core tables')
        ON CONFLICT (version) DO NOTHING;
    )SQL");
}

} // namespace

struct StorageService::Impl {
    explicit Impl(StorageConfig config)
        : config(std::move(config)) {}

    StorageConfig config;
    mutable std::mutex mutex;
    std::unique_ptr<pqxx::connection> connection;
};

StorageService::StorageService(StorageConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

StorageService::~StorageService() = default;
StorageService::StorageService(StorageService&&) noexcept = default;
StorageService& StorageService::operator=(StorageService&&) noexcept = default;

const StorageConfig& StorageService::config() const noexcept {
    return impl_->config;
}

StorageResult StorageService::connect() {
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->connection = std::make_unique<pqxx::connection>(impl_->config.connection_uri);

        if (impl_->config.auto_migrate) {
            pqxx::work tx(*impl_->connection);
            runInitialSchema(tx);
            tx.commit();
        }

        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

void StorageService::disconnect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    // 释放连接对象即可关闭 PostgreSQL 连接，避免依赖不同 libpqxx 版本的 close() 行为。
    impl_->connection.reset();
}

bool StorageService::isConnected() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->connection && impl_->connection->is_open();
}

StorageResult StorageService::migrate() {
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        runInitialSchema(tx);
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

StorageResult StorageService::createUser(const UserProfile& profile) {
    if (profile.player_id.empty()) {
        return StorageResult{false, "player_id is empty"};
    }
    if (profile.account.empty()) {
        return StorageResult{false, "account is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        tx.exec_params(R"SQL(
            INSERT INTO rcs_users
                (player_id, account, password_hash, display_name, avatar_url, role, status, metadata)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb)
        )SQL",
                       profile.player_id,
                       profile.account,
                       profile.password_hash,
                       profile.display_name,
                       profile.avatar_url,
                       profile.role,
                       profile.status,
                       profile.metadata.dump());
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

StorageResult StorageService::upsertUser(const UserProfile& profile) {
    if (profile.player_id.empty()) {
        return StorageResult{false, "player_id is empty"};
    }
    if (profile.account.empty()) {
        return StorageResult{false, "account is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        tx.exec_params(R"SQL(
            INSERT INTO rcs_users
                (player_id, account, password_hash, display_name, avatar_url, role, status, metadata)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8::jsonb)
            ON CONFLICT (player_id) DO UPDATE SET
                account = EXCLUDED.account,
                password_hash = CASE
                    WHEN EXCLUDED.password_hash <> '' THEN EXCLUDED.password_hash
                    ELSE rcs_users.password_hash
                END,
                display_name = EXCLUDED.display_name,
                avatar_url = EXCLUDED.avatar_url,
                role = EXCLUDED.role,
                status = EXCLUDED.status,
                metadata = EXCLUDED.metadata,
                updated_at = now()
        )SQL",
                       profile.player_id,
                       profile.account,
                       profile.password_hash,
                       profile.display_name,
                       profile.avatar_url,
                       profile.role,
                       profile.status,
                       profile.metadata.dump());
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

std::optional<UserProfile> StorageService::findUser(const std::string& player_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensureConnected(impl_->connection);

    pqxx::read_transaction tx(*impl_->connection);
    const auto rows = tx.exec_params(R"SQL(
        SELECT player_id, account, password_hash, display_name, avatar_url, role, status, metadata::text
        FROM rcs_users
        WHERE player_id = $1
    )SQL",
                                    player_id);

    if (rows.empty()) {
        return std::nullopt;
    }

    UserProfile profile;
    profile.player_id = rows[0]["player_id"].as<std::string>();
    profile.account = rows[0]["account"].as<std::string>();
    profile.password_hash = rows[0]["password_hash"].as<std::string>();
    profile.display_name = rows[0]["display_name"].as<std::string>();
    profile.avatar_url = rows[0]["avatar_url"].as<std::string>();
    profile.role = rows[0]["role"].as<std::string>();
    profile.status = rows[0]["status"].as<std::string>();
    profile.metadata = parseJsonOrEmpty(rows[0]["metadata"].as<std::string>());
    return profile;
}

std::optional<UserProfile> StorageService::findUserByAccount(const std::string& account) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensureConnected(impl_->connection);

    pqxx::read_transaction tx(*impl_->connection);
    const auto rows = tx.exec_params(R"SQL(
        SELECT player_id, account, password_hash, display_name, avatar_url, role, status, metadata::text
        FROM rcs_users
        WHERE account = $1
    )SQL",
                                    account);

    if (rows.empty()) {
        return std::nullopt;
    }

    UserProfile profile;
    profile.player_id = rows[0]["player_id"].as<std::string>();
    profile.account = rows[0]["account"].as<std::string>();
    profile.password_hash = rows[0]["password_hash"].as<std::string>();
    profile.display_name = rows[0]["display_name"].as<std::string>();
    profile.avatar_url = rows[0]["avatar_url"].as<std::string>();
    profile.role = rows[0]["role"].as<std::string>();
    profile.status = rows[0]["status"].as<std::string>();
    profile.metadata = parseJsonOrEmpty(rows[0]["metadata"].as<std::string>());
    return profile;
}

InsertResult StorageService::appendPlayerSession(const PlayerSessionRecord& record) {
    if (record.player_id.empty()) {
        return InsertResult{false, "player_id is empty", 0};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        const auto rows = tx.exec_params(R"SQL(
            INSERT INTO rcs_player_sessions
                (player_id, token_id, connection_id, client_ip, user_agent, metadata)
            VALUES ($1, $2, $3, $4, $5, $6::jsonb)
            RETURNING id
        )SQL",
                                         record.player_id,
                                         record.token_id,
                                         static_cast<std::int64_t>(record.connection_id),
                                         record.client_ip,
                                         record.user_agent,
                                         record.metadata.dump());
        tx.commit();
        return InsertResult{true, {}, rows[0][0].as<std::int64_t>()};
    } catch (const std::exception& e) {
        return InsertResult{false, e.what(), 0};
    }
}

InsertResult StorageService::startCulturalInteraction(const CulturalInteractionRecord& record) {
    if (record.player_id.empty()) {
        return InsertResult{false, "player_id is empty", 0};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        const auto rows = tx.exec_params(R"SQL(
            INSERT INTO rcs_cultural_interactions
                (service_interaction_id, player_id, room_id, scene_id, trigger_id,
                 interaction_type, ai_flow_id, topic, question, answer, explanation,
                 navigation_text, audio_id, status, score, metadata)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12,
                    NULLIF($13::text, ''), $14, $15, $16::jsonb)
            RETURNING id
        )SQL",
                                         static_cast<std::int64_t>(record.service_interaction_id),
                                         record.player_id,
                                         static_cast<std::int64_t>(record.room_id),
                                         record.scene_id,
                                         record.trigger_id,
                                         record.interaction_type,
                                         static_cast<std::int64_t>(record.ai_flow_id),
                                         record.topic,
                                         record.question,
                                         record.answer,
                                         record.explanation,
                                         record.navigation_text,
                                         record.audio_id,
                                         record.status,
                                         record.score,
                                         record.metadata.dump());
        tx.commit();
        return InsertResult{true, {}, rows[0][0].as<std::int64_t>()};
    } catch (const std::exception& e) {
        return InsertResult{false, e.what(), 0};
    }
}

StorageResult StorageService::completeCulturalInteraction(const CulturalInteractionRecord& record) {
    if (record.id <= 0) {
        return StorageResult{false, "interaction database id is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        tx.exec_params(R"SQL(
            UPDATE rcs_cultural_interactions
            SET
                answer = $2,
                explanation = $3,
                navigation_text = CASE WHEN $4 <> '' THEN $4 ELSE navigation_text END,
                audio_id = NULLIF($5, ''),
                status = $6,
                score = $7,
                metadata = metadata || $8::jsonb,
                ai_flow_id = CASE WHEN $9::bigint <> 0 THEN $9::bigint ELSE ai_flow_id END,
                answered_at = now(),
                completed_at = CASE WHEN $6 = 'completed' THEN now() ELSE completed_at END
            WHERE id = $1
        )SQL",
                       record.id,
                       record.answer,
                       record.explanation,
                       record.navigation_text,
                       record.audio_id,
                       record.status,
                       record.score,
                       record.metadata.dump(),
                       static_cast<std::int64_t>(record.ai_flow_id));
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

StorageResult StorageService::saveTtsAudioResource(const TtsAudioResourceRecord& record) {
    if (record.audio_id.empty()) {
        return StorageResult{false, "audio_id is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        tx.exec_params(R"SQL(
            INSERT INTO rcs_tts_audio_resources
                (audio_id, player_id, interaction_id, mime_type, format, byte_size,
                 duration_ms, storage_type, storage_uri, metadata)
            VALUES ($1, NULLIF($2::text, ''), CASE WHEN $3::bigint = 0 THEN NULL ELSE $3::bigint END, $4, $5, $6,
                    $7, $8, $9, $10::jsonb)
            ON CONFLICT (audio_id) DO UPDATE SET
                player_id = EXCLUDED.player_id,
                interaction_id = EXCLUDED.interaction_id,
                mime_type = EXCLUDED.mime_type,
                format = EXCLUDED.format,
                byte_size = EXCLUDED.byte_size,
                duration_ms = EXCLUDED.duration_ms,
                storage_type = EXCLUDED.storage_type,
                storage_uri = EXCLUDED.storage_uri,
                metadata = EXCLUDED.metadata
        )SQL",
                       record.audio_id,
                       record.player_id,
                       record.interaction_id,
                       record.mime_type,
                       record.format,
                       record.byte_size,
                       record.duration_ms,
                       record.storage_type,
                       record.storage_uri,
                       record.metadata.dump());
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

InsertResult StorageService::appendAnswerRecord(const AnswerRecord& record) {
    if (record.player_id.empty()) {
        return InsertResult{false, "player_id is empty", 0};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        const auto rows = tx.exec_params(R"SQL(
            INSERT INTO rcs_answer_records
                (player_id, interaction_id, question_id, question, answer, correct, score, metadata)
            VALUES ($1, CASE WHEN $2::bigint = 0 THEN NULL ELSE $2::bigint END, $3, $4, $5, $6, $7, $8::jsonb)
            RETURNING id
        )SQL",
                                         record.player_id,
                                         record.interaction_id,
                                         record.question_id,
                                         record.question,
                                         record.answer,
                                         record.correct,
                                         record.score,
                                         record.metadata.dump());
        tx.commit();
        return InsertResult{true, {}, rows[0][0].as<std::int64_t>()};
    } catch (const std::exception& e) {
        return InsertResult{false, e.what(), 0};
    }
}

std::vector<AnswerRecord> StorageService::listAnswerRecords(const std::string& player_id, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensureConnected(impl_->connection);

    pqxx::read_transaction tx(*impl_->connection);
    const auto rows = tx.exec_params(R"SQL(
        SELECT id, COALESCE(interaction_id, 0) AS interaction_id, player_id, question_id, question,
               answer, correct, score, metadata::text, created_at::text
        FROM rcs_answer_records
        WHERE player_id = $1
        ORDER BY created_at DESC
        LIMIT $2
    )SQL",
                                    player_id,
                                    static_cast<int>(limit));

    std::vector<AnswerRecord> records;
    records.reserve(rows.size());
    for (const auto& row : rows) {
        AnswerRecord record;
        record.id = row["id"].as<std::int64_t>();
        record.interaction_id = row["interaction_id"].as<std::int64_t>();
        record.player_id = row["player_id"].as<std::string>();
        record.question_id = row["question_id"].as<std::string>();
        record.question = row["question"].as<std::string>();
        record.answer = row["answer"].as<std::string>();
        record.correct = row["correct"].as<bool>();
        record.score = row["score"].as<double>();
        record.metadata = parseJsonOrEmpty(row["metadata"].as<std::string>());
        record.created_at = row["created_at"].as<std::string>();
        records.push_back(std::move(record));
    }
    return records;
}

StorageResult StorageService::saveProgress(const ProgressRecord& record) {
    if (record.player_id.empty()) {
        return StorageResult{false, "player_id is empty"};
    }
    if (record.scene_id.empty()) {
        return StorageResult{false, "scene_id is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        tx.exec_params(R"SQL(
            INSERT INTO rcs_progress_records (player_id, scene_id, progress)
            VALUES ($1, $2, $3::jsonb)
            ON CONFLICT (player_id, scene_id) DO UPDATE SET
                progress = EXCLUDED.progress,
                updated_at = now()
        )SQL",
                       record.player_id,
                       record.scene_id,
                       record.progress.dump());
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

std::optional<ProgressRecord> StorageService::loadProgress(const std::string& player_id,
                                                            const std::string& scene_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensureConnected(impl_->connection);

    pqxx::read_transaction tx(*impl_->connection);
    const auto rows = tx.exec_params(R"SQL(
        SELECT player_id, scene_id, progress::text, updated_at::text
        FROM rcs_progress_records
        WHERE player_id = $1 AND scene_id = $2
    )SQL",
                                    player_id,
                                    scene_id);

    if (rows.empty()) {
        return std::nullopt;
    }

    ProgressRecord record;
    record.player_id = rows[0]["player_id"].as<std::string>();
    record.scene_id = rows[0]["scene_id"].as<std::string>();
    record.progress = parseJsonOrEmpty(rows[0]["progress"].as<std::string>());
    record.updated_at = rows[0]["updated_at"].as<std::string>();
    return record;
}

InsertResult StorageService::appendEventLog(const EventLog& event) {
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensureConnected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        const auto rows = tx.exec_params(R"SQL(
            INSERT INTO rcs_event_logs (level, category, message, metadata)
            VALUES ($1, $2, $3, $4::jsonb)
            RETURNING id
        )SQL",
                                         event.level,
                                         event.category,
                                         event.message,
                                         event.metadata.dump());
        tx.commit();
        return InsertResult{true, {}, rows[0][0].as<std::int64_t>()};
    } catch (const std::exception& e) {
        return InsertResult{false, e.what(), 0};
    }
}

std::vector<EventLog> StorageService::listEventLogs(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensureConnected(impl_->connection);

    pqxx::read_transaction tx(*impl_->connection);
    const auto rows = tx.exec_params(R"SQL(
        SELECT id, level, category, message, metadata::text, created_at::text
        FROM rcs_event_logs
        ORDER BY created_at DESC
        LIMIT $1
    )SQL",
                                    static_cast<int>(limit));

    std::vector<EventLog> events;
    events.reserve(rows.size());
    for (const auto& row : rows) {
        EventLog event;
        event.id = row["id"].as<std::int64_t>();
        event.level = row["level"].as<std::string>();
        event.category = row["category"].as<std::string>();
        event.message = row["message"].as<std::string>();
        event.metadata = parseJsonOrEmpty(row["metadata"].as<std::string>());
        event.created_at = row["created_at"].as<std::string>();
        events.push_back(std::move(event));
    }
    return events;
}

} // namespace rcs::storage
