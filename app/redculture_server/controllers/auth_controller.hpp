#pragma once

#include "app/redculture_server/dto/auth_dto.hpp"
#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::server::controllers {

class AuthController : public std::enable_shared_from_this<AuthController> {
public:
    explicit AuthController(std::shared_ptr<application::ServiceContext> context);

    void register_routes(http::HttpRouter& router);

private:
    http::HttpResponse login(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::server::controllers
