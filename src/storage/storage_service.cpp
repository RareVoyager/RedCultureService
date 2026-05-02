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

nlohmann::json parse_json_or_empty(const std::string& text) {
    if (text.empty()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(text);
}

void ensure_connected(const std::unique_ptr<pqxx::connection>& connection) {
    if (!connection || !connection->is_open()) {
        throw std::runtime_error("PostgreSQL connection is not open");
    }
}

void run_initial_schema(pqxx::work& tx) {
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
            player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
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
            run_initial_schema(tx);
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

bool StorageService::is_connected() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->connection && impl_->connection->is_open();
}

StorageResult StorageService::migrate() {
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensure_connected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        run_initial_schema(tx);
        tx.commit();
        return ok();
    } catch (const std::exception& e) {
        return fail(e);
    }
}

StorageResult StorageService::create_user(const UserProfile& profile) {
    if (profile.player_id.empty()) {
        return StorageResult{false, "player_id is empty"};
    }
    if (profile.account.empty()) {
        return StorageResult{false, "account is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensure_connected(impl_->connection);

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

StorageResult StorageService::upsert_user(const UserProfile& profile) {
    if (profile.player_id.empty()) {
        return StorageResult{false, "player_id is empty"};
    }
    if (profile.account.empty()) {
        return StorageResult{false, "account is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensure_connected(impl_->connection);

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

std::optional<UserProfile> StorageService::find_user(const std::string& player_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensure_connected(impl_->connection);

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
    profile.metadata = parse_json_or_empty(rows[0]["metadata"].as<std::string>());
    return profile;
}

std::optional<UserProfile> StorageService::find_user_by_account(const std::string& account) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensure_connected(impl_->connection);

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
    profile.metadata = parse_json_or_empty(rows[0]["metadata"].as<std::string>());
    return profile;
}

InsertResult StorageService::append_answer_record(const AnswerRecord& record) {
    if (record.player_id.empty()) {
        return InsertResult{false, "player_id is empty", 0};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensure_connected(impl_->connection);

        pqxx::work tx(*impl_->connection);
        const auto rows = tx.exec_params(R"SQL(
            INSERT INTO rcs_answer_records
                (player_id, question_id, question, answer, correct, score, metadata)
            VALUES ($1, $2, $3, $4, $5, $6, $7::jsonb)
            RETURNING id
        )SQL",
                                         record.player_id,
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

std::vector<AnswerRecord> StorageService::list_answer_records(const std::string& player_id, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensure_connected(impl_->connection);

    pqxx::read_transaction tx(*impl_->connection);
    const auto rows = tx.exec_params(R"SQL(
        SELECT id, player_id, question_id, question, answer, correct, score, metadata::text, created_at::text
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
        record.player_id = row["player_id"].as<std::string>();
        record.question_id = row["question_id"].as<std::string>();
        record.question = row["question"].as<std::string>();
        record.answer = row["answer"].as<std::string>();
        record.correct = row["correct"].as<bool>();
        record.score = row["score"].as<double>();
        record.metadata = parse_json_or_empty(row["metadata"].as<std::string>());
        record.created_at = row["created_at"].as<std::string>();
        records.push_back(std::move(record));
    }
    return records;
}

StorageResult StorageService::save_progress(const ProgressRecord& record) {
    if (record.player_id.empty()) {
        return StorageResult{false, "player_id is empty"};
    }
    if (record.scene_id.empty()) {
        return StorageResult{false, "scene_id is empty"};
    }

    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensure_connected(impl_->connection);

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

std::optional<ProgressRecord> StorageService::load_progress(const std::string& player_id,
                                                            const std::string& scene_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensure_connected(impl_->connection);

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
    record.progress = parse_json_or_empty(rows[0]["progress"].as<std::string>());
    record.updated_at = rows[0]["updated_at"].as<std::string>();
    return record;
}

InsertResult StorageService::append_event_log(const EventLog& event) {
    try {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ensure_connected(impl_->connection);

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

std::vector<EventLog> StorageService::list_event_logs(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ensure_connected(impl_->connection);

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
        event.metadata = parse_json_or_empty(row["metadata"].as<std::string>());
        event.created_at = row["created_at"].as<std::string>();
        events.push_back(std::move(event));
    }
    return events;
}

} // namespace rcs::storage
