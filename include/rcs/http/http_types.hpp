#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace rcs::http {

// HTTP 请求的项目内统一表示，避免业务层直接依赖 Boost.Beast 的类型。
struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string remote_address;
    std::uint16_t remote_port{0};
};

// HTTP 响应的项目内统一表示，业务 handler 只需要返回这个结构。
struct HttpResponse {
    int status_code{200};
    std::string content_type{"application/json"};
    std::string body;
    std::map<std::string, std::string> headers;

    // 返回 JSON 文本。body 需要已经是合法 JSON 字符串。
    static HttpResponse json(int status_code, std::string body);

    // 返回普通文本，常用于错误信息或 Prometheus 指标。
    static HttpResponse text(int status_code, std::string body, std::string content_type = "text/plain; charset=utf-8");

    // 204 响应主要用于 CORS 预检请求。
    static HttpResponse no_content();

    // 常用错误响应快捷构造。
    static HttpResponse not_found();
    static HttpResponse bad_request(std::string message);
    static HttpResponse internal_error(std::string message);
};

} // namespace rcs::http
