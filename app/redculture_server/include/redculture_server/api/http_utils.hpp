#pragma once

#include "redculture_server/application/service_application.hpp"
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

// 统一构造 JSON HTTP 响应，所有业务接口返回 code/msg/data 结构。
http::HttpResponse jsonResponse(int status_code, const Json& body);
http::HttpResponse successResponse(Json data = nullptr, std::string msg = "success", int code = 200, int http_status = 200);
http::HttpResponse errorResponse(int http_status, int code, std::string msg);
http::HttpResponse errorResponseAt(int http_status,
                                   int code,
                                   std::string msg,
                                   const char* file,
                                   int line,
                                   const char* function);

// 解析 JSON 请求体，失败时返回 Result<Json>。
common::Result<Json> parseJsonBody(const http::HttpRequest& request);

// 从 JSON 中读取常用类型，字段缺失或类型不匹配时返回 fallback。
std::string readStringOr(const Json& body, const char* key, std::string fallback = {});
bool readBoolOr(const Json& body, const char* key, bool fallback);
std::uint64_t readUint64Or(const Json& body, const char* key, std::uint64_t fallback);
std::size_t readSizeOr(const Json& body, const char* key, std::size_t fallback);

// HTTP 辅助读取：Header、Bearer Token、Query 参数。
std::optional<std::string> findHeader(const http::HttpRequest& request, const std::string& name);
std::optional<std::string> bearerToken(const http::HttpRequest& request);
std::optional<std::string> queryValue(const http::HttpRequest& request, const std::string& key);

// 统一解析玩家身份：优先 Bearer Token，本地开发模式下允许 body.player_id。
common::Result<std::string> resolvePlayer(const http::HttpRequest& request,
                                          const Json& body,
                                          const std::shared_ptr<application::ServiceContext>& context);

} // namespace rcs::api::support

#define RCS_API_ERROR_RESPONSE(http_status, code, msg) \
    ::rcs::api::support::errorResponseAt((http_status), (code), (msg), __FILE__, __LINE__, __func__)