#include "rcs/observability/telemetry_service.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

namespace rcs::observability {

namespace {

spdlog::level::level_enum toSpdlogLevel(LogLevel level)
{
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

std::string sanitizeMetricName(std::string name)
{
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

std::string escapeLabelValue(const std::string& value)
{
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

std::string labelsToPrometheus(const Labels& labels)
{
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
        oss << sanitizeMetricName(key) << "=\"" << escapeLabelValue(value) << '"';
    }
    oss << '}';
    return oss.str();
}

std::string metricHelpOrDefault(const std::string& name, const std::string& help)
{
    return help.empty() ? name : help;
}

} // namespace

TelemetryService::TelemetryService(TelemetryConfig config)
    : config_(std::move(config))
{
    std::lock_guard<std::mutex> lock(mutex_);
    configureLoggerLocked();
}

const TelemetryConfig& TelemetryService::config() const noexcept
{
    return config_;
}

void TelemetryService::reConfigure(TelemetryConfig config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = std::move(config);
    configureLoggerLocked();
}

void TelemetryService::log(const LogEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    writeLogLocked(event);
}

void TelemetryService::trace(std::string category, std::string message, Fields fields)
{
    log(LogEvent{LogLevel::trace, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::debug(std::string category, std::string message, Fields fields)
{
    log(LogEvent{LogLevel::debug, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::info(std::string category, std::string message, Fields fields)
{
    log(LogEvent{LogLevel::info, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::warn(std::string category, std::string message, Fields fields)
{
    log(LogEvent{LogLevel::warn, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::error(std::string category, std::string message, Fields fields)
{
    log(LogEvent{LogLevel::error, std::move(category), std::move(message), std::move(fields)});
}

void TelemetryService::incrementCounter(std::string name, double value, Labels labels, std::string help)
{
    if (value < 0.0) {
        warn("observability", "counter increment ignored because value is negative", {{"metric", name}});
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metricLocked(MetricType::counter, name, labels, help);
    metric.value += value;
}

void TelemetryService::setGauge(std::string name, double value, Labels labels, std::string help)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metricLocked(MetricType::gauge, name, labels, help);
    metric.value = value;
}

void TelemetryService::addGauge(std::string name, double delta, Labels labels, std::string help)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metricLocked(MetricType::gauge, name, labels, help);
    metric.value += delta;
}

void TelemetryService::observeHistogram(std::string name, double value, Labels labels, std::string help)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& metric = metricLocked(MetricType::histogram, name, labels, help);
    metric.sum += value;
    ++metric.count;

    for (std::size_t i = 0; i < metric.buckets.size(); ++i) {
        if (value <= metric.buckets[i]) {
            ++metric.bucket_counts[i];
        }
    }
}

TraceSpan TelemetryService::startSpan(std::string name, Fields attributes, std::string parent_span_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    TraceSpan span;
    span.trace_id = config_.serviceName + "-" + std::to_string(next_span_id_);
    span.span_id = std::to_string(next_span_id_++);
    span.parent_span_id = std::move(parent_span_id);
    span.name = std::move(name);
    span.attributes = std::move(attributes);
    span.started_at = std::chrono::steady_clock::now();
    return span;
}

void TelemetryService::finishSpan(TraceSpan span)
{
    span.ended_at = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(span.ended_at - span.started_at);

    Fields fields = span.attributes;
    fields["trace_id"] = span.trace_id;
    fields["span_id"] = span.span_id;
    fields["duration_us"] = std::to_string(duration.count());

    info("trace", "span finished: " + span.name, fields);
    observeHistogram("rcs_trace_span_duration_seconds",
                     static_cast<double>(duration.count()) / 1000000.0,
                     {{"span", span.name}},
                     "Trace span duration in seconds");
}

std::vector<MetricSnapshot> TelemetryService::collect() const
{
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

std::string TelemetryService::exportPrometheus() const
{
    const auto snapshots = collect();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);

    for (const auto& metric : snapshots) {
        const auto name = sanitizeMetricName(metric.name);
        oss << "# HELP " << name << ' ' << metricHelpOrDefault(name, metric.help) << '\n';
        oss << "# TYPE " << name << ' ' << toString(metric.type) << '\n';

        if (metric.type == MetricType::histogram) {
            for (std::size_t i = 0; i < metric.buckets.size(); ++i) {
                auto labels = metric.labels;
                labels["le"] = std::to_string(metric.buckets[i]);
                oss << name << "_bucket" << labelsToPrometheus(labels) << ' ' << metric.bucket_counts[i] << '\n';
            }

            auto labels = metric.labels;
            labels["le"] = "+Inf";
            oss << name << "_bucket" << labelsToPrometheus(labels) << ' ' << metric.count << '\n';
            oss << name << "_sum" << labelsToPrometheus(metric.labels) << ' ' << metric.sum << '\n';
            oss << name << "_count" << labelsToPrometheus(metric.labels) << ' ' << metric.count << '\n';
        } else {
            oss << name << labelsToPrometheus(metric.labels) << ' ' << metric.value << '\n';
        }

        oss << '\n';
    }

    return oss.str();
}

void TelemetryService::resetMetrics()
{
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.clear();
}

void TelemetryService::configureLoggerLocked() const
{
    std::vector<spdlog::sink_ptr> sinks;
    if (config_.enable_console_log) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (config_.enable_file_log && !config_.file_path.empty()) {
        const std::filesystem::path log_path(config_.file_path);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path());
        }
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config_.file_path, true));
    }

    if (sinks.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
    }

    spdlog::drop(config_.logger_name);
    auto logger = std::make_shared<spdlog::logger>(config_.logger_name, sinks.begin(), sinks.end());
    logger->set_level(toSpdlogLevel(config_.min_log_level));
    logger->set_pattern(config_.pattern);
    logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(logger);
    spdlog::set_default_logger(logger);
    spdlog::set_level(toSpdlogLevel(config_.min_log_level));
}

void TelemetryService::writeLogLocked(const LogEvent& event)
{
    // 当前日志级别低于最小输出级别时直接忽略。
    if (toSpdlogLevel(event.level) < toSpdlogLevel(config_.min_log_level)) {
        return;
    }

    auto logger = spdlog::get(config_.logger_name);
    if (!logger) {
        return;
    }

    std::string message;
    if (config_.enable_json_log) {
        nlohmann::json payload;
        payload["service"] = config_.serviceName;
        payload["level"] = toString(event.level);
        payload["category"] = event.category;
        payload["message"] = event.message;
        payload["fields"] = event.fields;
        message = payload.dump();
    } else {
        message = "[" + event.category + "] " + event.message;
    }

    logger->log(toSpdlogLevel(event.level), message);
}

TelemetryService::MetricState& TelemetryService::metricLocked(MetricType type,
                                                              const std::string& name,
                                                              const Labels& labels,
                                                              const std::string& help)
{
    const auto key = metricKey(type, name, labels);
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

std::string TelemetryService::metricKey(MetricType type, const std::string& name, const Labels& labels) const
{
    std::ostringstream oss;
    oss << static_cast<int>(type) << ':' << name;
    for (const auto& [key, value] : labels) {
        oss << '|' << key << '=' << value;
    }
    return oss.str();
}

const char* toString(LogLevel level)
{
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

const char* toString(MetricType type)
{
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
