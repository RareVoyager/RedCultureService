#pragma once

#include "rcs/ai_orchestrator/ai_orchestrator_service.hpp"
#include "rcs/room/room_match_service.hpp"
#include "rcs/storage/storage_service.hpp"
#include "rcs/voice_tts/voice_tts_service.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace rcs::gameplay {

using InteractionId = std::uint64_t;

struct CulturalInteractionConfig {
    // 开启后，带 room_id 的互动必须由房间成员发起，避免玩家操作不属于自己的房间。
    bool require_room_membership{true};

    // 示例阶段 AI/TTS 使用同步推进，方便 Unity 一次请求拿到题目或讲解。
    std::size_t ai_tick_batch_size{4};
    std::size_t tts_tick_batch_size{2};

    // 答题后是否自动生成讲解语音。
    bool enable_tts{true};

    // PostgreSQL 不可用时是否允许业务继续返回成功。本地开发建议 true，生产可改为 false。
    bool allow_without_storage{true};
};

struct StartInteractionRequest {
    std::string player_id;
    room::RoomId room_id{0};
    std::string scene_id;
    std::string trigger_id;
    std::string topic;
    std::string question_prompt_template;
    std::unordered_map<std::string, std::string> metadata;
};

struct StartInteractionResult {
    bool ok{false};
    std::string error;
    InteractionId interaction_id{0};
    ai_orchestrator::AiFlowId flow_id{0};
    ai_orchestrator::AiTaskId question_task_id{0};
    std::string player_id;
    room::RoomId room_id{0};
    std::string scene_id;
    std::string trigger_id;
    std::string topic;
    std::string question;
    bool storage_saved{false};
};

struct SubmitAnswerRequest {
    std::string player_id;
    InteractionId interaction_id{0};
    ai_orchestrator::AiFlowId flow_id{0};
    std::string answer;
};

struct SubmitAnswerResult {
    bool ok{false};
    std::string error;
    InteractionId interaction_id{0};
    ai_orchestrator::AiFlowId flow_id{0};
    ai_orchestrator::AiTaskId explanation_task_id{0};
    voice_tts::TtsTaskId tts_task_id{0};
    std::string player_id;
    std::string scene_id;
    std::string trigger_id;
    std::string topic;
    std::string question;
    std::string answer;
    std::string explanation;
    std::string audio_id;
    std::string audio_mime_type;
    bool tts_cache_hit{false};
    bool storage_saved{false};
};

struct InteractionSnapshot {
    InteractionId interaction_id{0};
    ai_orchestrator::AiFlowId flow_id{0};
    std::string player_id;
    room::RoomId room_id{0};
    std::string scene_id;
    std::string trigger_id;
    std::string topic;
    std::string question;
    std::string answer;
    std::string explanation;
    std::string audio_id;
    std::string audio_mime_type;
    bool completed{false};
};

class CulturalInteractionService {
public:
    CulturalInteractionService(std::shared_ptr<room::RoomMatchService> room_service,
                               std::shared_ptr<ai_orchestrator::AiOrchestratorService> ai_service,
                               std::shared_ptr<voice_tts::VoiceTtsService> tts_service,
                               std::shared_ptr<storage::StorageService> storage_service = nullptr,
                               CulturalInteractionConfig config = {});

    const CulturalInteractionConfig& config() const noexcept;

    // 玩家进入互动点时调用：校验房间成员身份，生成题目，并记录互动上下文。
    StartInteractionResult start_interaction(const StartInteractionRequest& request);

    // 玩家提交答案时调用：生成 AI 讲解、生成 TTS 资源，并尽力写入 PostgreSQL。
    SubmitAnswerResult submit_answer(const SubmitAnswerRequest& request);

    std::optional<InteractionSnapshot> find_interaction(InteractionId interaction_id) const;
    std::optional<InteractionSnapshot> find_interaction_by_flow(ai_orchestrator::AiFlowId flow_id) const;

private:
    struct InteractionState {
        InteractionSnapshot snapshot;
        ai_orchestrator::AiTaskId question_task_id{0};
        ai_orchestrator::AiTaskId explanation_task_id{0};
        voice_tts::TtsTaskId tts_task_id{0};
    };

    bool is_room_member(room::RoomId room_id, const std::string& player_id, std::string& error) const;
    bool save_start_event(const InteractionState& state) const;
    bool save_answer_event(const InteractionState& state, double score) const;
    bool storage_available() const;

    std::shared_ptr<room::RoomMatchService> room_service_;
    std::shared_ptr<ai_orchestrator::AiOrchestratorService> ai_service_;
    std::shared_ptr<voice_tts::VoiceTtsService> tts_service_;
    std::shared_ptr<storage::StorageService> storage_service_;
    CulturalInteractionConfig config_;

    mutable std::mutex mutex_;
    InteractionId next_interaction_id_{1};
    std::unordered_map<InteractionId, InteractionState> interactions_;
    std::unordered_map<ai_orchestrator::AiFlowId, InteractionId> flow_to_interaction_;
};

} // namespace rcs::gameplay
