#include "rcs/api/controllers/tts_controller.hpp"

#include "rcs/api/http_utils.hpp"

#include <string>
#include <utility>

namespace rcs::api::controllers {

TtsController::TtsController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void TtsController::register_routes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.get("/api/v1/tts/audio", [self](const http::HttpRequest& request) {
        return self->get_audio(request);
    });
    router.post("/api/v1/tts/audio", [self](const http::HttpRequest& request) {
        return self->get_audio(request);
    });
}

http::HttpResponse TtsController::get_audio(const http::HttpRequest& request)
{
    auto audio_id = support::query_value(request, "audio_id");

    if (!audio_id || audio_id->empty()) {
        const auto parsed = support::parse_json_body(request);
        if (parsed.ok()) {
            audio_id = support::read_string_or(parsed.data(), "audio_id");
        }
    }

    if (!audio_id || audio_id->empty()) {
        return RCS_API_ERROR_RESPONSE(400, 400, "audio_id is required");
    }

    const auto audio = context_->tts_service->find_audio(*audio_id);
    if (!audio) {
        return RCS_API_ERROR_RESPONSE(404, 404, "audio resource was not found or expired");
    }

    std::string bytes(audio->bytes.begin(), audio->bytes.end());
    auto response = http::HttpResponse::text(200, std::move(bytes), audio->mime_type);
    response.headers["Cache-Control"] = "private, max-age=1800";
    return response;
}

} // namespace rcs::api::controllers
