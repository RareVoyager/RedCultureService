#pragma once

#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>
#include <string>

namespace rcs::api::controllers {

class OpsController : public std::enable_shared_from_this<OpsController> {
public:
    explicit OpsController(std::shared_ptr<application::ServiceContext> context);

    void registerRoutes(http::HttpRouter& router);

private:
    http::HttpResponse forwardJson(const http::HttpRequest& request, std::string ops_path, std::string successMsg);
    http::HttpResponse metrics(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers
