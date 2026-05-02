#include "rcs/api/controllers/interaction_controller.hpp"

#include "rcs/api/http_utils.hpp"

#include <utility>

namespace rcs::api::controllers {
namespace {

support::Json start_result_to_json(const gameplay::StartInteractionResult& result)
{
    return support::Json{
        {"interaction_id", result.interaction_id},
        {"flow_id", result.flow_id},
        {"question_task_id", result.question_task_id},
        {"player_id", result.player_id},
        {"room_id", result.room_id},
        {"scene_id", result.scene_id},
        {"trigger_id", result.trigger_id},
        {"topic", result.topic},
        {"question", result.question},
        {"storage_saved", result.storage_saved},
    };
}

support::Json answer_result_to_json(const gameplay::SubmitAnswerResult& result)
{
    return support::Json{
        {"interaction_id", result.interaction_id},
        {"flow_id", result.flow_id},
        {"explanation_task_id", result.explanation_task_id},
        {"tts_task_id", result.tts_task_id},
        {"player_id", result.player_id},
        {"scene_id", result.scene_id},
        {"trigger_id", result.trigger_id},
        {"topic", result.topic},
        {"question", result.question},
        {"answer", result.answer},
        {"explanation", result.explanation},
        {"audio_id", result.audio_id},
        {"audio_mime_type", result.audio_mime_type},
        {"audio_url", result.audio_id.empty() ? "" : "/api/v1/tts/audio?audio_id=" + result.audio_id},
        {"tts_cache_hit", result.tts_cache_hit},
        {"storage_saved", result.storage_saved},
    };
}

} // namespace

InteractionController::InteractionController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void InteractionController::register_routes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.post("/api/v1/interactions/start", [self](const http::HttpRequest& request) {
        return self->start_interaction(request);
    });
    router.post("/api/v1/interactions/answer", [self](const http::HttpRequest& request) {
        return self->answer_interaction(request);
    });
}

http::HttpResponse InteractionController::start_interaction(const http::HttpRequest& request)
{
    const auto parsed = support::parse_json_body(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolve_player(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    gameplay::StartInteractionRequest interaction;
    interaction.player_id = player.data();
    interaction.room_id = support::read_uint64_or(body, "room_id", 0);
    interaction.scene_id = support::read_string_or(body, "scene_id");
    interaction.trigger_id = support::read_string_or(body, "trigger_id");
    interaction.topic = support::read_string_or(body, "topic", "红色文化");
    interaction.question_prompt_template = support::read_string_or(body, "question_prompt");

    if (body.contains("metadata") && body["metadata"].is_object()) {
        for (const auto& [key, value] : body["metadata"].items()) {
            if (value.is_string()) {
                interaction.metadata[key] = value.get<std::string>();
            }
        }
    }

    const auto started = context_->gameplay_service->start_interaction(interaction);
    if (!started.ok) {
        return RCS_API_ERROR_RESPONSE(400, 400, started.error.empty() ? "start interaction failed" : started.error);
    }

    return support::success_response(start_result_to_json(started), "start interaction success");
}

http::HttpResponse InteractionController::answer_interaction(const http::HttpRequest& request)
{
    const auto parsed = support::parse_json_body(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolve_player(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    gameplay::SubmitAnswerRequest answer;
    answer.player_id = player.data();
    answer.interaction_id = support::read_uint64_or(body, "interaction_id", 0);
    answer.flow_id = support::read_uint64_or(body, "flow_id", 0);
    answer.answer = support::read_string_or(body, "answer");

    const auto submitted = context_->gameplay_service->submit_answer(answer);
    if (!submitted.ok) {
        return RCS_API_ERROR_RESPONSE(400, 400, submitted.error.empty() ? "answer interaction failed" : submitted.error);
    }

    return support::success_response(answer_result_to_json(submitted), "answer interaction success");
}

} // namespace rcs::api::controllers
