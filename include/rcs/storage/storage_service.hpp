#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace rcs::storage {

struct StorageConfig {
    // PostgreSQL 连接串。生产环境建议由 config_hotreload 模块从配置文件或环境变量注入。
    std::string connection_uri{"postgresql://postgres:postgres@127.0.0.1:5432/redculture"};

    // 连接成功后是否自动创建基础表结构。
    bool auto_migrate{true};
};

struct StorageResult {
    bool ok{false};
    std::string error;
};

struct InsertResult {
    bool ok{false};
    std::string error;
    std::int64_t id{0};
};

struct UserProfile {
    std::string player_id;
    std::string account;
    std::string display_name;
    nlohmann::json metadata = nlohmann::json::object();
};

struct AnswerRecord {
    std::int64_t id{0};
    std::string player_id;
    std::string question_id;
    std::string question;
    std::string answer;
    bool correct{false};
    double score{0.0};
    nlohmann::json metadata = nlohmann::json::object();
    std::string created_at;
};

struct ProgressRecord {
    std::string player_id;
    std::string scene_id;
    nlohmann::json progress = nlohmann::json::object();
    std::string updated_at;
};

struct EventLog {
    std::int64_t id{0};
    std::string level{"info"};
    std::string category;
    std::string message;
    nlohmann::json metadata = nlohmann::json::object();
    std::string created_at;
};

class StorageService {
public:
    explicit StorageService(StorageConfig config = {});
    ~StorageService();

    StorageService(const StorageService&) = delete;
    StorageService& operator=(const StorageService&) = delete;
    StorageService(StorageService&&) noexcept;
    StorageService& operator=(StorageService&&) noexcept;

    const StorageConfig& config() const noexcept;

    StorageResult connect();
    void disconnect();
    bool is_connected() const;

    // 创建 PostgreSQL 基础表结构，支持重复执行。
    StorageResult migrate();

    StorageResult upsert_user(const UserProfile& profile);
    std::optional<UserProfile> find_user(const std::string& player_id) const;

    InsertResult append_answer_record(const AnswerRecord& record);
    std::vector<AnswerRecord> list_answer_records(const std::string& player_id, std::size_t limit = 50) const;

    StorageResult save_progress(const ProgressRecord& record);
    std::optional<ProgressRecord> load_progress(const std::string& player_id, const std::string& scene_id) const;

    InsertResult append_event_log(const EventLog& event);
    std::vector<EventLog> list_event_logs(std::size_t limit = 100) const;

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace rcs::storage
