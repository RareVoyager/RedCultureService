#include "rcs/api/controllers/room_controller.hpp"

#include "rcs/api/http_utils.hpp"

#include <utility>

namespace rcs::api::controllers {
namespace {

support::Json room_member_to_json(const room::RoomMember& member)
{
    return support::Json{
        {"player_id", member.player.player_id},
        {"connection_id", member.player.connection_id},
        {"ready", member.ready},
    };
}

support::Json room_to_json(const room::RoomInfo& room_info)
{
    support::Json members = support::Json::array();
    for (const auto& member : room_info.members) {
        members.push_back(room_member_to_json(member));
    }

    return support::Json{
        {"room_id", room_info.id},
        {"mode", room_info.mode},
        {"state", room::to_string(room_info.state)},
        {"max_players", room_info.max_players},
        {"auto_start_when_full", room_info.auto_start_when_full},
        {"members", std::move(members)},
    };
}

} // namespace

RoomController::RoomController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void RoomController::register_routes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.post("/api/v1/rooms/create", [self](const http::HttpRequest& request) {
        return self->create_room(request);
    });
    router.post("/api/v1/rooms/join", [self](const http::HttpRequest& request) {
        return self->join_room(request);
    });
    router.get("/api/v1/rooms", [self](const http::HttpRequest& request) {
        return self->list_rooms(request);
    });
}

http::HttpResponse RoomController::create_room(const http::HttpRequest& request)
{
    const auto parsed = support::parse_json_body(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolve_player(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    room::RoomOptions options;
    options.mode = support::read_string_or(body, "mode", "default");
    options.max_players = support::read_size_or(body, "max_players", 4);
    options.auto_start_when_full = support::read_bool_or(body, "auto_start_when_full", true);

    const room::PlayerRef host{
        player.data(),
        support::read_uint64_or(body, "connection_id", 0),
    };

    const auto created = context_->room_service->create_room(host, options);
    if (!created.ok || !created.room) {
        return RCS_API_ERROR_RESPONSE(400, 400, created.error.empty() ? "create room failed" : created.error);
    }

    return support::success_response(support::Json{{"room", room_to_json(*created.room)}}, "create room success");
}

http::HttpResponse RoomController::join_room(const http::HttpRequest& request)
{
    const auto parsed = support::parse_json_body(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolve_player(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    const auto room_id = support::read_uint64_or(body, "room_id", 0);
    if (room_id == 0) {
        return RCS_API_ERROR_RESPONSE(400, 400, "room_id is required");
    }

    const room::PlayerRef member{
        player.data(),
        support::read_uint64_or(body, "connection_id", 0),
    };

    const auto joined = context_->room_service->join_room(room_id, member);
    if (!joined.ok || !joined.room) {
        return RCS_API_ERROR_RESPONSE(400, 400, joined.error.empty() ? "join room failed" : joined.error);
    }

    return support::success_response(support::Json{{"room", room_to_json(*joined.room)}}, "join room success");
}

http::HttpResponse RoomController::list_rooms(const http::HttpRequest&)
{
    support::Json rooms = support::Json::array();
    for (const auto& room_info : context_->room_service->list_rooms(false)) {
        rooms.push_back(room_to_json(room_info));
    }

    return support::success_response(support::Json{{"rooms", std::move(rooms)}}, "list rooms success");
}

} // namespace rcs::api::controllers
