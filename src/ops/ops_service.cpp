#include "rcs/ops/ops_service.hpp"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include <utility>

namespace rcs::ops {

namespace {

std::int64_t epochMillis(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::string normalizeMethod(std::string method) {
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return method;
}

nlohmann::json componentToJson(const ComponentHealth& component) {
    return {
        {"component", component.component},
        {"healthy", component.healthy},
        {"message", component.message},
        {"checked_at_ms", epochMillis(component.checked_at)},
    };
}

} // namespace

OpsService::OpsService(OpsConfig config)
    : config_(std::move(config)),
      started_at_(std::chrono::system_clock::now()) {}

const OpsConfig& OpsService::config() const noexcept {
    return config_;
}

VersionInfo OpsService::versionInfo() const {
    return VersionInfo{
        config_.serviceName,
        config_.version,
        config_.environment,
        config_.instance_id,
        started_at_,
    };
}

void OpsService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = ServiceStatus::running;
    ready_ = true;
    unhealthy_reason_.clear();
}

void OpsService::setReady(bool ready) {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = ready;
}

void OpsService::markUnhealthy(std::string reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = ServiceStatus::unhealthy;
    ready_ = false;
    unhealthy_reason_ = std::move(reason);
}

void OpsService::beginShutdown(std::string reason) {
    ShutdownCallback callback;
    std::string callback_reason;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_ == ServiceStatus::stopping || status_ == ServiceStatus::stopped) {
            return;
        }

        status_ = ServiceStatus::draining;
        ready_ = false;
        shutdown_reason_ = std::move(reason);
        callback_reason = shutdown_reason_.empty() ? "shutdown requested" : shutdown_reason_;
        callback = shutdown_callback_;
    }

    if (callback) {
        callback(callback_reason);
    }
}

void OpsService::completeShutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = ServiceStatus::stopped;
    ready_ = false;
}

bool OpsService::isReady() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_ && status_ == ServiceStatus::running;
}

bool OpsService::isShuttingDown() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_ == ServiceStatus::draining || status_ == ServiceStatus::stopping;
}

ServiceStatus OpsService::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

std::optional<std::string> OpsService::shutdownReason() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_reason_.empty()) {
        return std::nullopt;
    }
    return shutdown_reason_;
}

bool OpsService::registerHealthCheck(std::string component, HealthCheck check) {
    if (component.empty() || !check) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    health_checks_[std::move(component)] = std::move(check);
    return true;
}

bool OpsService::unregisterHealthCheck(const std::string& component) {
    std::lock_guard<std::mutex> lock(mutex_);
    return health_checks_.erase(component) > 0;
}

void OpsService::setMetricsExporter(MetricsExporter exporter) {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_exporter_ = std::move(exporter);
}

void OpsService::setShutdownCallback(ShutdownCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    shutdown_callback_ = std::move(callback);
}

HealthReport OpsService::collectHealthReport() const {
    ServiceStatus current_status;
    bool current_ready;
    std::string unhealthy_reason;
    auto checks = healthChecksSnapshot();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_status = status_;
        current_ready = ready_;
        unhealthy_reason = unhealthy_reason_;
    }

    HealthReport report;
    report.ready = current_ready && current_status == ServiceStatus::running;
    report.status = current_status;
    report.checked_at = std::chrono::system_clock::now();

    if (current_status == ServiceStatus::unhealthy) {
        report.components.push_back(ComponentHealth{
            "service",
            false,
            unhealthy_reason.empty() ? "service marked unhealthy" : unhealthy_reason,
            report.checked_at,
        });
    }

    for (const auto& check : checks) {
        try {
            auto component = check();
            if (component.checked_at == std::chrono::system_clock::time_point{}) {
                component.checked_at = report.checked_at;
            }
            report.components.push_back(std::move(component));
        } catch (const std::exception& e) {
            report.components.push_back(ComponentHealth{
                "unknown",
                false,
                e.what(),
                report.checked_at,
            });
        }
    }

    report.healthy = current_status != ServiceStatus::unhealthy &&
                     std::all_of(report.components.begin(), report.components.end(), [](const ComponentHealth& item) {
                         return item.healthy;
                     });
    return report;
}

AdminResponse OpsService::handleRequest(const AdminRequest& request) {
    const auto method = normalizeMethod(request.method);

    if (method == "GET" && request.path == "/health") {
        return healthResponse();
    }
    if (method == "GET" && request.path == "/ready") {
        return readyResponse();
    }
    if (method == "GET" && request.path == "/version") {
        return versionResponse();
    }
    if (method == "GET" && request.path == "/metrics") {
        return metricsResponse();
    }
    if (method == "POST" && request.path == "/shutdown") {
        return shutdownResponse(request.body.empty() ? "admin request" : request.body);
    }

    return jsonResponse(404, R"({"error":"admin route not found"})");
}

AdminResponse OpsService::healthResponse() const {
    const auto report = collectHealthReport();
    return jsonResponse(report.healthy ? 200 : 503, healthReportJson(report));
}

AdminResponse OpsService::readyResponse() const {
    const auto ready = isReady();
    nlohmann::json body;
    body["ready"] = ready;
    body["status"] = toString(status());
    return jsonResponse(ready ? 200 : 503, body.dump());
}

AdminResponse OpsService::versionResponse() const {
    return jsonResponse(200, versionJson());
}

AdminResponse OpsService::metricsResponse() const {
    MetricsExporter exporter;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        exporter = metrics_exporter_;
    }

    if (!exporter) {
        return textResponse(200, "# metrics exporter is not configured\n", "text/plain; version=0.0.4");
    }

    return textResponse(200, exporter(), "text/plain; version=0.0.4");
}

AdminResponse OpsService::shutdownResponse(std::string reason) {
    beginShutdown(std::move(reason));

    nlohmann::json body;
    body["accepted"] = true;
    body["status"] = toString(status());
    body["grace_period_seconds"] = config_.shutdown_grace_period.count();
    if (auto reason_value = shutdownReason()) {
        body["reason"] = *reason_value;
    }
    return jsonResponse(202, body.dump());
}

std::vector<OpsService::HealthCheck> OpsService::healthChecksSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<HealthCheck> checks;
    checks.reserve(health_checks_.size());
    for (const auto& [_, check] : health_checks_) {
        checks.push_back(check);
    }
    return checks;
}

std::string OpsService::healthReportJson(const HealthReport& report) const {
    nlohmann::json body;
    body["healthy"] = report.healthy;
    body["ready"] = report.ready;
    body["status"] = toString(report.status);
    body["checked_at_ms"] = epochMillis(report.checked_at);
    body["components"] = nlohmann::json::array();

    for (const auto& component : report.components) {
        body["components"].push_back(componentToJson(component));
    }

    return body.dump();
}

std::string OpsService::versionJson() const {
    const auto version = versionInfo();

    nlohmann::json body;
    body["serviceName"] = version.serviceName;
    body["version"] = version.version;
    body["environment"] = version.environment;
    body["instance_id"] = version.instance_id;
    body["started_at_ms"] = epochMillis(version.started_at);
    return body.dump();
}

AdminResponse OpsService::jsonResponse(int status_code, std::string body) const {
    return AdminResponse{
        status_code,
        "application/json",
        std::move(body),
        {{"cache-control", "no-store"}},
    };
}

AdminResponse OpsService::textResponse(int status_code, std::string body, std::string content_type) const {
    return AdminResponse{
        status_code,
        std::move(content_type),
        std::move(body),
        {{"cache-control", "no-store"}},
    };
}

const char* toString(ServiceStatus status) {
    switch (status) {
        case ServiceStatus::starting:
            return "starting";
        case ServiceStatus::running:
            return "running";
        case ServiceStatus::draining:
            return "draining";
        case ServiceStatus::stopping:
            return "stopping";
        case ServiceStatus::stopped:
            return "stopped";
        case ServiceStatus::unhealthy:
            return "unhealthy";
    }
    return "unknown";
}

} // namespace rcs::ops
