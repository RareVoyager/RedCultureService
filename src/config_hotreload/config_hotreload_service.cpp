#include "rcs/config_hotreload/config_hotreload_service.hpp"

#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace rcs::config_hotreload {

namespace {

template <typename T>
T read_or(const YAML::Node& node, const char* key, T fallback) {
    if (!node || !node[key]) {
        return fallback;
    }
    return node[key].as<T>();
}

std::string read_string_or(const YAML::Node& node, const char* key, std::string fallback) {
    if (!node || !node[key] || node[key].IsNull()) {
        return fallback;
    }
    return node[key].as<std::string>();
}

std::uint16_t read_u16_or(const YAML::Node& node, const char* key, std::uint16_t fallback) {
    const auto value = read_or<std::uint32_t>(node, key, fallback);
    if (value > 65535U) {
        throw std::out_of_range(std::string(key) + " is out of uint16 range");
    }
    return static_cast<std::uint16_t>(value);
}

NetworkConfig parse_network(const YAML::Node& root) {
    NetworkConfig config;
    const auto node = root["network"];
    config.listen_address = read_string_or(node, "listen_address", config.listen_address);
    config.port = read_u16_or(node, "port", config.port);
    config.max_connections = read_or<std::uint32_t>(node, "max_connections", config.max_connections);
    config.max_messages_per_window = read_or<std::uint32_t>(
        node,
        "max_messages_per_window",
        config.max_messages_per_window);
    return config;
}

AuthConfig parse_auth(const YAML::Node& root) {
    AuthConfig config;
    const auto node = root["auth"];
    config.issuer = read_string_or(node, "issuer", config.issuer);
    config.audience = read_string_or(node, "audience", config.audience);
    config.jwt_secret_env = read_string_or(node, "jwt_secret_env", config.jwt_secret_env);
    config.token_ttl_seconds = read_or<std::uint32_t>(node, "token_ttl_seconds", config.token_ttl_seconds);
    config.session_idle_timeout_seconds = read_or<std::uint32_t>(
        node,
        "session_idle_timeout_seconds",
        config.session_idle_timeout_seconds);
    return config;
}

StorageConfig parse_storage(const YAML::Node& root) {
    StorageConfig config;
    const auto node = root["storage"];
    config.postgres_uri_env = read_string_or(node, "postgres_uri_env", config.postgres_uri_env);
    config.postgres_uri = read_string_or(node, "postgres_uri", config.postgres_uri);
    config.auto_migrate = read_or<bool>(node, "auto_migrate", config.auto_migrate);
    return config;
}

AiConfig parse_ai(const YAML::Node& root) {
    AiConfig config;
    const auto node = root["ai"];
    config.provider = read_string_or(node, "provider", config.provider);
    config.base_url = read_string_or(node, "base_url", config.base_url);
    config.api_key_env = read_string_or(node, "api_key_env", config.api_key_env);
    config.model = read_string_or(node, "model", config.model);
    config.timeout_ms = read_or<std::uint32_t>(node, "timeout_ms", config.timeout_ms);
    config.max_retries = read_or<std::uint32_t>(node, "max_retries", config.max_retries);
    return config;
}

VoiceTtsConfig parse_voice_tts(const YAML::Node& root) {
    VoiceTtsConfig config;
    const auto node = root["voice_tts"];
    config.provider = read_string_or(node, "provider", config.provider);
    config.base_url = read_string_or(node, "base_url", config.base_url);
    config.api_key_env = read_string_or(node, "api_key_env", config.api_key_env);
    config.voice_id = read_string_or(node, "voice_id", config.voice_id);
    config.language = read_string_or(node, "language", config.language);
    config.timeout_ms = read_or<std::uint32_t>(node, "timeout_ms", config.timeout_ms);
    config.max_retries = read_or<std::uint32_t>(node, "max_retries", config.max_retries);
    config.cache_ttl_seconds = read_or<std::uint32_t>(node, "cache_ttl_seconds", config.cache_ttl_seconds);
    return config;
}

HotReloadConfig parse_hot_reload(const YAML::Node& root) {
    HotReloadConfig config;
    const auto node = root["hot_reload"];
    config.enabled = read_or<bool>(node, "enabled", config.enabled);
    config.check_interval_ms = read_or<std::uint32_t>(node, "check_interval_ms", config.check_interval_ms);
    return config;
}

std::unordered_map<std::string, std::string> parse_raw_overrides(const YAML::Node& root) {
    std::unordered_map<std::string, std::string> values;
    const auto node = root["overrides"];
    if (!node || !node.IsMap()) {
        return values;
    }

    for (const auto& item : node) {
        values[item.first.as<std::string>()] = item.second.as<std::string>();
    }
    return values;
}

AppConfig parse_app_config(const YAML::Node& root) {
    AppConfig config;
    config.network = parse_network(root);
    config.auth = parse_auth(root);
    config.storage = parse_storage(root);
    config.ai = parse_ai(root);
    config.voice_tts = parse_voice_tts(root);
    config.hot_reload = parse_hot_reload(root);
    config.raw_overrides = parse_raw_overrides(root);
    return config;
}

} // namespace

ConfigHotReloadService::ConfigHotReloadService(std::filesystem::path config_path)
    : config_path_(std::move(config_path)) {}

const std::filesystem::path& ConfigHotReloadService::config_path() const noexcept {
    return config_path_;
}

void ConfigHotReloadService::set_config_path(std::filesystem::path config_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_path_ = std::move(config_path);
    current_config_.reset();
    last_write_time_ = {};
    last_checked_at_ = {};
}

ConfigLoadResult ConfigHotReloadService::load() {
    ReloadCallback callback;
    ConfigLoadResult result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = load_locked(true);
        if (result.ok && result.config) {
            callback = on_reload_;
        }
    }

    if (callback && result.config) {
        callback(*result.config);
    }

    return result;
}

ConfigLoadResult ConfigHotReloadService::reload() {
    return load();
}

ConfigLoadResult ConfigHotReloadService::poll() {
    ReloadCallback callback;
    ConfigLoadResult result;
    bool changed = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto now = std::chrono::steady_clock::now();
        if (current_config_ && !current_config_->hot_reload.enabled) {
            return ConfigLoadResult{true, {}, current_config_};
        }

        const auto interval = current_config_ ? std::chrono::milliseconds(current_config_->hot_reload.check_interval_ms)
                                             : std::chrono::milliseconds(0);
        if (current_config_ && now - last_checked_at_ < interval) {
            return ConfigLoadResult{true, {}, current_config_};
        }

        const auto previous_write_time = last_write_time_;
        last_checked_at_ = now;
        result = load_locked(false);
        changed = result.ok && result.config && last_write_time_ != previous_write_time;
        if (changed) {
            callback = on_reload_;
        }
    }

    if (callback && result.config) {
        callback(*result.config);
    }

    return result;
}

std::optional<AppConfig> ConfigHotReloadService::current_config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_config_;
}

void ConfigHotReloadService::set_on_reload(ReloadCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_reload_ = std::move(callback);
}

std::chrono::milliseconds ConfigHotReloadService::check_interval() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_config_) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::milliseconds(current_config_->hot_reload.check_interval_ms);
}

bool ConfigHotReloadService::hot_reload_enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_config_ && current_config_->hot_reload.enabled;
}

std::optional<std::string> ConfigHotReloadService::resolve_env(const std::string& env_name) const {
    if (env_name.empty()) {
        return std::nullopt;
    }

    const char* value = std::getenv(env_name.c_str());
    if (!value || !*value) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string ConfigHotReloadService::resolve_env_or(const std::string& env_name, std::string fallback) const {
    auto value = resolve_env(env_name);
    return value ? *value : std::move(fallback);
}

ConfigLoadResult ConfigHotReloadService::load_locked(bool force) {
    try {
        if (!std::filesystem::exists(config_path_)) {
            return ConfigLoadResult{false, "config file does not exist: " + config_path_.string(), std::nullopt};
        }

        const auto write_time = std::filesystem::last_write_time(config_path_);
        if (!force && current_config_ && write_time == last_write_time_) {
            return ConfigLoadResult{true, {}, current_config_};
        }

        const auto root = YAML::LoadFile(config_path_.string());
        auto parsed = parse_app_config(root);
        current_config_ = parsed;
        last_write_time_ = write_time;
        return ConfigLoadResult{true, {}, current_config_};
    } catch (const std::exception& e) {
        return ConfigLoadResult{false, e.what(), std::nullopt};
    }
}

} // namespace rcs::config_hotreload
