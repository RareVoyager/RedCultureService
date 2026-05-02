#pragma once

#include "rcs/api/dto/auth_dto.hpp"
#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

// 登录与鉴权相关 HTTP 接口控制器。
class AuthController : public std::enable_shared_from_this<AuthController> {
public:
    explicit AuthController(std::shared_ptr<application::ServiceContext> context);

    void register_routes(http::HttpRouter& router);

private:
    http::HttpResponse register_user(const http::HttpRequest& request);
    http::HttpResponse login(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers
