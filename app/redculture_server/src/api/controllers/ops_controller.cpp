#include "redculture_server/api/controllers/ops_controller.hpp"

#include "redculture_server/api/http_utils.hpp"

#include <utility>

namespace rcs::api::controllers {

OpsController::OpsController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void OpsController::registerRoutes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.get("/api/v1/ops/health", [self](const http::HttpRequest& request) {
        return self->forwardJson(request, "/health", "health checked");
    });
    router.get("/api/v1/ops/ready", [self](const http::HttpRequest& request) {
        return self->forwardJson(request, "/ready", "ready checked");
    });
    router.get("/api/v1/ops/version", [self](const http::HttpRequest& request) {
        return self->forwardJson(request, "/version", "version fetched");
    });
    router.get("/api/v1/ops/metrics", [self](const http::HttpRequest& request) {
        return self->metrics(request);
    });
    router.post("/api/v1/ops/shutdown", [self](const http::HttpRequest& request) {
        return self->forwardJson(request, "/shutdown", "shutdown requested");
    });
}

http::HttpResponse OpsController::forwardJson(const http::HttpRequest& request,
                                               std::string ops_path,
                                               std::string successMsg)
{
    ops::AdminRequest admin_request;
    admin_request.method = request.method;
    admin_request.path = std::move(ops_path);
    admin_request.body = request.body;
    admin_request.headers = request.headers;

    const auto admin_response = context_->ops_service->handleRequest(admin_request);
    auto data = support::Json::parse(admin_response.body, nullptr, false);
    if (data.is_discarded()) {
        data = support::Json{{"raw", admin_response.body}};
    }

    if (admin_response.status_code >= 400) {
        return support::jsonResponse(admin_response.status_code,
                                      support::Json{{"code", admin_response.status_code},
                                                    {"msg", successMsg},
                                                    {"data", std::move(data)}});
    }

    return support::successResponse(std::move(data), std::move(successMsg));
}

http::HttpResponse OpsController::metrics(const http::HttpRequest& request)
{
    ops::AdminRequest admin_request;
    admin_request.method = request.method;
    admin_request.path = "/metrics";
    admin_request.headers = request.headers;

    const auto admin_response = context_->ops_service->handleRequest(admin_request);

    http::HttpResponse response;
    response.status_code = admin_response.status_code;
    response.content_type = admin_response.content_type;
    response.body = admin_response.body;
    response.headers = admin_response.headers;
    return response;
}

} // namespace rcs::api::controllers
