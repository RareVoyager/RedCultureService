#pragma once

#include "redculture_server/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api {

// 挂载 redculture_server 的正式业务 HTTP 接口。
void registerServerRoutes(http::HttpRouter& router, std::shared_ptr<application::ServiceContext> context);

} // namespace rcs::api