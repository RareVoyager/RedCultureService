#include "redculture_server/application/service_application.hpp"

#include "redculture_server/api/server_routes.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
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
            builder << "请围绕《" << request.context.topic << "》提出一个互动问题："
                    << "你在场景《" << request.context.scene_id << "》里观察到了什么红色文化元素？";
        } else if (request.kind == ai_orchestrator::AiTaskKind::generate_explanation) {
            builder << "讲解：玩家回答《" << request.context.user_input << "》。"
                    << "可以继续补充历史背景、人物精神和现实启发。";
        } else {
            builder << "提示：先观察场景中的文字、人物、建筑和任务目标，再组织答案。";
        }

        response.text = builder.str();
        response.metadata["provider"] = "local_mock";
        return response;
    }
};

void appendAscii(voice_tts::AudioBytes& bytes, const char* value)
{
    while (*value != '\0') {
        bytes.push_back(static_cast<std::uint8_t>(*value));
        ++value;
    }
}

void appendLe16(voice_tts::AudioBytes& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void appendLe32(voice_tts::AudioBytes& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

voice_tts::AudioBytes makeLocalMockWav(std::chrono::milliseconds duration)
{
    constexpr std::uint32_t sample_rate = 16000;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint16_t block_align = channels * bits_per_sample / 8;
    constexpr std::uint32_t byte_rate = sample_rate * block_align;
    constexpr std::uint32_t frequency = 880;

    const auto sample_count = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(sample_rate) * static_cast<std::uint64_t>(duration.count())) / 1000ULL);
    const auto data_size = sample_count * block_align;

    voice_tts::AudioBytes bytes;
    bytes.reserve(44 + data_size);

    appendAscii(bytes, "RIFF");
    appendLe32(bytes, 36U + data_size);
    appendAscii(bytes, "WAVE");
    appendAscii(bytes, "fmt ");
    appendLe32(bytes, 16);
    appendLe16(bytes, 1);
    appendLe16(bytes, channels);
    appendLe32(bytes, sample_rate);
    appendLe32(bytes, byte_rate);
    appendLe16(bytes, block_align);
    appendLe16(bytes, bits_per_sample);
    appendAscii(bytes, "data");
    appendLe32(bytes, data_size);

    const auto half_period = std::max<std::uint32_t>(1, sample_rate / frequency / 2U);
    for (std::uint32_t i = 0; i < sample_count; ++i) {
        const auto wave_high = ((i / half_period) % 2U) == 0U;
        const auto sample = static_cast<std::int16_t>(wave_high ? 5000 : -5000);
        appendLe16(bytes, static_cast<std::uint16_t>(sample));
    }

    return bytes;
}

class LocalTtsClient final : public voice_tts::ITtsClient {
public:
    voice_tts::TtsProviderResponse synthesize(const voice_tts::TtsProviderRequest& request) override
    {
        voice_tts::TtsProviderResponse response;
        response.ok = true;
        response.format = voice_tts::AudioFormat::wav;
        response.mime_type = voice_tts::mimeType(response.format);

        const auto duration_ms = std::min<int>(
            3000,
            std::max<int>(700, static_cast<int>(request.request.text.size()) * 45));
        response.duration = std::chrono::milliseconds(duration_ms);
        response.bytes = makeLocalMockWav(response.duration);
        response.metadata["provider"] = "local_mock_wav";
        response.metadata["note"] = "generated_valid_wav_placeholder";
        return response;
    }
};

ops::ComponentHealth makeHealth(std::string component, bool healthy, std::string message)
{
    ops::ComponentHealth health;
    health.component = std::move(component);
    health.healthy = healthy;
    health.message = std::move(message);
    health.checked_at = std::chrono::system_clock::now();
    return health;
}

std::string maskConnectionUri(std::string uri)
{
    const auto scheme = uri.find("://");
    if (scheme == std::string::npos) {
        return "<configured>";
    }

    const auto credential_start = scheme + 3;
    const auto at = uri.find('@', credential_start);
    if (at == std::string::npos) {
        return uri;
    }

    return uri.substr(0, credential_start) + "***:***@" + uri.substr(at + 1);
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

    context_->ops_service->registerHealthCheck("auth", [authService = context_->auth_service] {
        return makeHealth("auth", true, "sessions=" + std::to_string(authService->sessionCount()));
    });

    context_->ops_service->registerHealthCheck("room", [roomService = context_->room_service] {
        return makeHealth("room", true, "rooms=" + std::to_string(roomService->roomCount(false)));
    });

    context_->ops_service->registerHealthCheck("ai_orchestrator", [aiService = context_->ai_service] {
        return makeHealth("ai_orchestrator", true, "queued_tasks=" + std::to_string(aiService->queuedTaskCount()));
    });

    context_->ops_service->registerHealthCheck("tts", [ttsService = context_->tts_service] {
        return makeHealth("tts", true, "queued_tasks=" + std::to_string(ttsService->queuedTaskCount()));
    });

    context_->ops_service->registerHealthCheck("storage", [context = context_] {
        if (!context->storage_service) {
            return makeHealth("storage", true, "disabled: enable storage in config/app.yaml or set RCS_POSTGRES_URI/--postgres-uri");
        }
        if (!context->storage_service->isConnected()) {
            const auto message = context->storage_startup_error.empty()
                                     ? "disconnected"
                                     : "disconnected: " + context->storage_startup_error;
            return makeHealth("storage", false, message);
        }
        return makeHealth("storage", true, "connected");
    });

    context_->ops_service->setMetricsExporter([context = context_] {
        std::ostringstream metrics;
        metrics << "# TYPE rcs_auth_sessions gauge\n";
        metrics << "rcs_auth_sessions " << context->auth_service->sessionCount() << "\n";
        metrics << "# TYPE rcs_rooms gauge\n";
        metrics << "rcs_rooms " << context->room_service->roomCount(false) << "\n";
        metrics << "# TYPE rcs_ai_queued_tasks gauge\n";
        metrics << "rcs_ai_queued_tasks " << context->ai_service->queuedTaskCount() << "\n";
        metrics << "# TYPE rcs_tts_queued_tasks gauge\n";
        metrics << "rcs_tts_queued_tasks " << context->tts_service->queuedTaskCount() << "\n";
        return metrics.str();
    });

    // app 层负责把 controller 挂到 HTTP router，src/include 只提供可复用服务能力。
    api::registerServerRoutes(*router_, context_);
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
        if (result.ok) {
            context_->storage_startup_error.clear();
            spdlog::info("storage_connected location={}:{} uri={}",
                         __FILE__,
                         __LINE__,
                         maskConnectionUri(context_->storage_service->config().connection_uri));
        } else {
            context_->storage_startup_error = result.error;
            spdlog::error("storage_connect_failed location={}:{} error={}",
                          __FILE__,
                          __LINE__,
                          result.error);
        }
        if (!result.ok && !context_->config.gameplay.allow_without_storage) {
            throw std::runtime_error("storage connect failed: " + result.error);
        }
    } else {
        context_->storage_startup_error = "storage is disabled by config/app.yaml";
        spdlog::warn("storage_disabled location={}:{} reason={}",
                     __FILE__,
                     __LINE__,
                     context_->storage_startup_error);
    }

    http_server_->start();
    context_->ops_service->setReady(true);
}

void ServiceApplication::stop()
{
    if (http_server_ && http_server_->isRunning()) {
        context_->ops_service->beginShutdown("application stop requested");
        http_server_->stop();
        context_->ops_service->completeShutdown();
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

bool ServiceApplication::isRunning() const
{
    return http_server_ && http_server_->isRunning();
}

} // namespace rcs::application
