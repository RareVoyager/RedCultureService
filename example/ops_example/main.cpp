#include "rcs/observability/telemetry_service.hpp"
#include "rcs/ops/ops_service.hpp"

#include <iostream>

int main() {
    rcs::observability::TelemetryService telemetry;
    telemetry.increment_counter("rcs_requests_total", 3.0, {{"route", "/health"}});
    telemetry.set_gauge("rcs_online_players", 12.0);

    rcs::ops::OpsConfig config;
    config.service_name = "red_culture_service";
    config.version = "0.1.0";
    config.environment = "local";
    config.instance_id = "ops-example";

    rcs::ops::OpsService ops(config);
    ops.set_metrics_exporter([&telemetry]() {
        return telemetry.export_prometheus();
    });
    ops.set_shutdown_callback([](const std::string& reason) {
        std::cout << "shutdown callback: " << reason << '\n';
    });

    ops.register_health_check("database", []() {
        return rcs::ops::ComponentHealth{
            "database",
            true,
            "postgresql reachable",
            std::chrono::system_clock::now(),
        };
    });
    ops.register_health_check("ai", []() {
        return rcs::ops::ComponentHealth{
            "ai",
            true,
            "client configured",
            std::chrono::system_clock::now(),
        };
    });

    ops.start();

    for (const auto& path : {"/health", "/ready", "/version", "/metrics"}) {
        const auto response = ops.handle_request({"GET", path});
        std::cout << "\n== GET " << path << " => " << response.status_code << " ==\n";
        std::cout << response.body << '\n';
    }

    const auto shutdown = ops.handle_request({"POST", "/shutdown", "deploy rolling restart"});
    std::cout << "\n== POST /shutdown => " << shutdown.status_code << " ==\n";
    std::cout << shutdown.body << '\n';

    return 0;
}
