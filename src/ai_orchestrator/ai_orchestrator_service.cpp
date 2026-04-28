#include "rcs/ai_orchestrator/ai_orchestrator_service.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace rcs::ai_orchestrator {

namespace {

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }

    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::uint32_t max_attempts_from_retries(std::uint32_t max_retries) {
    return max_retries + 1;
}

} // namespace

AiOrchestratorService::AiOrchestratorService(std::shared_ptr<IAiClient> client,
                                             AiOrchestratorConfig config)
    : config_(std::move(config)),
      client_(std::move(client)) {}

const AiOrchestratorConfig& AiOrchestratorService::config() const noexcept {
    return config_;
}

void AiOrchestratorService::set_client(std::shared_ptr<IAiClient> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_ = std::move(client);
}

bool AiOrchestratorService::register_trigger_rule(const TriggerRule& rule) {
    if (rule.id.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    trigger_rules_[rule.id] = rule;
    return true;
}

bool AiOrchestratorService::remove_trigger_rule(const std::string& rule_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return trigger_rules_.erase(rule_id) > 0;
}

std::vector<AiTask> AiOrchestratorService::handle_trigger_event(const TriggerEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AiTask> tasks;
    const auto now = std::chrono::steady_clock::now();

    for (const auto& [_, rule] : trigger_rules_) {
        if (!rule_matches_event(rule, event)) {
            continue;
        }

        const auto cooldown_key = trigger_cooldown_key(rule, event);
        const auto cooldown_it = last_triggered_at_.find(cooldown_key);
        if (cooldown_it != last_triggered_at_.end() && now - cooldown_it->second < rule.cooldown) {
            continue;
        }

        auto context = event.context;
        if (!rule.topic.empty()) {
            context.topic = rule.topic;
        }

        auto enqueue = enqueue_task_locked(rule.task_kind, std::move(context), rule.prompt_template, std::nullopt);
        if (enqueue.ok && enqueue.task) {
            last_triggered_at_[cooldown_key] = now;
            tasks.push_back(*enqueue.task);
        }
    }

    return tasks;
}

FlowResult AiOrchestratorService::start_question_flow(AiContext context,
                                                      std::string question_prompt_template,
                                                      std::string explanation_prompt_template) {
    std::lock_guard<std::mutex> lock(mutex_);

    AiInteractionFlow flow;
    flow.id = next_flow_id_++;
    flow.stage = AiFlowStage::generating_question;
    flow.context = std::move(context);
    flow.question_prompt_template = question_prompt_template.empty() ? config_.default_question_prompt
                                                                     : std::move(question_prompt_template);
    flow.explanation_prompt_template = explanation_prompt_template.empty() ? config_.default_explanation_prompt
                                                                          : std::move(explanation_prompt_template);
    flow.created_at = std::chrono::steady_clock::now();
    flow.updated_at = flow.created_at;

    auto enqueue = enqueue_task_locked(AiTaskKind::generate_question,
                                       flow.context,
                                       flow.question_prompt_template,
                                       flow.id);
    if (!enqueue.ok || !enqueue.task) {
        return FlowResult{false, enqueue.error, std::nullopt};
    }

    flow.question_task_id = enqueue.task->id;
    flows_[flow.id] = flow;
    return FlowResult{true, {}, flow};
}

FlowResult AiOrchestratorService::submit_answer(AiFlowId flow_id, std::string answer) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto flow_it = flows_.find(flow_id);
    if (flow_it == flows_.end()) {
        return FlowResult{false, "flow not found", std::nullopt};
    }

    auto& flow = flow_it->second;
    if (flow.stage != AiFlowStage::waiting_answer) {
        return FlowResult{false, "flow is not waiting for answer", flow};
    }

    flow.submitted_answer = std::move(answer);
    flow.context.user_input = flow.submitted_answer;
    flow.context.metadata["question"] = flow.generated_question;
    flow.stage = AiFlowStage::generating_explanation;
    flow.updated_at = std::chrono::steady_clock::now();

    auto enqueue = enqueue_task_locked(AiTaskKind::generate_explanation,
                                       flow.context,
                                       flow.explanation_prompt_template,
                                       flow.id);
    if (!enqueue.ok || !enqueue.task) {
        flow.stage = AiFlowStage::failed;
        flow.error = enqueue.error;
        return FlowResult{false, enqueue.error, flow};
    }

    flow.explanation_task_id = enqueue.task->id;
    return FlowResult{true, {}, flow};
}

EnqueueResult AiOrchestratorService::enqueue_task(AiTaskKind kind,
                                                  AiContext context,
                                                  std::string prompt_template) {
    std::lock_guard<std::mutex> lock(mutex_);
    return enqueue_task_locked(kind, std::move(context), std::move(prompt_template), std::nullopt);
}

bool AiOrchestratorService::cancel_task(AiTaskId task_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return false;
    }
    if (it->second.status == AiTaskStatus::succeeded || it->second.status == AiTaskStatus::failed) {
        return false;
    }

    it->second.status = AiTaskStatus::canceled;
    it->second.updated_at = std::chrono::steady_clock::now();
    return true;
}

TickResult AiOrchestratorService::tick(std::size_t max_tasks) {
    TickResult result;
    if (max_tasks == 0) {
        return result;
    }

    for (std::size_t i = 0; i < max_tasks; ++i) {
        std::shared_ptr<IAiClient> client;
        RunningTask running;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto maybe_task = take_next_task_locked(std::chrono::steady_clock::now());
            if (!maybe_task) {
                break;
            }
            running = *maybe_task;
            client = client_;
        }

        const auto started_at = std::chrono::steady_clock::now();
        AiResponse response;
        if (!client) {
            response.ok = false;
            response.error = "AI client is not configured";
        } else {
            response = client->complete(running.request);
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);

        std::lock_guard<std::mutex> lock(mutex_);
        finish_task_locked(running, std::move(response), elapsed, result);
    }

    return result;
}

std::optional<AiTask> AiOrchestratorService::find_task(AiTaskId task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<AiInteractionFlow> AiOrchestratorService::find_flow(AiFlowId flow_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = flows_.find(flow_id);
    if (it == flows_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<AiTask> AiOrchestratorService::list_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AiTask> tasks;
    tasks.reserve(tasks_.size());
    for (const auto& [_, task] : tasks_) {
        tasks.push_back(task);
    }
    return tasks;
}

std::vector<AiInteractionFlow> AiOrchestratorService::list_flows() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AiInteractionFlow> flows;
    flows.reserve(flows_.size());
    for (const auto& [_, flow] : flows_) {
        flows.push_back(flow);
    }
    return flows;
}

std::size_t AiOrchestratorService::queued_task_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::size_t>(std::count_if(tasks_.begin(), tasks_.end(), [](const auto& item) {
        return item.second.status == AiTaskStatus::queued;
    }));
}

std::size_t AiOrchestratorService::flow_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flows_.size();
}

EnqueueResult AiOrchestratorService::enqueue_task_locked(AiTaskKind kind,
                                                         AiContext context,
                                                         std::string prompt_template,
                                                         std::optional<AiFlowId> flow_id) {
    const auto active_task_count = static_cast<std::size_t>(std::count_if(tasks_.begin(), tasks_.end(), [](const auto& item) {
        return item.second.status == AiTaskStatus::queued || item.second.status == AiTaskStatus::running;
    }));

    if (active_task_count >= config_.max_queued_tasks) {
        return EnqueueResult{false, "AI task queue is full", std::nullopt};
    }

    if (prompt_template.empty()) {
        prompt_template = kind == AiTaskKind::generate_explanation ? config_.default_explanation_prompt
                                                                   : config_.default_question_prompt;
    }

    AiTask task;
    task.id = next_task_id_++;
    task.flow_id = flow_id;
    task.kind = kind;
    task.status = AiTaskStatus::queued;
    task.context = std::move(context);
    task.prompt_template = std::move(prompt_template);
    task.rendered_prompt = render_prompt(task.prompt_template, task.context);
    task.created_at = std::chrono::steady_clock::now();
    task.updated_at = task.created_at;
    task.next_attempt_at = task.created_at;

    tasks_[task.id] = task;
    return EnqueueResult{true, {}, task};
}

std::optional<AiOrchestratorService::RunningTask> AiOrchestratorService::take_next_task_locked(
    std::chrono::steady_clock::time_point now) {
    auto selected = tasks_.end();
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
        auto& task = it->second;
        if (task.status != AiTaskStatus::queued || task.next_attempt_at > now) {
            continue;
        }
        if (selected == tasks_.end() || task.created_at < selected->second.created_at) {
            selected = it;
        }
    }

    if (selected == tasks_.end()) {
        return std::nullopt;
    }

    auto& task = selected->second;
    task.status = AiTaskStatus::running;
    task.updated_at = now;
    ++task.attempts;

    AiRequest request;
    request.task_id = task.id;
    request.kind = task.kind;
    request.context = task.context;
    request.prompt = task.rendered_prompt;
    request.attempt = task.attempts;

    return RunningTask{task, request};
}

AiTask AiOrchestratorService::finish_task_locked(const RunningTask& running,
                                                 AiResponse response,
                                                 std::chrono::milliseconds elapsed,
                                                 TickResult& result) {
    auto& task = tasks_.at(running.task.id);
    const auto now = std::chrono::steady_clock::now();

    const bool timed_out = elapsed > config_.request_timeout;
    if (timed_out) {
        response.ok = false;
        response.error = "AI request timed out";
    }

    if (response.ok) {
        task.status = AiTaskStatus::succeeded;
        task.response = std::move(response);
        task.last_error.clear();
        task.updated_at = now;
        update_flow_after_task_locked(task);
        result.succeeded_tasks.push_back(task);
        return task;
    }

    task.last_error = response.error.empty() ? "AI request failed" : response.error;
    task.response = std::move(response);

    if (task.attempts < max_attempts_from_retries(config_.max_retries)) {
        task.status = AiTaskStatus::queued;
        task.next_attempt_at = now + config_.retry_backoff * task.attempts;
        task.updated_at = now;
        result.retry_tasks.push_back(task);
        return task;
    }

    task.status = timed_out ? AiTaskStatus::timed_out : AiTaskStatus::failed;
    task.updated_at = now;
    update_flow_after_task_locked(task);
    result.failed_tasks.push_back(task);
    return task;
}

void AiOrchestratorService::update_flow_after_task_locked(const AiTask& task) {
    if (!task.flow_id) {
        return;
    }

    const auto flow_it = flows_.find(*task.flow_id);
    if (flow_it == flows_.end()) {
        return;
    }

    auto& flow = flow_it->second;
    flow.updated_at = std::chrono::steady_clock::now();

    if (task.status == AiTaskStatus::succeeded && task.response) {
        if (task.kind == AiTaskKind::generate_question) {
            flow.generated_question = task.response->text;
            flow.stage = AiFlowStage::waiting_answer;
        } else if (task.kind == AiTaskKind::generate_explanation) {
            flow.generated_explanation = task.response->text;
            flow.stage = AiFlowStage::completed;
        }
        return;
    }

    if (task.status == AiTaskStatus::failed || task.status == AiTaskStatus::timed_out) {
        flow.stage = AiFlowStage::failed;
        flow.error = task.last_error;
    }
}

bool AiOrchestratorService::rule_matches_event(const TriggerRule& rule, const TriggerEvent& event) const {
    if (!rule.enabled || rule.type != event.type) {
        return false;
    }
    if (!rule.scene_id.empty() && rule.scene_id != event.context.scene_id) {
        return false;
    }
    return true;
}

std::string AiOrchestratorService::render_prompt(const std::string& prompt_template,
                                                 const AiContext& context) const {
    std::string prompt = prompt_template;
    replace_all(prompt, "{room_id}", std::to_string(context.room_id));
    replace_all(prompt, "{player_id}", context.player_id);
    replace_all(prompt, "{scene_id}", context.scene_id);
    replace_all(prompt, "{topic}", context.topic);
    replace_all(prompt, "{user_input}", context.user_input);

    for (const auto& [key, value] : context.metadata) {
        replace_all(prompt, "{metadata." + key + "}", value);
    }

    return prompt;
}

std::string AiOrchestratorService::trigger_cooldown_key(const TriggerRule& rule,
                                                        const TriggerEvent& event) const {
    std::ostringstream oss;
    oss << rule.id << ':' << event.context.room_id << ':' << event.context.player_id << ':' << event.context.scene_id;
    return oss.str();
}

const char* to_string(TriggerType type) {
    switch (type) {
        case TriggerType::manual:
            return "manual";
        case TriggerType::enter_area:
            return "enter_area";
        case TriggerType::answer_submitted:
            return "answer_submitted";
        case TriggerType::progress_updated:
            return "progress_updated";
    }
    return "unknown";
}

const char* to_string(AiTaskKind kind) {
    switch (kind) {
        case AiTaskKind::generate_question:
            return "generate_question";
        case AiTaskKind::generate_explanation:
            return "generate_explanation";
        case AiTaskKind::generate_hint:
            return "generate_hint";
    }
    return "unknown";
}

const char* to_string(AiTaskStatus status) {
    switch (status) {
        case AiTaskStatus::queued:
            return "queued";
        case AiTaskStatus::running:
            return "running";
        case AiTaskStatus::succeeded:
            return "succeeded";
        case AiTaskStatus::failed:
            return "failed";
        case AiTaskStatus::timed_out:
            return "timed_out";
        case AiTaskStatus::canceled:
            return "canceled";
    }
    return "unknown";
}

const char* to_string(AiFlowStage stage) {
    switch (stage) {
        case AiFlowStage::generating_question:
            return "generating_question";
        case AiFlowStage::waiting_answer:
            return "waiting_answer";
        case AiFlowStage::generating_explanation:
            return "generating_explanation";
        case AiFlowStage::completed:
            return "completed";
        case AiFlowStage::failed:
            return "failed";
    }
    return "unknown";
}

} // namespace rcs::ai_orchestrator
