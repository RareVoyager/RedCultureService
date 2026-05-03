#include "redculture_server/application/service_application.hpp"

#include "rcs/config_hotreload/config_hotreload_service.hpp"
#include "rcs/observability/telemetry_service.hpp"

#include <boost/asio.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

struct CliOptions {
    std::optional<std::filesystem::path> config_path;
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
    std::optional<std::string> postgres_uri;
    bool prod_auth{false};
};

std::optional<std::string> readEnv(const std::string& name)
{
    if (name.empty()) {
        return std::nullopt;
    }

    const auto* value = std::getenv(name.c_str());
    if (value == nullptr || std::string(value).empty()) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string readEnvOr(const std::string& name, std::string fallback)
{
    auto value = readEnv(name);
    return value ? *value : std::move(fallback);
}

std::uint16_t parsePort(const std::string& value, std::uint16_t fallback)
{
    try {
        const auto port = std::stoul(value);
        if (port == 0 || port > 65535) {
            return fallback;
        }
        return static_cast<std::uint16_t>(port);
    } catch (...) {
        return fallback;
    }
}

std::optional<std::uint16_t> parsePortArg(const std::string& value)
{
    try {
        const auto port = std::stoul(value);
        if (port == 0 || port > 65535) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(port);
    } catch (...) {
        return std::nullopt;
    }
}

// 命令行只做临时覆盖；常规运行参数由 YAML 配置文件管理。
CliOptions parseCliOptions(int argc, char** argv)
{
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            options.config_path = std::filesystem::path(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            options.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            options.port = parsePortArg(argv[++i]);
        } else if (arg == "--postgres-uri" && i + 1 < argc) {
            options.postgres_uri = argv[++i];
        } else if (arg == "--prod-auth") {
            options.prod_auth = true;
        }
    }
    return options;
}

std::optional<std::filesystem::path> existingPath(const std::filesystem::path& path)
{
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        return std::filesystem::weakly_canonical(path, ec);
    }
    return std::nullopt;
}

std::filesystem::path resolveConfigPath(const CliOptions& options, const char* argv0)
{
    if (options.config_path) {
        return *options.config_path;
    }

    if (auto path = existingPath("config/app.yaml")) {
        return *path;
    }

    if (argv0 != nullptr && std::string(argv0).size() > 0) {
        std::error_code ec;
        const auto executable_path = std::filesystem::absolute(argv0, ec);
        const auto executable_dir = executable_path.parent_path();

        if (auto path = existingPath(executable_dir / "config" / "app.yaml")) {
            return *path;
        }

        // 兼容 CLion 常见目录：build/debug/app/redculture_server/redculture_server。
        if (auto path = existingPath(executable_dir / ".." / ".." / ".." / ".." / "config" / "app.yaml")) {
            return *path;
        }
    }

    return "config/app.yaml";
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

rcs::observability::LogLevel parseLogLevel(std::string level)
{
    level = toLower(std::move(level));
    if (level == "trace") {
        return rcs::observability::LogLevel::trace;
    }
    if (level == "debug") {
        return rcs::observability::LogLevel::debug;
    }
    if (level == "warn" || level == "warning") {
        return rcs::observability::LogLevel::warn;
    }
    if (level == "error") {
        return rcs::observability::LogLevel::error;
    }
    if (level == "critical") {
        return rcs::observability::LogLevel::critical;
    }
    return rcs::observability::LogLevel::info;
}

rcs::observability::TelemetryConfig toTelemetryConfig(const rcs::config_hotreload::AppConfig& file_config)
{
    rcs::observability::TelemetryConfig config;
    config.serviceName = file_config.logging.service_name.empty()
                             ? file_config.app.service_name
                             : file_config.logging.service_name;
    config.logger_name = file_config.logging.logger_name;
    config.min_log_level = parseLogLevel(file_config.logging.level);
    config.enable_console_log = file_config.logging.console;
    config.enable_file_log = file_config.logging.file_enabled;
    config.enable_json_log = file_config.logging.json;
    config.file_path = file_config.logging.file_path;
    config.pattern = file_config.logging.pattern;
    return config;
}

rcs::application::ApplicationConfig toApplicationConfig(const rcs::config_hotreload::AppConfig& file_config,
                                                        const CliOptions& options)
{
    rcs::application::ApplicationConfig config;

    config.http.listen_address = file_config.server.listen_address;
    config.http.listen_port = file_config.server.port;
    config.http.thread_count = file_config.server.thread_count == 0 ? 1 : file_config.server.thread_count;
    config.http.max_body_bytes = file_config.server.max_body_bytes;
    config.http.enable_cors = file_config.server.enable_cors;

    config.auth.issuer = file_config.auth.issuer;
    config.auth.audience = file_config.auth.audience;
    config.auth.jwt_secret = readEnvOr(file_config.auth.jwt_secret_env, file_config.auth.jwt_secret);
    config.auth.token_ttl = std::chrono::seconds(file_config.auth.token_ttl_seconds);
    config.auth.session_idle_timeout = std::chrono::seconds(file_config.auth.session_idle_timeout_seconds);

    config.storage.connection_uri = readEnvOr(file_config.storage.postgres_uri_env, file_config.storage.postgres_uri);
    config.storage.auto_migrate = file_config.storage.auto_migrate;
    config.enable_storage = file_config.storage.enabled || readEnv(file_config.storage.postgres_uri_env).has_value();
    config.gameplay.allow_without_storage = file_config.storage.allow_without_storage;

    config.ops.serviceName = file_config.app.service_name;
    config.ops.version = file_config.app.version;
    config.ops.environment = readEnvOr("RCS_ENV", file_config.app.environment);
    config.allow_dev_auth = file_config.app.allow_dev_auth;

    // 兼容旧的环境变量；优先级：YAML < 环境变量 < 命令行。
    if (auto host = readEnv("RCS_HTTP_HOST")) {
        config.http.listen_address = *host;
    }
    if (auto port = readEnv("RCS_HTTP_PORT")) {
        config.http.listen_port = parsePort(*port, config.http.listen_port);
    }

    if (options.host) {
        config.http.listen_address = *options.host;
    }
    if (options.port) {
        config.http.listen_port = *options.port;
    }
    if (options.postgres_uri) {
        config.enable_storage = true;
        config.storage.connection_uri = *options.postgres_uri;
    }
    if (options.prod_auth) {
        config.allow_dev_auth = false;
    }

    return config;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto cli_options = parseCliOptions(argc, argv);
        const auto config_path = resolveConfigPath(cli_options, argc > 0 ? argv[0] : nullptr);

        rcs::config_hotreload::ConfigHotReloadService config_loader(config_path);
        const auto load_result = config_loader.load();
        if (!load_result.ok || !load_result.config) {
            throw std::runtime_error("load config failed: " + config_path.string() + ": " + load_result.error);
        }

        auto telemetry = std::make_unique<rcs::observability::TelemetryService>(toTelemetryConfig(*load_result.config));
        auto config = toApplicationConfig(*load_result.config, cli_options);

        telemetry->info("config", "yaml config loaded", {
            {"path", config_path.string()},
            {"host", config.http.listen_address},
            {"port", std::to_string(config.http.listen_port)},
            {"storage_enabled", config.enable_storage ? "true" : "false"},
        });

        rcs::application::ServiceApplication app(config);
        app.start();

        telemetry->info("server", "redculture_server started", {
            {"host", config.http.listen_address},
            {"port", std::to_string(config.http.listen_port)},
            {"storage_enabled", config.enable_storage ? "true" : "false"},
        });

        // 主线程只处理退出信号，HTTP 服务运行在 HttpServer 的工作线程中。
        boost::asio::io_context signalContext;
        boost::asio::signal_set signals(signalContext, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            app.stop();
            signalContext.stop();
        });
        signalContext.run();

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "redculture failed: " << ex.what() << '\n';
        return 1;
    }
}
