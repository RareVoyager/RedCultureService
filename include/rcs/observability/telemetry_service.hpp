#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::observability {

    /**
     * @brief 日志等级枚举
     *
     * 日志等级从低到高依次为：
     *
     * trace    : 最详细的跟踪信息，一般用于排查非常细节的问题
     * debug    : 调试信息，开发阶段常用
     * info     : 普通运行信息，例如服务启动、请求到达
     * warn     : 警告信息，程序还能运行，但可能存在风险
     * error    : 错误信息，某个操作失败
     * critical : 严重错误，可能导致服务不可用
     */
enum class LogLevel {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    error = 4,
    critical = 5,
};

    /**
     * @brief 指标类型
     *
     * counter   : 计数器，只能增加，不能减少，例如请求总数、错误总数
     * gauge     : 仪表盘数值，可以增加也可以减少，例如当前连接数、CPU 使用率
     * histogram : 直方图，用于统计数据分布，例如请求耗时分布
     */
enum class MetricType {
    counter = 0,
    gauge = 1,
    histogram = 2,
};

    /**
     * @brief 可观测性模块的配置项
     *
     * 这个结构体用于控制日志、指标、追踪等功能的基础行为。
     */
struct TelemetryConfig {
    /**
     * @brief 服务名称
     */
    std::string service_name{"red_culture_service"};

    /**
     * @brief spdlog 中使用的 logger 名称
     */
    std::string logger_name{"redculture"};

    /**
     * @brief 最小日志等级
     * 低于该等级的日志会被过滤。
     */
    LogLevel min_log_level{LogLevel::info};

    /**
     * @brief 是否启用控制台日志(true 启用，false 禁用)
     */
    bool enable_console_log{true};

    /**
     * @brief 是否启用 JSON 格式日志(true 启用，false 禁用)
     */
    bool enable_json_log{true};

    /**
     * @brief 默认 histogram 桶配置
     *
     * 这些值通常用于统计耗时分布，单位一般是秒。
     * 如果某次请求耗时为 0.08 秒，那么它会被计入 <= 0.1 的桶中，同时也会计入后面更大的桶中。
     */
    std::vector<double> default_histogram_buckets{
        0.005, 0.01, 0.025, 0.05, 0.1,
        0.25, 0.5, 1.0, 2.5, 5.0
    };
};

    /**
     * @brief 指标标签类型
     *
     * 使用 std::map 是比较合适的，因为 map 会按照 key 排序。
     * 这样同一组 labels 即使插入顺序不同，也能生成稳定的 metric key。
     */
using Labels = std::map<std::string, std::string>;

    /**
     * @brief 日志字段类型
     * 用于给日志附加额外信息。
     * 这里使用 unordered_map，因为日志字段一般不需要稳定排序。
     */
using Fields = std::unordered_map<std::string, std::string>;

    /**
     * @brief 单条日志事件
     * 调用 log() 时，通常会传入这个结构体。
     */
struct LogEvent {
    /**
     * @brief 日志等级
     */
    LogLevel level{LogLevel::info};

    /**
     * @brief 日志分类
     */
    std::string category;

    /**
     * @brief 日志正文
     */
    std::string message;

    /**
     * @brief 日志附加字段
     */
    Fields fields;
};

    /**
     * @brief 指标快照
     */
struct MetricSnapshot {
    /**
     * @brief 指标类型
     */
    MetricType type{MetricType::counter};

    /**
     * @brief 指标名称
     */
    std::string name;

    /**
     * @brief 指标说明
     * Prometheus 导出时会用于 HELP 注释。
     */
    std::string help;

    /**
     * @brief 指标标签
     *
     * 同一个指标名可以通过不同标签区分不同维度。
     */
    Labels labels;

    /**
     * @brief counter 或 gauge 的当前值
     * 对于 histogram，这个字段通常不使用。
     */
    double value{0.0};

    /**
     * @brief histogram 观测值总和
     *
     * 例如记录了三次请求耗时：
     * 0.1、0.2、0.3
     *
     * 那么 sum = 0.6
     */
    double sum{0.0};

    /**
     * @brief histogram 观测次数
     *
     * 例如记录了三次请求耗时：
     * 0.1、0.2、0.3
     *
     * 那么 count = 3
     */
    std::uint64_t count{0};

    /**
     * @brief histogram 的桶边界
     */
    std::vector<double> buckets;

    /**
     * @brief 每个 histogram 桶对应的计数
     *
     * bucket_counts[i] 对应 buckets[i]。
     */
    std::vector<std::uint64_t> bucket_counts;
};

    /**
     * @brief 简单的追踪 Span
     */
struct TraceSpan {
    /**
     * @brief trace ID
     *
     * 一条完整调用链应该共享同一个 trace_id。
     */
    std::string trace_id;

    /**
     * @brief 当前 span 的 ID
     */
    std::string span_id;

    /**
     * @brief 父 span 的 ID
     *
     * 如果当前 span 是某个操作的子操作，可以通过 parent_span_id 建立关系。
     */
    std::string parent_span_id;

    /**
     * @brief span 名称
     *
     * 示例：
     * "http_request"
     * "db_query"
     * "load_user"
     */
    std::string name;

    /**
     * @brief span 附加属性
     *
     * 示例：
     * {
     *     {"method", "GET"},
     *     {"path", "/api/users"}
     * }
     */
    Fields attributes;

    /**
     * @brief span 开始时间
     *
     * 使用 steady_clock 是合适的，因为它适合计算时间间隔。
     */
    std::chrono::steady_clock::time_point started_at{};

    /**
     * @brief span 结束时间
     */
    std::chrono::steady_clock::time_point ended_at{};
};

    /**
     * @brief 可观测性服务类
     *
     * 这个类统一封装了：
     *
     * 1. 日志输出
     * 2. 指标记录
     * 3. Prometheus 指标导出
     * 4. 简单链路追踪
     *
     * 它是你项目中 observability 模块的核心类。
     */
class TelemetryService {
public:
    /**
     * @brief 构造函数
     *
     * 创建 TelemetryService 时可以传入配置。
     * 如果不传，则使用 TelemetryConfig 的默认配置。
     */
    explicit TelemetryService(TelemetryConfig config = {});

    /**
     * @brief 获取当前配置
     *
     * 注意：
     * 当前返回的是引用。
     * 如果多线程环境下会调用 configure() 修改配置，
     * 这里返回引用可能存在并发风险。
     */
    const TelemetryConfig& config() const noexcept;

    /**
     * @brief 重新配置 TelemetryService
     *
     * 通常会重新配置日志等级、日志格式等。
     */
    void configure(TelemetryConfig config);

    /**
     * @brief 写入一条日志事件
     */
    void log(const LogEvent& event);

    /**
     * @brief 输出 trace 级别日志
     */
    void trace(std::string category, std::string message, Fields fields = {});

    /**
     * @brief 输出 debug 级别日志
     */
    void debug(std::string category, std::string message, Fields fields = {});

    /**
     * @brief 输出 info 级别日志
     */
    void info(std::string category, std::string message, Fields fields = {});

    /**
     * @brief 输出 warn 级别日志
     */
    void warn(std::string category, std::string message, Fields fields = {});

    /**
     * @brief 输出 error 级别日志
     */
    void error(std::string category, std::string message, Fields fields = {});

    /**
     * @brief 增加 counter 指标
     *
     * counter 只能增加，不能减少。
     *
     * 示例：
     * increment_counter("http_requests_total");
     * increment_counter("http_errors_total", 1, {{"status", "500"}});
     */
    void increment_counter(
        std::string name,
        double value = 1.0,
        Labels labels = {},
        std::string help = {}
    );

    /**
     * @brief 设置 gauge 指标的值
     *
     * gauge 可以增加、减少，也可以直接设置。
     *
     * 示例：
     * set_gauge("active_connections", 10);
     */
    void set_gauge(
        std::string name,
        double value,
        Labels labels = {},
        std::string help = {}
    );

    /**
     * @brief 增加或减少 gauge 指标
     *
     * delta 可以是正数，也可以是负数。
     *
     * 示例：
     * add_gauge("active_connections", 1);
     * add_gauge("active_connections", -1);
     */
    void add_gauge(
        std::string name,
        double delta,
        Labels labels = {},
        std::string help = {}
    );

    /**
     * @brief 记录一次 histogram 观测值
     *
     * 常用于统计请求耗时、数据库查询耗时等。
     *
     * 示例：
     * observe_histogram("http_request_duration_seconds", 0.123);
     */
    void observe_histogram(
        std::string name,
        double value,
        Labels labels = {},
        std::string help = {}
    );

    /**
     * @brief 开始一个 trace span
     *
     * 返回 TraceSpan 对象，后续需要调用 finish_span() 结束它。
     *
     * 示例：
     *
     * auto span = telemetry.start_span("db_query");
     * // 执行数据库查询
     * telemetry.finish_span(span);
     */
    TraceSpan start_span(
        std::string name,
        Fields attributes = {},
        std::string parent_span_id = {}
    );

    /**
     * @brief 结束一个 trace span
     *
     * 会计算 span 耗时，并记录日志和 histogram 指标。
     */
    void finish_span(TraceSpan span);

    /**
     * @brief 收集当前所有指标快照
     *
     * 返回的是当前 metrics_ 中所有指标的拷贝。
     */
    std::vector<MetricSnapshot> collect() const;

    /**
     * @brief 以 Prometheus 文本格式导出指标
     *
     * 通常可以挂到 HTTP 接口上，例如：
     *
     * GET /metrics
     *
     * 返回 export_prometheus() 的结果。
     */
    std::string export_prometheus() const;

    /**
     * @brief 清空所有指标
     *
     * 一般测试时比较有用。
     * 生产环境中需要谨慎调用。
     */
    void reset_metrics();

private:
    /**
     * @brief 内部保存的指标状态
     *
     * MetricState 是真实存储在 TelemetryService 内部的指标数据。
     * MetricSnapshot 是对外返回的指标快照。
     */
    struct MetricState {
        /**
         * @brief 指标类型
         */
        MetricType type{MetricType::counter};

        /**
         * @brief 指标名称
         */
        std::string name;

        /**
         * @brief 指标说明
         */
        std::string help;

        /**
         * @brief 指标标签
         */
        Labels labels;

        /**
         * @brief counter 或 gauge 的值
         */
        double value{0.0};

        /**
         * @brief histogram 的观测值总和
         */
        double sum{0.0};

        /**
         * @brief histogram 的观测次数
         */
        std::uint64_t count{0};

        /**
         * @brief histogram 桶边界
         */
        std::vector<double> buckets;

        /**
         * @brief histogram 每个桶对应的数量
         */
        std::vector<std::uint64_t> bucket_counts;
    };

    /**
     * @brief 配置 logger
     *
     * 函数名带 locked 表示：
     * 调用该函数前，外部应该已经持有 mutex_ 锁。
     *
     * 这里被声明为 const，但它内部可能会修改 spdlog 的全局状态。
     */
    void configure_logger_locked() const;

    /**
     * @brief 实际写日志
     *
     * 函数名带 locked 表示：
     * 调用该函数前，外部应该已经持有 mutex_ 锁。
     */
    void write_log_locked(const LogEvent& event);

    /**
     * @brief 获取或创建一个指标
     *
     * 如果指定的指标已经存在，返回已有指标。
     * 如果不存在，则创建一个新的 MetricState。
     */
    MetricState& metric_locked(
        MetricType type,
        const std::string& name,
        const Labels& labels,
        const std::string& help
    );

    /**
     * @brief 生成指标唯一 key
     *
     * 用 type + name + labels 生成唯一字符串。
     *
     * 例如：
     * counter:http_requests_total|method=GET|path=/api/users
     */
    std::string metric_key(
        MetricType type,
        const std::string& name,
        const Labels& labels
    ) const;

    /**
     * @brief 当前配置
     */
    TelemetryConfig config_;

    /**
     * @brief 互斥锁
     *
     * 用于保护：
     * - config_
     * - metrics_
     * - next_span_id_
     *
     * mutable 表示即使在 const 成员函数中，也允许对 mutex_ 加锁。
     * 例如 collect() const 里面需要加锁读取 metrics_。
     */
    mutable std::mutex mutex_;

    /**
     * @brief 保存所有指标
     *
     * key   : metric_key() 生成的唯一字符串
     * value : 指标的内部状态
     */
    std::unordered_map<std::string, MetricState> metrics_;

    /**
     * @brief 下一个 span ID
     *
     * start_span() 每调用一次，这个值通常会递增。
     */
    std::uint64_t next_span_id_{1};
};

/**
 * @brief 将 LogLevel 转成字符串
 *
 * 示例：
 * LogLevel::info -> "info"
 */
const char* to_string(LogLevel level);

/**
 * @brief 将 MetricType 转成字符串
 *
 * 示例：
 * MetricType::counter -> "counter"
 */
const char* to_string(MetricType type);

} // namespace rcs::observability