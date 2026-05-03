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
    // PostgreSQL 连接串。生产环境建议由配置文件或环境变量注入。
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
    std::string password_hash;
    std::string display_name;
    std::string avatar_url;
    std::string role{"player"};
    std::string status{"active"};
    nlohmann::json metadata = nlohmann::json::object();
};

struct AnswerRecord {
    std::int64_t id{0};
    std::int64_t interaction_id{0};
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

struct PlayerSessionRecord {
    std::int64_t id{0};
    std::string player_id;
    std::string token_id;
    std::uint64_t connection_id{0};
    std::string client_ip;
    std::string user_agent;
    nlohmann::json metadata = nlohmann::json::object();
};

struct CulturalInteractionRecord {
    std::int64_t id{0};
    std::uint64_t service_interaction_id{0};
    std::string player_id;
    std::uint64_t room_id{0};
    std::string scene_id;
    std::string trigger_id;
    std::string interaction_type{"qa"};
    std::uint64_t ai_flow_id{0};
    std::string topic;
    std::string question;
    std::string answer;
    std::string explanation;
    std::string navigation_text;
    std::string audio_id;
    std::string status{"started"};
    double score{0.0};
    nlohmann::json metadata = nlohmann::json::object();
};

struct TtsAudioResourceRecord {
    std::string audio_id;
    std::string player_id;
    std::int64_t interaction_id{0};
    std::string mime_type{"audio/wav"};
    std::string format{"mp3"};
    std::int64_t byte_size{0};
    std::int64_t duration_ms{0};
    std::string storage_type{"memory"};
    std::string storage_uri;
    nlohmann::json metadata = nlohmann::json::object();
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
    bool isConnected() const;

    // 创建 PostgreSQL 基础表结构，支持重复执行。
    StorageResult migrate();

    StorageResult createUser(const UserProfile& profile);
    StorageResult upsertUser(const UserProfile& profile);
    std::optional<UserProfile> findUser(const std::string& player_id) const;
    std::optional<UserProfile> findUserByAccount(const std::string& account) const;

    InsertResult appendPlayerSession(const PlayerSessionRecord& record);
    InsertResult startCulturalInteraction(const CulturalInteractionRecord& record);
    StorageResult completeCulturalInteraction(const CulturalInteractionRecord& record);
    StorageResult saveTtsAudioResource(const TtsAudioResourceRecord& record);

    InsertResult appendAnswerRecord(const AnswerRecord& record);
    std::vector<AnswerRecord> listAnswerRecords(const std::string& player_id, std::size_t limit = 50) const;

    StorageResult saveProgress(const ProgressRecord& record);
    std::optional<ProgressRecord> loadProgress(const std::string& player_id, const std::string& scene_id) const;

    InsertResult appendEventLog(const EventLog& event);
    std::vector<EventLog> listEventLogs(std::size_t limit = 100) const;

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace rcs::storage
