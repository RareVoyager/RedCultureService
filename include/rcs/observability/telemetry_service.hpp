#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::observability {

enum class LogLevel {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    critical = 5,
};

enum class MetricType {
    counter = 0,
    gauge = 1,
    histogram = 2,
};

struct TelemetryConfig {
    std::string service_name{"red_culture_service"};
    std::string logger_name{"redculture"};
    LogLevel min_log_level{LogLevel::info};
    bool enable_console_log{true};
    bool enable_json_log{true};
    std::vector<double> default_histogram_buckets{0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0};
};

using Labels = std::map<std::string, std::string>;
using Fields = std::unordered_map<std::string, std::string>;

struct LogEvent {
    LogLevel level{LogLevel::info};
    std::string category;
    std::string message;
    Fields fields;
};

struct MetricSnapshot {
    MetricType type{MetricType::counter};
    std::string name;
    std::string help;
    Labels labels;
    double value{0.0};
    double sum{0.0};
    std::uint64_t count{0};
    std::vector<double> buckets;
    std::vector<std::uint64_t> bucket_counts;
};

struct TraceSpan {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string name;
    Fields attributes;
    std::chrono::steady_clock::time_point started_at{};
    std::chrono::steady_clock::time_point ended_at{};
};

class TelemetryService {
public:
    explicit TelemetryService(TelemetryConfig config = {});

    const TelemetryConfig& config() const noexcept;
    void configure(TelemetryConfig config);

    void log(const LogEvent& event);
    void trace(std::string category, std::string message, Fields fields = {});
    void debug(std::string category, std::string message, Fields fields = {});
    void info(std::string category, std::string message, Fields fields = {});
    void warn(std::string category, std::string message, Fields fields = {});
    void error(std::string category, std::string message, Fields fields = {});

    void increment_counter(std::string name, double value = 1.0, Labels labels = {}, std::string help = {});
    void set_gauge(std::string name, double value, Labels labels = {}, std::string help = {});
    void add_gauge(std::string name, double delta, Labels labels = {}, std::string help = {});
    void observe_histogram(std::string name, double value, Labels labels = {}, std::string help = {});

    TraceSpan start_span(std::string name, Fields attributes = {}, std::string parent_span_id = {});
    void finish_span(TraceSpan span);

    std::vector<MetricSnapshot> collect() const;
    std::string export_prometheus() const;
    void reset_metrics();

private:
    struct MetricState {
        MetricType type{MetricType::counter};
        std::string name;
        std::string help;
        Labels labels;
        double value{0.0};
        double sum{0.0};
        std::uint64_t count{0};
        std::vector<double> buckets;
        std::vector<std::uint64_t> bucket_counts;
    };

    void configure_logger_locked();
    void write_log_locked(const LogEvent& event);
    MetricState& metric_locked(MetricType type,
                               const std::string& name,
                               const Labels& labels,
                               const std::string& help);
    std::string metric_key(MetricType type, const std::string& name, const Labels& labels) const;

    TelemetryConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MetricState> metrics_;
    std::uint64_t next_span_id_{1};
};

const char* to_string(LogLevel level);
const char* to_string(MetricType type);

} // namespace rcs::observability
