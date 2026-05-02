#include "rcs/api/http_utils.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace rcs::api::support {
namespace {

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

http::HttpResponse json_response(int status_code, const Json& body)
{
    return http::HttpResponse::json(status_code, body.dump());
}

http::HttpResponse success_response(Json data, std::string msg, int code, int http_status)
{
    Json body;
    body["code"] = code;
    body["msg"] = std::move(msg);
    body["data"] = std::move(data);
    return json_response(http_status, body);
}

http::HttpResponse error_response(int http_status, int code, std::string msg)
{
    Json body;
    body["code"] = code;
    body["msg"] = std::move(msg);
    body["data"] = nullptr;
    return json_response(http_status, body);
}

http::HttpResponse error_response_at(int http_status,
                                     int code,
                                     std::string msg,
                                     const char* file,
                                     int line,
                                     const char* function)
{
    spdlog::warn("api_error location={}:{} function={} http_status={} code={} msg={}",
                 file,
                 line,
                 function,
                 http_status,
                 code,
                 msg);
    return error_response(http_status, code, std::move(msg));
}

common::Result<Json> parse_json_body(const http::HttpRequest& request)
{
    if (request.body.empty()) {
        return common::Result<Json>::error(400, "request body must be a JSON object");
    }

    auto body = Json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return common::Result<Json>::error(400, "request body must be a valid JSON object");
    }

    return common::Result<Json>::success(std::move(body));
}

std::string read_string_or(const Json& body, const char* key, std::string fallback)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        return fallback;
    }

    return it->get<std::string>();
}

bool read_bool_or(const Json& body, const char* key, bool fallback)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_boolean()) {
        return fallback;
    }

    return it->get<bool>();
}

std::uint64_t read_uint64_or(const Json& body, const char* key, std::uint64_t fallback)
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

std::size_t read_size_or(const Json& body, const char* key, std::size_t fallback)
{
    return static_cast<std::size_t>(read_uint64_or(body, key, fallback));
}

std::optional<std::string> find_header(const http::HttpRequest& request, const std::string& name)
{
    const auto expected = lower_copy(name);
    for (const auto& [header_name, header_value] : request.headers) {
        if (lower_copy(header_name) == expected) {
            return header_value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> bearer_token(const http::HttpRequest& request)
{
    const auto header = find_header(request, "Authorization");
    if (!header) {
        return std::nullopt;
    }

    const std::string prefix = "bearer ";
    const auto lowered = lower_copy(*header);
    if (lowered.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    return header->substr(prefix.size());
}

std::optional<std::string> query_value(const http::HttpRequest& request, const std::string& key)
{
    std::size_t begin = 0;
    while (begin <= request.query.size()) {
        const auto end = request.query.find('&', begin);
        const auto item = request.query.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        const auto eq = item.find('=');
        if (eq != std::string::npos && item.substr(0, eq) == key) {
            return item.substr(eq + 1);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::nullopt;
}

common::Result<std::string> resolve_player(const http::HttpRequest& request,
                                           const Json& body,
                                           const std::shared_ptr<application::ServiceContext>& context)
{
    auto token = bearer_token(request);
    const auto body_token = read_string_or(body, "token");
    if (!body_token.empty()) {
        token = body_token;
    }

    if (token && !token->empty()) {
        const auto result = context->auth_service->validate_token(*token);
        if (!result.ok || !result.claims) {
            return common::Result<std::string>::error(401, result.error.empty() ? "token is invalid" : result.error);
        }
        return common::Result<std::string>::success(result.claims->player_id);
    }

    const auto player_id = read_string_or(body, "player_id");
    if (context->config.allow_dev_auth && !player_id.empty()) {
        return common::Result<std::string>::success(player_id);
    }

    return common::Result<std::string>::error(401, "missing bearer token or player_id in local dev mode");
}

} // namespace rcs::api::support
