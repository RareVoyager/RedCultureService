#pragma once

#include "rcs/http/http_types.hpp"

#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace rcs::http {

class HttpRouter {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    // 注册 method + path 到一个 handler，path 当前采用精确匹配。
    void addRoute(std::string method, std::string path, Handler handler);

    // 常用 HTTP 方法的便捷注册函数，让写法接近 Spring 的路由声明。
    void get(std::string path, Handler handler);
    void post(std::string path, Handler handler);
    void put(std::string path, Handler handler);
    void del(std::string path, Handler handler);

    // 根据请求路径和方法分发到具体业务 handler。
    HttpResponse route(const HttpRequest& request) const;
    bool hasRoute(const std::string& method, const std::string& path) const;

private:
    static std::string normalizeMethod(std::string method);

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::unordered_map<std::string, Handler>> routes_;
};

} // namespace rcs::http
