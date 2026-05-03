#include "redculture_server/api/controllers/interaction_controller.hpp"

#include "redculture_server/api/http_utils.hpp"

#include <utility>

namespace rcs::api::controllers {
namespace {

support::Json startResultToJson(const gameplay::StartInteractionResult& result)
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

support::Json answerResultToJson(const gameplay::SubmitAnswerResult& result)
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

void InteractionController::registerRoutes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.post("/api/v1/interactions/start", [self](const http::HttpRequest& request) {
        return self->startInteraction(request);
    });
    router.post("/api/v1/interactions/answer", [self](const http::HttpRequest& request) {
        return self->answerInteraction(request);
    });
}

http::HttpResponse InteractionController::startInteraction(const http::HttpRequest& request)
{
    const auto parsed = support::parseJsonBody(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolvePlayer(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    gameplay::StartInteractionRequest interaction;
    interaction.player_id = player.data();
    interaction.room_id = support::readUint64Or(body, "room_id", 0);
    interaction.scene_id = support::readStringOr(body, "scene_id");
    interaction.trigger_id = support::readStringOr(body, "trigger_id");
    interaction.topic = support::readStringOr(body, "topic", "红色文化");
    interaction.question_prompt_template = support::readStringOr(body, "question_prompt");

    if (body.contains("metadata") && body["metadata"].is_object()) {
        for (const auto& [key, value] : body["metadata"].items()) {
            if (value.is_string()) {
                interaction.metadata[key] = value.get<std::string>();
            }
        }
    }

    const auto started = context_->gameplay_service->startInteraction(interaction);
    if (!started.ok) {
        return RCS_API_ERROR_RESPONSE(400, 400, started.error.empty() ? "start interaction failed" : started.error);
    }

    return support::successResponse(startResultToJson(started), "start interaction success");
}

http::HttpResponse InteractionController::answerInteraction(const http::HttpRequest& request)
{
    const auto parsed = support::parseJsonBody(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolvePlayer(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    gameplay::SubmitAnswerRequest answer;
    answer.player_id = player.data();
    answer.interaction_id = support::readUint64Or(body, "interaction_id", 0);
    answer.flow_id = support::readUint64Or(body, "flow_id", 0);
    answer.answer = support::readStringOr(body, "answer");

    const auto submitted = context_->gameplay_service->submitAnswer(answer);
    if (!submitted.ok) {
        return RCS_API_ERROR_RESPONSE(400, 400, submitted.error.empty() ? "answer interaction failed" : submitted.error);
    }

    return support::successResponse(answerResultToJson(submitted), "answer interaction success");
}

} // namespace rcs::api::controllers
