#include "rcs/api/controllers/auth_controller.hpp"

#include "rcs/api/http_utils.hpp"
#include "rcs/auth/password_hasher.hpp"
#include "rcs/common/result.hpp"

#include <exception>
#include <string>
#include <utility>

namespace rcs::api::controllers {
namespace {

common::Result<dto::RegisterRequest> parse_register_request(const http::HttpRequest& request)
{
    const auto parsed = support::parse_json_body(request);
    if (!parsed.ok()) {
        return common::Result<dto::RegisterRequest>::error(parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    dto::RegisterRequest reg;
    reg.player_id = support::read_string_or(body, "player_id");
    reg.account = support::read_string_or(body, "account");
    reg.password = support::read_string_or(body, "password");
    reg.display_name = support::read_string_or(body, "display_name", reg.account);
    reg.avatar_url = support::read_string_or(body, "avatar_url");
    reg.connection_id = support::read_uint64_or(body, "connection_id", 0);

    return common::Result<dto::RegisterRequest>::success(std::move(reg));
}

common::Result<dto::LoginRequest> parse_login_request(const http::HttpRequest& request)
{
    const auto parsed = support::parse_json_body(request);
    if (!parsed.ok()) {
        return common::Result<dto::LoginRequest>::error(parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    dto::LoginRequest login;
    login.player_id = support::read_string_or(body, "player_id");
    login.account = support::read_string_or(body, "account", login.player_id);
    login.password = support::read_string_or(body, "password");
    login.token = support::read_string_or(body, "token");
    login.connection_id = support::read_uint64_or(body, "connection_id", 0);

    return common::Result<dto::LoginRequest>::success(std::move(login));
}

support::Json session_to_json(const auth::Session& session)
{
    return support::Json{
        {"session_id", session.id},
        {"player_id", session.player_id},
        {"account", session.account},
        {"connection_id", session.connection_id},
    };
}

support::Json auth_response_to_json(const dto::AuthResponse& response)
{
    return support::Json{
        {"token", response.token},
        {"session", session_to_json(response.session)},
        {"user", support::Json{
            {"player_id", response.session.player_id},
            {"account", response.session.account},
            {"display_name", response.display_name},
            {"avatar_url", response.avatar_url},
        }},
    };
}

support::Json metadata_from_body(const support::Json& body)
{
    if (body.contains("metadata") && body["metadata"].is_object()) {
        return body["metadata"];
    }
    return support::Json::object();
}

} // namespace

AuthController::AuthController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void AuthController::register_routes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    // 注册和登录都放在 API 层，app 只负责启动进程和挂载路由。
    router.post("/api/v1/auth/register", [self](const http::HttpRequest& request) {
        return self->register_user(request);
    });
    router.post("/api/v1/auth/login", [self](const http::HttpRequest& request) {
        return self->login(request);
    });
}

http::HttpResponse AuthController::register_user(const http::HttpRequest& request)
{
    const auto parsed_json = support::parse_json_body(request);
    if (!parsed_json.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed_json.code(), parsed_json.msg());
    }

    const auto parsed = parse_register_request(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    if (!context_->storage_service || !context_->storage_service->is_connected()) {
        return RCS_API_ERROR_RESPONSE(503, 503, "storage is required for register");
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

    if (context_->storage_service->find_user(reg.player_id)) {
        return RCS_API_ERROR_RESPONSE(409, 409, "player_id already exists");
    }
    if (context_->storage_service->find_user_by_account(reg.account)) {
        return RCS_API_ERROR_RESPONSE(409, 409, "account already exists");
    }

    storage::UserProfile profile;
    profile.player_id = reg.player_id;
    profile.account = reg.account;
    try {
        profile.password_hash = auth::hash_password_bcrypt(reg.password);
    } catch (const std::exception& e) {
        return RCS_API_ERROR_RESPONSE(500, 500, std::string("password hash failed: ") + e.what());
    }
    profile.display_name = reg.display_name;
    profile.avatar_url = reg.avatar_url;
    profile.metadata = metadata_from_body(parsed_json.data());
    profile.metadata["source"] = "unity_register";

    const auto created = context_->storage_service->create_user(profile);
    if (!created.ok) {
        return RCS_API_ERROR_RESPONSE(500, 500, created.error.empty() ? "register failed" : created.error);
    }

    const auto token = context_->auth_service->issue_token(profile.player_id, profile.account);
    const auto logged_in = context_->auth_service->login_with_token(token, reg.connection_id);
    if (!logged_in.ok || !logged_in.session) {
        return RCS_API_ERROR_RESPONSE(500, 500, logged_in.error.empty() ? "auto login failed" : logged_in.error);
    }

    dto::AuthResponse response;
    response.token = token;
    response.session = *logged_in.session;
    response.display_name = profile.display_name;
    response.avatar_url = profile.avatar_url;

    return support::success_response(auth_response_to_json(response), "register success");
}

http::HttpResponse AuthController::login(const http::HttpRequest& request)
{
    const auto parsed = parse_login_request(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    auto login_request = parsed.data();
    std::string display_name;
    std::string avatar_url;

    if (login_request.token.empty() && !login_request.account.empty() && !login_request.password.empty()) {
        if (!context_->storage_service || !context_->storage_service->is_connected()) {
            return RCS_API_ERROR_RESPONSE(503, 503, "storage is required for password login");
        }

        const auto profile = context_->storage_service->find_user_by_account(login_request.account);
        if (!profile || profile->status != "active" || !auth::verify_password_bcrypt(login_request.password, profile->password_hash)) {
            return RCS_API_ERROR_RESPONSE(401, 401, "account or password is invalid");
        }

        login_request.player_id = profile->player_id;
        login_request.account = profile->account;
        login_request.token = context_->auth_service->issue_token(profile->player_id, profile->account);
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

        login_request.token = context_->auth_service->issue_token(login_request.player_id, login_request.account);
    }

    const auto logged_in = context_->auth_service->login_with_token(login_request.token, login_request.connection_id);
    if (!logged_in.ok || !logged_in.session) {
        return RCS_API_ERROR_RESPONSE(401, 401, logged_in.error.empty() ? "login failed" : logged_in.error);
    }

    if (display_name.empty() && context_->storage_service && context_->storage_service->is_connected()) {
        if (const auto profile = context_->storage_service->find_user(logged_in.session->player_id)) {
            display_name = profile->display_name;
            avatar_url = profile->avatar_url;
        }
    }

    dto::AuthResponse response;
    response.token = std::move(login_request.token);
    response.session = *logged_in.session;
    response.display_name = display_name;
    response.avatar_url = avatar_url;

    return support::success_response(auth_response_to_json(response), "login success");
}

} // namespace rcs::api::controllers
