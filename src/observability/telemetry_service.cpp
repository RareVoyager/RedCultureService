#include "rcs/observability/telemetry_service.hpp"

#include <algorithm>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace rcs::observability {

namespace {

    // 返回日志等级
spdlog::level::level_enum to_spdlog_level(LogLevel level) {
    switch (level) {
        case LogLevel::trace:
            return spdlog::level::trace;
        case LogLevel::debug:
            return spdlog::level::debug;
        case LogLevel::info:
            return spdlog::level::info;
        case LogLevel::warn:
            return spdlog::level::warn;
        case LogLevel::error:
            return spdlog::level::err;
        case LogLevel::critical:
            return spdlog::level::critical;
    }
    return spdlog::level::info;
}

    //
std::string sanitize_metric_name(std::string name) {
    for (auto& ch : name) {
        const bool valid = (ch >= 'a' && ch <= 'z') ||
                           (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') ||
                           ch == '_' ||
                           ch == ':';
        if (!valid) {
            ch = '_';
        }
    }
    return name;
}

std::string escape_label_value(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string labels_to_prometheus(const Labels& labels) {
    if (labels.empty()) {
        return {};
    }

    std::ostringstream oss;
    oss << '{';
    bool first = true;
    for (const auto& [key, value] : labels) {
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << sanitize_metric_name(key) << "=\"" << escape_label_value(value) << '"';
    }
    oss << '}';
    return oss.str();
}

std::string metric_help_or_default(const std::string& name, const std::string& help) {
    return help.empty() ? name : help;
}

} // namespace

TelemetryService::TelemetryService(TelemetryConfig config)
    : config_(std::move(config)) {
    std::lock_guard<std::mutex> lock(mutex_);
    configure_logger_locked();
}

const TelemetryConfig& TelemetryService::config() const noexcept {
    return config_;
}

void TelemetryService::configure(TelemetryConfig config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = std::move(config);
    configure_logger_locked();
}

void TelemetryService::log(const LogEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    write_log_locked(event);
}

void TelemetryService::trace(std::string category, std::string message, Fields fields) {
    log(LogEvent{LogLevel::trace, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::debug(std::string category, std::string message, Fields fields) {
    log(LogEvent{LogLevel::debug, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::info(std::string category, std::string message, Fields fields) {
    log(LogEvent{LogLevel::info, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::warn(std::string category, std::string message, Fields fields) {
    log(LogEvent{LogLevel::warn, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::error(std::string category, std::string message, Fields fields) {
    log(LogEvent{LogLevel::error, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::increment_counter(std::string name, double value, Labels labels, std::string help) {
    if (value < 0.0) {
        warn("observability", "counter increment ignored because value is negative", {{"metric", name}});
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metric_locked(MetricType::counter, name, labels, help);
    metric.value += value;
}

void TelemetryService::set_gauge(std::string name, double value, Labels labels, std::string help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metric_locked(MetricType::gauge, name, labels, help);
    metric.value = value;
}

void TelemetryService::add_gauge(std::string name, double delta, Labels labels, std::string help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metric_locked(MetricType::gauge, name, labels, help);
    metric.value += delta;
}

void TelemetryService::observe_histogram(std::string name, double value, Labels labels, std::string help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metric_locked(MetricType::histogram, name, labels, help);
    metric.sum += value;
    ++metric.count;

    for (std::size_t i = 0; i < metric.buckets.size(); ++i) {
        if (value <= metric.buckets[i]) {
            ++metric.bucket_counts[i];
        }
    }
}

TraceSpan TelemetryService::start_span(std::string name, Fields attributes, std::string parent_span_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    TraceSpan span;
    span.trace_id = config_.service_name + "-" + std::to_string(next_span_id_);
    span.span_id = std::to_string(next_span_id_++);
    span.parent_span_id = std::move(parent_span_id);
    span.name = std::move(name);
    span.attributes = std::move(attributes);
    span.started_at = std::chrono::steady_clock::now();
    return span;
}

void TelemetryService::finish_span(TraceSpan span) {
    span.ended_at = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(span.ended_at - span.started_at);

    Fields fields = span.attributes;
    fields["trace_id"] = span.trace_id;
    fields["span_id"] = span.span_id;
    fields["duration_us"] = std::to_string(duration.count());

    info("trace", "span finished: " + span.name, fields);
    observe_histogram("rcs_trace_span_duration_seconds",
                      static_cast<double>(duration.count()) / 1000000.0,
                      {{"span", span.name}},
                      "Trace span duration in seconds");
}

std::vector<MetricSnapshot> TelemetryService::collect() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<MetricSnapshot> snapshots;
    snapshots.reserve(metrics_.size());
    for (const auto& [_, metric] : metrics_) {
        snapshots.push_back(MetricSnapshot{
            metric.type,
            metric.name,
            metric.help,
            metric.labels,
            metric.value,
            metric.sum,
            metric.count,
            metric.buckets,
            metric.bucket_counts,
        });
    }

    std::sort(snapshots.begin(), snapshots.end(), [](const MetricSnapshot& lhs, const MetricSnapshot& rhs) {
        return lhs.name < rhs.name;
    });
    return snapshots;
}

std::string TelemetryService::export_prometheus() const {
    const auto snapshots = collect();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    for (const auto& metric : snapshots) {
        const auto name = sanitize_metric_name(metric.name);
        oss << "# HELP " << name << ' ' << metric_help_or_default(name, metric.help) << '\n';
        oss << "# TYPE " << name << ' ' << to_string(metric.type) << '\n';

        if (metric.type == MetricType::histogram) {
            for (std::size_t i = 0; i < metric.buckets.size(); ++i) {
                auto labels = metric.labels;
                labels["le"] = std::to_string(metric.buckets[i]);
                oss << name << "_bucket" << labels_to_prometheus(labels) << ' ' << metric.bucket_counts[i] << '\n';
            }

            auto labels = metric.labels;
            labels["le"] = "+Inf";
            oss << name << "_bucket" << labels_to_prometheus(labels) << ' ' << metric.count << '\n';
            oss << name << "_sum" << labels_to_prometheus(metric.labels) << ' ' << metric.sum << '\n';
            oss << name << "_count" << labels_to_prometheus(metric.labels) << ' ' << metric.count << '\n';
        } else {
            oss << name << labels_to_prometheus(metric.labels) << ' ' << metric.value << '\n';
        }

        oss << '\n';
    }

    return oss.str();
}

void TelemetryService::reset_metrics() {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.clear();
}

void TelemetryService::configure_logger_locked() const {
    if (!config_.enable_console_log) {
        spdlog::drop(config_.logger_name);
        return;
    }

    auto logger = spdlog::get(config_.logger_name);
    if (!logger) {
        logger = spdlog::stdout_color_mt(config_.logger_name);
    }

    logger->set_level(to_spdlog_level(config_.min_log_level));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
}

void TelemetryService::write_log_locked(const LogEvent& event) {
    if (to_spdlog_level(event.level) < to_spdlog_level(config_.min_log_level)) {
        return;
    }

    auto logger = spdlog::get(config_.logger_name);
    if (!logger) {
        return;
    }

    std::string message;
    if (config_.enable_json_log) {
        nlohmann::json payload;
        payload["service"] = config_.service_name;
        payload["level"] = to_string(event.level);
        payload["category"] = event.category;
        payload["message"] = event.message;
        payload["fields"] = event.fields;
        message = payload.dump();
    } else {
        message = "[" + event.category + "] " + event.message;
    }

    logger->log(to_spdlog_level(event.level), message);
}

TelemetryService::MetricState& TelemetryService::metric_locked(MetricType type,
                                                               const std::string& name,
                                                               const Labels& labels,
                                                               const std::string& help) {
    const auto key = metric_key(type, name, labels);
    auto it = metrics_.find(key);
    if (it != metrics_.end()) {
        return it->second;
    }

    MetricState state;
    state.type = type;
    state.name = name;
    state.help = help;
    state.labels = labels;
    if (type == MetricType::histogram) {
        state.buckets = config_.default_histogram_buckets;
        state.bucket_counts.assign(state.buckets.size(), 0);
    }

    auto [inserted, _] = metrics_.emplace(key, std::move(state));
    return inserted->second;
}

std::string TelemetryService::metric_key(MetricType type, const std::string& name, const Labels& labels) const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << ':' << name;
    for (const auto& [key, value] : labels) {
        oss << '|' << key << '=' << value;
    }
    return oss.str();
}

const char* to_string(LogLevel level) {
    switch (level) {
        case LogLevel::trace:
            return "trace";
        case LogLevel::debug:
            return "debug";
        case LogLevel::info:
            return "info";
        case LogLevel::warn:
            return "warn";
        case LogLevel::error:
            return "error";
        case LogLevel::critical:
            return "critical";
    }
    return "info";
}

const char* to_string(MetricType type) {
    switch (type) {
        case MetricType::counter:
            return "counter";
        case MetricType::gauge:
            return "gauge";
        case MetricType::histogram:
            return "histogram";
    }
    return "gauge";
}

} // namespace rcs::observability
