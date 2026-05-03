#include "redculture_server/api/controllers/tts_controller.hpp"

#include "redculture_server/api/http_utils.hpp"

#include <string>
#include <utility>

namespace rcs::api::controllers {
namespace {

void persistTtsAudioAccess(const std::shared_ptr<application::ServiceContext>& context,
                              const http::HttpRequest& request,
                              const std::string& audio_id,
                              bool found)
{
    if (!context->storage_service || !context->storage_service->isConnected()) {
        return;
    }

    storage::EventLog event;
    event.level = found ? "info" : "warn";
    event.category = "tts.audio";
    event.message = found ? "tts audio fetched" : "tts audio fetch failed";
    event.metadata = {
        {"audio_id", audio_id},
        {"found", found},
        {"remoteAddress", request.remoteAddress},
        {"remote_port", request.remote_port},
    };
    context->storage_service->appendEventLog(event);
}

} // namespace

TtsController::TtsController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void TtsController::registerRoutes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.get("/api/v1/tts/audio", [self](const http::HttpRequest& request) {
        return self->getAudio(request);
    });
    router.post("/api/v1/tts/audio", [self](const http::HttpRequest& request) {
        return self->getAudio(request);
    });
}

http::HttpResponse TtsController::getAudio(const http::HttpRequest& request)
{
    auto audio_id = support::queryValue(request, "audio_id");

    if (!audio_id || audio_id->empty()) {
        const auto parsed = support::parseJsonBody(request);
        if (parsed.ok()) {
            audio_id = support::readStringOr(parsed.data(), "audio_id");
        }
    }

    if (!audio_id || audio_id->empty()) {
        return RCS_API_ERROR_RESPONSE(400, 400, "audio_id is required");
    }

    const auto audio = context_->tts_service->findAudio(*audio_id);
    if (!audio) {
        persistTtsAudioAccess(context_, request, *audio_id, false);
        return RCS_API_ERROR_RESPONSE(404, 404, "audio resource was not found or expired");
    }

    persistTtsAudioAccess(context_, request, *audio_id, true);

    std::string bytes(audio->bytes.begin(), audio->bytes.end());
    auto response = http::HttpResponse::text(200, std::move(bytes), audio->mime_type);
    response.headers["Cache-Control"] = "private, max-age=1800";
    return response;
}

} // namespace rcs::api::controllers
