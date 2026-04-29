#include "rcs/application/service_application.hpp"

#include "rcs/application/api_routes.hpp"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rcs::application {
namespace {

class LocalAiClient final : public ai_orchestrator::IAiClient {
public:
    ai_orchestrator::AiResponse complete(const ai_orchestrator::AiRequest& request) override
    {
        ai_orchestrator::AiResponse response;
        response.ok = true;

        std::ostringstream builder;
        if (request.kind == ai_orchestrator::AiTaskKind::generate_question) {
            builder << "请围绕「" << request.context.topic << "」回答一个互动问题："
                    << "你在场景「" << request.context.scene_id << "」里观察到了什么红色文化元素？";
        } else if (request.kind == ai_orchestrator::AiTaskKind::generate_explanation) {
            builder << "讲解：玩家回答「" << request.context.user_input << "」。"
                    << "可以继续补充历史背景、人物精神和现实启发。";
        } else {
            builder << "提示：先观察场景中的文字、人物、建筑和任务目标，再组织答案。";
        }

        response.text = builder.str();
        response.metadata["provider"] = "local_mock";
        return response;
    }
};

class LocalTtsClient final : public voice_tts::ITtsClient {
public:
    voice_tts::TtsProviderResponse synthesize(const voice_tts::TtsProviderRequest& request) override
    {
        voice_tts::TtsProviderResponse response;
        response.ok = true;
        response.format = voice_tts::AudioFormat::mp3;
        response.mime_type = voice_tts::mime_type(response.format);

        const auto text = "FAKE_MP3_AUDIO:" + request.request.text;
        response.bytes.assign(text.begin(), text.end());
        response.duration = std::chrono::milliseconds(static_cast<int>(request.request.text.size()) * 80);
        response.metadata["provider"] = "local_mock";
        return response;
    }
};

ops::ComponentHealth make_health(std::string component, bool healthy, std::string message)
{
    ops::ComponentHealth health;
    health.component = std::move(component);
    health.healthy = healthy;
    health.message = std::move(message);
    health.checked_at = std::chrono::system_clock::now();
    return health;
}

} // namespace

ServiceApplication::ServiceApplication(ApplicationConfig config)
    : context_(std::make_shared<ServiceContext>()),
      router_(std::make_shared<http::HttpRouter>())
{
    context_->config = std::move(config);
    context_->auth_service = std::make_shared<auth::SessionAuthService>(context_->config.auth);
    context_->room_service = std::make_shared<room::RoomMatchService>();
    context_->ai_service = std::make_shared<ai_orchestrator::AiOrchestratorService>(std::make_shared<LocalAiClient>());
    context_->tts_service = std::make_shared<voice_tts::VoiceTtsService>(std::make_shared<LocalTtsClient>());
    if (context_->config.enable_storage) {
        context_->storage_service = std::make_shared<storage::StorageService>(context_->config.storage);
    }
    context_->gameplay_service = std::make_shared<gameplay::CulturalInteractionService>(
        context_->room_service,
        context_->ai_service,
        context_->tts_service,
        context_->storage_service,
        context_->config.gameplay);
    context_->ops_service = std::make_shared<ops::OpsService>(context_->config.ops);

    context_->ops_service->register_health_check("auth", [auth_service = context_->auth_service] {
        return make_health("auth", true, "sessions=" + std::to_string(auth_service->session_count()));
    });

    context_->ops_service->register_health_check("room", [room_service = context_->room_service] {
        return make_health("room", true, "rooms=" + std::to_string(room_service->room_count(false)));
    });

    context_->ops_service->register_health_check("ai_orchestrator", [ai_service = context_->ai_service] {
        return make_health("ai_orchestrator", true, "queued_tasks=" + std::to_string(ai_service->queued_task_count()));
    });

    context_->ops_service->register_health_check("tts", [tts_service = context_->tts_service] {
        return make_health("tts", true, "queued_tasks=" + std::to_string(tts_service->queued_task_count()));
    });

    context_->ops_service->register_health_check("storage", [storage_service = context_->storage_service] {
        if (!storage_service) {
            return make_health("storage", true, "disabled");
        }
        return make_health("storage", storage_service->is_connected(), storage_service->is_connected() ? "connected" : "disconnected");
    });

    context_->ops_service->set_metrics_exporter([context = context_] {
        std::ostringstream metrics;
        metrics << "# TYPE rcs_auth_sessions gauge\n";
        metrics << "rcs_auth_sessions " << context->auth_service->session_count() << "\n";
        metrics << "# TYPE rcs_rooms gauge\n";
        metrics << "rcs_rooms " << context->room_service->room_count(false) << "\n";
        metrics << "# TYPE rcs_ai_queued_tasks gauge\n";
        metrics << "rcs_ai_queued_tasks " << context->ai_service->queued_task_count() << "\n";
        metrics << "# TYPE rcs_tts_queued_tasks gauge\n";
        metrics << "rcs_tts_queued_tasks " << context->tts_service->queued_task_count() << "\n";
        return metrics.str();
    });

    register_api_routes(*router_, context_);
    http_server_ = std::make_unique<http::HttpServer>(context_->config.http, router_);
}

ServiceApplication::~ServiceApplication()
{
    stop();
}

void ServiceApplication::start()
{
    context_->ops_service->start();
    if (context_->storage_service) {
        const auto result = context_->storage_service->connect();
        if (!result.ok && !context_->config.gameplay.allow_without_storage) {
            throw std::runtime_error("storage connect failed: " + result.error);
        }
    }
    http_server_->start();
    context_->ops_service->set_ready(true);
}

void ServiceApplication::stop()
{
    if (http_server_ && http_server_->is_running()) {
        context_->ops_service->begin_shutdown("application stop requested");
        http_server_->stop();
        context_->ops_service->complete_shutdown();
    }

    if (context_->storage_service) {
        context_->storage_service->disconnect();
    }
}

std::shared_ptr<ServiceContext> ServiceApplication::context() const
{
    return context_;
}

std::shared_ptr<http::HttpRouter> ServiceApplication::router() const
{
    return router_;
}

bool ServiceApplication::is_running() const
{
    return http_server_ && http_server_->is_running();
}

} // namespace rcs::application
