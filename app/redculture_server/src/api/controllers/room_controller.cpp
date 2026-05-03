#include "redculture_server/api/controllers/room_controller.hpp"

#include "redculture_server/api/http_utils.hpp"

#include <utility>

namespace rcs::api::controllers {
namespace {

support::Json roomMemberToJson(const room::RoomMember& member)
{
    return support::Json{
        {"player_id", member.player.player_id},
        {"connection_id", member.player.connection_id},
        {"ready", member.ready},
    };
}

support::Json roomToJson(const room::RoomInfo& room_info)
{
    support::Json members = support::Json::array();
    for (const auto& member : room_info.members) {
        members.push_back(roomMemberToJson(member));
    }

    return support::Json{
        {"room_id", room_info.id},
        {"mode", room_info.mode},
        {"state", room::toString(room_info.state)},
        {"max_players", room_info.max_players},
        {"auto_start_when_full", room_info.auto_start_when_full},
        {"members", std::move(members)},
    };
}

bool persistRoomEvent(const std::shared_ptr<application::ServiceContext>& context,
                        const std::string& action,
                        const room::RoomInfo& room_info,
                        const std::string& player_id)
{
    if (!context->storage_service || !context->storage_service->isConnected()) {
        return false;
    }

    storage::UserProfile profile;
    profile.player_id = player_id;
    profile.account = player_id;
    profile.display_name = player_id;
    profile.metadata = {{"source", "room"}};
    const auto user_saved = context->storage_service->findUser(player_id)
                                ? storage::StorageResult{true, {}}
                                : context->storage_service->upsertUser(profile);

    storage::EventLog event;
    event.level = user_saved.ok ? "info" : "warn";
    event.category = "room";
    event.message = action;
    event.metadata = {
        {"room_id", room_info.id},
        {"player_id", player_id},
        {"mode", room_info.mode},
        {"state", room::toString(room_info.state)},
        {"max_players", room_info.max_players},
        {"member_count", room_info.members.size()},
        {"user_saved", user_saved.ok},
        {"storage_error", user_saved.error},
    };
    const auto event_saved = context->storage_service->appendEventLog(event);
    return user_saved.ok && event_saved.ok;
}

} // namespace

RoomController::RoomController(std::shared_ptr<application::ServiceContext> context)
    : context_(std::move(context))
{
}

void RoomController::registerRoutes(http::HttpRouter& router)
{
    auto self = shared_from_this();

    router.post("/api/v1/rooms/create", [self](const http::HttpRequest& request) {
        return self->createRoom(request);
    });
    router.post("/api/v1/rooms/join", [self](const http::HttpRequest& request) {
        return self->joinRoom(request);
    });
    router.get("/api/v1/rooms", [self](const http::HttpRequest& request) {
        return self->listRooms(request);
    });
}

http::HttpResponse RoomController::createRoom(const http::HttpRequest& request)
{
    const auto parsed = support::parseJsonBody(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolvePlayer(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    room::RoomOptions options;
    options.mode = support::readStringOr(body, "mode", "default");
    options.max_players = support::readSizeOr(body, "max_players", 4);
    options.auto_start_when_full = support::readBoolOr(body, "auto_start_when_full", true);

    const room::PlayerRef host{
        player.data(),
        support::readUint64Or(body, "connection_id", 0),
    };

    const auto created = context_->room_service->createRoom(host, options);
    if (!created.ok || !created.room) {
        return RCS_API_ERROR_RESPONSE(400, 400, created.error.empty() ? "create room failed" : created.error);
    }

    const auto storage_saved = persistRoomEvent(context_, "room created", *created.room, player.data());
    return support::successResponse(support::Json{{"room", roomToJson(*created.room)},
                                                   {"storage_saved", storage_saved}},
                                     "create room success");
}

http::HttpResponse RoomController::joinRoom(const http::HttpRequest& request)
{
    const auto parsed = support::parseJsonBody(request);
    if (!parsed.ok()) {
        return RCS_API_ERROR_RESPONSE(400, parsed.code(), parsed.msg());
    }

    const auto& body = parsed.data();
    const auto player = support::resolvePlayer(request, body, context_);
    if (!player.ok()) {
        return RCS_API_ERROR_RESPONSE(player.code(), player.code(), player.msg());
    }

    const auto room_id = support::readUint64Or(body, "room_id", 0);
    if (room_id == 0) {
        return RCS_API_ERROR_RESPONSE(400, 400, "room_id is required");
    }

    const room::PlayerRef member{
        player.data(),
        support::readUint64Or(body, "connection_id", 0),
    };

    const auto joined = context_->room_service->joinRoom(room_id, member);
    if (!joined.ok || !joined.room) {
        return RCS_API_ERROR_RESPONSE(400, 400, joined.error.empty() ? "join room failed" : joined.error);
    }

    const auto storage_saved = persistRoomEvent(context_, "room joined", *joined.room, player.data());
    return support::successResponse(support::Json{{"room", roomToJson(*joined.room)},
                                                   {"storage_saved", storage_saved}},
                                     "join room success");
}

http::HttpResponse RoomController::listRooms(const http::HttpRequest&)
{
    support::Json rooms = support::Json::array();
    for (const auto& room_info : context_->room_service->listRooms(false)) {
        rooms.push_back(roomToJson(room_info));
    }

    return support::successResponse(support::Json{{"rooms", std::move(rooms)}}, "list rooms success");
}

} // namespace rcs::api::controllers
