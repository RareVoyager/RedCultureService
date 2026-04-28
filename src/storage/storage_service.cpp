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
            tx.exec(R"SQL(
                CREATE TABLE IF NOT EXISTS rcs_users (
                    player_id TEXT PRIMARY KEY,
                    account TEXT NOT NULL,
                    display_name TEXT NOT NULL,
                    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
                );

                CREATE TABLE IF NOT EXISTS rcs_answer_records (
                    id BIGSERIAL PRIMARY KEY,
                    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
                    question_id TEXT NOT NULL,
                    question TEXT NOT NULL,
                    answer TEXT NOT NULL,
                    correct BOOLEAN NOT NULL DEFAULT false,
                    score DOUBLE PRECISION NOT NULL DEFAULT 0,
                    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
                );

                CREATE TABLE IF NOT EXISTS rcs_progress_records (
                    player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
                    scene_id TEXT NOT NULL,
                    progress JSONB NOT NULL DEFAULT '{}'::jsonb,
                    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    PRIMARY KEY (player_id, scene_id)
                );

                CREATE TABLE IF NOT EXISTS rcs_event_logs (
                    id BIGSERIAL PRIMARY KEY,
                    level TEXT NOT NULL,
                    category TEXT NOT NULL,
                    message TEXT NOT NULL,
                    metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
                );

                CREATE INDEX IF NOT EXISTS idx_rcs_answer_records_player_created
                    ON rcs_answer_records(player_id, created_at DESC);

                CREATE INDEX IF NOT EXISTS idx_rcs_event_logs_created
                    ON rcs_event_logs(created_at DESC);
            )SQL");
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
        tx.exec(R"SQL(
            CREATE TABLE IF NOT EXISTS rcs_users (
                player_id TEXT PRIMARY KEY,
                account TEXT NOT NULL,
                display_name TEXT NOT NULL,
                metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
            );

            CREATE TABLE IF NOT EXISTS rcs_answer_records (
                id BIGSERIAL PRIMARY KEY,
                player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
                question_id TEXT NOT NULL,
                question TEXT NOT NULL,
                answer TEXT NOT NULL,
                correct BOOLEAN NOT NULL DEFAULT false,
                score DOUBLE PRECISION NOT NULL DEFAULT 0,
                metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                created_at TIMESTAMPTZ NOT NULL DEFAULT now()
            );

            CREATE TABLE IF NOT EXISTS rcs_progress_records (
                player_id TEXT NOT NULL REFERENCES rcs_users(player_id) ON DELETE CASCADE,
                scene_id TEXT NOT NULL,
                progress JSONB NOT NULL DEFAULT '{}'::jsonb,
                updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                PRIMARY KEY (player_id, scene_id)
            );

            CREATE TABLE IF NOT EXISTS rcs_event_logs (
                id BIGSERIAL PRIMARY KEY,
                level TEXT NOT NULL,
                category TEXT NOT NULL,
                message TEXT NOT NULL,
                metadata JSONB NOT NULL DEFAULT '{}'::jsonb,
                created_at TIMESTAMPTZ NOT NULL DEFAULT now()
            );

            CREATE INDEX IF NOT EXISTS idx_rcs_answer_records_player_created
                ON rcs_answer_records(player_id, created_at DESC);

            CREATE INDEX IF NOT EXISTS idx_rcs_event_logs_created
                ON rcs_event_logs(created_at DESC);
        )SQL");
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
            INSERT INTO rcs_users (player_id, account, display_name, metadata)
            VALUES ($1, $2, $3, $4::jsonb)
            ON CONFLICT (player_id) DO UPDATE SET
                account = EXCLUDED.account,
                display_name = EXCLUDED.display_name,
                metadata = EXCLUDED.metadata,
                updated_at = now()
        )SQL",
                       profile.player_id,
                       profile.account,
                       profile.display_name,
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
        SELECT player_id, account, display_name, metadata::text
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
    profile.display_name = rows[0]["display_name"].as<std::string>();
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
