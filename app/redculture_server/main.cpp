#include "rcs/application/service_application.hpp"

#include "rcs/api/server_routes.hpp"

#include <boost/asio.hpp>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "rcs/observability/telemetry_service.hpp"

namespace {

    // 从环境变量中获取内容或是使用默认值
std::string read_env_or(const char* name, std::string fallback)
{
    const auto* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        return fallback;
    }
    return value;
}

    // 是否存在环境变量
bool has_env(const char* name)
{
    const auto* value = std::getenv(name);
    return value != nullptr && !std::string(value).empty();
}

    // 检验端口号value 是否合法
std::uint16_t parse_port(const std::string& value, std::uint16_t fallback)
{
    try {
        // string to unsigned long
        const auto port = std::stoul(value);
        if (port == 0 || port > 65535) {
            return fallback;
        }
        return static_cast<std::uint16_t>(port);
    } catch (...) {
        return fallback;
    }
}

    // 解析启动程序时的参数
void apply_cli_args(int argc, char** argv, rcs::application::ApplicationConfig& config)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            config.http.listen_address = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.http.listen_port = parse_port(argv[++i], config.http.listen_port);
        } else if (arg == "--prod-auth") {
            config.allow_dev_auth = false;
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        rcs::application::ApplicationConfig config;
        config.http.listen_address = read_env_or("RCS_HTTP_HOST", "0.0.0.0");
        config.http.listen_port = parse_port(read_env_or("RCS_HTTP_PORT", "8080"), 8080);
        config.auth.jwt_secret = read_env_or("RCS_JWT_SECRET", "local-dev-secret");
        if (has_env("RCS_POSTGRES_URI")) {
            config.enable_storage = true;
            config.storage.connection_uri = read_env_or("RCS_POSTGRES_URI", config.storage.connection_uri);
        }
        config.ops.service_name = "red_culture_service";
        config.ops.version = "0.1.0";
        config.ops.environment = read_env_or("RCS_ENV", "local");
        rcs::observability::TelemetryConfig config1;
        config1.service_name = "red_culture_service";
        config1.min_log_level = rcs::observability::LogLevel::info;
        config1.enable_json_log = false;
        rcs::observability::TelemetryService telemetry(config1);
        apply_cli_args(argc, argv, config);

        rcs::application::ServiceApplication app(config);
        rcs::api::register_server_routes(*app.router(), app.context());
        app.start();

        telemetry.info("http", "request received", {
            {"method", "GET"},
            {"path", "/api/login"}
        });

        // std::cout << "RedCultureService HTTP server started at http://"
        //           << config.http.listen_address << ':' << config.http.listen_port << '\n';
        // std::cout << "Try: curl http://127.0.0.1:" << config.http.listen_port << "/api/v1/ops/health\n";
        // std::cout << "Press Ctrl+C to stop.\n";

        // 主线程只负责处理退出信号，HTTP 服务本身运行在 HttpServer 的工作线程中。
        boost::asio::io_context signal_context;
        boost::asio::signal_set signals(signal_context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            app.stop();
            signal_context.stop();
        });
        signal_context.run();

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "redculture_server failed: " << ex.what() << '\n';
        return 1;
    }
}
