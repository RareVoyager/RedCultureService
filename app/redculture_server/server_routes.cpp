#include "app/redculture_server/server_routes.hpp"

#include "app/redculture_server/controllers/auth_controller.hpp"

#include <memory>
#include <utility>

namespace rcs::server {

void register_server_routes(http::HttpRouter& router, std::shared_ptr<application::ServiceContext> context)
{
    auto auth_controller = std::make_shared<controllers::AuthController>(std::move(context));
    auth_controller->register_routes(router);
}

} // namespace rcs::server
