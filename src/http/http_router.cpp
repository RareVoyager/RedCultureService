#include "rcs/http/http_router.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

namespace rcs::http {

void HttpRouter::add_route(std::string method, std::string path, Handler handler)
{
    std::unique_lock lock(mutex_);
    routes_[std::move(path)][normalize_method(std::move(method))] = std::move(handler);
}

void HttpRouter::get(std::string path, Handler handler)
{
    add_route("GET", std::move(path), std::move(handler));
}

void HttpRouter::post(std::string path, Handler handler)
{
    add_route("POST", std::move(path), std::move(handler));
}

void HttpRouter::put(std::string path, Handler handler)
{
    add_route("PUT", std::move(path), std::move(handler));
}

void HttpRouter::del(std::string path, Handler handler)
{
    add_route("DELETE", std::move(path), std::move(handler));
}

HttpResponse HttpRouter::route(const HttpRequest& request) const
{
    std::shared_lock lock(mutex_);

    const auto path_it = routes_.find(request.path);
    if (path_it == routes_.end()) {
        return HttpResponse::not_found();
    }

    const auto method = normalize_method(request.method);
    const auto handler_it = path_it->second.find(method);
    if (handler_it == path_it->second.end()) {
        return HttpResponse::text(405, "method not allowed");
    }

    return handler_it->second(request);
}

bool HttpRouter::has_route(const std::string& method, const std::string& path) const
{
    std::shared_lock lock(mutex_);
    const auto path_it = routes_.find(path);
    if (path_it == routes_.end()) {
        return false;
    }

    return path_it->second.find(normalize_method(method)) != path_it->second.end();
}

std::string HttpRouter::normalize_method(std::string method)
{
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return method;
}

} // namespace rcs::http
