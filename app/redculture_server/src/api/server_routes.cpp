#include "redculture_server/api/server_routes.hpp"

#include "redculture_server/api/controllers/auth_controller.hpp"
#include "redculture_server/api/controllers/interaction_controller.hpp"
#include "redculture_server/api/controllers/ops_controller.hpp"
#include "redculture_server/api/controllers/room_controller.hpp"
#include "redculture_server/api/controllers/tts_controller.hpp"

#include <memory>
#include <utility>

namespace rcs::api {

void registerServerRoutes(http::HttpRouter& router, std::shared_ptr<application::ServiceContext> context)
{
    auto auth_controller = std::make_shared<controllers::AuthController>(context);
    auto room_controller = std::make_shared<controllers::RoomController>(context);
    auto interaction_controller = std::make_shared<controllers::InteractionController>(context);
    auto tts_controller = std::make_shared<controllers::TtsController>(context);
    auto ops_controller = std::make_shared<controllers::OpsController>(std::move(context));

    auth_controller->registerRoutes(router);
    room_controller->registerRoutes(router);
    interaction_controller->registerRoutes(router);
    tts_controller->registerRoutes(router);
    ops_controller->registerRoutes(router);
}

} // namespace rcs::api
