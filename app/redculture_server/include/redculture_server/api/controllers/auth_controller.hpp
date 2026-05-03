#pragma once

#include "redculture_server/api/dto/auth_dto.hpp"
#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api::controllers {

class AuthController : public std::enable_shared_from_this<AuthController> {
public:
    explicit AuthController(std::shared_ptr<application::ServiceContext> context);

    void registerRoutes(http::HttpRouter& router);

private:
    http::HttpResponse registerUser(const http::HttpRequest& request);
    http::HttpResponse login(const http::HttpRequest& request);

    std::shared_ptr<application::ServiceContext> context_;
};

} // namespace rcs::api::controllers