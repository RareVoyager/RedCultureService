#pragma once

#include "rcs/application/service_application.hpp"
#include "rcs/http/http_router.hpp"

#include <memory>

namespace rcs::api {

// 挂载正式业务 HTTP 接口。app 层只负责启动进程，不直接堆业务路由。
void register_server_routes(http::HttpRouter& router, std::shared_ptr<application::ServiceContext> context);

} // namespace rcs::api
