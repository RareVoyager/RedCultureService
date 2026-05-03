#include "redculture_server/api/controllers/auth_controller.hpp"

#include "redculture_server/api/http_utils.hpp"
#include "rcs/auth/password_hasher.hpp"
#include "rcs/common/result.hpp"

#include <exception>
#include <string>
#include <utility>

namespace rcs::api::controllers {
namespace {

common::Result<dto::RegisterRequest> parseRegisterRequest(const http::HttpRequest& request)
{
    const auto parsed = support::parseJsonBody(request);
    if (!parsed.ok()) {
        return common::Result<dto::RegisterRequest>::error(parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    dto::RegisterRequest reg;
    reg.player_id = support::readStringOr(body, "player_id");
    reg.account = support::readStringOr(body, "account");
    reg.password = support::readStringOr(body, "password");
    reg.display_name = support::readStringOr(body, "display_name", reg.account);
    reg.avatar_url = support::readStringOr(body, "avatar_url");
    reg.connection_id = support::readUint64Or(body, "connection_id", 0);

    return common::Result<dto::RegisterRequest>::success(std::move(reg));
}

common::Result<dto::LoginRequest> parseLoginRequest(const http::HttpRequest& request)
{
    const auto parsed = support::parseJsonBody(request);
    if (!parsed.ok()) {
        return common::Result<dto::LoginRequest>::error(parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    dto::LoginRequest login;
    login.player_id = support::readStringOr(body, "player_id");
    login.account = support::readStringOr(body, "account", login.player_id);
    login.password = support::readStringOr(body, "password");
    login.token = support::readStringOr(body, "token");
    login.connection_id = support::readUint64Or(body, "connection_id", 0);

    return common::Result<dto::LoginRequest>::success(std::move(login));
}

support::Json sessionToJson(const auth::Session& session)
{
    return support::Json{
        {"session_id", session.id},
        {"player_id", session.player_id},
        {"account", session.account},
        {"connection_id", session.connection_id},
    };
}

support::Json authResponseToJson(const dto::AuthResponse& response)
{
    return support::Json{
        {"token", response.token},
        {"session", sessionToJson(response.session)},
        {"user", support::Json{
            {"player_id", response.session.player_id},
            {"account", response.session.account},
            {"display_name", response.display_name},
            {"avatar_url", response.avatar_url},
        }},
        {"storage_saved", response.storage_saved},
    };
}

support::Json metadataFromBody(const support::Json& body)
{
    if (body.contains("metadata") && body["metadata"].is_object()) {
        return body["metadata"];
    }
    return support::Json::object();
}

bool persistLoginSession(const std::shared_ptr<application::ServiceContext>& context,
                           const http::HttpRequest& request,
                           const auth::Session& session,
                           const std::string& source)
{
    if (!context->storage_service || !context->storage_service->isConnected()) {
        return false;
    }

    try {
        if (!context->storage_service->findUser(session.player_id)) {
            storage::UserProfile profile;
            profile.player_id = session.player_id;
            profile.account = session.account.empty() ? session.player_id : session.account;
            profile.display_name = profile.account;
            profile.metadata = {{"source", source}};
            const auto user_saved = context->storage_service->upsertUser(profile);
            if (!user_saved.ok) {
                return false;
            }
        }

        storage::PlayerSessionRecord record;
        record.player_id = session.player_id;
        record.token_id = std::to_string(session.id);
        record.connection_id = session.connection_id;
        record.client_ip = request.remoteAddress;
        record.user_agent = support::findHeader(request, "User-Agent").value_or("");
        record.metadata = {
            {"source", source},
            {"account", session.account},
            {"session_id", session.id},
        };
        const auto session_saved = context->storage_service->appendPlayerSession(record);

        storage::EventLog event;
        event.level = session_saved.ok ? "info" : "warn";
        event.category = "auth.session";
        event.message = "player login session persisted";
        event.metadata = {
            {"source", source},
            {"player_id", session.player_id},
            {"account", session.account},
            {"session_id", session.id},
            {"connection_id", session.connection_id},
            {"session_saved", session_saved.ok},
            {"storage_error", session_saved.error},
        };
        context->storage_service->appendEventLog(event);
        return session_saved.ok;
    } catch (...) {
        return false;
    }
}

std::string storageUnavailableMessage(const std::shared_ptr<application::ServiceContext>& context, const std::string& operation)
{
    if (!context->storage_service) {
        return "storage is disabled: enable storage in config/app.yaml or set RCS_POSTGRES_URI/--postgres-uri before starting redculture_server for " + operation;
    }
    if (!context->storage_service->isConnected()) {
        if (!context->storage_startup_error.empty()) {
            return "storage is disconnected for " + operation + ": " + context->storage_startup_error;
        }
        return "storage is disconnected for " + operation + ": check storage_connect_failed startup log";
    }
    return {};
}

} // namespace

AuthController::AuthController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void AuthController::registerRoutes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    // 注册和登录都放在 API 层，app 只负责启动进程和挂载路由。
    router.post("/api/v1/auth/register", [self](const http::HttpRequest& request) {
        return self->registerUser(request);
    });
    router.post("/api/v1/auth/login", [self](const http::HttpRequest& request) {
        return self->login(request);
    });
}
http::HttpResponse AuthController::registerUser(const http::HttpRequest& request)
{
    const auto parsed_json = support::parseJsonBody(request);
    if (!parsed_json.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed_json.code(), parsed_json.msg());
    }

    const auto parsed = parseRegisterRequest(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }
    auto reg = parsed.data();
    if (reg.account.empty()) {
        return RCS_API_ERROR_RESPONSE(400, 400, "account is required");
    }
    if (reg.password.size() < 6) {
        return RCS_API_ERROR_RESPONSE(400, 400, "password length must be at least 6");
    }
    if (reg.player_id.empty()) {
        reg.player_id = reg.account;
    }
    if (reg.display_name.empty()) {
        reg.display_name = reg.account;
    }

    if (const auto storageError = storageUnavailableMessage(context_, "register"); !storageError.empty()) {
        return RCS_API_ERROR_RESPONSE(503, 503, storageError);
    }

    if (context_->storage_service->findUser(reg.player_id)) {
        return RCS_API_ERROR_RESPONSE(409, 409, "player_id already exists");
    }
    if (context_->storage_service->findUserByAccount(reg.account)) {
        return RCS_API_ERROR_RESPONSE(409, 409, "account already exists");
    }

    storage::UserProfile profile;
    profile.player_id = reg.player_id;
    profile.account = reg.account;
    try {
        profile.password_hash = auth::hashPasswordBcrypt(reg.password);
    } catch (const std::exception& e) {
        return RCS_API_ERROR_RESPONSE(500, 500, std::string("password hash failed: ") + e.what());
    }
    profile.display_name = reg.display_name;
    profile.avatar_url = reg.avatar_url;
    profile.metadata = metadataFromBody(parsed_json.data());
    profile.metadata["source"] = "unity_register";

    const auto created = context_->storage_service->createUser(profile);
    if (!created.ok) {
        return RCS_API_ERROR_RESPONSE(500, 500, created.error.empty() ? "register failed" : created.error);
    }

    const auto token = context_->auth_service->issueToken(profile.player_id, profile.account);
    const auto logged_in = context_->auth_service->loginWithToken(token, reg.connection_id);
    if (!logged_in.ok || !logged_in.session) {
        return RCS_API_ERROR_RESPONSE(500, 500, logged_in.error.empty() ? "auto login failed" : logged_in.error);
    }

    dto::AuthResponse response;
    response.token = token;
    response.session = *logged_in.session;
    response.display_name = profile.display_name;
    response.avatar_url = profile.avatar_url;
    response.storage_saved = persistLoginSession(context_, request, response.session, "register");

    return support::successResponse(authResponseToJson(response), "register success");
}

http::HttpResponse AuthController::login(const http::HttpRequest& request)
{
    const auto parsed = parseLoginRequest(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    auto login_request = parsed.data();
    std::string display_name;
    std::string avatar_url;

    if (login_request.token.empty() && !login_request.account.empty() && !login_request.password.empty()) {
        if (const auto storageError = storageUnavailableMessage(context_, "password login"); !storageError.empty()) {
            return RCS_API_ERROR_RESPONSE(503, 503, storageError);
        }

        const auto profile = context_->storage_service->findUserByAccount(login_request.account);
        if (!profile || profile->status != "active" || !auth::verifyPasswordBcrypt(login_request.password, profile->password_hash)) {
            return RCS_API_ERROR_RESPONSE(401, 401, "account or password is invalid");
        }

        login_request.player_id = profile->player_id;
        login_request.account = profile->account;
        login_request.token = context_->auth_service->issueToken(profile->player_id, profile->account);
        display_name = profile->display_name;
        avatar_url = profile->avatar_url;
    }

    if (login_request.token.empty()) {
        if (!context_->config.allow_dev_auth) {
            return RCS_API_ERROR_RESPONSE(401, 401, "token is required");
        }
        if (login_request.player_id.empty()) {
            return RCS_API_ERROR_RESPONSE(400, 400, "player_id is required");
        }

        login_request.token = context_->auth_service->issueToken(login_request.player_id, login_request.account);
    }

    const auto logged_in = context_->auth_service->loginWithToken(login_request.token, login_request.connection_id);
    if (!logged_in.ok || !logged_in.session) {
        return RCS_API_ERROR_RESPONSE(401, 401, logged_in.error.empty() ? "login failed" : logged_in.error);
    }

    if (display_name.empty() && context_->storage_service && context_->storage_service->isConnected()) {
        if (const auto profile = context_->storage_service->findUser(logged_in.session->player_id)) {
            display_name = profile->display_name;
            avatar_url = profile->avatar_url;
        }
    }

    dto::AuthResponse response;
    response.token = std::move(login_request.token);
    response.session = *logged_in.session;
    response.display_name = display_name;
    response.avatar_url = avatar_url;
    response.storage_saved = persistLoginSession(context_, request, response.session, "login");

    return support::successResponse(authResponseToJson(response), "login success");
}

} // namespace rcs::api::controllers
