#include "rcs/api/server_routes.hpp"

#include "rcs/api/controllers/auth_controller.hpp"
#include "rcs/api/controllers/interaction_controller.hpp"
#include "rcs/api/controllers/ops_controller.hpp"
#include "rcs/api/controllers/room_controller.hpp"
#include "rcs/api/controllers/tts_controller.hpp"

#include <memory>
#include <utility>

namespace rcs::api {

void register_server_routes(http::HttpRouter& router, std::shared_ptr<application::ServiceContext> context)
{
    auto auth_controller = std::make_shared<controllers::AuthController>(context);
    auto room_controller = std::make_shared<controllers::RoomController>(context);
    auto interaction_controller = std::make_shared<controllers::InteractionController>(context);
    auto tts_controller = std::make_shared<controllers::TtsController>(context);
    auto ops_controller = std::make_shared<controllers::OpsController>(std::move(context));

    auth_controller->register_routes(router);
    room_controller->register_routes(router);
    interaction_controller->register_routes(router);
    tts_controller->register_routes(router);
    ops_controller->register_routes(router);
}

} // namespace rcs::api
