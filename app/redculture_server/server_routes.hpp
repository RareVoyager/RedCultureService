#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::server {

void register_server_routes(http::HttpRouter& router, std::shared_ptr<application::ServiceContext> context);

} // namespace rcs::server
