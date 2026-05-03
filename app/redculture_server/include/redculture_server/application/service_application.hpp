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
#include <string>

namespace rcs::application {

// redculture_server 应用层配置，只服务当前可执行程序。
struct ApplicationConfig {
    http::HttpServerConfig http;
    auth::AuthConfig auth;
    storage::StorageConfig storage;
    gameplay::CulturalInteractionConfig gameplay;
    ops::OpsConfig ops;

    // 本地联调默认允许直接使用 player_id 登录；上线前应关闭并依赖 token 校验。
    bool allow_dev_auth{true};

    // 设置 RCS_POSTGRES_URI 或 --postgres-uri 后由入口打开数据库存储。
    bool enable_storage{false};
};

// 应用层上下文，HTTP controller 通过它访问基础服务封装。
struct ServiceContext {
    ApplicationConfig config;
    std::shared_ptr<auth::SessionAuthService> auth_service;
    std::shared_ptr<room::RoomMatchService> room_service;
    std::shared_ptr<ai_orchestrator::AiOrchestratorService> ai_service;
    std::shared_ptr<voice_tts::VoiceTtsService> tts_service;
    std::shared_ptr<storage::StorageService> storage_service;
    std::shared_ptr<gameplay::CulturalInteractionService> gameplay_service;
    std::shared_ptr<ops::OpsService> ops_service;

    // 保存启动时数据库连接错误，便于接口返回更具体的诊断信息。
    std::string storage_startup_error;
};

// redculture_server 应用对象，负责组装服务、挂载路由并启动 HTTP 服务。
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
    bool isRunning() const;

private:
    std::shared_ptr<ServiceContext> context_;
    std::shared_ptr<http::HttpRouter> router_;
    std::unique_ptr<http::HttpServer> http_server_;
};

} // namespace rcs::application