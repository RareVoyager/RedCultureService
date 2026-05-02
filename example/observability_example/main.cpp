#include "rcs/observability/telemetry_service.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace rcs::observability;

/**
 * @brief 测试基础日志等级
 */
void test_basic_logs(TelemetryService& telemetry) {
    telemetry.trace("test", "this is a trace log");
    telemetry.debug("test", "this is a debug log");
    telemetry.info("test", "this is an info log");
    telemetry.warn("test", "this is a warn log");
    telemetry.error("test", "this is an error log");

    // 你的 TelemetryService 当前没有 critical() 快捷函数，
    // 所以 critical 日志可以通过 log(LogEvent) 手动调用。
    LogEvent event;
    event.level = LogLevel::critical;
    event.category = "test";
    event.message = "this is a critical log";
    event.fields = {
        {"reason", "manual critical log test"}
    };

    telemetry.log(event);
}

/**
 * @brief 测试带字段的日志
 */
void test_log_with_fields(TelemetryService& telemetry) {
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

/**
 * @brief 测试异常日志
 */
void test_exception_log(TelemetryService& telemetry) {
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

/**
 * @brief 测试指标功能
 */
void test_metrics(TelemetryService& telemetry) {
    telemetry.increment_counter(
        "http_requests_total",
        1.0,
        {
            {"method", "GET"},
            {"path", "/api/user"}
        },
        "Total HTTP requests"
    );

    telemetry.increment_counter(
        "http_requests_total",
        1.0,
        {
            {"method", "POST"},
            {"path", "/api/login"}
        },
        "Total HTTP requests"
    );

    telemetry.set_gauge(
        "active_connections",
        10,
        {},
        "Current active connections"
    );

    telemetry.add_gauge(
        "active_connections",
        5,
        {},
        "Current active connections"
    );

    telemetry.observe_histogram(
        "http_request_duration_seconds",
        0.123,
        {
            {"method", "GET"},
            {"path", "/api/user"}
        },
        "HTTP request duration in seconds"
    );

    telemetry.observe_histogram(
        "http_request_duration_seconds",
        0.456,
        {
            {"method", "GET"},
            {"path", "/api/user"}
        },
        "HTTP request duration in seconds"
    );
}

/**
 * @brief 测试 TraceSpan
 */
void test_trace_span(TelemetryService& telemetry) {
    auto span = telemetry.start_span(
        "load_user_profile",
        {
            {"user_id", "10001"},
            {"operation", "load_user_profile"}
        }
    );

    // 模拟业务耗时
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    telemetry.finish_span(span);
}

int main() {
    TelemetryConfig config;

    config.service_name = "red_culture_service";
    config.logger_name = "redculture";

    // 为了测试 trace/debug，这里设置为 trace
    // 如果设置为 info，trace 和 debug 日志不会输出。
    config.min_log_level = LogLevel::trace;

    // 输出到控制台
    config.enable_console_log = true;

    // true  表示输出 JSON 风格日志
    // false 表示输出普通文本日志
    config.enable_json_log = true;

    TelemetryService telemetry(config);

    telemetry.info("main", "telemetry test started");

    test_basic_logs(telemetry);
    test_log_with_fields(telemetry);
    test_exception_log(telemetry);
    test_metrics(telemetry);
    test_trace_span(telemetry);

    telemetry.info("main", "telemetry test finished");

    std::cout << "\n================ Prometheus Metrics ================\n";
    std::cout << telemetry.export_prometheus() << std::endl;

    return 0;
}