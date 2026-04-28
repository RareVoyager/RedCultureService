#include "rcs/observability/telemetry_service.hpp"

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    rcs::observability::TelemetryConfig config;
    config.service_name = "red_culture_service_example";
    config.logger_name = "rcs-example";
    config.min_log_level = rcs::observability::LogLevel::debug;

    rcs::observability::TelemetryService telemetry(config);

    telemetry.info("server", "service started", {{"env", "local"}, {"module", "observability"}});
    telemetry.increment_counter("rcs_requests_total", 1.0, {{"route", "/login"}, {"status", "ok"}});
    telemetry.increment_counter("rcs_requests_total", 1.0, {{"route", "/login"}, {"status", "ok"}});
    telemetry.set_gauge("rcs_online_players", 42.0, {}, "Current online player count");
    telemetry.observe_histogram("rcs_request_duration_seconds", 0.032, {{"route", "/login"}});
    telemetry.observe_histogram("rcs_request_duration_seconds", 0.120, {{"route", "/login"}});

    auto span = telemetry.start_span("ai_orchestration", {{"player_id", "player-10001"}});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    telemetry.finish_span(span);

    std::cout << "\n--- prometheus metrics ---\n";
    std::cout << telemetry.export_prometheus();

    return 0;
}
