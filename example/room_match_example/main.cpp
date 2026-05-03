#include "rcs/room/room_match_service.hpp"

#include <iostream>

namespace {

void printRoom(const rcs::room::RoomInfo& room) {
    std::cout << "room id: " << room.id
              << ", mode: " << room.mode
              << ", state: " << rcs::room::toString(room.state)
              << ", members: " << room.members.size() << "/" << room.max_players
              << '\n';

    for (const auto& member : room.members) {
        std::cout << "  player: " << member.player.player_id
                  << ", connection: " << member.player.connection_id
                  << ", ready: " << (member.ready ? "true" : "false")
                  << '\n';
    }
}

} // namespace

int main() {
    rcs::room::RoomMatchService service;

    rcs::room::RoomOptions options;
    options.mode = "story";
    options.max_players = 2;

    auto create_result = service.createRoom({"player-1", 101}, options);
    if (!create_result.ok || !create_result.room) {
        std::cout << "create room failed: " << create_result.error << '\n';
        return 1;
    }

    auto join_result = service.joinRoom(create_result.room->id, {"player-2", 102});
    if (!join_result.ok || !join_result.room) {
        std::cout << "join room failed: " << join_result.error << '\n';
        return 1;
    }

    printRoom(*join_result.room);

    service.leaveRoom(join_result.room->id, "player-2");
    service.closeRoom(join_result.room->id);

    service.enqueueMatch({{"player-3", 103}, "quiz", 2});
    service.enqueueMatch({{"player-4", 104}, "quiz", 2});

    const auto tick_result = service.tick();
    std::cout << "created rooms from match: " << tick_result.created_rooms.size() << '\n';
    for (const auto& room : tick_result.created_rooms) {
        printRoom(room);
    }

    return 0;
}
