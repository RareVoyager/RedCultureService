#include "redculture_server/api/http_utils.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace rcs::api::support {
namespace {

std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

http::HttpResponse jsonResponse(int status_code, const Json& body)
{
    return http::HttpResponse::json(status_code, body.dump());
}

http::HttpResponse successResponse(Json data, std::string msg, int code, int http_status)
{
    Json body;
    body["code"] = code;
    body["msg"] = std::move(msg);
    body["data"] = std::move(data);
    return jsonResponse(http_status, body);
}

http::HttpResponse errorResponse(int http_status, int code, std::string msg)
{
    Json body;
    body["code"] = code;
    body["msg"] = std::move(msg);
    body["data"] = nullptr;
    return jsonResponse(http_status, body);
}

http::HttpResponse errorResponseAt(int http_status,
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
    return errorResponse(http_status, code, std::move(msg));
}

common::Result<Json> parseJsonBody(const http::HttpRequest& request)
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

std::string readStringOr(const Json& body, const char* key, std::string fallback)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_string()) {
        return fallback;
    }

    return it->get<std::string>();
}

bool readBoolOr(const Json& body, const char* key, bool fallback)
{
    const auto it = body.find(key);
    if (it == body.end() || !it->is_boolean()) {
        return fallback;
    }

    return it->get<bool>();
}

std::uint64_t readUint64Or(const Json& body, const char* key, std::uint64_t fallback)
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

std::size_t readSizeOr(const Json& body, const char* key, std::size_t fallback)
{
    return static_cast<std::size_t>(readUint64Or(body, key, fallback));
}

std::optional<std::string> findHeader(const http::HttpRequest& request, const std::string& name)
{
    const auto expected = lowerCopy(name);
    for (const auto& [header_name, header_value] : request.headers) {
        if (lowerCopy(header_name) == expected) {
            return header_value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> bearerToken(const http::HttpRequest& request)
{
    const auto header = findHeader(request, "Authorization");
    if (!header) {
        return std::nullopt;
    }

    const std::string prefix = "bearer ";
    const auto lowered = lowerCopy(*header);
    if (lowered.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    return header->substr(prefix.size());
}

std::optional<std::string> queryValue(const http::HttpRequest& request, const std::string& key)
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

common::Result<std::string> resolvePlayer(const http::HttpRequest& request,
                                           const Json& body,
                                           const std::shared_ptr<application::ServiceContext>& context)
{
    auto token = bearerToken(request);
    const auto body_token = readStringOr(body, "token");
    if (!body_token.empty()) {
        token = body_token;
    }

    if (token && !token->empty()) {
        const auto result = context->auth_service->validateToken(*token);
        if (!result.ok || !result.claims) {
            return common::Result<std::string>::error(401, result.error.empty() ? "token is invalid" : result.error);
        }
        return common::Result<std::string>::success(result.claims->player_id);
    }

    const auto player_id = readStringOr(body, "player_id");
    if (context->config.allow_dev_auth && !player_id.empty()) {
        return common::Result<std::string>::success(player_id);
    }

    return common::Result<std::string>::error(401, "missing bearer token or player_id in local dev mode");
}

} // namespace rcs::api::support
