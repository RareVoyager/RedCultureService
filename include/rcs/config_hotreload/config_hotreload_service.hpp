#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::config_hotreload {

struct NetworkConfig {
    std::string listen_address{"0.0.0.0"};
    std::uint16_t port{7000};
    std::uint32_t max_connections{4096};
    std::uint32_t max_messages_per_window{120};
};

struct AuthConfig {
    std::string issuer{"red-culture-service"};
    std::string audience{"unity-client"};
    std::string jwt_secret_env{"RCS_JWT_SECRET"};
    std::uint32_t token_ttl_seconds{7200};
    std::uint32_t session_idle_timeout_seconds{1800};
};

struct StorageConfig {
    std::string postgres_uri_env{"RCS_POSTGRES_URI"};
    std::string postgres_uri{"postgresql://postgres:postgres@127.0.0.1:5432/redculture"};
    bool auto_migrate{true};
};

struct AiConfig {
    std::string provider{"openai"};
    std::string base_url{"https://api.openai.com/v1"};
    std::string api_key_env{"OPENAI_API_KEY"};
    std::string model{"gpt-4.1-mini"};
    std::uint32_t timeout_ms{8000};
    std::uint32_t max_retries{2};
};

struct VoiceTtsConfig {
    std::string provider{"mock"};
    std::string base_url;
    std::string api_key_env;
    std::string voice_id{"default"};
    std::string language{"zh-CN"};
    std::uint32_t timeout_ms{8000};
    std::uint32_t max_retries{2};
    std::uint32_t cache_ttl_seconds{1800};
};

struct HotReloadConfig {
    bool enabled{true};
    std::uint32_t check_interval_ms{1000};
};

struct AppConfig {
    NetworkConfig network;
    AuthConfig auth;
    StorageConfig storage;
    AiConfig ai;
    VoiceTtsConfig voice_tts;
    HotReloadConfig hot_reload;
    std::unordered_map<std::string, std::string> raw_overrides;
};

struct ConfigLoadResult {
    bool ok{false};
    std::string error;
    std::optional<AppConfig> config;
};

class ConfigHotReloadService {
public:
    using ReloadCallback = std::function<void(const AppConfig&)>;

    explicit ConfigHotReloadService(std::filesystem::path config_path = "config/app.yaml");

    const std::filesystem::path& config_path() const noexcept;
    void set_config_path(std::filesystem::path config_path);

    ConfigLoadResult load();

    // 保留旧接口语义：强制重新加载当前配置文件。
    ConfigLoadResult reload();

    // 检查文件修改时间；只有配置文件变化时才重新加载并触发回调。
    ConfigLoadResult poll();

    std::optional<AppConfig> current_config() const;
    void set_on_reload(ReloadCallback callback);

    std::chrono::milliseconds check_interval() const;
    bool hot_reload_enabled() const;

    std::optional<std::string> resolve_env(const std::string& env_name) const;
    std::string resolve_env_or(const std::string& env_name, std::string fallback) const;

private:
    ConfigLoadResult load_locked(bool force);

    mutable std::mutex mutex_;
    std::filesystem::path config_path_;
    std::optional<AppConfig> current_config_;
    std::filesystem::file_time_type last_write_time_{};
    std::chrono::steady_clock::time_point last_checked_at_{};
    ReloadCallback on_reload_;
};

} // namespace rcs::config_hotreload
