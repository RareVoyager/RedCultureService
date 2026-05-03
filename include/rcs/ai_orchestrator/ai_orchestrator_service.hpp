#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rcs::ai_orchestrator {

using AiTaskId = std::uint64_t;
using AiFlowId = std::uint64_t;
using RoomId = std::uint64_t;

enum class TriggerType {
    manual = 0,
    enter_area = 1,
    answer_submitted = 2,
    progress_updated = 3,
};

enum class AiTaskKind {
    generate_question = 0,
    generate_explanation = 1,
    generate_hint = 2,
};

enum class AiTaskStatus {
    queued = 0,
    running = 1,
    succeeded = 2,
    failed = 3,
    timed_out = 4,
    canceled = 5,
};

enum class AiFlowStage {
    generating_question = 0,
    waiting_answer = 1,
    generating_explanation = 2,
    completed = 3,
    failed = 4,
};

struct AiContext {
    RoomId room_id{0};
    std::string player_id;
    std::string scene_id;
    std::string topic;
    std::string user_input;
    std::unordered_map<std::string, std::string> metadata;
};

struct TriggerEvent {
    TriggerType type{TriggerType::manual};
    AiContext context;
};

struct TriggerRule {
    std::string id;
    TriggerType type{TriggerType::manual};
    std::string scene_id;
    std::string topic;
    AiTaskKind task_kind{AiTaskKind::generate_question};
    std::string prompt_template;
    std::chrono::milliseconds cooldown{std::chrono::seconds(3)};
    bool enabled{true};
};

struct AiRequest {
    AiTaskId task_id{0};
    AiTaskKind kind{AiTaskKind::generate_question};
    AiContext context;
    std::string prompt;
    std::uint32_t attempt{0};
};

struct AiResponse {
    bool ok{false};
    std::string text;
    std::string error;
    std::unordered_map<std::string, std::string> metadata;
};

struct AiTask {
    AiTaskId id{0};
    std::optional<AiFlowId> flow_id;
    AiTaskKind kind{AiTaskKind::generate_question};
    AiTaskStatus status{AiTaskStatus::queued};
    AiContext context;
    std::string prompt_template;
    std::string rendered_prompt;
    std::uint32_t attempts{0};
    std::string last_error;
    std::optional<AiResponse> response;
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point updated_at{};
    std::chrono::steady_clock::time_point next_attempt_at{};
};

struct AiInteractionFlow {
    AiFlowId id{0};
    AiFlowStage stage{AiFlowStage::generating_question};
    AiContext context;
    AiTaskId question_task_id{0};
    AiTaskId explanation_task_id{0};
    std::string question_prompt_template;
    std::string explanation_prompt_template;
    std::string generated_question;
    std::string submitted_answer;
    std::string generated_explanation;
    std::string error;
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point updated_at{};
};

struct AiOrchestratorConfig {
    std::uint32_t max_retries{2};
    std::chrono::milliseconds request_timeout{std::chrono::seconds(8)};
    std::chrono::milliseconds retry_backoff{std::chrono::milliseconds(500)};
    std::size_t max_queued_tasks{1024};
    std::string default_question_prompt{
        "请围绕主??{topic} 为玩??{player_id} 生成一道互动题目。场景：{scene_id}??"
        };
    std::string default_explanation_prompt{
        "请根据题目和玩家答案给出讲解。题目：{metadata.question}。玩家答案：{user_input}??"
        };
};

struct FlowResult {
    bool ok{false};
    std::string error;
    std::optional<AiInteractionFlow> flow;
};

struct EnqueueResult {
    bool ok{false};
    std::string error;
    std::optional<AiTask> task;
};

struct TickResult {
    std::vector<AiTask> succeeded_tasks;
    std::vector<AiTask> retry_tasks;
    std::vector<AiTask> failed_tasks;
};

class IAiClient {
public:
    virtual ~IAiClient() = default;

    // 具体 AI 调用由 C++ 实现类完成，可以是 HTTP、gRPC 或本地 mock。
    virtual AiResponse complete(const AiRequest& request) = 0;
};

class AiOrchestratorService {
public:
    explicit AiOrchestratorService(std::shared_ptr<IAiClient> client = nullptr,
                                   AiOrchestratorConfig config = {});

    const AiOrchestratorConfig& config() const noexcept;
    void setClient(std::shared_ptr<IAiClient> client);

    bool registerTriggerRule(const TriggerRule& rule);
    bool removeTriggerRule(const std::string& rule_id);
    std::vector<AiTask> handleTriggerEvent(const TriggerEvent& event);

    FlowResult startQuestionFlow(AiContext context,
                                   std::string question_prompt_template = {},
                                   std::string explanation_prompt_template = {});
    FlowResult submitAnswer(AiFlowId flow_id, std::string answer);

    EnqueueResult enqueueTask(AiTaskKind kind, AiContext context, std::string prompt_template = {});
    bool cancelTask(AiTaskId task_id);

    TickResult tick(std::size_t max_tasks = 1);

    std::optional<AiTask> findTask(AiTaskId task_id) const;
    std::optional<AiInteractionFlow> findFlow(AiFlowId flow_id) const;
    std::vector<AiTask> listTasks() const;
    std::vector<AiInteractionFlow> listFlows() const;

    std::size_t queuedTaskCount() const;
    std::size_t flowCount() const;

private:
    struct RunningTask {
        AiTask task;
        AiRequest request;
    };

    EnqueueResult enqueueTaskLocked(AiTaskKind kind,
                                      AiContext context,
                                      std::string prompt_template,
                                      std::optional<AiFlowId> flow_id);
    std::optional<RunningTask> takeNextTaskLocked(std::chrono::steady_clock::time_point now);
    AiTask finishTaskLocked(const RunningTask& running,
                              AiResponse response,
                              std::chrono::milliseconds elapsed,
                              TickResult& result);
    void updateFlowAfterTaskLocked(const AiTask& task);
    bool ruleMatchesEvent(const TriggerRule& rule, const TriggerEvent& event) const;
    std::string renderPrompt(const std::string& prompt_template, const AiContext& context) const;
    std::string triggerCooldownKey(const TriggerRule& rule, const TriggerEvent& event) const;

    AiOrchestratorConfig config_;
    std::shared_ptr<IAiClient> client_;
    mutable std::mutex mutex_;
    AiTaskId next_task_id_{1};
    AiFlowId next_flow_id_{1};
    std::unordered_map<AiTaskId, AiTask> tasks_;
    std::unordered_map<AiFlowId, AiInteractionFlow> flows_;
    std::unordered_map<std::string, TriggerRule> trigger_rules_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_triggered_at_;
};

const char* toString(TriggerType type);
const char* toString(AiTaskKind kind);
const char* toString(AiTaskStatus status);
const char* toString(AiFlowStage stage);

} // namespace rcs::ai_orchestrator
