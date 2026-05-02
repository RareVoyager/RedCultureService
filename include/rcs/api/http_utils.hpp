#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/common/result.hpp"
#include "rcs/http/http_types.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace rcs::api::support {

using Json = nlohmann::json;

// 统一构造 JSON HTTP 响应，所有业务接口都返回 code/msg/data 结构。
http::HttpResponse json_response(int status_code, const Json& body);
http::HttpResponse success_response(Json data = nullptr, std::string msg = "success", int code = 200, int http_status = 200);
http::HttpResponse error_response(int http_status, int code, std::string msg);
http::HttpResponse error_response_at(int http_status,
                                     int code,
                                     std::string msg,
                                     const char* file,
                                     int line,
                                     const char* function);

// 解析请求体，失败时返回 Result<Json>，Controller 不需要重复写 JSON 校验。
common::Result<Json> parse_json_body(const http::HttpRequest& request);

// 从 JSON 中读取常见类型，字段缺失或类型不匹配时返回 fallback。
std::string read_string_or(const Json& body, const char* key, std::string fallback = {});
bool read_bool_or(const Json& body, const char* key, bool fallback);
std::uint64_t read_uint64_or(const Json& body, const char* key, std::uint64_t fallback);
std::size_t read_size_or(const Json& body, const char* key, std::size_t fallback);

// HTTP 辅助读取：Header、Bearer Token、Query 参数。
std::optional<std::string> find_header(const http::HttpRequest& request, const std::string& name);
std::optional<std::string> bearer_token(const http::HttpRequest& request);
std::optional<std::string> query_value(const http::HttpRequest& request, const std::string& key);

// 统一解析玩家身份：优先 Bearer Token，本地开发模式下允许 body.player_id。
common::Result<std::string> resolve_player(const http::HttpRequest& request,
                                           const Json& body,
                                           const std::shared_ptr<application::ServiceContext>& context);

} // namespace rcs::api::support

#define RCS_API_ERROR_RESPONSE(http_status, code, msg) \
    ::rcs::api::support::error_response_at((http_status), (code), (msg), __FILE__, __LINE__, __func__)
