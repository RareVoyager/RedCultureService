#include <rcs/observability/telemetry_service.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace rcs::observability;

void testBasicLogs(TelemetryService& telemetry)
{
    telemetry.trace("test", "this is a trace log");
    telemetry.debug("test", "this is a debug log");
    telemetry.info("test", "this is an info log");
    telemetry.warn("test", "this is a warn log");
    telemetry.error("test", "this is an error log");

    // TelemetryService 没有 critical() 快捷函数，因此这里手动构造 LogEvent。
    LogEvent event;
    event.level = LogLevel::critical;
    event.category = "test";
    event.message = "this is a critical log";
    event.fields = {
        {"reason", "manual critical log test"}
    };

    telemetry.log(event);
}

void testLogWithFields(TelemetryService& telemetry)
{
    telemetry.info(
        "server",
        "server started",
        {
            {"host", "0.0.0.0"},
            {"port", "8080"},
            {"mode", "development"}
        }
    );

    telemetry.warn(
        "auth",
        "invalid login attempt",
        {
            {"username", "admin"},
            {"client_ip", "127.0.0.1"},
            {"reason", "wrong password"}
        }
    );

    telemetry.error(
        "database",
        "query failed",
        {
            {"sql", "select * from user where id = ?"},
            {"error_code", "500"}
        }
    );
}

void testExceptionLog(TelemetryService& telemetry)
{
    try {
        throw std::runtime_error("failed to connect database");
    } catch (const std::exception& e) {
        telemetry.error(
            "database",
            "database connection exception",
            {
                {"exception", e.what()},
                {"host", "127.0.0.1"},
                {"port", "5432"}
            }
        );
    }
}

void testMetrics(TelemetryService& telemetry)
{
    telemetry.incrementCounter(
        "http_requests_total",
        1.0,
        {
            {"method", "GET"},
            {"path", "/api/user"}
        },
        "Total HTTP requests"
    );

    telemetry.incrementCounter(
        "http_requests_total",
        1.0,
        {
            {"method", "POST"},
            {"path", "/api/login"}
        },
        "Total HTTP requests"
    );

    telemetry.setGauge("active_connections", 10, {}, "Current active connections");
    telemetry.addGauge("active_connections", 5, {}, "Current active connections");

    telemetry.observeHistogram(
        "http_request_duration_seconds",
        0.123,
        {
            {"method", "GET"},
            {"path", "/api/user"}
        },
        "HTTP request duration in seconds"
    );

    telemetry.observeHistogram(
        "http_request_duration_seconds",
        0.456,
        {
            {"method", "GET"},
            {"path", "/api/user"}
        },
        "HTTP request duration in seconds"
    );
}

void testTraceSpan(TelemetryService& telemetry)
{
    auto span = telemetry.startSpan(
        "load_user_profile",
        {
            {"user_id", "10001"},
            {"operation", "load_user_profile"}
        }
    );

    // 模拟业务耗时。
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    telemetry.finishSpan(span);
}

int main()
{
    TelemetryConfig config;
    config.logger_name = "redculture";

    // 为了测试 trace/debug，这里设置为 trace。
    // 如果设置为 info，trace 和 debug 日志不会输出。
    config.min_log_level = LogLevel::trace;
    config.enable_console_log = true;
    config.enable_json_log = false;

    TelemetryService telemetry(config);

    telemetry.info("main", "telemetry test started");

    testBasicLogs(telemetry);
    testLogWithFields(telemetry);
    testExceptionLog(telemetry);
    testMetrics(telemetry);
    testTraceSpan(telemetry);

    std::cout << "\n==== Prometheus metrics ====\n";
    std::cout << telemetry.exportPrometheus() << '\n';

    return 0;
}
