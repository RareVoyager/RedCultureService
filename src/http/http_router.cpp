#include "rcs/http/http_router.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

namespace rcs::http {

void HttpRouter::addRoute(std::string method, std::string path, Handler handler)
{
    std::unique_lock lock(mutex_);
    routes_[std::move(path)][normalizeMethod(std::move(method))] = std::move(handler);
}

void HttpRouter::get(std::string path, Handler handler)
{
    addRoute("GET", std::move(path), std::move(handler));
}

void HttpRouter::post(std::string path, Handler handler)
{
    addRoute("POST", std::move(path), std::move(handler));
}

void HttpRouter::put(std::string path, Handler handler)
{
    addRoute("PUT", std::move(path), std::move(handler));
}

void HttpRouter::del(std::string path, Handler handler)
{
    addRoute("DELETE", std::move(path), std::move(handler));
}

HttpResponse HttpRouter::route(const HttpRequest& request) const
{
    std::shared_lock lock(mutex_);

    const auto path_it = routes_.find(request.path);
    if (path_it == routes_.end()) {
        spdlog::warn("http_route_not_found location={}:{} method={} path={} target={}",
                     __FILE__,
                     __LINE__,
                     request.method,
                     request.path,
                     request.target);
        return HttpResponse::notFound();
    }

    const auto method = normalizeMethod(request.method);
    const auto handler_it = path_it->second.find(method);
    if (handler_it == path_it->second.end()) {
        spdlog::warn("http_method_not_allowed location={}:{} method={} path={} target={}",
                     __FILE__,
                     __LINE__,
                     request.method,
                     request.path,
                     request.target);
        return HttpResponse::text(405, "method not allowed");
    }

    return handler_it->second(request);
}

bool HttpRouter::hasRoute(const std::string& method, const std::string& path) const
{
    std::shared_lock lock(mutex_);
    const auto path_it = routes_.find(path);
    if (path_it == routes_.end()) {
        return false;
    }

    return path_it->second.find(normalizeMethod(method)) != path_it->second.end();
}

std::string HttpRouter::normalizeMethod(std::string method)
{
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return method;
}

} // namespace rcs::http
