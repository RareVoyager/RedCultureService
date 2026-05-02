#include "app/redculture_server/controllers/auth_controller.hpp"

#include "rcs/common/result.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace rcs::server::controllers {
namespace {

using json = nlohmann::json;

http::HttpResponse json_response(int status_code, const json& body)
{
    return http::HttpResponse::json(status_code, body.dump());
}

template <typename T>
json result_body(const common::Result<T>& result, json data)
{
    json body;
    body["code"] = result.code();
    body["msg"] = result.msg();
    body["data"] = std::move(data);
    return body;
}

json result_body(const common::Result<void>& result)
{
    json body;
    body["code"] = result.code();
    body["msg"] = result.msg();
    body["data"] = nullptr;
    return body;
}

http::HttpResponse error_response(int status_code, int code, std::string msg)
{
    return json_response(status_code, result_body(common::Result<void>::error(code, std::move(msg))));
}

std::uint64_t read_uint64_or(const json& body, const char* key, std::uint64_t fallback)
{
    const auto it = body.find(key);
    if (it == body.end()) {
        return fallback;
    }

    if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
    }

    if (it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        return value < 0 ? fallback : static_cast<std::uint64_t>(value);
    }

    if (it->is_string()) {
        try {
            return static_cast<std::uint64_t>(std::stoull(it->get<std::string>()));
        } catch (...) {
            return fallback;
        }
    }

    return fallback;
}

std::string read_string_or(const json& body, const char* key, std::string fallback = {})
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        return fallback;
    }

    return it->get<std::string>();
}

common::Result<dto::LoginRequest> parse_login_request(const http::HttpRequest& request)
{
    if (request.body.empty()) {
        return common::Result<dto::LoginRequest>::error(400, "request body must be a JSON object");
    }

    auto body = json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return common::Result<dto::LoginRequest>::error(400, "request body must be a valid JSON object");
    }

    dto::LoginRequest login;
    login.player_id = read_string_or(body, "player_id");
    login.account = read_string_or(body, "account", login.player_id);
    login.token = read_string_or(body, "token");
    login.connection_id = read_uint64_or(body, "connection_id", 0);

    return common::Result<dto::LoginRequest>::success(std::move(login));
}

json session_to_json(const auth::Session& session)
{
    return json{
        {"session_id", session.id},
        {"player_id", session.player_id},
        {"account", session.account},
        {"connection_id", session.connection_id},
    };
}

json login_response_to_json(const dto::LoginResponse& response)
{
    return json{
        {"token", response.token},
        {"session", session_to_json(response.session)},
    };
}

} // namespace

AuthController::AuthController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void AuthController::register_routes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    // 登录接口写在 app 层，覆盖底层默认路由；后续注册、登出、刷新 token 也放在这里。
    router.post("/api/v1/auth/login", [self](const http::HttpRequest& request) {
        return self->login(request);
    });
}

http::HttpResponse AuthController::login(const http::HttpRequest& request)
{
    const auto parsed = parse_login_request(request);
    if (!parsed.ok()) {
        return error_response(400, parsed.code(), parsed.msg());
    }

    auto login_request = parsed.data();
    if (login_request.token.empty()) {
        if (!context_->config.allow_dev_auth) {
            return error_response(401, 401, "token is required");
        }
        if (login_request.player_id.empty()) {
            return error_response(400, 400, "player_id is required");
        }

        login_request.token = context_->auth_service->issue_token(login_request.player_id, login_request.account);
    }

    const auto logged_in = context_->auth_service->login_with_token(login_request.token, login_request.connection_id);
    if (!logged_in.ok || !logged_in.session) {
        return error_response(401, 401, logged_in.error.empty() ? "login failed" : logged_in.error);
    }

    dto::LoginResponse response;
    response.token = std::move(login_request.token);
    response.session = *logged_in.session;

    const auto result = common::Result<dto::LoginResponse>::success("login success", std::move(response));
    return json_response(200, result_body(result, login_response_to_json(result.data())));
}

} // namespace rcs::server::controllers
