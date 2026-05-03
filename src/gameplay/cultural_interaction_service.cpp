#include "rcs/gameplay/cultural_interaction_service.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <utility>

namespace rcs::gameplay {
namespace {

bool containsPlayer(const room::RoomInfo& roomInfo, const std::string& playerId)
{
    return std::any_of(roomInfo.members.begin(), roomInfo.members.end(), [&](const room::RoomMember& member) {
        return member.player.player_id == playerId;
    });
}

std::string fallbackQuestion(const StartInteractionRequest& request)
{
    if (!request.topic.empty()) {
        return "请围绕《" + request.topic + "》说说你的观察和理解。";
    }
    return "请说说你在当前红色文化场景中的观察和理解。";
}

std::string fallbackExplanation(const SubmitAnswerRequest& request)
{
    return "讲解生成中。你的回答是：" + request.answer;
}

double estimateScore(const std::string& answer)
{
    // 当前先用简单启发式评分，后续可以替换为 AI 判分或题库标准答案。
    if (answer.size() >= 36) {
        return 1.0;
    }
    if (answer.size() >= 12) {
        return 0.7;
    }
    return 0.4;
}

} // namespace

CulturalInteractionService::CulturalInteractionService(
    std::shared_ptr<room::RoomMatchService> room_service,
    std::shared_ptr<ai_orchestrator::AiOrchestratorService> ai_service,
    std::shared_ptr<voice_tts::VoiceTtsService> tts_service,
    std::shared_ptr<storage::StorageService> storage_service,
    CulturalInteractionConfig config)
    : room_service_(std::move(room_service)),
      ai_service_(std::move(ai_service)),
      tts_service_(std::move(tts_service)),
      storage_service_(std::move(storage_service)),
      config_(std::move(config))
{
}

const CulturalInteractionConfig& CulturalInteractionService::config() const noexcept
{
    return config_;
}

StartInteractionResult CulturalInteractionService::startInteraction(const StartInteractionRequest& request)
{
    if (request.player_id.empty()) {
        return StartInteractionResult{false, "player_id is required"};
    }
    if (request.scene_id.empty()) {
        return StartInteractionResult{false, "scene_id is required"};
    }
    if (request.trigger_id.empty()) {
        return StartInteractionResult{false, "trigger_id is required"};
    }

    std::string room_error;
    if (!isRoomMember(request.room_id, request.player_id, room_error)) {
        return StartInteractionResult{false, room_error};
    }
    if (!config_.allow_without_storage && !storageAvailable()) {
        return StartInteractionResult{false, "storage is not connected"};
    }

    ai_orchestrator::AiContext ai_context;
    ai_context.room_id = request.room_id;
    ai_context.player_id = request.player_id;
    ai_context.scene_id = request.scene_id;
    ai_context.topic = request.topic.empty() ? "红色文化" : request.topic;
    ai_context.metadata = request.metadata;
    ai_context.metadata["trigger_id"] = request.trigger_id;

    auto flow_result = ai_service_->startQuestionFlow(ai_context, request.question_prompt_template);
    if (!flow_result.ok || !flow_result.flow) {
        return StartInteractionResult{false, flow_result.error.empty() ? "failed to start ai interaction" : flow_result.error};
    }

    ai_service_->tick(config_.ai_tick_batch_size);
    const auto latest_flow = ai_service_->findFlow(flow_result.flow->id).value_or(*flow_result.flow);

    InteractionState state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state.snapshot.interaction_id = next_interaction_id_++;
    }
    state.snapshot.flow_id = latest_flow.id;
    state.snapshot.player_id = request.player_id;
    state.snapshot.room_id = request.room_id;
    state.snapshot.scene_id = request.scene_id;
    state.snapshot.trigger_id = request.trigger_id;
    state.snapshot.topic = ai_context.topic;
    state.snapshot.question = latest_flow.generated_question.empty() ? fallbackQuestion(request) : latest_flow.generated_question;
    state.snapshot.completed = false;
    state.question_task_id = latest_flow.question_task_id;

    const auto storage_start = saveStartEvent(state);
    state.storage_interaction_id = storage_start.id;
    const auto storage_saved = storage_start.ok;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        interactions_[state.snapshot.interaction_id] = state;
        flow_to_interaction_[state.snapshot.flow_id] = state.snapshot.interaction_id;
    }

    StartInteractionResult result;
    result.ok = true;
    result.interaction_id = state.snapshot.interaction_id;
    result.flow_id = state.snapshot.flow_id;
    result.question_task_id = state.question_task_id;
    result.player_id = state.snapshot.player_id;
    result.room_id = state.snapshot.room_id;
    result.scene_id = state.snapshot.scene_id;
    result.trigger_id = state.snapshot.trigger_id;
    result.topic = state.snapshot.topic;
    result.question = state.snapshot.question;
    result.storage_saved = storage_saved;
    return result;
}

SubmitAnswerResult CulturalInteractionService::submitAnswer(const SubmitAnswerRequest& request)
{
    if (request.player_id.empty()) {
        return SubmitAnswerResult{false, "player_id is required"};
    }
    if (request.answer.empty()) {
        return SubmitAnswerResult{false, "answer is required"};
    }
    if (!config_.allow_without_storage && !storageAvailable()) {
        return SubmitAnswerResult{false, "storage is not connected"};
    }

    InteractionState state;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto interaction_id = request.interaction_id;
        if (interaction_id == 0 && request.flow_id != 0) {
            const auto flow_it = flow_to_interaction_.find(request.flow_id);
            if (flow_it != flow_to_interaction_.end()) {
                interaction_id = flow_it->second;
            }
        }

        const auto it = interactions_.find(interaction_id);
        if (it == interactions_.end()) {
            return SubmitAnswerResult{false, "interaction was not found"};
        }
        if (it->second.snapshot.player_id != request.player_id) {
            return SubmitAnswerResult{false, "player does not own this interaction"};
        }
        if (it->second.snapshot.completed) {
            return SubmitAnswerResult{false, "interaction was already completed"};
        }
        state = it->second;
    }

    auto flow_result = ai_service_->submitAnswer(state.snapshot.flow_id, request.answer);
    if (!flow_result.ok || !flow_result.flow) {
        return SubmitAnswerResult{false, flow_result.error.empty() ? "failed to submit answer" : flow_result.error};
    }

    ai_service_->tick(config_.ai_tick_batch_size);
    const auto latest_flow = ai_service_->findFlow(state.snapshot.flow_id).value_or(*flow_result.flow);

    state.snapshot.answer = request.answer;
    state.snapshot.explanation = latest_flow.generated_explanation.empty() ? fallbackExplanation(request) : latest_flow.generated_explanation;
    state.explanation_task_id = latest_flow.explanation_task_id;

    bool tts_cache_hit = false;
    if (config_.enable_tts && tts_service_) {
        voice_tts::TtsRequest tts_request;
        tts_request.text = state.snapshot.explanation;
        tts_request.player_id = state.snapshot.player_id;
        tts_request.purpose = "cultural_interaction_explanation";
        tts_request.metadata["interaction_id"] = std::to_string(state.snapshot.interaction_id);
        tts_request.metadata["flow_id"] = std::to_string(state.snapshot.flow_id);
        tts_request.metadata["scene_id"] = state.snapshot.scene_id;
        tts_request.metadata["trigger_id"] = state.snapshot.trigger_id;

        auto tts_submit = tts_service_->submit(std::move(tts_request));
        if (tts_submit.ok && tts_submit.task) {
            tts_cache_hit = tts_submit.cache_hit;
            state.tts_task_id = tts_submit.task->id;

            if (!tts_submit.cache_hit) {
                tts_service_->tick(config_.tts_tick_batch_size);
            }

            const auto latest_tts_task = tts_service_->findTask(state.tts_task_id).value_or(*tts_submit.task);
            if (latest_tts_task.audio) {
                state.snapshot.audio_id = latest_tts_task.audio->id;
                state.snapshot.audio_mime_type = latest_tts_task.audio->mime_type;
                state.audio_format = voice_tts::toString(latest_tts_task.audio->format);
                state.audio_byte_size = static_cast<std::int64_t>(latest_tts_task.audio->bytes.size());
                state.audio_duration_ms = static_cast<std::int64_t>(latest_tts_task.audio->duration.count());
            }
        }
    }

    state.snapshot.completed = true;
    const auto score = estimateScore(request.answer);
    const auto storage_saved = saveAnswerEvent(state, score);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        interactions_[state.snapshot.interaction_id] = state;
    }

    SubmitAnswerResult result;
    result.ok = true;
    result.interaction_id = state.snapshot.interaction_id;
    result.flow_id = state.snapshot.flow_id;
    result.explanation_task_id = state.explanation_task_id;
    result.tts_task_id = state.tts_task_id;
    result.player_id = state.snapshot.player_id;
    result.scene_id = state.snapshot.scene_id;
    result.trigger_id = state.snapshot.trigger_id;
    result.topic = state.snapshot.topic;
    result.question = state.snapshot.question;
    result.answer = state.snapshot.answer;
    result.explanation = state.snapshot.explanation;
    result.audio_id = state.snapshot.audio_id;
    result.audio_mime_type = state.snapshot.audio_mime_type;
    result.tts_cache_hit = tts_cache_hit;
    result.storage_saved = storage_saved;
    return result;
}

std::optional<InteractionSnapshot> CulturalInteractionService::findInteraction(InteractionId interaction_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = interactions_.find(interaction_id);
    if (it == interactions_.end()) {
        return std::nullopt;
    }
    return it->second.snapshot;
}

std::optional<InteractionSnapshot> CulturalInteractionService::findInteractionByFlow(
    ai_orchestrator::AiFlowId flow_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto flow_it = flow_to_interaction_.find(flow_id);
    if (flow_it == flow_to_interaction_.end()) {
        return std::nullopt;
    }

    const auto interaction_it = interactions_.find(flow_it->second);
    if (interaction_it == interactions_.end()) {
        return std::nullopt;
    }
    return interaction_it->second.snapshot;
}

bool CulturalInteractionService::isRoomMember(room::RoomId room_id,
                                                const std::string& player_id,
                                                std::string& error) const
{
    if (room_id == 0 || !config_.require_room_membership) {
        return true;
    }

    if (!room_service_) {
        error = "room service is not configured";
        return false;
    }

    const auto room_info = room_service_->findRoom(room_id);
    if (!room_info) {
        error = "room was not found";
        return false;
    }

    if (!containsPlayer(*room_info, player_id)) {
        error = "player is not in the room";
        return false;
    }

    return true;
}

storage::InsertResult CulturalInteractionService::saveStartEvent(const InteractionState& state) const
{
    if (!storageAvailable()) {
        return storage::InsertResult{false, "storage is not connected", 0};
    }

    storage::UserProfile profile;
    profile.player_id = state.snapshot.player_id;
    profile.account = state.snapshot.player_id;
    profile.display_name = state.snapshot.player_id;
    profile.metadata = {{"source", "gameplay"}};
    const auto user_saved = storage_service_->findUser(state.snapshot.player_id)
                                ? storage::StorageResult{true, {}}
                                : storage_service_->upsertUser(profile);
    if (!user_saved.ok) {
        return storage::InsertResult{false, user_saved.error, 0};
    }

    storage::CulturalInteractionRecord interaction;
    interaction.service_interaction_id = state.snapshot.interaction_id;
    interaction.player_id = state.snapshot.player_id;
    interaction.room_id = state.snapshot.room_id;
    interaction.scene_id = state.snapshot.scene_id;
    interaction.trigger_id = state.snapshot.trigger_id;
    interaction.interaction_type = "qa";
    interaction.ai_flow_id = state.snapshot.flow_id;
    interaction.topic = state.snapshot.topic;
    interaction.question = state.snapshot.question;
    interaction.status = "started";
    interaction.metadata = {
        {"question_task_id", state.question_task_id},
        {"source", "gameplay.startInteraction"},
    };
    const auto interaction_inserted = storage_service_->startCulturalInteraction(interaction);

    storage::EventLog event;
    event.level = interaction_inserted.ok ? "info" : "warn";
    event.category = "gameplay.interaction";
    event.message = "cultural interaction started";
    event.metadata = {
        {"interaction_id", state.snapshot.interaction_id},
        {"storage_interaction_id", interaction_inserted.id},
        {"flow_id", state.snapshot.flow_id},
        {"player_id", state.snapshot.player_id},
        {"room_id", state.snapshot.room_id},
        {"scene_id", state.snapshot.scene_id},
        {"trigger_id", state.snapshot.trigger_id},
        {"topic", state.snapshot.topic},
        {"interaction_saved", interaction_inserted.ok},
        {"storage_error", interaction_inserted.error},
    };
    storage_service_->appendEventLog(event);

    return interaction_inserted;
}

bool CulturalInteractionService::saveAnswerEvent(const InteractionState& state, double score) const
{
    if (!storageAvailable()) {
        return false;
    }

    storage::UserProfile profile;
    profile.player_id = state.snapshot.player_id;
    profile.account = state.snapshot.player_id;
    profile.display_name = state.snapshot.player_id;
    profile.metadata = {{"source", "gameplay"}};
    if (!storage_service_->findUser(state.snapshot.player_id)) {
        storage_service_->upsertUser(profile);
    }

    storage::AnswerRecord answer;
    answer.interaction_id = state.storage_interaction_id;
    answer.player_id = state.snapshot.player_id;
    answer.question_id = state.snapshot.trigger_id.empty()
                             ? std::to_string(state.snapshot.flow_id)
                             : state.snapshot.trigger_id;
    answer.question = state.snapshot.question;
    answer.answer = state.snapshot.answer;
    answer.correct = score >= 0.6;
    answer.score = score;
    answer.metadata = {
        {"interaction_id", state.snapshot.interaction_id},
        {"flow_id", state.snapshot.flow_id},
        {"room_id", state.snapshot.room_id},
        {"scene_id", state.snapshot.scene_id},
        {"topic", state.snapshot.topic},
        {"explanation", state.snapshot.explanation},
        {"audio_id", state.snapshot.audio_id},
    };
    const auto answer_inserted = storage_service_->appendAnswerRecord(answer);

    storage::CulturalInteractionRecord completed_interaction;
    completed_interaction.id = state.storage_interaction_id;
    completed_interaction.ai_flow_id = state.snapshot.flow_id;
    completed_interaction.answer = state.snapshot.answer;
    completed_interaction.explanation = state.snapshot.explanation;
    completed_interaction.audio_id = state.snapshot.audio_id;
    completed_interaction.status = "completed";
    completed_interaction.score = score;
    completed_interaction.metadata = {
        {"answer_record_id", answer_inserted.id},
        {"explanation_task_id", state.explanation_task_id},
        {"tts_task_id", state.tts_task_id},
    };
    const auto interaction_updated = state.storage_interaction_id > 0
                                         ? storage_service_->completeCulturalInteraction(completed_interaction)
                                         : storage::StorageResult{false, "interaction was not persisted at start"};

    storage::StorageResult tts_saved{true, {}};
    if (!state.snapshot.audio_id.empty()) {
        storage::TtsAudioResourceRecord audio;
        audio.audio_id = state.snapshot.audio_id;
        audio.player_id = state.snapshot.player_id;
        audio.interaction_id = state.storage_interaction_id;
        audio.mime_type = state.snapshot.audio_mime_type.empty() ? "audio/wav" : state.snapshot.audio_mime_type;
        audio.format = state.audio_format;
        audio.byte_size = state.audio_byte_size;
        audio.duration_ms = state.audio_duration_ms;
        audio.storage_type = "memory";
        audio.storage_uri = "/api/v1/tts/audio?audio_id=" + state.snapshot.audio_id;
        audio.metadata = {
            {"interaction_id", state.snapshot.interaction_id},
            {"flow_id", state.snapshot.flow_id},
            {"tts_task_id", state.tts_task_id},
            {"scene_id", state.snapshot.scene_id},
            {"trigger_id", state.snapshot.trigger_id},
        };
        tts_saved = storage_service_->saveTtsAudioResource(audio);
    }

    storage::ProgressRecord progress;
    progress.player_id = state.snapshot.player_id;
    progress.scene_id = state.snapshot.scene_id;
    progress.progress = {
        {"last_interaction_id", state.snapshot.interaction_id},
        {"last_trigger_id", state.snapshot.trigger_id},
        {"last_topic", state.snapshot.topic},
        {"last_score", score},
        {"completed_interaction", true},
    };
    const auto progress_saved = storage_service_->saveProgress(progress);

    storage::EventLog event;
    event.level = answer_inserted.ok && progress_saved.ok && interaction_updated.ok && tts_saved.ok ? "info" : "warn";
    event.category = "gameplay.interaction";
    event.message = "cultural interaction answered";
    event.metadata = {
        {"interaction_id", state.snapshot.interaction_id},
        {"storage_interaction_id", state.storage_interaction_id},
        {"flow_id", state.snapshot.flow_id},
        {"player_id", state.snapshot.player_id},
        {"score", score},
        {"interaction_updated", interaction_updated.ok},
        {"answer_saved", answer_inserted.ok},
        {"progress_saved", progress_saved.ok},
        {"tts_saved", tts_saved.ok},
        {"interaction_error", interaction_updated.error},
        {"answer_error", answer_inserted.error},
        {"progress_error", progress_saved.error},
        {"tts_error", tts_saved.error},
    };
    storage_service_->appendEventLog(event);

    return answer_inserted.ok && progress_saved.ok && interaction_updated.ok && tts_saved.ok;
}

bool CulturalInteractionService::storageAvailable() const
{
    return storage_service_ && storage_service_->isConnected();
}

} // namespace rcs::gameplay
