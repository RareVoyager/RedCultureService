#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>
#include <string>

namespace rcs::api::controllers {

// 运维接口控制器：健康检查、就绪检查、版本、指标、停服。
class OpsController : public std::enable_shared_from_this<OpsController> {
public:
    explicit OpsController(std::shared_ptr<application::ServiceContext> context);

    void register_routes(http::HttpRouter& router);

private:
    http::HttpResponse forward_json(const http::HttpRequest& request, std::string ops_path, std::string success_msg);
    http::HttpResponse metrics(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers
