#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::ops {

enum class ServiceStatus {
    starting = 0,
    running = 1,
    draining = 2,
    stopping = 3,
    stopped = 4,
    unhealthy = 5,
};

struct OpsConfig {
    std::string service_name{"red_culture_service"};
    std::string version{"0.1.0"};
    std::string environment{"local"};
    std::string instance_id{"local-instance"};
    std::string admin_listen_address{"127.0.0.1"};
    std::uint16_t admin_port{9000};
    std::chrono::seconds shutdown_grace_period{10};
};

struct VersionInfo {
    std::string service_name;
    std::string version;
    std::string environment;
    std::string instance_id;
    std::chrono::system_clock::time_point started_at{};
};

struct ComponentHealth {
    std::string component;
    bool healthy{true};
    std::string message{"ok"};
    std::chrono::system_clock::time_point checked_at{};
};

struct HealthReport {
    bool healthy{true};
    bool ready{false};
    ServiceStatus status{ServiceStatus::starting};
    std::vector<ComponentHealth> components;
    std::chrono::system_clock::time_point checked_at{};
};

struct AdminRequest {
    std::string method{"GET"};
    std::string path{"/health"};
    std::string body;
    std::map<std::string, std::string> headers;
};

struct AdminResponse {
    int status_code{200};
    std::string content_type{"application/json"};
    std::string body;
    std::map<std::string, std::string> headers;
};

class OpsService {
public:
    using HealthCheck = std::function<ComponentHealth()>;
    using MetricsExporter = std::function<std::string()>;
    using ShutdownCallback = std::function<void(const std::string& reason)>;

    explicit OpsService(OpsConfig config = {});

    const OpsConfig& config() const noexcept;
    VersionInfo version_info() const;

    void start();
    void set_ready(bool ready);
    void mark_unhealthy(std::string reason);
    void begin_shutdown(std::string reason);
    void complete_shutdown();

    bool is_ready() const;
    bool is_shutting_down() const;
    ServiceStatus status() const;
    std::optional<std::string> shutdown_reason() const;

    bool register_health_check(std::string component, HealthCheck check);
    bool unregister_health_check(const std::string& component);
    void set_metrics_exporter(MetricsExporter exporter);
    void set_shutdown_callback(ShutdownCallback callback);

    HealthReport collect_health_report() const;

    // 处理基础管理接口。真实 HTTP/gRPC 服务可以把请求转为 AdminRequest 后调用这里。
    AdminResponse handle_request(const AdminRequest& request);

    AdminResponse health_response() const;
    AdminResponse ready_response() const;
    AdminResponse version_response() const;
    AdminResponse metrics_response() const;
    AdminResponse shutdown_response(std::string reason);

private:
    std::vector<HealthCheck> health_checks_snapshot() const;
    std::string health_report_json(const HealthReport& report) const;
    std::string version_json() const;
    AdminResponse json_response(int status_code, std::string body) const;
    AdminResponse text_response(int status_code, std::string body, std::string content_type = "text/plain") const;

    OpsConfig config_;
    std::chrono::system_clock::time_point started_at_;

    mutable std::mutex mutex_;
    ServiceStatus status_{ServiceStatus::starting};
    bool ready_{false};
    std::string unhealthy_reason_;
    std::string shutdown_reason_;
    std::unordered_map<std::string, HealthCheck> health_checks_;
    MetricsExporter metrics_exporter_;
    ShutdownCallback shutdown_callback_;
};

const char* to_string(ServiceStatus status);

} // namespace rcs::ops
