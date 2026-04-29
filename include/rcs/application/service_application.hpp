#pragma once

#include "rcs/ai_orchestrator/ai_orchestrator_service.hpp"
#include "rcs/auth/session_auth_service.hpp"
#include "rcs/gameplay/cultural_interaction_service.hpp"
#include "rcs/http/http_router.hpp"
#include "rcs/http/http_server.hpp"
#include "rcs/ops/ops_service.hpp"
#include "rcs/room/room_match_service.hpp"
#include "rcs/storage/storage_service.hpp"
#include "rcs/voice_tts/voice_tts_service.hpp"

#include <memory>

namespace rcs::application {

// 应用层总配置，后面可以由 ConfigHotReloadService 从 YAML 中填充。
struct ApplicationConfig {
    http::HttpServerConfig http;
    auth::AuthConfig auth;
    storage::StorageConfig storage;
    gameplay::CulturalInteractionConfig gameplay;
    ops::OpsConfig ops;

    // 本地联调默认允许直接用 player_id 登录；上线前应改成 false，并接入账号系统或平台票据校验。
    bool allow_dev_auth{true};

    // 默认不强制连接数据库；配置 RCS_POSTGRES_URI 后服务入口会打开它。
    bool enable_storage{false};
};

// 业务服务上下文。HTTP handler 通过它访问鉴权、房间、AI、运维等模块。
struct ServiceContext {
    ApplicationConfig config;
    std::shared_ptr<auth::SessionAuthService> auth_service;
    std::shared_ptr<room::RoomMatchService> room_service;
    std::shared_ptr<ai_orchestrator::AiOrchestratorService> ai_service;
    std::shared_ptr<voice_tts::VoiceTtsService> tts_service;
    std::shared_ptr<storage::StorageService> storage_service;
    std::shared_ptr<gameplay::CulturalInteractionService> gameplay_service;
    std::shared_ptr<ops::OpsService> ops_service;
};

// 主应用对象，负责组装模块、注册路由、启动/停止 HTTP 服务。
class ServiceApplication {
public:
    explicit ServiceApplication(ApplicationConfig config = {});
    ~ServiceApplication();

    ServiceApplication(const ServiceApplication&) = delete;
    ServiceApplication& operator=(const ServiceApplication&) = delete;

    void start();
    void stop();

    std::shared_ptr<ServiceContext> context() const;
    std::shared_ptr<http::HttpRouter> router() const;
    bool is_running() const;

private:
    std::shared_ptr<ServiceContext> context_;
    std::shared_ptr<http::HttpRouter> router_;
    std::unique_ptr<http::HttpServer> http_server_;
};

} // namespace rcs::application
