#include "rcs/config_hotreload/config_hotreload_service.hpp"

#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace rcs::config_hotreload {

namespace {

template <typename T>
T readOr(const YAML::Node& node, const char* key, T fallback) {
    if (!node || !node[key]) {
        return fallback;
    }
    return node[key].as<T>();
}

std::string readStringOr(const YAML::Node& node, const char* key, std::string fallback) {
    if (!node || !node[key] || node[key].IsNull()) {
        return fallback;
    }
    return node[key].as<std::string>();
}

std::uint16_t readU16Or(const YAML::Node& node, const char* key, std::uint16_t fallback) {
    const auto value = readOr<std::uint32_t>(node, key, fallback);
    if (value > 65535U) {
        throw std::out_of_range(std::string(key) + " is out of uint16 range");
    }
    return static_cast<std::uint16_t>(value);
}

NetworkConfig parseNetwork(const YAML::Node& root) {
    NetworkConfig config;
    const auto node = root["network"];
    config.listen_address = readStringOr(node, "listen_address", config.listen_address);
    config.port = readU16Or(node, "port", config.port);
    config.max_connections = readOr<std::uint32_t>(node, "max_connections", config.max_connections);
    config.max_messages_per_window = readOr<std::uint32_t>(
        node,
        "max_messages_per_window",
        config.max_messages_per_window);
    return config;
}

ServerConfig parseServer(const YAML::Node& root) {
    ServerConfig config;
    const auto node = root["server"];
    config.listen_address = readStringOr(node, "listen_address", config.listen_address);
    config.port = readU16Or(node, "port", config.port);
    config.thread_count = readOr<std::uint32_t>(node, "thread_count", config.thread_count);
    config.max_body_bytes = readOr<std::uint32_t>(node, "max_body_bytes", config.max_body_bytes);
    config.enable_cors = readOr<bool>(node, "enable_cors", config.enable_cors);
    return config;
}

RuntimeConfig parseRuntime(const YAML::Node& root) {
    RuntimeConfig config;
    const auto node = root["app"];
    config.service_name = readStringOr(node, "service_name", config.service_name);
    config.version = readStringOr(node, "version", config.version);
    config.environment = readStringOr(node, "environment", config.environment);
    config.allow_dev_auth = readOr<bool>(node, "allow_dev_auth", config.allow_dev_auth);
    return config;
}

AuthConfig parseAuth(const YAML::Node& root) {
    AuthConfig config;
    const auto node = root["auth"];
    config.issuer = readStringOr(node, "issuer", config.issuer);
    config.audience = readStringOr(node, "audience", config.audience);
    config.jwt_secret = readStringOr(node, "jwt_secret", config.jwt_secret);
    config.jwt_secret_env = readStringOr(node, "jwt_secret_env", config.jwt_secret_env);
    config.token_ttl_seconds = readOr<std::uint32_t>(node, "token_ttl_seconds", config.token_ttl_seconds);
    config.session_idle_timeout_seconds = readOr<std::uint32_t>(
        node,
        "session_idle_timeout_seconds",
        config.session_idle_timeout_seconds);
    return config;
}

StorageConfig parseStorage(const YAML::Node& root) {
    StorageConfig config;
    const auto node = root["storage"];
    config.enabled = readOr<bool>(node, "enabled", config.enabled);
    config.postgres_uri_env = readStringOr(node, "postgres_uri_env", config.postgres_uri_env);
    config.postgres_uri = readStringOr(node, "postgres_uri", config.postgres_uri);
    config.auto_migrate = readOr<bool>(node, "auto_migrate", config.auto_migrate);
    config.allow_without_storage = readOr<bool>(node, "allow_without_storage", config.allow_without_storage);
    return config;
}

AiConfig parseAi(const YAML::Node& root) {
    AiConfig config;
    const auto node = root["ai"];
    config.provider = readStringOr(node, "provider", config.provider);
    config.base_url = readStringOr(node, "base_url", config.base_url);
    config.api_key_env = readStringOr(node, "api_key_env", config.api_key_env);
    config.model = readStringOr(node, "model", config.model);
    config.timeout_ms = readOr<std::uint32_t>(node, "timeout_ms", config.timeout_ms);
    config.max_retries = readOr<std::uint32_t>(node, "max_retries", config.max_retries);
    return config;
}

VoiceTtsConfig parseVoiceTts(const YAML::Node& root) {
    VoiceTtsConfig config;
    const auto node = root["voice_tts"];
    config.provider = readStringOr(node, "provider", config.provider);
    config.base_url = readStringOr(node, "base_url", config.base_url);
    config.api_key_env = readStringOr(node, "api_key_env", config.api_key_env);
    config.voice_id = readStringOr(node, "voice_id", config.voice_id);
    config.language = readStringOr(node, "language", config.language);
    config.timeout_ms = readOr<std::uint32_t>(node, "timeout_ms", config.timeout_ms);
    config.max_retries = readOr<std::uint32_t>(node, "max_retries", config.max_retries);
    config.cache_ttl_seconds = readOr<std::uint32_t>(node, "cache_ttl_seconds", config.cache_ttl_seconds);
    return config;
}

HotReloadConfig parseHotReload(const YAML::Node& root) {
    HotReloadConfig config;
    const auto node = root["hot_reload"];
    config.enabled = readOr<bool>(node, "enabled", config.enabled);
    config.check_interval_ms = readOr<std::uint32_t>(node, "check_interval_ms", config.check_interval_ms);
    return config;
}

LoggingConfig parseLogging(const YAML::Node& root) {
    LoggingConfig config;
    const auto node = root["logging"];
    config.service_name = readStringOr(node, "service_name", config.service_name);
    config.logger_name = readStringOr(node, "logger_name", config.logger_name);
    config.level = readStringOr(node, "level", config.level);
    config.console = readOr<bool>(node, "console", config.console);
    config.json = readOr<bool>(node, "json", config.json);
    config.file_enabled = readOr<bool>(node, "file_enabled", config.file_enabled);
    config.file_path = readStringOr(node, "file_path", config.file_path);
    config.pattern = readStringOr(node, "pattern", config.pattern);
    return config;
}

std::unordered_map<std::string, std::string> parseRawOverrides(const YAML::Node& root) {
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

AppConfig parseAppConfig(const YAML::Node& root) {
    AppConfig config;
    config.app = parseRuntime(root);
    config.server = parseServer(root);
    config.network = parseNetwork(root);
    config.auth = parseAuth(root);
    config.storage = parseStorage(root);
    config.ai = parseAi(root);
    config.voice_tts = parseVoiceTts(root);
    config.logging = parseLogging(root);
    config.hot_reload = parseHotReload(root);
    config.raw_overrides = parseRawOverrides(root);
    return config;
}

} // namespace

ConfigHotReloadService::ConfigHotReloadService(std::filesystem::path configPath)
    : config_path_(std::move(configPath)) {}

const std::filesystem::path& ConfigHotReloadService::configPath() const noexcept {
    return config_path_;
}

void ConfigHotReloadService::setConfigPath(std::filesystem::path configPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_path_ = std::move(configPath);
    current_config_.reset();
    last_write_time_ = {};
    last_checked_at_ = {};
}

ConfigLoadResult ConfigHotReloadService::load() {
    ReloadCallback callback;
    ConfigLoadResult result;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = loadLocked(true);
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
        result = loadLocked(false);
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

std::optional<AppConfig> ConfigHotReloadService::currentConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_config_;
}

void ConfigHotReloadService::setOnReload(ReloadCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_reload_ = std::move(callback);
}

std::chrono::milliseconds ConfigHotReloadService::checkInterval() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_config_) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::milliseconds(current_config_->hot_reload.check_interval_ms);
}

bool ConfigHotReloadService::hotReloadEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_config_ && current_config_->hot_reload.enabled;
}

std::optional<std::string> ConfigHotReloadService::resolveEnv(const std::string& env_name) const {
    if (env_name.empty()) {
        return std::nullopt;
    }

    const char* value = std::getenv(env_name.c_str());
    if (!value || !*value) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string ConfigHotReloadService::resolveEnvOr(const std::string& env_name, std::string fallback) const {
    auto value = resolveEnv(env_name);
    return value ? *value : std::move(fallback);
}

ConfigLoadResult ConfigHotReloadService::loadLocked(bool force) {
    try {
        if (!std::filesystem::exists(config_path_)) {
            return ConfigLoadResult{false, "config file does not exist: " + config_path_.string(), std::nullopt};
        }

        const auto write_time = std::filesystem::last_write_time(config_path_);
        if (!force && current_config_ && write_time == last_write_time_) {
            return ConfigLoadResult{true, {}, current_config_};
        }

        const auto root = YAML::LoadFile(config_path_.string());
        auto parsed = parseAppConfig(root);
        current_config_ = parsed;
        last_write_time_ = write_time;
        return ConfigLoadResult{true, {}, current_config_};
    } catch (const std::exception& e) {
        return ConfigLoadResult{false, e.what(), std::nullopt};
    }
}

} // namespace rcs::config_hotreload
