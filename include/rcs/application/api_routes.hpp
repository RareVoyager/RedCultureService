#pragma once

#include "rcs/application/service_application.hpp"

namespace rcs::application {

// 注册 Unity/运维可调用的 JSON HTTP API。
void register_api_routes(http::HttpRouter& router, std::shared_ptr<ServiceContext> context);

} // namespace rcs::application
