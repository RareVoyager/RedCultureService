#include "rcs/room/room_match_service.hpp"

#include <iostream>

namespace {

void print_room(const rcs::room::RoomInfo& room) {
    std::cout << "room id: " << room.id
              << ", mode: " << room.mode
              << ", state: " << rcs::room::to_string(room.state)
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

    auto create_result = service.create_room({"player-1", 101}, options);
    if (!create_result.ok || !create_result.room) {
        std::cout << "create room failed: " << create_result.error << '\n';
        return 1;
    }

    auto join_result = service.join_room(create_result.room->id, {"player-2", 102});
    if (!join_result.ok || !join_result.room) {
        std::cout << "join room failed: " << join_result.error << '\n';
        return 1;
    }

    print_room(*join_result.room);

    service.leave_room(join_result.room->id, "player-2");
    service.close_room(join_result.room->id);

    service.enqueue_match({{"player-3", 103}, "quiz", 2});
    service.enqueue_match({{"player-4", 104}, "quiz", 2});

    const auto tick_result = service.tick();
    std::cout << "created rooms from match: " << tick_result.created_rooms.size() << '\n';
    for (const auto& room : tick_result.created_rooms) {
        print_room(room);
    }

    return 0;
}
